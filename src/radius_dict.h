/*
 * NPS Log Viewer
 * (C) 2026, Eugene Nesterenko - eenest@eenest.net
 */

#ifndef RADIUS_DICT_H
#define RADIUS_DICT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard RADIUS attributes (RFC 2865, 2866, IANA RADIUS Types registry, etc.) */
const char* radius_attr_name(int code);

/* Microsoft vendor-specific attributes (Vendor ID 311) */
const char* ms_radius_attr_name(int code);

/* Vendor-Specific Attribute helpers */
const char* radius_vendor_name(unsigned int vendor_id);
const char* radius_vendor_attr_name(unsigned int vendor_id, unsigned int vendor_type);
int decode_vendor_specific(const char* value, char* out, size_t out_size);
int decode_radius_field(const char* name, const char* value, char* out, size_t out_size);

/* Common value decoders */
const char* decode_service_type(int value);
const char* decode_framed_protocol(int value);
const char* decode_framed_routing(int value);
const char* decode_framed_compression(int value);
const char* decode_login_service(int value);
const char* decode_acct_status_type(int value);
const char* decode_nas_port_type(int value);
const char* decode_tunnel_type(int value);
const char* decode_tunnel_medium_type(int value);
const char* decode_termination_action(int value);
const char* decode_acct_authentic(int value);
const char* decode_acct_terminate_cause(int value);
const char* decode_arap_zone_access(int value);
const char* decode_prompt(int value);
const char* decode_packet_type(int value);
const char* decode_reason_code(int value);
const char* decode_authentication_type(int value);
const char* decode_eap_type(int value);

#ifdef __cplusplus
}
#endif

#endif
