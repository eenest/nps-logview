/*
 * NPS Log Viewer
 * (C) 2026, Eugene Nesterenko - eenest@eenest.net
 */

#include "radius_dict.h"
#include "compat.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MAX_VSA_BYTES 1024

const char* radius_attr_name(int code)
{
    static const char* names[247] = {
        [1] = "User-Name",
        [2] = "User-Password",
        [3] = "CHAP-Password",
        [4] = "NAS-IP-Address",
        [5] = "NAS-Port",
        [6] = "Service-Type",
        [7] = "Framed-Protocol",
        [8] = "Framed-IP-Address",
        [9] = "Framed-IP-Netmask",
        [10] = "Framed-Routing",
        [11] = "Filter-Id",
        [12] = "Framed-MTU",
        [13] = "Framed-Compression",
        [14] = "Login-IP-Host",
        [15] = "Login-Service",
        [16] = "Login-TCP-Port",
        [18] = "Reply-Message",
        [19] = "Callback-Number",
        [20] = "Callback-Id",
        [22] = "Framed-Route",
        [23] = "Framed-IPX-Network",
        [24] = "State",
        [25] = "Class",
        [26] = "Vendor-Specific",
        [27] = "Session-Timeout",
        [28] = "Idle-Timeout",
        [29] = "Termination-Action",
        [30] = "Called-Station-Id",
        [31] = "Calling-Station-Id",
        [32] = "NAS-Identifier",
        [33] = "Proxy-State",
        [34] = "Login-LAT-Service",
        [35] = "Login-LAT-Node",
        [36] = "Login-LAT-Group",
        [37] = "Framed-AppleTalk-Link",
        [38] = "Framed-AppleTalk-Network",
        [39] = "Framed-AppleTalk-Zone",
        [40] = "Acct-Status-Type",
        [41] = "Acct-Delay-Time",
        [42] = "Acct-Input-Octets",
        [43] = "Acct-Output-Octets",
        [44] = "Acct-Session-Id",
        [45] = "Acct-Authentic",
        [46] = "Acct-Session-Time",
        [47] = "Acct-Input-Packets",
        [48] = "Acct-Output-Packets",
        [49] = "Acct-Terminate-Cause",
        [50] = "Acct-Multi-Session-Id",
        [51] = "Acct-Link-Count",
        [52] = "Acct-Input-Gigawords",
        [53] = "Acct-Output-Gigawords",
        [55] = "Event-Timestamp",
        [56] = "Egress-VLANID",
        [57] = "Ingress-Filters",
        [58] = "Egress-VLAN-Name",
        [59] = "User-Priority-Table",
        [60] = "CHAP-Challenge",
        [61] = "NAS-Port-Type",
        [62] = "Port-Limit",
        [63] = "Login-LAT-Port",
        [64] = "Tunnel-Type",
        [65] = "Tunnel-Medium-Type",
        [66] = "Tunnel-Client-Endpoint",
        [67] = "Tunnel-Server-Endpoint",
        [68] = "Acct-Tunnel-Connection",
        [69] = "Tunnel-Password",
        [70] = "ARAP-Password",
        [71] = "ARAP-Features",
        [72] = "ARAP-Zone-Access",
        [73] = "ARAP-Security",
        [74] = "ARAP-Security-Data",
        [75] = "Password-Retry",
        [76] = "Prompt",
        [77] = "Connect-Info",
        [78] = "Configuration-Token",
        [79] = "EAP-Message",
        [80] = "Message-Authenticator",
        [81] = "Tunnel-Private-Group-ID",
        [82] = "Tunnel-Assignment-ID",
        [83] = "Tunnel-Preference",
        [84] = "ARAP-Challenge-Response",
        [85] = "Acct-Interim-Interval",
        [86] = "Acct-Tunnel-Packets-Lost",
        [87] = "NAS-Port-Id",
        [88] = "Framed-Pool",
        [89] = "CUI",
        [90] = "Tunnel-Client-Auth-ID",
        [91] = "Tunnel-Server-Auth-ID",
        [92] = "NAS-Filter-Rule",
        [94] = "Originating-Line-Info",
        [95] = "NAS-IPv6-Address",
        [96] = "Framed-Interface-Id",
        [97] = "Framed-IPv6-Prefix",
        [98] = "Login-IPv6-Host",
        [99] = "Framed-IPv6-Route",
        [100] = "Framed-IPv6-Pool",
        [101] = "Error-Cause",
        [102] = "EAP-Key-Name",
        [103] = "Digest-Response",
        [104] = "Digest-Realm",
        [105] = "Digest-Nonce",
        [106] = "Digest-Response-Auth",
        [107] = "Digest-Nextnonce",
        [108] = "Digest-Method",
        [109] = "Digest-URI",
        [110] = "Digest-Qop",
        [111] = "Digest-Algorithm",
        [112] = "Digest-Entity-Body-Hash",
        [113] = "Digest-CNonce",
        [114] = "Digest-Nonce-Count",
        [115] = "Digest-Username",
        [116] = "Digest-Opaque",
        [117] = "Digest-Auth-Param",
        [118] = "Digest-AKA-Auts",
        [119] = "Digest-Domain",
        [120] = "Digest-Stale",
        [121] = "Digest-HA1",
        [122] = "SIP-AOR",
        [123] = "Delegated-IPv6-Prefix",
        [124] = "MIP6-Feature-Vector",
        [125] = "MIP6-Home-Link-Prefix",
        [126] = "Operator-Name",
        [127] = "Location-Information",
        [128] = "Location-Data",
        [129] = "Basic-Location-Policy-Rules",
        [130] = "Extended-Location-Policy-Rules",
        [131] = "Location-Capable",
        [132] = "Requested-Location-Info",
        [133] = "Framed-Management-Protocol",
        [134] = "Management-Transport-Protection",
        [135] = "Management-Policy-Id",
        [136] = "Management-Privilege-Level",
        [137] = "PKM-SS-Cert",
        [138] = "PKM-CA-Cert",
        [139] = "PKM-Config-Settings",
        [140] = "PKM-Cryptosuite-List",
        [141] = "PKM-SAID",
        [142] = "PKM-SA-Descriptor",
        [143] = "PKM-Auth-Key",
        [144] = "DS-Lite-Tunnel-Name",
        [145] = "Mobile-Node-Identifier",
        [146] = "Service-Selection",
        [147] = "PMIP6-Home-LMA-IPv6-Address",
        [148] = "PMIP6-Visited-LMA-IPv6-Address",
        [149] = "PMIP6-Home-LMA-IPv4-Address",
        [150] = "PMIP6-Visited-LMA-IPv4-Address",
        [151] = "PMIP6-Home-HN-Prefix",
        [152] = "PMIP6-Visited-HN-Prefix",
        [153] = "PMIP6-Home-Interface-ID",
        [154] = "PMIP6-Visited-Interface-ID",
        [155] = "PMIP6-Home-IPv4-HoA",
        [156] = "PMIP6-Visited-IPv4-HoA",
        [157] = "PMIP6-Home-DHCP4-Server-Address",
        [158] = "PMIP6-Visited-DHCP4-Server-Address",
        [159] = "PMIP6-Home-DHCP6-Server-Address",
        [160] = "PMIP6-Visited-DHCP6-Server-Address",
        [161] = "PMIP6-Home-IPv4-Gateway",
        [162] = "PMIP6-Visited-IPv4-Gateway",
        [163] = "EAP-Lower-Layer",
        [164] = "GSS-Acceptor-Service-Name",
        [165] = "GSS-Acceptor-Host-Name",
        [166] = "GSS-Acceptor-Service-Specifics",
        [167] = "GSS-Acceptor-Realm-Name",
        [168] = "Framed-IPv6-Address",
        [169] = "DNS-Server-IPv6-Address",
        [170] = "Route-IPv6-Information",
        [171] = "Delegated-IPv6-Prefix-Pool",
        [172] = "Stateful-IPv6-Address-Pool",
        [173] = "IPv6-6rd-Configuration",
        [174] = "Allowed-Called-Station-Id",
        [175] = "EAP-Peer-Id",
        [176] = "EAP-Server-Id",
        [177] = "Mobility-Domain-Id",
        [178] = "Preauth-Timeout",
        [179] = "Network-Id-Name",
        [180] = "EAPoL-Announcement",
        [181] = "WLAN-HESSID",
        [182] = "WLAN-Venue-Info",
        [183] = "WLAN-Venue-Language",
        [184] = "WLAN-Venue-Name",
        [185] = "WLAN-Reason-Code",
        [186] = "WLAN-Pairwise-Cipher",
        [187] = "WLAN-Group-Cipher",
        [188] = "WLAN-AKM-Suite",
        [189] = "WLAN-Group-Mgmt-Cipher",
        [190] = "WLAN-RF-Band",
        [241] = "Extended-Attribute-1",
        [242] = "Extended-Attribute-2",
        [243] = "Extended-Attribute-3",
        [244] = "Extended-Attribute-4",
        [245] = "Extended-Attribute-5",
        [246] = "Extended-Attribute-6"
    };

    if (code < 0 || code > 255) return "Unknown";
    if (code >= 192 && code <= 223) return "Experimental Use";
    if (code >= 224 && code <= 240) return "Implementation Specific";
    if (code >= 247 && code <= 255) return "Reserved";
    if (code < (int)(sizeof(names) / sizeof(names[0])) && names[code]) return names[code];
    return "Unassigned";
}

const char* ms_radius_attr_name(int code)
{
    switch (code) {
        case 1:  return "MS-CHAP-Response";
        case 2:  return "MS-CHAP-Error";
        case 3:  return "MS-CHAP-CPW-1";
        case 4:  return "MS-CHAP-CPW-2";
        case 5:  return "MS-CHAP-LM-Enc-PW";
        case 6:  return "MS-CHAP-NT-Enc-PW";
        case 7:  return "MS-MPPE-Encryption-Policy";
        case 8:  return "MS-MPPE-Encryption-Types";
        case 9:  return "MS-RAS-Vendor";
        case 10: return "MS-CHAP-Domain";
        case 11: return "MS-CHAP-Challenge";
        case 12: return "MS-CHAP-MPPE-Keys";
        case 13: return "MS-BAP-Usage";
        case 14: return "MS-Link-Utilization-Threshold";
        case 15: return "MS-Link-Drop-Time-Limit";
        case 16: return "MS-MPPE-Send-Key";
        case 17: return "MS-MPPE-Recv-Key";
        case 18: return "MS-RAS-Version";
        case 19: return "MS-Old-ARAP-Password";
        case 20: return "MS-New-ARAP-Password";
        case 21: return "MS-ARAP-PW-Change-Reason";
        case 22: return "MS-Filter";
        case 23: return "MS-Acct-Auth-Type";
        case 24: return "MS-Acct-EAP-Type";
        case 25: return "MS-CHAP2-Response";
        case 26: return "MS-CHAP2-Success";
        case 27: return "MS-CHAP2-CPW";
        case 29: return "MS-Primary-DNS-Server";
        case 30: return "MS-Secondary-DNS-Server";
        case 31: return "MS-Primary-NBNS-Server";
        case 32: return "MS-Secondary-NBNS-Server";
        case 33: return "MS-ARAP-Gateway";
        case 34: return "MS-ARAP-Security-Data";
        case 35: return "MS-Password-Changeable";
        case 36: return "MS-Password-Expiry";
        case 37: return "MS-MPPE-Recv-Key";
        case 38: return "MS-MPPE-Send-Key";
        case 39: return "MS-Quarantine-IPFilter";
        case 40: return "MS-Quarantine-Session-Timeout";
        case 41: return "MS-User-Security-Identity";
        case 42: return "MS-Identity-Type";
        case 43: return "MS-Service-Class";
        case 44: return "MS-Quarantine-State";
        case 45: return "MS-Quarantine-Grace-Time";
        case 46: return "MS-Network-Access-Server-Type";
        case 47: return "MS-AFW-Zone";
        case 48: return "MS-AFW-Protection-Level";
        case 49: return "MS-Machine-Name";
        case 50: return "MS-IPv6-Filter";
        case 51: return "MS-IPv6-Policy-Name";
        case 52: return "MS-Health-Valid-S-Cat";
        case 53: return "MS-Encryption-Policy";
        case 54: return "MS-Encryption-Types";
        case 55: return "MS-PEAP-Fast-Roamed-Session";
        case 56: return "MS-Identity-Type";
        case 57: return "MS-Service-Class";
        case 58: return "MS-Quarantine-User-Class";
        case 59: return "MS-Quarantine-State-Error";
        case 60: return "MS-Quarantine-Session-ID";
        case 61: return "MS-Quarantine-Hash";
        case 62: return "MS-Quarantine-System-Health-Result";
        case 63: return "MS-Quarantine-User-Health-Result";
        case 64: return "MS-Quarantine-Process-Name";
        case 65: return "MS-Quarantine-URL";
        case 66: return "MS-Quarantine-Fixup-Servers";
        case 67: return "MS-Quarantine-Fixup-Interval";
        case 68: return "MS-Quarantine-Fixup-Retry-Count";
        case 69: return "MS-Quarantine-Fixup-Error-ID";
        case 70: return "MS-Quarantine-Fixup-Error-Message";
        case 71: return "MS-Quarantine-Fixup-Has-Error";
        case 72: return "MS-Quarantine-Fixup-Status";
        case 73: return "MS-Quarantine-Fixup-Has-Rem-Server";
        case 74: return "MS-Quarantine-Has-Rem-Server";
        case 75: return "MS-Quarantine-Rem-Server-String";
        case 76: return "MS-Quarantine-Rem-Server-Type";
        case 77: return "MS-Quarantine-Rem-Server-Flags";
        case 78: return "MS-Quarantine-Rem-Server-URL";
        case 79: return "MS-Quarantine-Rem-Server-IP";
        case 80: return "MS-Quarantine-Rem-Server-Port";
        case 81: return "MS-Quarantine-Rem-Server-Path";
        case 82: return "MS-Quarantine-Rem-Server-Script";
        case 83: return "MS-Quarantine-Rem-Server-Param";
        case 84: return "MS-Quarantine-Rem-Server-Method";
        case 85: return "MS-Quarantine-Rem-Server-Version";
        case 86: return "MS-Quarantine-Rem-Server-User-Agent";
        case 87: return "MS-Quarantine-Rem-Server-Referer";
        case 88: return "MS-Quarantine-Rem-Server-Content-Type";
        case 89: return "MS-Quarantine-Rem-Server-Content-Length";
        case 90: return "MS-Quarantine-Rem-Server-Headers";
        case 91: return "MS-Quarantine-Rem-Server-Body";
        case 92: return "MS-Quarantine-Rem-Server-Cookies";
        case 93: return "MS-Quarantine-Rem-Server-Response-Code";
        case 94: return "MS-Quarantine-Rem-Server-Response-Text";
        case 95: return "MS-Quarantine-Rem-Server-Response-Headers";
        case 96: return "MS-Quarantine-Rem-Server-Response-Body";
        case 97: return "MS-Quarantine-Rem-Server-Response-Cookies";
        case 98: return "MS-Quarantine-Rem-Server-Response-Time";
        case 99: return "MS-Quarantine-Rem-Server-Retry-Count";
        case 100: return "MS-Quarantine-Rem-Server-Retry-Interval";
        default: return "Unknown-MS";
    }
}

const char* radius_vendor_name(unsigned int vendor_id)
{
    switch (vendor_id) {
        case 9:     return "Cisco";
        case 311:   return "Microsoft";
        case 2636:  return "Juniper";
        case 6889:  return "Avaya";
        case 12356: return "Fortinet";
        case 25461: return "Palo Alto Networks";
        case 29671: return "Cisco Meraki";
        default:    return "Unknown Vendor";
    }
}

static const char* cisco_radius_attr_name(unsigned int code)
{
    switch (code) {
        case 1:   return "Cisco-AVPair";
        case 2:   return "Cisco-NAS-Port";
        case 3:   return "Cisco-Fax-Account-Id-Origin";
        case 4:   return "Cisco-Fax-Msg-Id";
        case 5:   return "Cisco-Fax-Pages";
        case 6:   return "Cisco-Fax-Coverpage-Flag";
        case 7:   return "Cisco-Fax-Modem-Time";
        case 8:   return "Cisco-Fax-Connect-Speed";
        case 9:   return "Cisco-Fax-Recipient-Count";
        case 10:  return "Cisco-Fax-Process-Abort-Flag";
        case 11:  return "Cisco-Fax-Dsn-Address";
        case 12:  return "Cisco-Fax-Dsn-Flag";
        case 13:  return "Cisco-Fax-Mdn-Address";
        case 14:  return "Cisco-Fax-Mdn-Flag";
        case 15:  return "Cisco-Fax-Auth-Status";
        case 16:  return "Cisco-Email-Server-Address";
        case 17:  return "Cisco-Email-Server-Ack-Flag";
        case 18:  return "Cisco-Gateway-Id";
        case 19:  return "Cisco-Call-Type";
        case 20:  return "Cisco-Port-Used";
        case 21:  return "Cisco-Abort-Cause";
        case 22:  return "Cisco-Fax-Session-Id";
        case 23:  return "Cisco-Multilink-ID";
        case 24:  return "Cisco-Num-In-Multilink";
        case 25:  return "Cisco-Pre-Input-Octets";
        case 26:  return "Cisco-Pre-Output-Octets";
        case 27:  return "Cisco-Pre-Input-Packets";
        case 28:  return "Cisco-Pre-Output-Packets";
        case 29:  return "Cisco-Maximum-Time";
        case 30:  return "Cisco-Disconnect-Cause";
        case 31:  return "Cisco-Data-Rate";
        case 32:  return "Cisco-PreSession-Time";
        case 33:  return "Cisco-PW-Lifetime";
        case 34:  return "Cisco-IP-Direct";
        case 35:  return "Cisco-PPP-VJ-Slot-Comp";
        case 36:  return "Cisco-PPP-Async-Map";
        case 37:  return "Cisco-IP-Pool-Definition";
        case 38:  return "Cisco-Assign-IP-Pool";
        case 39:  return "Cisco-Route-IP";
        case 40:  return "Cisco-Link-Compression";
        case 41:  return "Cisco-Target-Util";
        case 42:  return "Cisco-Maximum-Channels";
        case 43:  return "Cisco-Data-Filter";
        case 44:  return "Cisco-Call-Filter";
        case 45:  return "Cisco-Idle-Limit";
        case 46:  return "Cisco-Account-Info";
        case 47:  return "Cisco-Service-Info";
        case 48:  return "Cisco-Command-Code";
        case 49:  return "Cisco-Control-Info";
        case 250: return "Cisco-Account-Info";
        case 251: return "Cisco-Service-Info";
        case 252: return "Cisco-Command-Code";
        case 253: return "Cisco-Control-Info";
        default:  return NULL;
    }
}

static const char* juniper_radius_attr_name(unsigned int code)
{
    switch (code) {
        case 1:   return "Juniper-Local-User-Name";
        case 2:   return "Juniper-Allow-Commands";
        case 3:   return "Juniper-Deny-Commands";
        case 4:   return "Juniper-Allow-Configuration";
        case 5:   return "Juniper-Deny-Configuration";
        case 6:   return "Juniper-User-Permissions";
        case 7:   return "Juniper-Junosspace-Profile";
        case 8:   return "Juniper-Interactive-Command";
        case 9:   return "Juniper-Configuration-Change";
        case 10:  return "Juniper-Ingress-Policy-Name";
        case 11:  return "Juniper-Egress-Policy-Name";
        case 31:  return "Juniper-Primary-DNS";
        case 32:  return "Juniper-Primary-WINS";
        case 33:  return "Juniper-Secondary-DNS";
        case 34:  return "Juniper-Secondary-WINS";
        case 35:  return "Juniper-Interface-ID";
        case 46:  return "Juniper-PPP-Profile-Name";
        case 48:  return "Juniper-PPP-Logical-System";
        case 49:  return "Juniper-PPP-Routing-Instance";
        case 130: return "Juniper-Qos-Set-Name";
        default:  return NULL;
    }
}

static const char* fortinet_radius_attr_name(unsigned int code)
{
    switch (code) {
        case 1:  return "Fortinet-Group-Name";
        case 2:  return "Fortinet-Client-IP-Address";
        case 3:  return "Fortinet-Vdom-Name";
        case 4:  return "Fortinet-Client-IPv6-Address";
        case 5:  return "Fortinet-Interface-Name";
        case 6:  return "Fortinet-Access-Profile";
        case 7:  return "Fortinet-SSID";
        case 8:  return "Fortinet-AP-Name";
        case 9:  return "Fortinet-Device-Name";
        case 10: return "Fortinet-Client-Name";
        case 11: return "Fortinet-Client-MAC-Address";
        case 12: return "Fortinet-Client-IPv6-Prefix";
        case 13: return "Fortinet-FortiAP-Name";
        case 14: return "Fortinet-FortiAP-Serial";
        case 15: return "Fortinet-FortiAP-Group";
        case 26: return "Fortinet-FWN-AVPair";
        case 40: return "Fortinet-FPC-User-Role";
        case 41: return "Fortinet-FPC-Tenant-Identification";
        default: return NULL;
    }
}

static const char* avaya_radius_attr_name(unsigned int code)
{
    switch (code) {
        case 1: return "Avaya-SBCE-Role-Name";
        default: return NULL;
    }
}

static const char* meraki_radius_attr_name(unsigned int code)
{
    switch (code) {
        case 1: return "Meraki-Group-Policy";
        default: return NULL;
    }
}

static const char* palo_alto_radius_attr_name(unsigned int code)
{
    switch (code) {
        case 1:  return "PaloAlto-Admin-Role";
        case 2:  return "PaloAlto-Admin-Access-Domain";
        case 3:  return "PaloAlto-Panorama-Admin-Role";
        case 4:  return "PaloAlto-Panorama-Admin-Access-Domain";
        case 5:  return "PaloAlto-User-Group";
        case 6:  return "PaloAlto-User-Domain";
        case 7:  return "PaloAlto-Client-Source-IP";
        case 8:  return "PaloAlto-Client-OS";
        case 9:  return "PaloAlto-Client-Hostname";
        case 10: return "PaloAlto-GlobalProtect-Client-Version";
        default: return NULL;
    }
}

const char* radius_vendor_attr_name(unsigned int vendor_id, unsigned int vendor_type)
{
    switch (vendor_id) {
        case 9:     return cisco_radius_attr_name(vendor_type);
        case 311: {
            const char* name = ms_radius_attr_name((int)vendor_type);
            return strcasecmp(name, "Unknown-MS") == 0 ? NULL : name;
        }
        case 2636:  return juniper_radius_attr_name(vendor_type);
        case 6889:  return avaya_radius_attr_name(vendor_type);
        case 12356: return fortinet_radius_attr_name(vendor_type);
        case 25461: return palo_alto_radius_attr_name(vendor_type);
        case 29671: return meraki_radius_attr_name(vendor_type);
        default:    return NULL;
    }
}

static int hex_value(int ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int is_hex_separator(int ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
           ch == ':' || ch == '-';
}

static void normalize_field_name(const char* name, char* out, size_t out_size)
{
    size_t i;

    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!name) return;

    for (i = 0; name[i] && i + 1 < out_size; i++) {
        out[i] = (name[i] == '_') ? '-' : name[i];
    }
    out[i] = '\0';
}

static int hex_to_bytes(const char* value, unsigned char* out, size_t out_cap, size_t* out_len)
{
    int high = -1;
    size_t len = 0;

    if (!value || !out || !out_len) return 0;

    for (size_t i = 0; value[i]; i++) {
        int hv;

        if (value[i] == '0' && (value[i + 1] == 'x' || value[i + 1] == 'X')) {
            i++;
            continue;
        }
        if (is_hex_separator((unsigned char)value[i])) {
            continue;
        }

        hv = hex_value((unsigned char)value[i]);
        if (hv < 0) return 0;

        if (high < 0) {
            high = hv;
        } else {
            if (len >= out_cap) return 0;
            out[len++] = (unsigned char)((high << 4) | hv);
            high = -1;
        }
    }

    if (high >= 0) return 0;
    *out_len = len;
    return len > 0;
}

static void appendf(char* out, size_t out_size, size_t* pos, const char* fmt, ...)
{
    va_list args;
    int written;

    if (!out || !pos || *pos >= out_size) return;

    va_start(args, fmt);
    written = vsnprintf(out + *pos, out_size - *pos, fmt, args);
    va_end(args);

    if (written < 0) return;
    if ((size_t)written >= out_size - *pos) {
        *pos = out_size - 1;
    } else {
        *pos += (size_t)written;
    }
}

static int is_printable_ascii(const unsigned char* bytes, size_t len)
{
    if (!bytes || len == 0) return 0;
    for (size_t i = 0; i < len; i++) {
        if (bytes[i] < 32 || bytes[i] > 126) return 0;
    }
    return 1;
}

static void append_hex_value(char* out, size_t out_size, size_t* pos,
                             const unsigned char* bytes, size_t len)
{
    static const char hex[] = "0123456789ABCDEF";

    appendf(out, out_size, pos, "0x");
    for (size_t i = 0; i < len && *pos + 2 < out_size; i++) {
        out[(*pos)++] = hex[(bytes[i] >> 4) & 0x0F];
        out[(*pos)++] = hex[bytes[i] & 0x0F];
        out[*pos] = '\0';
    }
}

static void append_ascii_value(char* out, size_t out_size, size_t* pos,
                               const unsigned char* bytes, size_t len)
{
    appendf(out, out_size, pos, "\"");
    for (size_t i = 0; i < len && *pos + 2 < out_size; i++) {
        if (bytes[i] == '"' || bytes[i] == '\\') {
            out[(*pos)++] = '\\';
        }
        out[(*pos)++] = (char)bytes[i];
        out[*pos] = '\0';
    }
    appendf(out, out_size, pos, "\"");
}

static int is_hex_string(const char* value)
{
    int digits = 0;

    if (!value || !*value) return 0;
    for (size_t i = 0; value[i]; i++) {
        if (value[i] == '0' && (value[i + 1] == 'x' || value[i + 1] == 'X')) {
            i++;
            continue;
        }
        if (is_hex_separator((unsigned char)value[i])) continue;
        if (hex_value((unsigned char)value[i]) < 0) return 0;
        digits++;
    }
    return digits >= 2 && (digits % 2) == 0;
}

static int is_decimal_string(const char* value)
{
    if (!value || !*value) return 0;
    for (size_t i = 0; value[i]; i++) {
        if (!isdigit((unsigned char)value[i])) return 0;
    }
    return 1;
}

static int looks_ipv6_field(const char* name)
{
    return name && strstr(name, "IPv6") != NULL;
}

static void format_ipv6_bytes(const unsigned char* bytes, char* out, size_t out_size)
{
    unsigned int words[8];
    int best_start = -1;
    int best_len = 0;
    int cur_start = -1;
    int cur_len = 0;
    size_t pos = 0;

    if (!out || out_size == 0) return;
    out[0] = '\0';

    for (int i = 0; i < 8; i++) {
        words[i] = ((unsigned int)bytes[i * 2] << 8) | bytes[i * 2 + 1];
        if (words[i] == 0) {
            if (cur_start < 0) {
                cur_start = i;
                cur_len = 1;
            } else {
                cur_len++;
            }
        } else {
            if (cur_len > best_len) {
                best_start = cur_start;
                best_len = cur_len;
            }
            cur_start = -1;
            cur_len = 0;
        }
    }
    if (cur_len > best_len) {
        best_start = cur_start;
        best_len = cur_len;
    }
    if (best_len < 2) {
        best_start = -1;
        best_len = 0;
    }

    for (int i = 0; i < 8; i++) {
        if (i == best_start) {
            appendf(out, out_size, &pos, "::");
            i += best_len - 1;
            continue;
        }
        if (i > 0 && i != best_start + best_len) {
            appendf(out, out_size, &pos, ":");
        }
        appendf(out, out_size, &pos, "%x", words[i]);
    }
}

static int decode_ipv6_value(const char* name, const char* value, char* out, size_t out_size)
{
    unsigned char bytes[32];
    char addr[64];
    size_t len = 0;

    if (!looks_ipv6_field(name) || !is_hex_string(value)) return 0;
    if (!hex_to_bytes(value, bytes, sizeof(bytes), &len)) return 0;

    if (len == 16) {
        format_ipv6_bytes(bytes, addr, sizeof(addr));
        snprintf(out, out_size, "%s (%s)", value, addr);
        return 1;
    }

    if (len == 18 &&
        (strcasecmp(name, "Framed-IPv6-Prefix") == 0 ||
         strcasecmp(name, "Delegated-IPv6-Prefix") == 0 ||
         strcasecmp(name, "Route-IPv6-Information") == 0)) {
        format_ipv6_bytes(bytes + 2, addr, sizeof(addr));
        snprintf(out, out_size, "%s (%s/%u)", value, addr, (unsigned int)bytes[1]);
        return 1;
    }

    return 0;
}

static int decode_hex_value(const char* name, const char* value, char* out, size_t out_size)
{
    unsigned char bytes[MAX_VSA_BYTES];
    size_t len = 0;
    size_t pos = 0;

    if (!name || !value || !is_hex_string(value) || is_decimal_string(value)) return 0;
    if (strcasecmp(name, "MS-CHAP-Domain") != 0 &&
        !(strlen(name) >= 3 && name[0] == 'M' && name[1] == 'S' && name[2] == '-') &&
        strcasecmp(name, "EAP-Message") != 0 &&
        strcasecmp(name, "Message-Authenticator") != 0) {
        return 0;
    }
    if (!hex_to_bytes(value, bytes, sizeof(bytes), &len)) return 0;

    appendf(out, out_size, &pos, "%s (", value);
    if (len > 1 && bytes[0] < 32 && is_printable_ascii(bytes + 1, len - 1)) {
        appendf(out, out_size, &pos, "prefix 0x%02X; text ", bytes[0]);
        append_ascii_value(out, out_size, &pos, bytes + 1, len - 1);
    } else if (is_printable_ascii(bytes, len)) {
        appendf(out, out_size, &pos, "text ");
        append_ascii_value(out, out_size, &pos, bytes, len);
    } else {
        appendf(out, out_size, &pos, "%u byte%s", (unsigned int)len, len == 1 ? "" : "s");
        if (len == 4) {
            unsigned int number = ((unsigned int)bytes[0] << 24) |
                                  ((unsigned int)bytes[1] << 16) |
                                  ((unsigned int)bytes[2] << 8) |
                                  (unsigned int)bytes[3];
            appendf(out, out_size, &pos, "; decimal %u", number);
        }
    }
    appendf(out, out_size, &pos, ")");
    return out[0] != '\0';
}

static int decode_class_value(const char* value, char* out, size_t out_size)
{
    unsigned int vendor_id;
    unsigned int class_type;
    unsigned int sequence;
    char server[64];
    char date[32];
    char time[32];
    const char* vendor_name;

    if (!value) return 0;
    if (sscanf(value, "%u %u %63s %31s %31s %u",
               &vendor_id, &class_type, server, date, time, &sequence) != 6) {
        return 0;
    }

    vendor_name = radius_vendor_name(vendor_id);
    snprintf(out, out_size,
             "%s (Vendor: %s (%u); Type: %u; Server: %s; Issued: %s %s; Sequence: %u)",
             value, vendor_name, vendor_id, class_type, server, date, time, sequence);
    return 1;
}

int decode_vendor_specific(const char* value, char* out, size_t out_size)
{
    unsigned char bytes[MAX_VSA_BYTES];
    size_t len = 0;
    size_t pos = 4;
    size_t out_pos = 0;
    unsigned int vendor_id;
    const char* vendor_name;

    if (!out || out_size == 0) return 0;
    out[0] = '\0';

    if (!hex_to_bytes(value, bytes, sizeof(bytes), &len) || len < 6) {
        return 0;
    }

    vendor_id = ((unsigned int)bytes[0] << 24) |
                ((unsigned int)bytes[1] << 16) |
                ((unsigned int)bytes[2] << 8) |
                (unsigned int)bytes[3];
    vendor_name = radius_vendor_name(vendor_id);

    appendf(out, out_size, &out_pos, "%s (%u): ", vendor_name, vendor_id);

    while (pos < len) {
        unsigned int vendor_type;
        unsigned int vendor_len;
        const char* attr_name = NULL;
        const unsigned char* data;
        size_t data_len;

        if (pos + 2 > len) {
            appendf(out, out_size, &out_pos, "trailing %u byte%s",
                    (unsigned int)(len - pos), (len - pos) == 1 ? "" : "s");
            break;
        }

        vendor_type = bytes[pos];
        vendor_len = bytes[pos + 1];
        if (vendor_len < 2 || pos + vendor_len > len) {
            appendf(out, out_size, &out_pos,
                    "malformed Vendor-Type %u length %u", vendor_type, vendor_len);
            break;
        }

        if (pos > 4) appendf(out, out_size, &out_pos, "; ");

        data = bytes + pos + 2;
        data_len = vendor_len - 2;
        attr_name = radius_vendor_attr_name(vendor_id, vendor_type);

        if (attr_name) {
            appendf(out, out_size, &out_pos, "%s[%u] = ", attr_name, vendor_type);
        } else {
            appendf(out, out_size, &out_pos, "Vendor-Type %u = ", vendor_type);
        }

        if (is_printable_ascii(data, data_len)) {
            append_ascii_value(out, out_size, &out_pos, data, data_len);
        } else {
            append_hex_value(out, out_size, &out_pos, data, data_len);
            if (data_len == 4) {
                unsigned int number = ((unsigned int)data[0] << 24) |
                                      ((unsigned int)data[1] << 16) |
                                      ((unsigned int)data[2] << 8) |
                                      (unsigned int)data[3];
                appendf(out, out_size, &out_pos, " (%u)", number);
            }
        }

        pos += vendor_len;
    }

    return out[0] != '\0';
}

int decode_radius_field(const char* name, const char* value, char* out, size_t out_size)
{
    int val;
    const char* decoded = NULL;
    char name_buf[128];

    if (!out || out_size == 0) return 0;
    out[0] = '\0';
    if (!name || !value || !*value) return 0;

    normalize_field_name(name, name_buf, sizeof(name_buf));

    if (strcasecmp(name_buf, "Vendor-Specific") == 0 &&
        decode_vendor_specific(value, out, out_size)) {
        return 1;
    }

    if (strcasecmp(name_buf, "Class") == 0 &&
        decode_class_value(value, out, out_size)) {
        return 1;
    }

    if (decode_ipv6_value(name_buf, value, out, out_size)) {
        return 1;
    }

    if (decode_hex_value(name_buf, value, out, out_size)) {
        return 1;
    }

    val = atoi(value);
    if (val == 0 && value[0] != '0') return 0;

    if (strcasecmp(name_buf, "Packet-Type") == 0) decoded = decode_packet_type(val);
    else if (strcasecmp(name_buf, "Service-Type") == 0) decoded = decode_service_type(val);
    else if (strcasecmp(name_buf, "Framed-Protocol") == 0) decoded = decode_framed_protocol(val);
    else if (strcasecmp(name_buf, "Framed-Routing") == 0) decoded = decode_framed_routing(val);
    else if (strcasecmp(name_buf, "Framed-Compression") == 0) decoded = decode_framed_compression(val);
    else if (strcasecmp(name_buf, "Login-Service") == 0) decoded = decode_login_service(val);
    else if (strcasecmp(name_buf, "Acct-Status-Type") == 0) decoded = decode_acct_status_type(val);
    else if (strcasecmp(name_buf, "NAS-Port-Type") == 0) decoded = decode_nas_port_type(val);
    else if (strcasecmp(name_buf, "Tunnel-Type") == 0) decoded = decode_tunnel_type(val);
    else if (strcasecmp(name_buf, "Tunnel-Medium-Type") == 0) decoded = decode_tunnel_medium_type(val);
    else if (strcasecmp(name_buf, "Termination-Action") == 0) decoded = decode_termination_action(val);
    else if (strcasecmp(name_buf, "Acct-Authentic") == 0) decoded = decode_acct_authentic(val);
    else if (strcasecmp(name_buf, "Acct-Terminate-Cause") == 0) decoded = decode_acct_terminate_cause(val);
    else if (strcasecmp(name_buf, "ARAP-Zone-Access") == 0) decoded = decode_arap_zone_access(val);
    else if (strcasecmp(name_buf, "Prompt") == 0) decoded = decode_prompt(val);
    else if (strcasecmp(name_buf, "Reason-Code") == 0) decoded = decode_reason_code(val);
    else if (strcasecmp(name_buf, "Authentication-Type") == 0) decoded = decode_authentication_type(val);
    else if (strcasecmp(name_buf, "EAP-Type") == 0) decoded = decode_eap_type(val);

    if (decoded && strcasecmp(decoded, "Unknown") != 0) {
        snprintf(out, out_size, "%s (%s)", value, decoded);
        return 1;
    }

    return 0;
}

const char* decode_service_type(int value)
{
    switch (value) {
        case 1:  return "Login";
        case 2:  return "Framed";
        case 3:  return "Callback Login";
        case 4:  return "Callback Framed";
        case 5:  return "Outbound";
        case 6:  return "Administrative";
        case 7:  return "NAS Prompt";
        case 8:  return "Authenticate Only";
        case 9:  return "Callback NAS Prompt";
        case 10: return "Call Check";
        case 11: return "Callback Administrative";
        default: return "Unknown";
    }
}

const char* decode_framed_protocol(int value)
{
    switch (value) {
        case 1:  return "PPP";
        case 2:  return "SLIP";
        case 3:  return "ARAP";
        case 4:  return "Gandalf SLML";
        case 5:  return "Xylogics IPX/S";
        case 6:  return "X.75 Synchronous";
        case 7:  return "GPRS PDP Context";
        default: return "Unknown";
    }
}

const char* decode_acct_status_type(int value)
{
    switch (value) {
        case 1:  return "Start";
        case 2:  return "Stop";
        case 3:  return "Interim-Update";
        case 4:  return "Modem-Start";
        case 5:  return "Modem-Stop";
        case 6:  return "Tunnel-Start";
        case 7:  return "Tunnel-Stop";
        case 8:  return "Tunnel-Reject";
        case 9:  return "Tunnel-Link-Start";
        case 10: return "Tunnel-Link-Stop";
        case 11: return "Tunnel-Link-Reject";
        case 12: return "Failed";
        default: return "Unknown";
    }
}

const char* decode_nas_port_type(int value)
{
    switch (value) {
        case 0:   return "Async";
        case 1:   return "Sync";
        case 2:   return "ISDN Sync";
        case 3:   return "ISDN Async V.120";
        case 4:   return "ISDN Async V.110";
        case 5:   return "Virtual";
        case 6:   return "PIAFS";
        case 7:   return "HDLC Clear Channel";
        case 8:   return "X.25";
        case 9:   return "X.75";
        case 10:  return "G.3 Fax";
        case 11:  return "SDSL";
        case 12:  return "ADSL-CAP";
        case 13:  return "ADSL-DMT";
        case 14:  return "IDSL";
        case 15:  return "Ethernet";
        case 16:  return "xDSL";
        case 17:  return "Cable";
        case 18:  return "Wireless - Other";
        case 19:  return "Wireless - IEEE 802.11";
        case 20:  return "Token-Ring";
        case 21:  return "FDDI";
        case 22:  return "Wireless - CDMA2000";
        case 23:  return "Wireless - UMTS";
        case 24:  return "Wireless - 1X-EV";
        case 25:  return "IAPP";
        case 26:  return "Wireless - IEEE 802.16";
        case 27:  return "Wireless - IEEE 802.20";
        case 28:  return "Wireless - IEEE 802.22";
        case 29:  return "PPPoA";
        case 30:  return "PPPoEoA";
        case 31:  return "PPPoEoE";
        case 32:  return "PPPoEoVLAN";
        case 33:  return "PPPoEoQinQ";
        case 34:  return "xPON";
        case 35:  return "Wireless - XGP";
        default:  return "Unknown";
    }
}

const char* decode_tunnel_type(int value)
{
    switch (value) {
        case 1:  return "PPTP";
        case 2:  return "L2F";
        case 3:  return "L2TP";
        case 4:  return "ATMP";
        case 5:  return "VTP";
        case 6:  return "AH";
        case 7:  return "IP-IP";
        case 8:  return "MIN-IP-IP";
        case 9:  return "ESP";
        case 10: return "GRE";
        case 11: return "DVS";
        case 12: return "IP-in-IP Tunneling";
        case 13: return "VLAN";
        default: return "Unknown";
    }
}

const char* decode_tunnel_medium_type(int value)
{
    switch (value) {
        case 1:  return "IPv4";
        case 2:  return "IPv6";
        case 3:  return "NSAP";
        case 4:  return "HDLC";
        case 5:  return "BBN 1822";
        case 6:  return "IEEE 802";
        case 7:  return "E.163";
        case 8:  return "E.164";
        case 9:  return "F.69";
        case 10: return "X.121";
        case 11: return "IPX";
        case 12: return "AppleTalk";
        case 13: return "Decnet IV";
        case 14: return "Banyan Vines";
        case 15: return "E.164 with NSAP";
        default: return "Unknown";
    }
}

const char* decode_termination_action(int value)
{
    switch (value) {
        case 0:  return "Default";
        case 1:  return "RADIUS-Request";
        default: return "Unknown";
    }
}

const char* decode_acct_authentic(int value)
{
    switch (value) {
        case 1:  return "RADIUS";
        case 2:  return "Local";
        case 3:  return "Remote";
        case 4:  return "Diameter";
        default: return "Unknown";
    }
}

const char* decode_acct_terminate_cause(int value)
{
    switch (value) {
        case 1:  return "User Request";
        case 2:  return "Lost Carrier";
        case 3:  return "Lost Service";
        case 4:  return "Idle Timeout";
        case 5:  return "Session Timeout";
        case 6:  return "Admin Reset";
        case 7:  return "Admin Reboot";
        case 8:  return "Port Error";
        case 9:  return "NAS Error";
        case 10: return "NAS Request";
        case 11: return "NAS Reboot";
        case 12: return "Port Unneeded";
        case 13: return "Port Preempted";
        case 14: return "Port Suspended";
        case 15: return "Service Unavailable";
        case 16: return "Callback";
        case 17: return "User Error";
        case 18: return "Host Request";
        case 19: return "Supplicant Restart";
        case 20: return "Reauthentication Failure";
        case 21: return "Port Reinitialized";
        case 22: return "Port Administratively Disabled";
        case 23: return "Lost Power";
        default: return "Unknown";
    }
}

const char* decode_packet_type(int value)
{
    switch (value) {
        case 1:  return "Access-Request";
        case 2:  return "Access-Accept";
        case 3:  return "Access-Reject";
        case 4:  return "Accounting-Request";
        case 5:  return "Accounting-Response";
        case 11: return "Access-Challenge";
        case 12: return "Status-Server (experimental)";
        case 13: return "Status-Client (experimental)";
        case 40: return "Disconnect-Request";
        case 41: return "Disconnect-ACK";
        case 42: return "Disconnect-NAK";
        case 43: return "CoA-Request";
        case 44: return "CoA-ACK";
        case 45: return "CoA-NAK";
        case 50: return "IP-Address-Allocate";
        case 51: return "IP-Address-Release";
        default: return "Unknown";
    }
}

const char* decode_reason_code(int value)
{
    switch (value) {
        case 0:  return "IAS_SUCCESS";
        case 1:  return "IAS_INTERNAL_ERROR";
        case 2:  return "IAS_ACCESS_DENIED";
        case 3:  return "IAS_MALFORMED_REQUEST";
        case 4:  return "IAS_GLOBAL_CATALOG_UNAVAILABLE";
        case 5:  return "IAS_DOMAIN_UNAVAILABLE";
        case 6:  return "IAS_SERVER_UNAVAILABLE";
        case 7:  return "IAS_NO_SUCH_DOMAIN";
        case 8:  return "IAS_NO_SUCH_USER";
        case 16: return "IAS_AUTH_FAILURE";
        case 17: return "IAS_CHANGE_PASSWORD_FAILURE";
        case 18: return "IAS_UNSUPPORTED_AUTH_TYPE";
        case 19: return "IAS_PASSWORD_EXPIRED";
        case 20: return "IAS_ACCOUNT_DISABLED";
        case 21: return "IAS_ACCOUNT_RESTRICTION";
        case 22: return "IAS_ACCOUNT_LOGON_HOURS";
        case 23: return "IAS_ACCOUNT_EXPIRED";
        case 24: return "IAS_PASSWORD_MUST_CHANGE";
        case 25: return "IAS_ACCOUNT_LOCKED_OUT";
        case 32: return "IAS_POLICY_MATCH";
        case 33: return "IAS_DIALIN_LOCKED_OUT";
        case 34: return "IAS_DIALIN_DISABLED";
        case 35: return "IAS_INVALID_AUTH_TYPE";
        case 36: return "IAS_INVALID_CALLING_STATION";
        case 37: return "IAS_INVALID_DIALIN_HOURS";
        case 38: return "IAS_INVALID_CALLED_STATION";
        case 39: return "IAS_INVALID_PORT_TYPE";
        case 48: return "IAS_INVALID_RADIUS_CLIENT";
        case 49: return "IAS_INVALID_RADIUS_CLIENT_CONFIGURATION";
        case 64: return "IAS_NO_RECORD";
        case 65: return "IAS_SESSION_TIMEOUT";
        case 66: return "IAS_UNEXPECTED_REQUEST";
        default: return "Unknown";
    }
}

const char* decode_authentication_type(int value)
{
    switch (value) {
        case 1:  return "PAP";
        case 2:  return "CHAP";
        case 3:  return "MS-CHAP v1";
        case 4:  return "MS-CHAP v2";
        case 5:  return "EAP";
        case 7:  return "None";
        case 8:  return "Custom";
        case 9:  return "RSA SecurID";
        case 10: return "PEAP";
        case 11: return "EAP-MSCHAP v2";
        default: return "Unknown";
    }
}

const char* decode_framed_routing(int value)
{
    switch (value) {
        case 0:  return "None";
        case 1:  return "Send";
        case 2:  return "Listen";
        case 3:  return "Send and Listen";
        default: return "Unknown";
    }
}

const char* decode_framed_compression(int value)
{
    switch (value) {
        case 0:  return "None";
        case 1:  return "Van Jacobson TCP/IP";
        case 2:  return "IPX Header Compression";
        case 3:  return "Stac-LZS";
        default: return "Unknown";
    }
}

const char* decode_login_service(int value)
{
    switch (value) {
        case 0:  return "Telnet";
        case 1:  return "Rlogin";
        case 2:  return "TCP Clear";
        case 3:  return "PortMaster";
        case 4:  return "LAT";
        case 5:  return "X.25-PAD";
        case 6:  return "X.25-T3POS";
        case 7:  return "TCP Clear Quiet";
        default: return "Unknown";
    }
}

const char* decode_arap_zone_access(int value)
{
    switch (value) {
        case 1:  return "Only allow access to default zone";
        case 2:  return "Use zone filter inlet";
        case 3:  return "Not trusted";
        case 4:  return "Use zone filter outlet";
        default: return "Unknown";
    }
}

const char* decode_prompt(int value)
{
    switch (value) {
        case 0:  return "No Echo";
        case 1:  return "Echo";
        default: return "Unknown";
    }
}

const char* decode_eap_type(int value)
{
    switch (value) {
        case 1:   return "Identity";
        case 2:   return "Notification";
        case 3:   return "Legacy Nak";
        case 4:   return "MD5-Challenge";
        case 5:   return "One-Time Password (OTP)";
        case 6:   return "Generic Token Card";
        case 7:   return "Allocated";
        case 8:   return "Allocated";
        case 9:   return "RSA Public Key";
        case 10:  return "DSS Unilateral";
        case 11:  return "KEA";
        case 12:  return "KEA-VALIDATE";
        case 13:  return "EAP-TLS";
        case 14:  return "Defender Token";
        case 15:  return "RSA Security SecurID EAP";
        case 16:  return "Arcot Systems EAP";
        case 17:  return "EAP-LEAP";
        case 18:  return "EAP-SIM";
        case 19:  return "SRP-SHA1";
        case 20:  return "Unassigned";
        case 21:  return "EAP-TTLS";
        case 22:  return "Remote Access Service";
        case 23:  return "EAP-AKA";
        case 24:  return "EAP-3Com Wireless";
        case 25:  return "PEAP";
        case 26:  return "MS-EAP-Authentication";
        case 27:  return "Mutual";
        case 28:  return "EAP-PSK";
        case 29:  return "EAP-MSCHAP v2";
        case 30:  return "DynamID";
        case 31:  return "Robi";
        case 32:  return "Arcot EAP";
        case 33:  return "EAP-PAX";
        case 34:  return "EAP-PSK";
        case 35:  return "EAP-SAKE";
        case 36:  return "EAP-IKEv2";
        case 37:  return "EAP-AKA'";
        case 38:  return "EAP-GPSK";
        case 39:  return "EAP-PWD";
        case 40:  return "EAP-EKE";
        case 41:  return "EAP-Miana";
        case 42:  return "EAP-FAST";
        case 43:  return "ZigBee EAP";
        case 44:  return "EAP-Link";
        case 45:  return "EAP-PWDv2";
        case 46:  return "EAP-TEAP";
        case 47:  return "EAP-ERP";
        case 49:  return "EAP-NOOB";
        case 50:  return "EAP-AKA";
        case 51:  return "EAP-AKA'";
        case 52:  return "EAP-AKA-PRIME";
        case 53:  return "EAP-UTE";
        case 54:  return "EAP-MSCHAPv2";
        case 55:  return "EAP-PSK";
        case 56:  return "EAP-PAX";
        case 57:  return "EAP-SAKE";
        case 58:  return "EAP-IKEv2";
        case 59:  return "EAP-AKA'";
        case 60:  return "EAP-GPSK";
        default:  return "Unknown";
    }
}
