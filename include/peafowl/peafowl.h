/*
 * peafowl.h
 *
 * Created on: 19/09/2012
 *
 * =========================================================================
 *  Copyright (C) 2012-2013, Daniele De Sensi (d.desensi.software@gmail.com)
 *
 *  This file is part of Peafowl.
 *
 *  Peafowl is free software: you can redistribute it and/or
 *  modify it under the terms of the Lesser GNU General Public
 *  License as published by the Free Software Foundation, either
 *  version 3 of the License, or (at your option) any later version.

 *  Peafowl is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  Lesser GNU General Public License for more details.
 *
 *  You should have received a copy of the Lesser GNU General Public
 *  License along with Peafowl.
 *  If not, see <http://www.gnu.org/licenses/>.
 *
 * =========================================================================
 *  Provided features:
 *		+Support for IPv6 and its optional headers
 *  	+Tunneling: 6in6, 4in4, 6in4, 4in6
 *  	+Protection against truncated or corrupted L3 and L4 packets.
 *  	 It can be enabled by DPI_ENABLE_L3_TRUNCATION_PROTECTION or
 *  	 DPI_ENABLE_L4_TRUNCATION_PROTECTION in the cases in which
 *  	 is not provided by the lower layers. This check is not done
 *  	 computing the checksum but only looking at the header/payload
 *  	 lengths.
 *  	+Robust IPv4 and IPv6 defragmentation support tested against a
 *  	 large number of possible attacks.
 *  	+TCP stream reassembly.
 *  	+Possibility to activate and deactivate protocol inspector and
 *  	 callbacks at runtime.
 *      +The framework can be used in two different modes: Stateful and
 *       Stateless.
 *           +Stateful: is suited for applications which don't have a
 *            concept of 'flow'. In this case the user simply pass to
 *            the library a stream of packets without concerning about
 *            how to store the flow. All the flow management and storing
 *            will be done by the library.
 *           +Stateless: is suited for applications which already have a
 *            concept of 'flow'. In this case the framework demand the
 *            storage of the flow data to the application. The user
 *            application should be modified in order to store with
 *            their own flow informations also the informations needed by
 *            the framework to identify the protocols.
 *      +The protocol recognition can be done with
 *       dpi_stateful_identify_application_protocol(...) giving simply a
 *       pointer to the packet, or separating the two stages and using in
 *       sequence dpi_parse_L3_L4_headers(...) and then
 *       dpi_stateful_get_app_protocol_v4(...) (or _v6). The latter
 *       solution gives to the user the possibility to use the L3 and L4
 *       informations that maybe he already has (skipping the L3 and L4
 *       info extraction) and to explicitly get a pointer to the beginning
 *       of application flow in the case in which he wants to invoke its
 *       own processing routines on the flow payload.
 *      +Support for user defined callbacks on any HTTP header field and
 *       HTTP body.
 *
 *
 */

#ifndef DPI_API_H
#define DPI_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include <peafowl/inspectors/http_parser_joyent.h>
#include <peafowl/inspectors/protocols_identifiers.h>
#include <peafowl/inspectors/fields.h>
#include <peafowl/utils.h>
#include <peafowl/external/utils/uthash.h>

#include <sys/types.h>

/** Errors **/
#define DPI_ERROR_WRONG_IPVERSION -1
#define DPI_ERROR_IPSEC_NOTSUPPORTED -2
#define DPI_ERROR_L3_TRUNCATED_PACKET -3
#define DPI_ERROR_L4_TRUNCATED_PACKET -4
#define DPI_ERROR_TRANSPORT_PROTOCOL_NOTSUPPORTED -5
#define DPI_ERROR_MAX_FLOWS -6

typedef struct dpi_flow_infos dpi_flow_infos_t;
typedef struct dpi_reassembly_fragment dpi_reassembly_fragment_t;
typedef struct dpi_tracking_informations dpi_tracking_informations_t;

/** Statuses */
typedef enum dpi_status {
  DPI_STATUS_OK = 0,
  DPI_STATUS_IP_FRAGMENT,
  DPI_STATUS_IP_LAST_FRAGMENT,
  DPI_STATUS_TCP_OUT_OF_ORDER,
  DPI_STATUS_TCP_CONNECTION_TERMINATED,  // Terminated means FIN received. This
                                         // status is not set for connection
                                         // closed by RST
} dpi_status_t;

enum dpi_state_update_status {
  DPI_STATE_UPDATE_SUCCESS = 0,
  DPI_STATE_UPDATE_FAILURE = 1
};

typedef struct dpi_identification_result {
  int8_t status;
  dpi_protocol_t protocol;
  void* user_flow_data;
} dpi_identification_result_t;

typedef struct dpi_pkt_infos {
  uint16_t srcport;   /** In network byte order. **/
  uint16_t dstport;   /** In network byte order. **/
  uint8_t ip_version; /** 4 if IPv4, 6 in IPv6. **/
  /**
* 0: From source to dest. 1: From dest to source
* (with respect to src and dst stored in the flow).
**/
  uint8_t direction;
  /**
* Id corresponds to the id defined for IPv4 protocol
* field (IPv6 next header field).
**/
  uint8_t l4prot;

  const unsigned char* pkt;
  uint16_t l4offset;
  uint16_t l7offset;
  /**
   * Length of the application data (from the end of L4 header to the
   * end).
**/
  uint32_t data_length;
  union src_addr { /** Addresses mantained in network byte order. **/
    struct in6_addr ipv6_srcaddr;
    uint32_t ipv4_srcaddr;
  } src_addr_t;
  union dst_addr {
    struct in6_addr ipv6_dstaddr;
    uint32_t ipv4_dstaddr;
  } dst_addr_t;
  /** Time when the library started the processing (in seconds). **/
  uint32_t processing_time;
} dpi_pkt_infos_t;

enum dpi_http_message_type {
  DPI_HTTP_REQUEST = HTTP_REQUEST,
  DPI_HTTP_RESPONSE = HTTP_RESPONSE
};

enum dpi_http_methods {
#define XX(num, name, string) DPI_HTTP_##name = num,
  HTTP_METHOD_MAP(XX)
#undef XX
};

typedef struct dpi_http_message_informations {
  uint8_t http_version_major;
  uint8_t http_version_minor;
  /**
   * HTTP method identifier if request_or_response==DPI_HTTP_REQUEST,
   * otherwise is the HTTP status code. The method identifiers are
   * named DPI_HTTP_METHOD_GET, DPI_HTTP_METHOD_POST, etc.
   */
  uint16_t method_or_code;
  /**
   * DPI_HTTP_REQUEST if the header field belongs to an HTTP request,
   * DPI_HTTP_RESPONSE otherwise.
   **/
  uint8_t request_or_response;
} dpi_http_message_informations_t;

/**
 * This callback is called when the flow is expired and deleted. It can be
 * used by the user to clear flow_specific_user_data
 * @param flow_specific_user_data A pointer to the user data specific to this
 * flow.
 */
typedef void(dpi_flow_cleaner_callback)(void* flow_specific_user_data);


/**
 * Callback for processing a field of a given protocol.
 * @param field_value    The value of the field.
 * @param field_len      The length of the field.
 * @param field_complete 1 if the field is complete, 0 if another chunk is to
 *                       be expected.
 * @param udata_global   A pointer to the global user data (set with the
 *                       pfwl_callbacks_fields_set_udata call).
 * @param udata_flow     A pointer to the user data specific
 *                       to this flow.
 * @param pkt_info       A pointer to the parsed packet.
 **/
typedef void(pfwl_field_callback)(const char* field_value,
                                  size_t field_len,
                                  uint8_t field_complete,
                                  void* udata_global,
                                  void** udata_flow,
                                  dpi_pkt_infos_t* pkt_info);

/**
 * Called when ssl inspector seen certificate
**/
typedef void(dpi_ssl_certificate_callback)(char* certificate, int size,
                                           void* user_data,
                                           dpi_pkt_infos_t* pkt);

/**
 * This callback is called when the corresponding header field is found.
 * If the field is divided into more TCP segments it is reconstructed by
 * the library and this callback is called only one time on the
 * reconstructed field. In any case, the pointer to the value is valid
 * only for the lifetime of the callback. It must be copied if the user
 * needs it.
 *
 * @param url A pointer to the string representing the value of the url
 *            It is valid only for the callback lifetime.
 * @param url_length The length of the string representing the url.
 * @param pkt_informations A pointer to the parsed packet.
 * @param flow_specific_user_data A pointer to the user data specific
 *                                to this flow.
 * @param user_data A pointer to the global HTTP user data.
 */
typedef void(dpi_http_header_url_callback)(const unsigned char* url,
                                           uint32_t url_length,
                                           dpi_pkt_infos_t* pkt_informations,
                                           void** flow_specific_user_data,
                                           void* user_data);

/**
 * This callback is called when the corresponding header field is found.
 * If the field is divided into more TCP segments it is reconstructed by
 * the library and this callback is called only one time on the
 * reconstructed field. In any case, the pointer to the value is valid
 * only for the lifetime of the callback. It must be copied if the user
 * needs it.
 *
 * @param http_message_informations   A pointer to the struct containing
 *                                    the message informations (method,
 *                                    http version, etc..). The pointer
 *                                    is valid only for the callback
 *                                    lifetime.
 * @param header_value                A pointer to the string representing
 *                                    the value of the header field (NOTE:
 *                                    Is not \0 terminated). It is valid
 *                                    only for the callback lifetime.
 * @param header_value_length         The length of the string representing
 *                                    the value of the header field.
 * @param pkt_informations            A pointer to the parsed packet.
 * @param flow_specific_user_data     A pointer to the user data specific
 *                                    to this flow.
 * @param user_data                   A pointer to the global HTTP user data.
 */
typedef void(dpi_http_header_field_callback)(
    dpi_http_message_informations_t* http_message_informations,
    const unsigned char* header_value, uint32_t header_value_length,
    dpi_pkt_infos_t* pkt_informations, void** flow_specific_user_data,
    void* user_data);

/**
 * This callback is called when an HTTP body is found. If the body is
 * divided in multiple TCP segments, then for each segment this callback
 * is called with the content of that specific segment. When the last
 * segment of the body arrives, the field last_chunk will be different
 * from 0. The pointer to the body chunk is valid only for the lifetime
 * of the callback.
 *
 * @param http_message_informations   A pointer to the struct containing
 *                                    the message informations (method,
 *                                    http version, etc..). The pointer
 *                                    is valid only for the callback
 *                                    lifetime.
 * @param body_chunk A pointer to a chunk of the HTTP body present in the
 *                   segment. It is valid only for the callback lifetime.
 * @param body_chunk_length           The length of the chunk.
 * @param pkt_informations            A pointer to the parsed packet.
 * @param flow_specific_user_data     A pointer to the user data specific
 *                                    to this flow.
 * @param user_data A pointer to the global HTTP user data.
 * @param last_chunk 0 if it is not the last chunk 1 otherwise.
 */
typedef void(dpi_http_body_callback)(
    dpi_http_message_informations_t* http_message_informations,
    const unsigned char* body_chunk, uint32_t body_chunk_length,
    dpi_pkt_infos_t* pkt_informations, void** flow_specific_user_data,
    void* user_data, uint8_t last_chunk);

typedef struct dpi_http_callbacks {
  /** Called on the HTTP request-URI. **/
  dpi_http_header_url_callback* header_url_callback;

  /** The names of the headers types that the user wants to inspect. **/
  const char** header_names;

  /** The number of headers types that the user wants to inspect. **/
  uint8_t num_header_types;

  /**
   * The callbacks that will be invoked for the specified header fields.
   * header_types_callbacks[i] will be invoked when an header with name
   * header_names[i] is found. The user cannot make any assumption on
   * the order in which the callbacks will be invoked.
   */
  dpi_http_header_field_callback** header_types_callbacks;

  /** Called on the entire HTTP body. **/
  dpi_http_body_callback* http_body_callback;
} dpi_http_callbacks_t;

typedef struct dpi_http_internal_informations {
  dpi_http_callbacks_t* callbacks;
  dpi_pkt_infos_t* pkt_informations;
  void** flow_specific_user_data;
  void* user_data;
  char* temp_buffer;
  size_t temp_buffer_size;
} dpi_http_internal_informations_t;

typedef struct dpi_ssl_callbacks {
  dpi_ssl_certificate_callback* certificate_callback;
} dpi_ssl_callbacks_t;

typedef struct dpi_ssl_internal_information {
  dpi_ssl_callbacks_t* callbacks;
  void* callbacks_user_data;
  uint8_t* pkt_buffer;
  int pkt_size;
  uint8_t ssl_detected;
} dpi_ssl_internal_information_t;

typedef void(sip_requestURI_callback)(const char* requestURI,
                                      size_t requestURI_len);

typedef struct dpi_sip_callbacks {
  /**
   * The callbacks that will be invoked when the specified messages are found.
   **/
  sip_requestURI_callback*
      requestURI_cb;  // Invoked by the framework when a requestURI is found
} dpi_sip_callbacks_t;

typedef struct library_state dpi_library_state_t;

/**
 * A generic protocol inspector.
 * @param state          A pointer to the state of the library.
 * @param pkt            A pointer to the parsed packet.
 * @param app_data       A pointer to the application payload.
 * @param data_length    The length of the application payload.
 * @param tracking       A pointer to the protocols tracking informations.
 * @return               DPI_PROTOCOL_MATCHES if the protocol matches.
 *                       DPI_PROTOCOL_NO_MATCHES if the protocol doesn't
 *                       matches.
 *                       DPI_PROTOCOL_MORE_DATA_NEEDED if the inspector
 *                       needs more data to decide.
 *                       DPI_ERROR if an error occurred.
 */
typedef uint8_t (*dpi_inspector_callback)(
    dpi_library_state_t* state, dpi_pkt_infos_t* pkt,
    const unsigned char* app_data, uint32_t data_length,
    dpi_tracking_informations_t* tracking);

typedef struct dpi_l7_skipping_infos dpi_l7_skipping_infos_t;

typedef enum {
  DPI_INSPECTOR_ACCURACY_LOW = 0,
  DPI_INSPECTOR_ACCURACY_MEDIUM,
  DPI_INSPECTOR_ACCURACY_HIGH,
} dpi_inspector_accuracy;

typedef struct {
    pfwl_field_callback** callbacks;
    uint8_t callbacks_num;
} pfwl_callbacks_field_entry_t;

struct library_state;

struct library_state {
  /********************************************************************/
  /** Created by dpi_init_state and never modified                   **/
  /********************************************************************/
  void* db4;
  void* db6;

  /********************************************************************/
  /** Can be modified during the execution but only using the state  **/
  /** update functions. They are never modified in other places      **/
  /********************************************************************/
  char protocols_to_inspect[BITNSLOTS(DPI_NUM_PROTOCOLS)];
  char active_callbacks[BITNSLOTS(DPI_NUM_PROTOCOLS)]; // TODO: Remove, replaced with field_callbacks_lengths

  dpi_l7_prot_id active_protocols;

  uint16_t max_trials;

  dpi_flow_cleaner_callback* flow_cleaner_callback;

  void* callbacks_udata;

  /** HTTP callbacks. **/
  void* http_callbacks;
  void* http_callbacks_user_data;

  /** SSL callbacks **/
  void* ssl_callbacks;
  void* ssl_callbacks_user_data;

  /** SIP callbacks **/
  void* sip_callbacks;
  void* sip_callbacks_user_data;

  /** DNS callbacks **/
  /* ## TODO ## */

  /** Field callbacks. **/
  pfwl_callbacks_field_entry_t callbacks_fields[DPI_NUM_PROTOCOLS];

  uint8_t tcp_reordering_enabled : 1;

  /** L7 skipping information. **/
  dpi_l7_skipping_infos_t* l7_skip;

  dpi_inspector_accuracy inspectors_accuracy[DPI_NUM_PROTOCOLS];

  /********************************************************************/
  /** The content of these structures can be modified during the     **/
  /** execution also in functions different from the state update    **/
  /** functions. This is the reason why when multiprocessor support  **/
  /** is used, we need to have one copy of these structures for each **/
  /** worker or we need to protect the access with mutual exclusion  **/
  /** mechanisms (e.g. locks).                                       **/
  /********************************************************************/
  void* ipv4_frag_state;
  void* ipv6_frag_state;
#ifdef WITH_PROMETHEUS
  void* prometheus_stats;
#endif
};

/**
 * Initializes the state of the library. If not specified otherwise after
 * the initialization, the library will consider all the protocols active.
 * @param size_v4 Size of the array of pointers used to build the database
 *                for v4 flows.
 * @param size_v6 Size of the array of pointers used to build the database
 *                for v6 flows.
 * @param max_active_v4_flows The maximum number of IPv4 flows which can
 *        be active at any time. After reaching this threshold, new flows
 *        will not be created.
 * @param max_active_v6_flows The maximum number of IPv6 flows which can
 *        be active at any time. After reaching this threshold, new flows
 *        will not be created.
 * @return A pointer to the state of the library otherwise.
 */
dpi_library_state_t* dpi_init_stateful(uint32_t size_v4, uint32_t size_v6,
                                       uint32_t max_active_v4_flows,
                                       uint32_t max_active_v6_flows);

/**
 * Initializes the state of the library. If not specified otherwise after
 * the initialization, the library will consider all the protocols active.
 * @return A pointer to the state of the library otherwise.
 */
dpi_library_state_t* dpi_init_stateless(void);

/**
 * Terminates the library.
 * @param state A pointer to the state of the library.
 */
void dpi_terminate(dpi_library_state_t* state);

/**
 * Sets the maximum number of times that the library tries to guess the
 * protocol. During the flow protocol identification, after this number
 * of trials, in the case in which it cannot decide between two or more
 * protocols, one of them will be chosen, otherwise DPI_PROTOCOL_UNKNOWN
 * will be returned.
 * @param state A pointer to the state of the library.
 * @param max_trials Maximum number of trials. Zero will be consider as
 *                   infinity.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded, DPI_STATE_UPDATE_FAILURE
 *         otherwise.
 */
uint8_t dpi_set_max_trials(dpi_library_state_t* state, uint16_t max_trials);

/**
 * Enable IPv4 defragmentation.
 * @param state        A pointer to the library state.
 * @param table_size   The size of the table to be used to store IPv4
 *                     fragments informations.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded, DPI_STATE_UPDATE_FAILURE
 *         otherwise.
 */
uint8_t dpi_ipv4_fragmentation_enable(dpi_library_state_t* state,
                                      uint16_t table_size);

/**
 * Enable IPv6 defragmentation.
 * @param state        A pointer to the library state.
 * @param table_size   The size of the table to be used to store IPv6
 *                     fragments informations.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded, DPI_STATE_UPDATE_FAILURE
 *         otherwise.
 */
uint8_t dpi_ipv6_fragmentation_enable(dpi_library_state_t* state,
                                      uint16_t table_size);

/**
 * Sets the amount of memory that a single host can use for IPv4
 * defragmentation.
 * @param state                   A pointer to the library state.
 * @param per_host_memory_limit   The maximum amount of memory that
 *                                any IPv4 host can use.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_ipv4_fragmentation_set_per_host_memory_limit(
    dpi_library_state_t* state, uint32_t per_host_memory_limit);

/**
 * Sets the amount of memory that a single host can use for IPv6
 * defragmentation.
 * @param state                   A pointer to the library state.
 * @param per_host_memory_limit   The maximum amount of memory that
 *                                 any IPv6 host can use.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_ipv6_fragmentation_set_per_host_memory_limit(
    dpi_library_state_t* state, uint32_t per_host_memory_limit);

/**
 * Sets the total amount of memory that can be used for IPv4
 * defragmentation.
 * If fragmentation is disabled and then enabled, this information must be
 * passed again.
 * Otherwise default value will be used.
 * @param state               A pointer to the state of the library
 * @param totel_memory_limit  The maximum amount of memory that can be used
 *                            for IPv4 defragmentation.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_ipv4_fragmentation_set_total_memory_limit(
    dpi_library_state_t* state, uint32_t total_memory_limit);

/**
 * Sets the total amount of memory that can be used for IPv6
 * defragmentation. If fragmentation is disabled and then enabled, this
 * information must be passed again. Otherwise default value will be used.
 * @param state               A pointer to the state of the library
 * @param total_memory_limit  The maximum amount of memory that can be
 *                            used for IPv6 defragmentation.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_ipv6_fragmentation_set_total_memory_limit(
    dpi_library_state_t* state, uint32_t total_memory_limit);

/**
 * Sets the maximum time (in seconds) that can be spent to reassembly an
 * IPv4 fragmented datagram. Is the maximum time gap between the first and
 * last fragments of the datagram.
 * @param state            A pointer to the state of the library.
 * @param timeout_seconds  The reassembly timeout.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_ipv4_fragmentation_set_reassembly_timeout(
    dpi_library_state_t* state, uint8_t timeout_seconds);

/**
 * Sets the maximum time (in seconds) that can be spent to reassembly an
 * IPv6 fragmented datagram. Is the maximum time gap between the first and
 * last fragments of the datagram.
 * @param state            A pointer to the state of the library.
 * @param timeout_seconds  The reassembly timeout.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_ipv6_fragmentation_set_reassembly_timeout(
    dpi_library_state_t* state, uint8_t timeout_seconds);

/**
 * Disable IPv4 defragmentation.
 * @param state A pointer to the state of the library.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_ipv4_fragmentation_disable(dpi_library_state_t* state);

/**
 * Disable IPv6 defragmentation.
 * @param state A pointer to the state of the library.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_ipv6_fragmentation_disable(dpi_library_state_t* state);

/**
 * If enabled, the library will reorder out of order TCP packets
 * (enabled by default).
 * @param state  A pointer to the state of the library.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_tcp_reordering_enable(dpi_library_state_t* state);

/**
 * If it is called, the library will not reorder out of order TCP packets.
 * Out-of-order segments will be delivered to the inspector as they
 * arrive. This means that the inspector may not be able to identify the
 * application protocol. Moreover, if there are callbacks saved for TCP
 * based protocols, if TCP reordering is disabled, the extracted
 * informations could be erroneous or incomplete.
 * @param state A pointer to the state of the library.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_tcp_reordering_disable(dpi_library_state_t* state);

/**
 * ---- DEPRECATED, replaced by dpi_enable_protocol ----
 * Enable a protocol inspector.
 * @param state         A pointer to the state of the library.
 * @param protocol      The protocol to enable.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_set_protocol(dpi_library_state_t* state, dpi_protocol_t protocol);

/**
 * ---- DEPRECATED, replaced by dpi_disable_protocol ----
 * Disable a protocol inspector.
 * @param state       A pointer to the state of the library.
 * @param protocol    The protocol to disable.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_delete_protocol(dpi_library_state_t* state,
                            dpi_protocol_t protocol);

/**
 * Enable a protocol inspector.
 * @param state         A pointer to the state of the library.
 * @param protocol      The protocol to enable.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_enable_protocol(dpi_library_state_t* state,
                            dpi_l7_prot_id protocol);

/**
 * Disable a protocol inspector.
 * @param state       A pointer to the state of the library.
 * @param protocol    The protocol to disable.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_disable_protocol(dpi_library_state_t* state,
                             dpi_l7_prot_id protocol);

/**
 * Enable all the protocol inspector.
 * @param state      A pointer to the state of the library.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_inspect_all(dpi_library_state_t* state);

/**
 * Disable all the protocol inspector.
 * @param state      A pointer to the state of the library.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_inspect_nothing(dpi_library_state_t* state);

/**
 * Skips the L7 parsing for packets traveling on some ports for some L4
 * protocol.
 * @param state A pointer to the state of the library.
 * @param l4prot The L4 protocol.
 * @param port The port.
 * @param id The protocol id that will be assigned to packets that matches with
 * this rule. If
 * id >= DPI_NUM_PROTOCOLS, it would be considered as a custom user protocol.
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_skip_L7_parsing_by_port(dpi_library_state_t* state, uint8_t l4prot,
                                    uint16_t port, dpi_l7_prot_id id);

/*
 * --- DEPRECATED, replaced by dpi_get_protocol ---
 * Try to detect the application protocol.
 * @param   state The state of the library.
 * @param   pkt The pointer to the beginning of IP header.
 * @param   data_length Length of the packet (from the beginning of the IP
 *          header, without L2 headers/trailers).
 * @param   current_time The current time in seconds.
 * @return  The status of the operation.  It gives additional informations
 *          about the processing of the request. If lesser than 0, an error
 *          occurred. dpi_get_error_msg() can be used to get a textual
 *          representation of the error. If greater or equal than 0 then
 *          it should not be interpreted as an error but simply gives
 *          additional informations (e.g. if the packet was IP fragmented,
 *          if it was out of order in the TCP stream, if is a segment of a
 *          larger application request, etc..). dpi_get_status_msg() can
 *          be used to get a textual representation of the status. Status
 *          and error codes are defined above in this header file. If an
 *          error occurred, the other returned fields are not meaningful.
 *
 *          The application protocol identifier plus the transport
 *          protocol identifier. The application protocol identifier is
 *          relative to the specific transport protocol.
 *
 * 			The flow specific user data (possibly manipulated by the
 * 			user callbacks).
 */
dpi_identification_result_t dpi_stateful_identify_application_protocol(
    dpi_library_state_t* state, const unsigned char* pkt, uint32_t length,
    uint32_t current_time);

/*
 * Try to detect the application protocol.
 * @param   state The state of the library.
 * @param   pkt The pointer to the beginning of IP header.
 * @param   data_length Length of the packet (from the beginning of the IP
 *          header, without L2 headers/trailers).
 * @param   current_time The current time in seconds.
 * @return  The status of the operation.  It gives additional informations
 *          about the processing of the request. If lesser than 0, an error
 *          occurred. dpi_get_error_msg() can be used to get a textual
 *          representation of the error. If greater or equal than 0 then
 *          it should not be interpreted as an error but simply gives
 *          additional informations (e.g. if the packet was IP fragmented,
 *          if it was out of order in the TCP stream, if is a segment of a
 *          larger application request, etc..). dpi_get_status_msg() can
 *          be used to get a textual representation of the status. Status
 *          and error codes are defined above in this header file. If an
 *          error occurred, the other returned fields are not meaningful.
 *
 *          The application protocol identifier plus the transport
 *          protocol identifier. The application protocol identifier is
 *          relative to the specific transport protocol.
 *
 * 			The flow specific user data (possibly manipulated by the
 * 			user callbacks).
 */
dpi_identification_result_t dpi_get_protocol(dpi_library_state_t* state,
                                             const unsigned char* pkt,
                                             uint32_t length,
                                             uint32_t current_time);

/*
 * Extract from the packet the informations about source and destination
 * addresses, source and destination ports, L4 protocol and the offset
 * where the application data starts.
 * @param   state The state of the library.
 * @param   pkt The pointer to the beginning of IP header.
 * @param   data_length Length of the packet (from the beginning of the
 *          IP header, without L2 headers/trailers).
 * @param   pkt_infos The pointer to the packet infos. It will be filled
 *          by the library.
 * @param   current_time The current time in seconds. It must be
 *          non-decreasing between two consecutive calls.
 * @return  The status of the operation. It gives additional informations
 *          about the processing of the request. If lesser than 0, an
 *          error occurred. dpi_get_error_msg() can be used to get a
 *          textual representation of the error. If greater or equal
 *          than 0 then it should not be interpreted as an error but
 *          simply gives additional informations (e.g. if the packet was
 *          IP fragmented, if it was out of order in the TCP stream, if is
 *          a segment of a larger application request, etc..).
 *          dpi_get_status_msg() can be used to get a textual
 *          representation of the status. Status and error codes are
 *          defined above in this header file.
 *
 *          The status is DPI_STATUS_IP_FRAGMENT if the datagram is a
 *          fragment. In this case, if IP fragmentation support is enabled,
 *          the library copied the content of the datagram, so if the user
 *          wants, he can release the resources used to store the datagram.
 *
 *          The status is DPI_STATUS_IP_LAST_FRAGMENT if the received
 *          datagram allows the library to reconstruct a fragmented
 *          datagram. In this case, pkt_infos->pkt will contain a pointer
 *          to the recomposed datagram. This pointer will be different
 *          from p_pkt. The user should free() this pointer when it is no
 *          more needed (e.g. after calling
 *          dpi_state*_get_app_protocol(..)).
 */
int8_t dpi_parse_L3_L4_headers(dpi_library_state_t* state,
                               const unsigned char* p_pkt, uint32_t p_length,
                               dpi_pkt_infos_t* pkt_infos,
                               uint32_t current_time);

/*
 * Try to detect the application protocol. Before calling it, a check on
 * L4 protocol should be done and the function should be called only if
 * the packet is TCP or UDP.
 * @param   state The pointer to the library state.
 * @param   pkt_infos The pointer to the packet infos.
 * @return  The status of the operation. It gives additional informations
 *          about the processing of the request. If lesser than 0, an
 *          error occurred. dpi_get_error_msg() can be used to get a
 *          textual representation of the error. If greater or equal
 *          than 0 then it should not be interpreted as an error but
 *          simply gives additional informations (e.g. if the packet was
 *          IP fragmented, if it was out of order in the TCP stream, if is
 *          a segment of a larger application request, etc..).
 *          dpi_get_status_msg() can be used to get a textual
 *          representation of the status. Status and error codes are
 *          defined above in this header file.
 *
 *          The status is DPI_STATUS_IP_FRAGMENT if the datagram is a
 *          fragment. In this case, if IP fragmentation support is
 *          enabled, the library copied the content of the datagram, so if
 *          the user wants, he can release the resources used to store the
 *          datagram.
 *
 *          The status is DPI_STATUS_IP_LAST_FRAGMENT if the received
 *          datagram allows the library to reconstruct a fragmented
 *          datagram. In this case, pkt_infos->pkt will contain a pointer
 *          to the recomposed datagram. This pointer will be different
 *          from p_pkt. The user should free() this pointer when it is no
 *          more needed (e.g. after calling
 *          dpi_state*_get_app_protocol(..)).
 */
dpi_identification_result_t dpi_stateful_get_app_protocol(
    dpi_library_state_t* state, dpi_pkt_infos_t* pkt_infos);

/*
 * Try to detect the application protocol. Before calling it, a check on
 * L4 protocol should be done and the function should be called only if
 * the packet is TCP or UDP. It should be used if the application already
 * has the concept of 'flow'. In this case the first time that the flow is
 * passed to the call, it must be initialized with
 * dpi_init_flow_infos(...).
 * @param   state The pointer to the library state.
 * @param   flow The informations about the flow. They must be kept by the
 *               user.
 * @param   pkt_infos The pointer to the packet infos.
 * @return  The status of the operation. It gives additional informations
 *          about the processing of the request. If lesser than 0, an error
 *          occurred. dpi_get_error_msg() can be used to get a textual
 *          representation of the error. If greater or equal than 0 then
 *          it should not be interpreted as an error but simply gives
 *          additional informations (e.g. if the packet was IP fragmented,
 *          if it was out of order in the TCP stream, if is a segment of
 *          a larger application request, etc..). dpi_get_status_msg()
 *          can be used to get a textual representation of the status.
 *          Status and error codes are defined above in this header file.
 *
 *          The status is DPI_STATUS_IP_FRAGMENT if the datagram is a
 *          fragment. In this case, if IP fragmentation support is
 *          enabled, the library copied the content of the datagram, so if
 *          the user wants, he can release the resources used to store the
 *          datagram.
 *
 *          The status is DPI_STATUS_IP_LAST_FRAGMENT if the received
 *          datagram allows the library to reconstruct a fragmented
 *          datagram. In this case, pkt_infos->pkt will contain a pointer
 *          to the recomposed datagram. This pointer will be different
 *          from p_pkt. The user should free() this pointer when it is no
 *          more needed (e.g. after calling
 *          dpi_state*_get_app_protocol(..)).
 */
dpi_identification_result_t dpi_stateless_get_app_protocol(
    dpi_library_state_t* state, dpi_flow_infos_t* flow,
    dpi_pkt_infos_t* pkt_infos);

/**
 * Initialize the flow informations passed as argument.
 * @param state       A pointer to the state of the library.
 * @param flow_infos  The informations that will be initialized by the
 *                    library.
 * @param l4prot      The transport protocol identifier.
 */
void dpi_init_flow_infos(dpi_library_state_t* state,
                         dpi_flow_infos_t* flow_infos, uint8_t l4prot);

/**
 * Try to guess the protocol looking only at source/destination ports.
 * This could be erroneous because sometimes protocols run over ports
 * which are not their well-known ports.
 * @param    pkt_infos The pointer to the packet infos.
 * @return   Returns the possible matching protocol.
 */
dpi_protocol_t dpi_guess_protocol(dpi_pkt_infos_t* pkt_infos);

/**
 * Get the string representing the error message associated to the
 * specified error_code.
 * @param   error_code The error code.
 * @return  The error message.
 */
const char* const dpi_get_error_msg(int8_t error_code);

/**
 * Get the string representing the status message associated to the
 * specified status_code.
 * @param   status_code The status code.
 * @return  The status message.
 */
const char* const dpi_get_status_msg(int8_t status_code);

/**
 * --- DEPRECATED, replaced by dpi_get_protocol_string ---
 * Returns a string corresponding to the given protocol.
 * @param   protocol The protocol identifier.
 * @return  A string representation of the given protocol.
 */
const char* const dpi_get_protocol_name(dpi_protocol_t protocol);

/**
 * Returns the string represetation of a protocol.
 * @param   protocol The protocol identifier.
 * @return  The string representation of the protocol with id 'protocol'.
 */
const char* const dpi_get_protocol_string(dpi_l7_prot_id protocol);

/**
 * Returns the protocol id corresponding to a protocol string.
 * @param string The protocols tring.
 * @return The protocol id corresponding to a protocol string.
 */
dpi_l7_prot_id dpi_get_protocol_id(const char* const string);

/**
 * Returns the string represetations of the protocols.
 * @return  An array A of string, such that A[i] is the
 * string representation of the protocol with id 'i'.
 */
const char** const dpi_get_protocols_strings();

/**
 * Sets the callback that will be called when a flow expires.
 * (Valid only if stateful API is used).
 * @param state     A pointer to the state of the library.
 * @param cleaner   The callback used to clear the user state.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_set_flow_cleaner_callback(dpi_library_state_t* state,
                                      dpi_flow_cleaner_callback* cleaner);

/**
 * Activate HTTP callbacks. When a protocol is identified the default
 * behavior is to not inspect the packets belonging to that flow anymore
 * and keep simply returning the same protocol identifier.
 *
 * If a callback is enabled for a certain protocol, then we keep
 * inspecting all the new flows with that protocol in order to invoke
 * the callbacks specified by the user on the various parts of the
 * message. Moreover, if the application protocol uses TCP, then we have
 * the additional cost of TCP reordering for all the segments. Is highly
 * recommended to enable TCP reordering if it is not already enabled
 * (remember that is enabled by default). Otherwise the informations
 * extracted could be erroneous/incomplete.
 *
 * The pointers to the data passed to the callbacks are valid only for the
 * duration of the callback.
 *
 * @param state       A pointer to the state of the library.
 * @param callbacks   A pointer to HTTP callbacks.
 * @param user_data   A pointer to global user HTTP data. This pointer
 *                    will be passed to any HTTP callback when it is
 *                    invoked.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 *
 **/
uint8_t dpi_http_activate_callbacks(dpi_library_state_t* state,
                                    dpi_http_callbacks_t* callbacks,
                                    void* user_data);

/**
 * Disable the HTTP callbacks. user_data is not freed/modified.
 * @param state       A pointer to the state of the library.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_http_disable_callbacks(dpi_library_state_t* state);

/**
    SSL callbacks.
**/
/**
 * Activate SSL callbacks. When a protocol is identified the default
 * behavior is to not inspect the packets belonging to that flow anymore
 * and keep simply returning the same protocol identifier.
 *
 * If a callback is enabled for a certain protocol, then we keep
 * inspecting all the new flows with that protocol in order to invoke
 * the callbacks specified by the user on the various parts of the
 * message. Moreover, if the application protocol uses TCP, then we have
 * the additional cost of TCP reordering for all the segments. Is highly
 * recommended to enable TCP reordering if it is not already enabled
 * (remember that is enabled by default). Otherwise the informations
 * extracted could be erroneous/incomplete.
 *
 * The pointers to the data passed to the callbacks are valid only for the
 * duration of the callback.
 *
 * @param state       A pointer to the state of the library.
 * @param callbacks   A pointer to SSL callbacks.
 * @param user_data   A pointer to global user SSL data. This pointer
 *                    will be passed to any SSL callback when it is
 *                    invoked.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 *
 **/
uint8_t dpi_ssl_activate_callbacks(dpi_library_state_t* state,
                                   dpi_ssl_callbacks_t* callbacks,
                                   void* user_data);
/**
 * Disable the SSL callbacks. user_data is not freed/modified.
 * @param state       A pointer to the state of the library.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_ssl_disable_callbacks(dpi_library_state_t* state);

/**
 * SIP callbacks.
 **/
/**
 * Activate SIP callbacks. When a protocol is identified the default
 * behavior is to not inspect the packets belonging to that flow anymore
 * and keep simply returning the same protocol identifier.
 *
 * If a callback is enabled for a certain protocol, then we keep
 * inspecting all the new flows with that protocol in order to invoke
 * the callbacks specified by the user on the various parts of the
 * message. Moreover, if the application protocol uses TCP, then we have
 * the additional cost of TCP reordering for all the segments. Is highly
 * recommended to enable TCP reordering if it is not already enabled
 * (remember that is enabled by default). Otherwise the informations
 * extracted could be erroneous/incomplete.
 *
 * The pointers to the data passed to the callbacks are valid only for the
 * duration of the callback.
 *
 * @param state       A pointer to the state of the library.
 * @param callbacks   A pointer to SIP callbacks.
 * @param user_data   A pointer to global user SIP data. This pointer
 *                    will be passed to any SIP callback when it is
 *                    invoked.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 *
 **/
uint8_t dpi_sip_activate_callbacks(dpi_library_state_t* state,
                                   dpi_sip_callbacks_t* callbacks,
                                   void* user_data);

/**
 * Disable the SIP callbacks. user_data is not freed/modified.
 * @param state       A pointer to the state of the library.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_sip_disable_callbacks(dpi_library_state_t* state);

/**
 * Returns the number of fields extracted for a specific protocol.
 * @param state    A pointer to the state of the library.
 * @param protocol The protocol.
 * @return The number of fields extracted for a specific protocol.
 */
size_t pfwl_callbacks_fields_get_num(dpi_library_state_t* state,
                                     dpi_l7_prot_id protocol);

/**
 * Set a field callback for a given protocol.
 * When a protocol is identified the default
 * behavior is to not inspect the packets belonging to that flow anymore
 * and keep simply returning the same protocol identifier.
 *
 * If a callback is enabled for a certain protocol, then we keep
 * inspecting all the new packets of that flow in order to invoke
 * the callbacks specified by the user on the various parts of the
 * message. Moreover, if the application protocol uses TCP, then we have
 * the additional cost of TCP reordering for all the segments. Is highly
 * recommended to enable TCP reordering if it is not already enabled
 * (remember that is enabled by default). Otherwise the informations
 * extracted could be erroneous/incomplete.
 *
 * The pointers to the data passed to the callbacks are valid only for the
 * duration of the callback. If the user needs to preserve the data, a copy
 * needs to be done.
 *
 * @param state        A pointer to the state of the library.
 * @param protocol     The protocol.
 * @param field_type   The field (check the enum for that specific protocol).
 * @param callback     The callback.
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 *
 **/
uint8_t pfwl_callbacks_field_add(dpi_library_state_t* state,
                                dpi_l7_prot_id protocol,
                                int field_type,
                                pfwl_field_callback* callback);

/**
 * Disable the a protocol field callback. udata is not freed/modified.
 * @param state       A pointer to the state of the library.
 * @param protocol     The protocol.
 * @param field_type   The field (check the enum for that specific protocol).
 *
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t pfwl_callbacks_field_remove(dpi_library_state_t* state,
                                    dpi_l7_prot_id protocol,
                                    int field_type);

/**
 * Adds a pointer to some data which will be passed as parameter to all
 * the fields callbacks.
 * @param state A pointer to the state of the library.
 * @param udata
 * @return
 */
uint8_t pfwl_callbacks_fields_set_udata(dpi_library_state_t* state,
                                 void* udata);


/**
 * Some protocols inspector (e.g. SIP) can be applied with a different
 * level of accuracy (and of processing time). By using this call
 * the user can decide if running the inspector in its most accurate
 * version (at the cost of a higher processing latency).
 * @param state       A pointer to the state of the library.
 * @param protocol    The protocol for which we want to change the accuracy.
 * @param accuracy    The accuracy level.
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_set_protocol_accuracy(dpi_library_state_t* state,
                                  dpi_l7_prot_id protocol,
                                  dpi_inspector_accuracy accuracy);

/**
 * Initializes the exporter to Prometheus DB.
 * @param state       A pointer to the state of the library.
 * @param port        The port on which the server should listen.
 * @return DPI_STATE_UPDATE_SUCCESS if succeeded,
 *         DPI_STATE_UPDATE_FAILURE otherwise.
 */
uint8_t dpi_prometheus_init(dpi_library_state_t* state, uint16_t port);

/****************************************/
/** Only to be used directly by mcdpi. **/
/****************************************/
dpi_library_state_t* dpi_init_stateful_num_partitions(
    uint32_t size_v4, uint32_t size_v6, uint32_t max_active_v4_flows,
    uint32_t max_active_v6_flows, uint16_t num_table_partitions);
int8_t mc_dpi_extract_packet_infos(dpi_library_state_t* state,
                                   const unsigned char* p_pkt,
                                   uint32_t p_length,
                                   dpi_pkt_infos_t* pkt_infos,
                                   uint32_t current_time, int tid);

#ifdef __cplusplus
}
#endif

#endif
