/* packet-ipp.c
 * Routines for IPP packet disassembly
 *
 * Guy Harris <guy@alum.mit.edu>
 *     (original implementation)
 * Michael R Sweet <michael.r.sweet@gmail.com>
 *     (general improvements and support beyond RFC 2910/2911)
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1998 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

/*
 * Remaining IPP items to support in this dissector:
 *
 *   - Support out-of-band values
 *   - Support 1setOf values in list
 *   - Support collections
 */

#include <epan/packet.h>
#include <epan/strutil.h>
#include <epan/to_str.h>
#include <epan/conversation.h>
#include <epan/wmem/wmem.h>
#include "packet-http.h"
#include <stdio.h>

void proto_register_ipp(void);
void proto_reg_handoff_ipp(void);

static int proto_ipp = -1;
/* Generated from convert_proto_tree_add_text.pl */
static int hf_ipp_request_id = -1;
static int hf_ipp_name = -1;
static int hf_ipp_tag = -1;
static int hf_ipp_value_length = -1;
static int hf_ipp_charstring_value = -1;
static int hf_ipp_status_code = -1;
static int hf_ipp_version = -1;
static int hf_ipp_bool_value = -1;
static int hf_ipp_name_length = -1;
static int hf_ipp_job_state = -1;
static int hf_ipp_bytes_value = -1;
static int hf_ipp_operation_id = -1;
static int hf_ipp_printer_state = -1;
static int hf_ipp_uint32_value = -1;

static int hf_ipp_response_in = -1;
static int hf_ipp_response_to = -1;
static int hf_ipp_response_time = -1;

typedef struct _ipp_transaction_t {
        guint32 req_frame;
        guint32 rep_frame;
        nstime_t req_time;
} ipp_transaction_t;

typedef struct _ipp_conv_info_t {
        wmem_map_t *pdus;
} ipp_conv_info_t;

static gint ett_ipp = -1;
static gint ett_ipp_as = -1;
static gint ett_ipp_attr = -1;

#define PRINT_JOB              0x0002
#define PRINT_URI              0x0003
#define VALIDATE_JOB           0x0004
#define CREATE_JOB             0x0005
#define SEND_DOCUMENT          0x0006
#define SEND_URI               0x0007
#define CANCEL_JOB             0x0008
#define GET_JOB_ATTRIBUTES     0x0009
#define GET_JOBS               0x000A
#define GET_PRINTER_ATTRIBUTES 0x000B

static const value_string operation_vals[] = {
    { PRINT_JOB,              "Print-Job" },
    { PRINT_URI,              "Print-URI" },
    { VALIDATE_JOB,           "Validate-Job" },
    { CREATE_JOB,             "Create-Job" },
    { SEND_DOCUMENT,          "Send-Document" },
    { SEND_URI,               "Send-URI" },
    { CANCEL_JOB,             "Cancel-Job" },
    { GET_JOB_ATTRIBUTES,     "Get-Job-Attributes" },
    { GET_JOBS,               "Get-Jobs" },
    { GET_PRINTER_ATTRIBUTES, "Get-Printer-Attributes" },
    { 0x000C,                 "Hold-Job" },
    { 0x000D,                 "Release-Job" },
    { 0x000E,                 "Restart-Job" },
    { 0x0010,                 "Pause-Printer" },
    { 0x0011,                 "Resume-Printer" },
    { 0x0012,                 "Purge-Jobs" },
    { 0x0013,                 "Set-Printer-Attributes" },
    { 0x0014,                 "Set-Job-Attributes" },
    { 0x0015,                 "Get-Printer-Supported-Values" },
    { 0x0016,                 "Create-Printer-Subscriptions" },
    { 0x0017,                 "Create-Job-Subscriptions" },
    { 0x0018,                 "Get-Subscription-Attributes" },
    { 0x0019,                 "Get-Subscriptions" },
    { 0x001A,                 "Renew-Subscription" },
    { 0x001B,                 "Cancel-Subscription" },
    { 0x001C,                 "Get-Notifications" },
    { 0x001D,                 "Reserved (ipp-indp-method)" },
    { 0x001E,                 "Reserved (ipp-get-resources)" },
    { 0x001F,                 "Reserved (ipp-get-resources)" },
    { 0x0020,                 "Reserved (ipp-get-resources)" },
    { 0x0021,                 "Reserved (ipp-install)" },
    { 0x0022,                 "Enable-Printer" },
    { 0x0023,                 "Disable-Printer" },
    { 0x0024,                 "Pause-Printer-After-Current-Job" },
    { 0x0025,                 "Hold-New-Jobs" },
    { 0x0026,                 "Release-Held-New-Jobs" },
    { 0x0027,                 "Deactivate-Printer" },
    { 0x0028,                 "Activate-Printer" },
    { 0x0029,                 "Restart-Printer" },
    { 0x002A,                 "Shutdown-Printer" },
    { 0x002B,                 "Startup-Printer" },
    { 0x002C,                 "Reprocess-Job" },
    { 0x002D,                 "Cancel-Current-Job" },
    { 0x002E,                 "Suspend-Current-Job" },
    { 0x002F,                 "Resume-Job" },
    { 0x0030,                 "Promote-Job" },
    { 0x0031,                 "Schedule-Job-After" },
    { 0x0033,                 "Cancel-Document" },
    { 0x0034,                 "Get-Document-Attributes" },
    { 0x0035,                 "Get-Documents" },
    { 0x0036,                 "Delete-Document" },
    { 0x0037,                 "Set-Document-Attributes" },
    { 0x0038,                 "Cancel-Jobs" },
    { 0x0039,                 "Cancel-My-Jobs" },
    { 0x003A,                 "Resubmit-Job" },
    { 0x003B,                 "Close-Job" },
    { 0x003C,                 "Identify-Printer" },
    { 0x003D,                 "Validate-Document" },
    { 0x003E,                 "Add-Document-Images" },
    { 0x003F,                 "Acknowledge-Document" },
    { 0x0040,                 "Acknowledge-Identify-Printer" },
    { 0x0041,                 "Acknowledge-Job" },
    { 0x0042,                 "Fetch-Document" },
    { 0x0043,                 "Fetch-Job" },
    { 0x0044,                 "Get-Output-Device-Attributes" },
    { 0x0045,                 "Update-Active-Jobs" },
    { 0x0046,                 "Deregister-Output-Device" },
    { 0x0047,                 "Update-Document-Status" },
    { 0x0048,                 "Update-Job-Status" },
    { 0x0049,                 "Update-Output-Device-Attributes" },
    { 0x004A,                 "Get-Next-Document-Data" },
    { 0x4001,                 "CUPS-Get-Default" },
    { 0x4002,                 "CUPS-Get-Printers" },
    { 0x4003,                 "CUPS-Add-Modify-Printer" },
    { 0x4004,                 "CUPS-Delete-Printer" },
    { 0x4005,                 "CUPS-Get-Classes" },
    { 0x4006,                 "CUPS-Add-Modify-Class" },
    { 0x4007,                 "CUPS-Delete-Class" },
    { 0x4008,                 "CUPS-Accept-Jobs" },
    { 0x4009,                 "CUPS-Reject-Jobs" },
    { 0x400A,                 "CUPS-Set-Default" },
    { 0x400B,                 "CUPS-Get-Devices" },
    { 0x400C,                 "CUPS-Get-PPDs" },
    { 0x400D,                 "CUPS-Move-Job" },
    { 0x400E,                 "CUPS-Authenticate-Job" },
    { 0x400F,                 "CUPS-Get-PPD" },
    { 0x4027,                 "CUPS-Get-Document" },
    { 0x4028,                 "CUPS-Create-Local-Printer" },
    { 0,                      NULL }
};

/* Printer States */
#define PRINTER_STATE_IDLE      0x3
#define PRINTER_STATE_PROCESSING    0x4
#define PRINTER_STATE_STOPPED       0x5
static const value_string printer_state_vals[] = {
    { PRINTER_STATE_IDLE,       "idle" },
    { PRINTER_STATE_PROCESSING, "processing" },
    { PRINTER_STATE_STOPPED,    "stopped" },
    { 0, NULL }
};

/* Job States */
static const value_string job_state_vals[] = {
    { 3, "pending" },
    { 4, "pending-held" },
    { 5, "processing" },
    { 6, "processing-stopped" },
    { 7, "canceled" },
    { 8, "aborted" },
    { 9, "completed" },
    { 0, NULL }
};

/* Document States */
static const value_string document_state_vals[] = {
    { 3, "pending" },
    { 5, "processing" },
    { 6, "processing-stopped" },
    { 7, "canceled" },
    { 8, "aborted" },
    { 9, "completed" },
    { 0, NULL }
};

/* Finishings Values */
static const value_string finishings_vals[] = {
    { 3, "none" },
    { 4, "staple" },
    { 5, "punch" },
    { 6, "cover" },
    { 7, "bind" },
    { 8, "saddle-stitch" },
    { 9, "edge-stitch" },
    { 10, "fold" },
    { 11, "trim" },
    { 12, "bale" },
    { 13, "booklet-maker" },
    { 14, "jog-offset" },
    { 15, "coat" },
    { 16, "laminate" },
    { 20, "staple-top-left" },
    { 21, "staple-bottom-left" },
    { 22, "staple-top-right" },
    { 23, "staple-bottom-right" },
    { 24, "edge-stitch-left" },
    { 25, "edge-stitch-top" },
    { 26, "edge-stitch-right" },
    { 27, "edge-stitch-bottom" },
    { 28, "staple-dual-left" },
    { 29, "staple-dual-top" },
    { 30, "staple-dual-right" },
    { 31, "staple-dual-bottom" },
    { 32, "staple-triple-left" },
    { 33, "staple-triple-top" },
    { 34, "staple-triple-right" },
    { 35, "staple-triple-bottom" },
    { 50, "bind-left" },
    { 51, "bind-top" },
    { 52, "bind-right" },
    { 53, "bind-bottom" },
    { 60, "trim-after-pages" },
    { 61, "trim-after-documents" },
    { 62, "trim-after-copies" },
    { 63, "trim-after-job" },
    { 70, "punch-top-left" },
    { 71, "punch-bottom-left" },
    { 72, "punch-top-right" },
    { 73, "punch-bottom-right" },
    { 74, "punch-dual-left" },
    { 75, "punch-dual-top" },
    { 76, "punch-dual-right" },
    { 77, "punch-dual-bottom" },
    { 78, "punch-triple-left" },
    { 79, "punch-triple-top" },
    { 80, "punch-triple-right" },
    { 81, "punch-triple-bottom" },
    { 82, "punch-quad-left" },
    { 83, "punch-quad-top" },
    { 84, "punch-quad-right" },
    { 85, "punch-quad-bottom" },
    { 86, "punch-multiple-left" },
    { 87, "punch-multiple-top" },
    { 88, "punch-multiple-right" },
    { 89, "punch-multiple-bottom" },
    { 90, "fold-accordion" },
    { 91, "fold-double-gate" },
    { 92, "fold-gate" },
    { 93, "fold-half" },
    { 94, "fold-half-z" },
    { 95, "fold-left-gate" },
    { 96, "fold-letter" },
    { 97, "fold-parallel" },
    { 98, "fold-poster" },
    { 99, "fold-right-gate" },
    { 100, "fold-z" },
    { 0, NULL }
};

static const value_string orientation_vals[] = {
    { 3, "portrait" },
    { 4, "landscape" },
    { 5, "reverse-landscape" },
    { 6, "reverse-portrait" },
    { 7, "none" },
    { 0, NULL }
};

static const value_string quality_vals[] = {
    { 3, "draft" },
    { 4, "normal" },
    { 5, "high" },
    { 0, NULL }
};

static const value_string transmission_status_vals[] = {
    { 3, "pending" },
    { 4, "pending-retry" },
    { 5, "processing" },
    { 7, "canceled" },
    { 8, "aborted" },
    { 9, "completed" },
    { 0, NULL }
};


#define STATUS_SUCCESSFUL    0x0000
#define STATUS_INFORMATIONAL 0x0100
#define STATUS_REDIRECTION   0x0200
#define STATUS_CLIENT_ERROR  0x0400
#define STATUS_SERVER_ERROR  0x0500

#define STATUS_TYPE_MASK     0xFF00

static const value_string status_vals[] = {
    { 0x0000, "successful-ok" },
    { 0x0001, "successful-ok-ignored-or-substituted-attributes" },
    { 0x0002, "successful-ok-conflicting-attributes" },
    { 0x0003, "successful-ok-ignored-subscriptions" },
    { 0x0005, "successful-ok-too-many-events" },
    { 0x0007, "successful-ok-events-complete" },
    { 0x0400, "client-error-bad-request" },
    { 0x0401, "client-error-forbidden" },
    { 0x0402, "client-error-not-authenticated" },
    { 0x0403, "client-error-not-authorized" },
    { 0x0404, "client-error-not-possible" },
    { 0x0405, "client-error-timeout" },
    { 0x0406, "client-error-not-found" },
    { 0x0407, "client-error-gone" },
    { 0x0408, "client-error-request-entity-too-large" },
    { 0x0409, "client-error-request-value-too-long" },
    { 0x040A, "client-error-document-format-not-supported" },
    { 0x040B, "client-error-attributes-or-values-not-supported" },
    { 0x040C, "client-error-uri-scheme-not-supported" },
    { 0x040D, "client-error-charset-not-supported" },
    { 0x040E, "client-error-conflicting-attributes" },
    { 0x040F, "client-error-compression-not-supported" },
    { 0x0410, "client-error-compression-error" },
    { 0x0411, "client-error-document-format-error" },
    { 0x0412, "client-error-document-access-error" },
    { 0x0413, "client-error-attributes-not-settable" },
    { 0x0414, "client-error-ignored-all-subscriptions" },
    { 0x0415, "client-error-too-many-subscriptions" },
    { 0x0418, "client-error-document-password-error" },
    { 0x0419, "client-error-document-permission-error" },
    { 0x041A, "client-error-document-security-error" },
    { 0x041B, "client-error-document-unprintable-error" },
    { 0x041C, "client-error-account-info-needed" },
    { 0x041D, "client-error-account-closed" },
    { 0x041E, "client-error-account-limit-reached" },
    { 0x041F, "client-error-account-authorization-failed" },
    { 0x0420, "client-error-not-fetchable" },
    { 0x0500, "server-error-internal-error" },
    { 0x0501, "server-error-operation-not-supported" },
    { 0x0502, "server-error-service-unavailable" },
    { 0x0503, "server-error-version-not-supported" },
    { 0x0504, "server-error-device-error" },
    { 0x0505, "server-error-temporary-error" },
    { 0x0506, "server-error-not-accepting-jobs" },
    { 0x0507, "server-error-busy" },
    { 0x0508, "server-error-job-canceled" },
    { 0x0509, "server-error-multiple-document-jobs-not-supported" },
    { 0x050A, "server-error-printer-is-deactivated" },
    { 0x050B, "server-error-too-many-jobs" },
    { 0x050C, "server-error-too-many-documents" },
    { 0, NULL }
};

static int parse_attributes(tvbuff_t *tvb, int offset, proto_tree *tree);
static proto_tree *add_integer_tree(proto_tree *tree, tvbuff_t *tvb,
                                        int offset, int name_length, int value_length, guint8 tag);
static void add_integer_value(const gchar *tag_desc, proto_tree *tree,
                                        tvbuff_t *tvb, int offset, int name_length, int value_length, guint8 tag);
static proto_tree *add_octetstring_tree(proto_tree *tree, tvbuff_t *tvb,
                                        int offset, guint8 tag, int name_length, int value_length);
static void add_octetstring_value(const gchar *tag_desc, proto_tree *tree,
                                        tvbuff_t *tvb, int offset, int name_length, int value_length);
static proto_tree *add_charstring_tree(proto_tree *tree, tvbuff_t *tvb,
                                        int offset, int name_length, int value_length);
static void add_charstring_value(const gchar *tag_desc, proto_tree *tree,
                                        tvbuff_t *tvb, int offset, int name_length, int value_length);
static int add_value_head(const gchar *tag_desc, proto_tree *tree,
                                        tvbuff_t *tvb, int offset, int name_length, int value_length, char **name_val);

static int
dissect_ipp(tvbuff_t *tvb, packet_info *pinfo, proto_tree *tree, void* data)
{
    proto_tree  *ipp_tree;
    proto_item  *ti;
    int         offset     = 0;
    http_message_info_t *message_info = (http_message_info_t *)data;
    gboolean    is_request;
    guint16     operation_status;
    const gchar *info;
    const gchar *status_type;
    guint32	request_id;
    conversation_t *conversation;
    ipp_conv_info_t *ipp_info;
    ipp_transaction_t *ipp_trans;

    if (message_info != NULL) {
        switch (message_info->type) {

        case HTTP_REQUEST:
            is_request = TRUE;
            break;

        case HTTP_RESPONSE:
            is_request = FALSE;
            break;

        default:
            /* This isn't strictly correct, but we should never come here anyways */
            is_request = (pinfo->destport == pinfo->match_uint);
            break;
        }
    } else {
        /* This isn't strictly correct, but we should never come here anyways */
        is_request = (pinfo->destport == pinfo->match_uint);
    }

    operation_status = tvb_get_ntohs(tvb, 2);
    request_id       = tvb_get_ntohl(tvb, 4);

    col_set_str(pinfo->cinfo, COL_PROTOCOL, "IPP");
    if (is_request)
        info = wmem_strdup_printf(wmem_packet_scope(), "IPP Request (%s)", val_to_str(operation_status, operation_vals, "0x%04x"));
    else
        info = wmem_strdup_printf(wmem_packet_scope(), "IPP Response (%s)", val_to_str(operation_status, status_vals, "0x%04x"));

    col_set_str(pinfo->cinfo, COL_INFO, info);

    ti = proto_tree_add_item(tree, proto_ipp, tvb, offset, -1, ENC_NA);
    ipp_tree = proto_item_add_subtree(ti, ett_ipp);

    conversation = find_or_create_conversation(pinfo);
    ipp_info = (ipp_conv_info_t *)conversation_get_proto_data(conversation, proto_ipp);
    if (!ipp_info) {
        ipp_info = wmem_new(wmem_file_scope(), ipp_conv_info_t);
        ipp_info->pdus=wmem_map_new(wmem_file_scope(), g_direct_hash, g_direct_equal);

        conversation_add_proto_data(conversation, proto_ipp, ipp_info);
    }
    if (!PINFO_FD_VISITED(pinfo)) {
        if (is_request) {
            /* This is a request */
            ipp_trans=wmem_new(wmem_file_scope(), ipp_transaction_t);
            ipp_trans->req_frame = pinfo->num;
            ipp_trans->rep_frame = 0;
            ipp_trans->req_time = pinfo->fd->abs_ts;
            wmem_map_insert(ipp_info->pdus, GUINT_TO_POINTER(request_id), (void *)ipp_trans);
        } else {
            ipp_trans=(ipp_transaction_t *)wmem_map_lookup(ipp_info->pdus, GUINT_TO_POINTER(request_id));
            if (ipp_trans) {
                ipp_trans->rep_frame = pinfo->num;
            }
        }
    } else {
        ipp_trans=(ipp_transaction_t *)wmem_map_lookup(ipp_info->pdus, GUINT_TO_POINTER(request_id));
    }
    if (!ipp_trans) {
        /* create a "fake" ipp_trans structure */
        ipp_trans=wmem_new(wmem_packet_scope(), ipp_transaction_t);
        ipp_trans->req_frame = 0;
        ipp_trans->rep_frame = 0;
        ipp_trans->req_time = pinfo->fd->abs_ts;
    }

    /* print state tracking in the tree */
    if (is_request) {
        /* This is a request */
        if (ipp_trans->rep_frame) {
            proto_item *it;

            it = proto_tree_add_uint(ipp_tree, hf_ipp_response_in,
                            tvb, 0, 0, ipp_trans->rep_frame);
            PROTO_ITEM_SET_GENERATED(it);
        }
    } else {
        /* This is a response */
        if (ipp_trans->req_frame) {
            proto_item *it;
            nstime_t ns;

            it = proto_tree_add_uint(ipp_tree, hf_ipp_response_to,
                            tvb, 0, 0, ipp_trans->req_frame);
            PROTO_ITEM_SET_GENERATED(it);

            nstime_delta(&ns, &pinfo->fd->abs_ts, &ipp_trans->req_time);
            it = proto_tree_add_time(ipp_tree, hf_ipp_response_time, tvb, 0, 0, &ns);
            PROTO_ITEM_SET_GENERATED(it);
        }
    }

    proto_tree_add_item(ipp_tree, hf_ipp_version, tvb, offset, 2, ENC_BIG_ENDIAN);
    offset += 2;

    if (is_request) {
        proto_tree_add_item(ipp_tree, hf_ipp_operation_id, tvb, offset, 2, ENC_BIG_ENDIAN);
    } else {
        switch (operation_status & STATUS_TYPE_MASK) {

        case STATUS_SUCCESSFUL:
            status_type = "Successful";
            break;

        case STATUS_INFORMATIONAL:
            status_type = "Informational";
            break;

        case STATUS_REDIRECTION:
            status_type = "Redirection";
            break;

        case STATUS_CLIENT_ERROR:
            status_type = "Client Error";
            break;

        case STATUS_SERVER_ERROR:
            status_type = "Server Error";
            break;

        default:
            status_type = "Unknown";
            break;
        }
        proto_tree_add_uint_format_value(ipp_tree, hf_ipp_status_code, tvb, offset, 2, operation_status, "%s (%s)", status_type, val_to_str(operation_status, status_vals, "0x%04x"));
    }
    offset += 2;

    proto_tree_add_item(ipp_tree, hf_ipp_request_id, tvb, offset, 4, ENC_BIG_ENDIAN);
    offset += 4;

    offset = parse_attributes(tvb, offset, ipp_tree);

    if (tvb_offset_exists(tvb, offset)) {
        call_data_dissector(tvb_new_subset_remaining(tvb, offset), pinfo, ipp_tree);
    }
    return tvb_captured_length(tvb);
}

#define TAG_TYPE(x)       ((x) & 0xF0)

#define TAG_TYPE_DELIMITER      0x00
#define TAG_TYPE_OUTOFBAND      0x10
#define TAG_TYPE_INTEGER        0x20
#define TAG_TYPE_OCTETSTRING    0x30
#define TAG_TYPE_CHARSTRING     0x40

#define TAG_END_OF_ATTRIBUTES   0x03

#define TAG_INTEGER             0x21
#define TAG_BOOLEAN             0x22
#define TAG_ENUM                0x23

#define TAG_OCTETSTRING         0x30
#define TAG_DATETIME            0x31
#define TAG_RESOLUTION          0x32
#define TAG_RANGEOFINTEGER      0x33
#define TAG_BEGINCOLLECTION     0x34
#define TAG_TEXTWITHLANGUAGE    0x35
#define TAG_NAMEWITHLANGUAGE    0x36
#define TAG_ENDCOLLECTION       0x37

#define TAG_TEXTWITHOUTLANGUAGE 0x41
#define TAG_NAMEWITHOUTLANGUAGE 0x42
#define TAG_KEYWORD             0x44
#define TAG_URI                 0x45
#define TAG_URISCHEME           0x46
#define TAG_CHARSET             0x47
#define TAG_NATURALLANGUAGE     0x48
#define TAG_MIMEMEDIATYPE       0x49
#define TAG_MEMBERNAME          0x4a

static const value_string tag_vals[] = {
    /* Delimiter tags */
    { 0x01,                    "operation-attributes-tag" },
    { 0x02,                    "job-attributes-tag" },
    { TAG_END_OF_ATTRIBUTES,   "end-of-attributes-tag" },
    { 0x04,                    "printer-attributes-tag" },
    { 0x05,                    "unsupported-attributes-tag" },
    { 0x06,                    "subscription-attributes-tag" },
    { 0x07,                    "event-notification-attributes-tag" },
    { 0x08,                    "resource-attributes-tag" },
    { 0x09,                    "document-attributes-tag" },

    /* Value tags */
    { 0x10,                    "unsupported" },
    { 0x12,                    "unknown" },
    { 0x13,                    "no-value" },
    { 0x15,                    "not-settable" },
    { 0x16,                    "delete-attribute" },
    { 0x17,                    "admin-define" },
    { TAG_INTEGER,             "integer" },
    { TAG_BOOLEAN,             "boolean" },
    { TAG_ENUM,                "enum" },
    { TAG_OCTETSTRING,         "octetString" },
    { TAG_DATETIME,            "dateTime" },
    { TAG_RESOLUTION,          "resolution" },
    { TAG_RANGEOFINTEGER,      "rangeOfInteger" },
    { TAG_BEGINCOLLECTION,     "begCollection" },
    { TAG_TEXTWITHLANGUAGE,    "textWithLanguage" },
    { TAG_NAMEWITHLANGUAGE,    "nameWithLanguage" },
    { TAG_ENDCOLLECTION,       "endCollection" },
    { TAG_TEXTWITHOUTLANGUAGE, "textWithoutLanguage" },
    { TAG_NAMEWITHOUTLANGUAGE, "nameWithoutLanguage" },
    { TAG_KEYWORD,             "keyword" },
    { TAG_URI,                 "uri" },
    { TAG_URISCHEME,           "uriScheme" },
    { TAG_CHARSET,             "charset" },
    { TAG_NATURALLANGUAGE,     "naturalLanguage" },
    { TAG_MIMEMEDIATYPE,       "mimeMediaType" },
    { TAG_MEMBERNAME,          "memberAttrName" },
    { 0,                       NULL }
};

static int
parse_attributes(tvbuff_t *tvb, int offset, proto_tree *tree)
{
    guint8       tag;
    const gchar *tag_desc;
    int          name_length, value_length;
    proto_tree  *as_tree      = tree;
    proto_item  *tas          = NULL;
    int          start_offset = offset;
    proto_tree  *attr_tree    = tree;
    proto_tree  *subtree      = NULL;

    while (tvb_offset_exists(tvb, offset)) {
        tag = tvb_get_guint8(tvb, offset);
        tag_desc = val_to_str(tag, tag_vals, "Reserved (0x%02x)");
        if (TAG_TYPE(tag) == TAG_TYPE_DELIMITER) {
            /*
             * If we had an attribute sequence we were
             * working on, we're done with it; set its
             * length to the length of all the stuff
             * we've done so far.
             */
            if (tas != NULL)
                proto_item_set_len(tas, offset - start_offset);

            /*
             * This tag starts a new attribute sequence;
             * create a new tree under this tag when we see
             * a non-delimiter tag, under which to put
             * those attributes.
             */
            as_tree   = NULL;
            attr_tree = tree;

            /*
             * Remember the offset at which this attribute
             * sequence started, so we can use it to compute
             * its length when it's finished.
             */
            start_offset = offset;

            /*
             * Now create a new item for this tag.
             */
            subtree = proto_tree_add_subtree(tree, tvb, offset, 1, ett_ipp_as, &tas, tag_desc);
            offset += 1;
            if (tag == TAG_END_OF_ATTRIBUTES) {
                /*
                 * No more attributes.
                 */
                break;
            }
        } else {
            /*
             * Value tag - get the name length.
             */
            name_length = tvb_get_ntohs(tvb, offset + 1);

            /*
             * OK, get the value length.
             */
            value_length = tvb_get_ntohs(tvb, offset + 1 + 2 + name_length);

            /*
             * OK, does the value run past the end of the
             * frame?
             */
            if (as_tree == NULL) {
                /*
                 * OK, there's an attribute to hang
                 * under a delimiter tag, but we don't
                 * have a tree for that tag yet; create
                 * a tree.
                 */
                as_tree = subtree;
                attr_tree = as_tree;
            }

            switch (TAG_TYPE(tag)) {

            case TAG_TYPE_INTEGER:
                if (name_length != 0) {
                    /*
                     * This is an attribute, not
                     * an additional value, so
                     * start a tree for it.
                     */
                    attr_tree = add_integer_tree(as_tree,
                                                 tvb, offset, name_length,
                                                 value_length, tag);
                }
                add_integer_value(tag_desc, attr_tree, tvb,
                                  offset, name_length, value_length, tag);
                break;

            case TAG_TYPE_OCTETSTRING:
                if (name_length != 0) {
                    /*
                     * This is an attribute, not
                     * an additional value, so
                     * start a tree for it.
                     */
                    attr_tree = add_octetstring_tree(as_tree, tvb, offset, tag, name_length, value_length);
                }
                add_octetstring_value(tag_desc, attr_tree, tvb,
                                      offset, name_length, value_length);
                break;

            case TAG_TYPE_CHARSTRING:
                if (name_length != 0) {
                    /*
                     * This is an attribute, not
                     * an additional value, so
                     * start a tree for it.
                     */
                    attr_tree = add_charstring_tree(as_tree,
                                                    tvb, offset, name_length,
                                                    value_length);
                }
                add_charstring_value(tag_desc, attr_tree, tvb,
                                     offset, name_length, value_length);
                break;
            }
            offset += 1 + 2 + name_length + 2 + value_length;
        }
    }

    return offset;
}

static const value_string bool_vals[] = {
    { 0x00, "false" },
    { 0x01, "true" },
    { 0,    NULL }
};

static proto_tree *
add_integer_tree(proto_tree *tree, tvbuff_t *tvb, int offset,
                 int name_length, int value_length, guint8 tag)
{
    proto_tree *subtree;
    guint8      bool_val;

    switch (tag) {

    case TAG_BOOLEAN:
        if (value_length != 1) {
            subtree = proto_tree_add_subtree_format(tree, tvb, offset,
                                     1 + 2 + name_length + 2 + value_length,
                                     ett_ipp_attr, NULL, "%s: Invalid boolean (length is %u, should be 1)",
                                     tvb_format_text(tvb, offset + 1 + 2, name_length),
                                     value_length);
        } else {
            bool_val = tvb_get_guint8(tvb,
                                      offset + 1 + 2 + name_length + 2);
            subtree = proto_tree_add_subtree_format(tree, tvb, offset,
                                     1 + 2 + name_length + 2 + value_length,
                                     ett_ipp_attr, NULL, "%s: %s",
                                     tvb_format_text(tvb, offset + 1 + 2, name_length),
                                     val_to_str(bool_val, bool_vals, "Unknown (0x%02x)"));
        }
        break;

    case TAG_INTEGER :
        if (value_length != 4) {
            subtree = proto_tree_add_subtree_format(tree, tvb, offset,
                                     1 + 2 + name_length + 2 + value_length,
                                     ett_ipp_attr, NULL, "%s: Invalid integer (length is %u, should be 4)",
                                     tvb_format_text(tvb, offset + 1 + 2, name_length),
                                     value_length);
        } else {
            const char *name_val;
            name_val=tvb_get_ptr(tvb, offset + 1 + 2, name_length);
            subtree = proto_tree_add_subtree_format(tree, tvb, offset,
                                     1 + 2 + name_length + 2 + value_length,
                                     ett_ipp_attr, NULL, "%s: %d",
                                     format_text(name_val, name_length),
                                     tvb_get_ntohl(tvb, offset + 1 + 2 + name_length + 2));
        }
        break;

    case TAG_ENUM :
        if (value_length != 4) {
            subtree = proto_tree_add_subtree_format(tree, tvb, offset,
                                     1 + 2 + name_length + 2 + value_length,
                                     ett_ipp_attr, NULL, "%s: Invalid enum (length is %u, should be 4)",
                                     tvb_format_text(tvb, offset + 1 + 2, name_length),
                                     value_length);
        } else {
            const char *name_val;
            name_val=tvb_get_ptr(tvb, offset + 1 + 2, name_length);
            if ((name_length > 5) && name_val && !tvb_memeql(tvb, offset + 1 + 2, "printer-state", 13)) {
                subtree = proto_tree_add_subtree_format(tree, tvb, offset,
                                         1 + 2 + name_length + 2 + value_length,
                                         ett_ipp_attr, NULL, "%s: %s",
                                         format_text(name_val, name_length),
                                         val_to_str_const(tvb_get_ntohl(tvb, offset + 1 + 2 + name_length + 2),
                                                          printer_state_vals,
                                                          "Unknown Printer State"));
            }
            else if ((name_length > 5) && name_val && !tvb_memeql(tvb, offset + 1 + 2, "job-state", 9)) {
                subtree = proto_tree_add_subtree_format(tree, tvb, offset,
                                         1 + 2 + name_length + 2 + value_length,
                                         ett_ipp_attr, NULL, "%s: %s",
                                         format_text(name_val, name_length),
                                         val_to_str_const(tvb_get_ntohl(tvb, offset + 1 + 2 + name_length + 2),
                                                          job_state_vals,
                                                          "Unknown Job State"));
            }
            else if ((name_length > 5) && name_val && !tvb_memeql(tvb, offset + 1 + 2, "document-state", 14)) {
                subtree = proto_tree_add_subtree_format(tree, tvb, offset,
                                         1 + 2 + name_length + 2 + value_length,
                                         ett_ipp_attr, NULL, "%s: %s",
                                         format_text(name_val, name_length),
                                         val_to_str_const(tvb_get_ntohl(tvb, offset + 1 + 2 + name_length + 2),
                                                          document_state_vals,
                                                          "Unknown Document State"));
            }
            else if ((name_length > 5) && name_val && !tvb_memeql(tvb, offset + 1 + 2, "operations-supported", 20)) {
                subtree = proto_tree_add_subtree_format(tree, tvb, offset,
                                         1 + 2 + name_length + 2 + value_length,
                                         ett_ipp_attr, NULL, "%s: %s",
                                         format_text(name_val, name_length),
                                         val_to_str_const(tvb_get_ntohl(tvb, offset + 1 + 2 + name_length + 2),
                                                          operation_vals,
                                                          "Unknown Operation ID"));
            }
            else if ((name_length > 5) && name_val && !tvb_memeql(tvb, offset + 1 + 2, "finishings", 10)) {
                subtree = proto_tree_add_subtree_format(tree, tvb, offset,
                                         1 + 2 + name_length + 2 + value_length,
                                         ett_ipp_attr, NULL, "%s: %s",
                                         format_text(name_val, name_length),
                                         val_to_str_const(tvb_get_ntohl(tvb, offset + 1 + 2 + name_length + 2),
                                                          finishings_vals,
                                                          "Unknown Finishings Value"));
            }
            else if ((name_length > 5) && name_val && (!tvb_memeql(tvb, offset + 1 + 2, "orientation-requested", 21) || !tvb_memeql(tvb, offset + 1 + 2, "media-feed-orientation", 22))) {
                subtree = proto_tree_add_subtree_format(tree, tvb, offset,
                                         1 + 2 + name_length + 2 + value_length,
                                         ett_ipp_attr, NULL, "%s: %s",
                                         format_text(name_val, name_length),
                                         val_to_str_const(tvb_get_ntohl(tvb, offset + 1 + 2 + name_length + 2),
                                                          orientation_vals,
                                                          "Unknown Orientation Value"));
            }
            else if ((name_length > 5) && name_val && !tvb_memeql(tvb, offset + 1 + 2, "print-quality", 13)) {
                subtree = proto_tree_add_subtree_format(tree, tvb, offset,
                                         1 + 2 + name_length + 2 + value_length,
                                         ett_ipp_attr, NULL, "%s: %s",
                                         format_text(name_val, name_length),
                                         val_to_str_const(tvb_get_ntohl(tvb, offset + 1 + 2 + name_length + 2),
                                                          quality_vals,
                                                          "Unknown Print Quality"));
            }
            else if ((name_length > 5) && name_val && !tvb_memeql(tvb, offset + 1 + 2, "transmission-status", 19)) {
                subtree = proto_tree_add_subtree_format(tree, tvb, offset,
                                         1 + 2 + name_length + 2 + value_length,
                                         ett_ipp_attr, NULL, "%s: %s",
                                         format_text(name_val, name_length),
                                         val_to_str_const(tvb_get_ntohl(tvb, offset + 1 + 2 + name_length + 2),
                                                          transmission_status_vals,
                                                          "Unknown Transmission Status"));
            }
            else {
                subtree = proto_tree_add_subtree_format(tree, tvb, offset,
                                         1 + 2 + name_length + 2 + value_length,
                                         ett_ipp_attr, NULL, "%s: %d",
                                         format_text(name_val, name_length),
                                         tvb_get_ntohl(tvb, offset + 1 + 2 + name_length + 2));
            }
        }
        break;

    default:
        subtree = proto_tree_add_subtree_format(tree, tvb, offset,
                                 1 + 2 + name_length + 2 + value_length,
                                 ett_ipp_attr, NULL, "%s: Unknown integer type 0x%02x",
                                 tvb_format_text(tvb, offset + 1 + 2, name_length),
                                 tag);
        break;
    }
    return subtree;
}

static void
add_integer_value(const gchar *tag_desc, proto_tree *tree, tvbuff_t *tvb,
                  int offset, int name_length, int value_length, guint8 tag)
{
    char *name_val = NULL;

    offset = add_value_head(tag_desc, tree, tvb, offset, name_length,
                            value_length, &name_val);

    switch (tag) {

    case TAG_BOOLEAN:
        if (value_length == 1) {
            proto_tree_add_item(tree, hf_ipp_bool_value, tvb, offset, value_length, ENC_BIG_ENDIAN);
        }
        break;

    case TAG_INTEGER:
    case TAG_ENUM:
        if (value_length == 4) {
            if ((name_length > 5) && name_val && !strcmp(name_val, "printer-state")) {
                proto_tree_add_item(tree, hf_ipp_printer_state, tvb, offset, value_length, ENC_BIG_ENDIAN);
            }
            else if ((name_length > 5) && name_val && !strcmp(name_val, "job-state")) {
                proto_tree_add_item(tree, hf_ipp_job_state, tvb, offset, value_length, ENC_BIG_ENDIAN);
            }
            else{
                proto_tree_add_item(tree, hf_ipp_uint32_value, tvb, offset, value_length, ENC_BIG_ENDIAN);
            }
        }
        break;
    }
}

static proto_tree *
add_octetstring_tree(proto_tree *tree, tvbuff_t *tvb, int offset,
                     guint8 tag, int name_length, int value_length)
{
    char valbuf[1024], *value = NULL;
    int valoffset = offset + 1 + 2 + name_length + 2;

    switch (tag) {
        case TAG_OCTETSTRING :
            value = tvb_format_text(tvb, offset + 1 + 2 + name_length + 2, value_length);
            break;

        case TAG_DATETIME :
            if (value_length == 11) {
                guint16 year = tvb_get_ntohs(tvb, valoffset + 0);
                guint8 month = tvb_get_guint8(tvb, valoffset + 2);
                guint8 day = tvb_get_guint8(tvb, valoffset + 3);
                guint8 hours = tvb_get_guint8(tvb, valoffset + 4);
                guint8 minutes = tvb_get_guint8(tvb, valoffset + 5);
                guint8 seconds = tvb_get_guint8(tvb, valoffset + 6);
                guint8 decisecs = tvb_get_guint8(tvb, valoffset + 7);
                guint8 utcsign = tvb_get_guint8(tvb, valoffset + 8);
                guint8 utchours = tvb_get_guint8(tvb, valoffset + 9);
                guint8 utcminutes = tvb_get_guint8(tvb, valoffset + 10);

                g_snprintf(valbuf, sizeof(valbuf), "%04d-%02d-%02dT%02d:%02d:%02d.%d%c%02d%02d", year, month, day, hours, minutes, seconds, decisecs, utcsign, utchours, utcminutes);
                value = valbuf;
            } else {
                value = tvb_bytes_to_str(wmem_packet_scope(), tvb, offset + 1 + 2 + name_length + 2, value_length);
            }
            break;

        case TAG_RESOLUTION :
            if (value_length == 9) {
                int xres = tvb_get_ntohl(tvb, valoffset + 0);
                int yres = tvb_get_ntohl(tvb, valoffset + 4);
                guint8 units = tvb_get_guint8(tvb, valoffset + 8);

                g_snprintf(valbuf, sizeof(valbuf), "%dx%d%s", xres, yres, units == 3 ? "dpi" : units == 4 ? "dpcm" : "unknown");
                value = valbuf;
            } else {
                value = tvb_bytes_to_str(wmem_packet_scope(), tvb, offset + 1 + 2 + name_length + 2, value_length);
            }
            break;

        case TAG_RANGEOFINTEGER :
            if (value_length == 8) {
                guint32 lower = tvb_get_ntohl(tvb, valoffset + 0);
                guint32 upper = tvb_get_ntohl(tvb, valoffset + 4);

                g_snprintf(valbuf, sizeof(valbuf), "%d-%d", lower, upper);
                value = valbuf;
            } else {
                value = tvb_bytes_to_str(wmem_packet_scope(), tvb, offset + 1 + 2 + name_length + 2, value_length);
            }
            break;

        case TAG_TEXTWITHLANGUAGE :
        case TAG_NAMEWITHLANGUAGE :
            if (value_length >= 4) {
                int language_length = tvb_get_ntohs(tvb, valoffset + 0);
                int string_length;

                if (tvb_offset_exists(tvb, valoffset + 2 + language_length)) {
                    string_length = tvb_get_ntohs(tvb, valoffset + 2 + language_length);
                    if (tvb_offset_exists(tvb, valoffset + 2 + language_length + 2 + string_length)) {
                        g_snprintf(valbuf, sizeof(valbuf), "%s (%s)", tvb_format_text(tvb, valoffset + 2 + language_length + 2, string_length), tvb_format_text(tvb, valoffset + 2, language_length));
                        value = valbuf;
                    }
                }
            }

            if (!value) {
                value = tvb_bytes_to_str(wmem_packet_scope(), tvb, offset + 1 + 2 + name_length + 2, value_length);
            }
            break;

        default :
            value = tvb_bytes_to_str(wmem_packet_scope(), tvb, offset + 1 + 2 + name_length + 2, value_length);
            break;
    }

    return proto_tree_add_subtree_format(tree, tvb, offset, 1 + 2 + name_length + 2 + value_length, ett_ipp_attr, NULL, "%s: %s", tvb_format_text(tvb, offset + 1 + 2, name_length), value);
}

static void
add_octetstring_value(const gchar *tag_desc, proto_tree *tree, tvbuff_t *tvb,
                      int offset, int name_length, int value_length)
{
    offset = add_value_head(tag_desc, tree, tvb, offset, name_length,
                            value_length, NULL);
    proto_tree_add_item(tree, hf_ipp_bytes_value, tvb, offset, value_length, ENC_NA);
}

static proto_tree *
add_charstring_tree(proto_tree *tree, tvbuff_t *tvb, int offset,
                    int name_length, int value_length)
{
    return proto_tree_add_subtree_format(tree, tvb, offset,
                             1 + 2 + name_length + 2 + value_length,
                             ett_ipp_attr, NULL, "%s: %s",
                             tvb_format_text(tvb, offset + 1 + 2, name_length),
                             tvb_format_text(tvb, offset + 1 + 2 + name_length + 2, value_length));
}

static void
add_charstring_value(const gchar *tag_desc, proto_tree *tree, tvbuff_t *tvb,
                     int offset, int name_length, int value_length)
{
    offset = add_value_head(tag_desc, tree, tvb, offset, name_length,
                            value_length, NULL);
    proto_tree_add_item(tree, hf_ipp_charstring_value, tvb, offset, value_length, ENC_NA|ENC_ASCII);
}

/* If name_val is !NULL then return the pointer to an emem allocated string in
 * this variable.
 */
static int
add_value_head(const gchar *tag_desc, proto_tree *tree, tvbuff_t *tvb,
               int offset, int name_length, int value_length, char **name_val)
{
    proto_tree_add_string(tree, hf_ipp_tag, tvb, offset, 1, tag_desc);
    offset += 1;
    proto_tree_add_uint(tree, hf_ipp_name_length, tvb, offset, 2, name_length);
    offset += 2;
    if (name_length != 0) {
        guint8 *nv;
        nv = tvb_get_string_enc(wmem_packet_scope(), tvb, offset, name_length, ENC_ASCII);
        proto_tree_add_string(tree, hf_ipp_name, tvb, offset, name_length, format_text(nv, name_length));
        if (name_val) {
            *name_val=nv;
        }
    }
    offset += name_length;
    proto_tree_add_uint(tree, hf_ipp_value_length, tvb, offset, 2, value_length);
    offset += 2;
    return offset;
}

static void
ipp_fmt_version( gchar *result, guint32 revision )
{
   g_snprintf( result, ITEM_LABEL_LENGTH, "%u.%u", (guint8)(( revision & 0xFF00 ) >> 8), (guint8)(revision & 0xFF) );
}

void
proto_register_ipp(void)
{
    static hf_register_info hf[] = {
      /* Generated from convert_proto_tree_add_text.pl */
      { &hf_ipp_version, { "Version", "ipp.version", FT_UINT16, BASE_CUSTOM, CF_FUNC(ipp_fmt_version), 0x0, NULL, HFILL }},
      { &hf_ipp_operation_id, { "Operation ID", "ipp.operation_id", FT_UINT16, BASE_HEX, VALS(operation_vals), 0x0, NULL, HFILL }},
      { &hf_ipp_status_code, { "Status Code", "ipp.status_code", FT_UINT16, BASE_HEX, VALS(status_vals), 0x0, NULL, HFILL }},
      { &hf_ipp_request_id, { "Request ID", "ipp.request_id", FT_UINT32, BASE_DEC, NULL, 0x0, NULL, HFILL }},
      { &hf_ipp_bool_value, { "Value", "ipp.bool_value", FT_UINT8, BASE_HEX, VALS(bool_vals), 0x0, NULL, HFILL }},
      { &hf_ipp_printer_state, { "Printer State", "ipp.printer_state", FT_UINT32, BASE_DEC, VALS(printer_state_vals), 0x0, NULL, HFILL }},
      { &hf_ipp_job_state, { "Job State", "ipp.job_state", FT_UINT32, BASE_DEC, VALS(job_state_vals), 0x0, NULL, HFILL }},
      { &hf_ipp_uint32_value, { "Value", "ipp.uint_value", FT_UINT32, BASE_DEC, NULL, 0x0, NULL, HFILL }},
      { &hf_ipp_bytes_value, { "Value", "ipp.bytes_value", FT_BYTES, BASE_NONE, NULL, 0x0, NULL, HFILL }},
      { &hf_ipp_charstring_value, { "Value", "ipp.charstring_value", FT_STRING, BASE_NONE, NULL, 0x0, NULL, HFILL }},
      { &hf_ipp_tag, { "Tag", "ipp.tag", FT_STRING, BASE_NONE, NULL, 0x0, NULL, HFILL }},
      { &hf_ipp_name_length, { "Name length", "ipp.name_length", FT_UINT16, BASE_DEC, NULL, 0x0, NULL, HFILL }},
      { &hf_ipp_name, { "Name", "ipp.name", FT_STRING, BASE_NONE, NULL, 0x0, NULL, HFILL }},
      { &hf_ipp_value_length, { "Value length", "ipp.value_length", FT_UINT16, BASE_DEC, NULL, 0x0, NULL, HFILL }},
      { &hf_ipp_response_in, { "Response In", "ipp.response_in", FT_FRAMENUM, BASE_NONE, FRAMENUM_TYPE(FT_FRAMENUM_RESPONSE), 0x0, "The response to this IPP request is in this frame", HFILL }},
      { &hf_ipp_response_to, { "Request In", "ipp.response_to", FT_FRAMENUM, BASE_NONE, FRAMENUM_TYPE(FT_FRAMENUM_REQUEST), 0x0, "This is a response to the IPP request in this frame", HFILL }},
      { &hf_ipp_response_time, { "Response Time", "ipp.response_time", FT_RELATIVE_TIME, BASE_NONE, NULL, 0x0, "The time between the Request and the Response", HFILL }}
    };
    static gint *ett[] = {
        &ett_ipp,
        &ett_ipp_as,
        &ett_ipp_attr
    };

    proto_ipp = proto_register_protocol("Internet Printing Protocol", "IPP", "ipp");

    proto_register_field_array(proto_ipp, hf, array_length(hf));
    proto_register_subtree_array(ett, array_length(ett));
}

void
proto_reg_handoff_ipp(void)
{
    dissector_handle_t ipp_handle;

    /*
     * Register ourselves as running atop HTTP and using port 631.
     */
    ipp_handle = create_dissector_handle(dissect_ipp, proto_ipp);
    http_tcp_dissector_add(631, ipp_handle);
    dissector_add_string("media_type", "application/ipp", ipp_handle);
}

/*
 * Editor modelines  -  http://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
