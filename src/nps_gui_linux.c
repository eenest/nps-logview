/*
 * NPS Log Viewer
 * (C) 2026, Eugene Nesterenko - eenest@eenest.net
 *
 * GTK3 Linux GUI
 */

#ifndef __linux__
#error "This file is for Linux builds only"
#endif

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include "parser.h"
#include "radius_dict.h"
#include "config.h"
#include "compat.h"

#define MAX_LIST_COLS 16
#define FILE_WATCH_INTERVAL_MS 2000
#define APP_VERSION "v0.21"
#define SUPPORT_URL "https://ko-fi.com/eenest"
#define WINDOW_CONFIG_SECTION "window.gtk"

typedef struct {
    const char* name;
    const char* title;
    int width;
    int decode;
} ColDef;

static GtkWidget* g_window = NULL;
static GtkWidget* g_treeview = NULL;
static GtkWidget* g_details = NULL;
static GtkWidget* g_status = NULL;
static GtkWidget* g_hpane = NULL;
static GtkWidget* g_filter_entry = NULL;
static GtkWidget* g_filter_rejected = NULL;
static GtkWidget* g_item_recent = NULL;
static GtkWidget* g_menu_recent = NULL;
static GtkWidget* g_menu_columns = NULL;
static GtkListStore* g_store = NULL;
static NpsLogFile g_logfile = {0};
static int g_recent_first = 0;
static char g_current_file[4096] = {0};
static long g_last_file_size = 0;
static int g_file_changed = 0;
static guint g_watch_id = 0;
static int g_selected_rec_idx = -1;
static char g_filter_text[128] = {0};
static int g_filter_rejected_only = 0;
static int g_visible_records[MAX_RECORDS];
static int g_visible_count = 0;
static char g_load_time[32] = {0};
static long g_load_ms = 0;
static long g_load_file_size = 0;

#define MAX_RECENT_FILES 8
static char g_recent_files[MAX_RECENT_FILES][4096] = {{0}};
static int g_recent_count = 0;

static ColDef col_defs[MAX_LIST_COLS];
static int num_col_defs = 0;
static char generic_col_names[MAX_LIST_COLS][32];
static guint g_column_save_timeout = 0;
static int g_restoring_columns = 0;
static int g_snapping_paned = 0;

static const char* RecordFieldValue(const NpsLogRecord* rec, const char* name);

static void ApplyMonospaceFont(GtkWidget* widget)
{
    static GtkCssProvider* provider = NULL;

    if (!provider) {
        provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(provider,
            "textview { font-family: Monospace; font-size: 10pt; }", -1, NULL);
    }
    gtk_style_context_add_provider(gtk_widget_get_style_context(widget),
        GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void CopyTruncated(char* out, size_t out_size, const char* text)
{
    size_t len;

    if (!out || out_size == 0) return;
    if (!text) text = "";
    len = strlen(text);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, text, len);
    out[len] = '\0';
}

static int TreeRowHeight(void)
{
    GtkTreeModel* model;
    GtkTreeIter iter;
    GtkTreePath* path;
    GdkRectangle rect;

    if (g_treeview) {
        model = gtk_tree_view_get_model(GTK_TREE_VIEW(g_treeview));
        if (model && gtk_tree_model_get_iter_first(model, &iter)) {
            path = gtk_tree_model_get_path(model, &iter);
            if (path) {
                gtk_tree_view_get_background_area(GTK_TREE_VIEW(g_treeview),
                                                  path, NULL, &rect);
                gtk_tree_path_free(path);
                if (rect.height > 0) return rect.height;
            }
        }
    }
    return 24;
}

static int SnapPanedPosition(int pos)
{
    int row_h = TreeRowHeight();
    int rows;

    if (row_h <= 0) return pos;
    rows = pos / row_h;
    if (rows < 1) rows = 1;
    return rows * row_h;
}

static void MigrateWindowConfig(void)
{
    static const char* keys[] = {
        "width", "height", "paned", NULL
    };
    int used_legacy = 0;

    for (int i = 0; keys[i]; i++) {
        const char* legacy = config_get_string("window", keys[i], NULL);
        if (legacy) {
            used_legacy = 1;
            if (!config_get_string(WINDOW_CONFIG_SECTION, keys[i], NULL)) {
                config_set_string(WINDOW_CONFIG_SECTION, keys[i], legacy);
            }
        }
    }
    if (used_legacy) {
        config_clear_section("window");
    }
}

static void InitAppConfig(void)
{
    config_init("nps-logview");
    config_set_string("app", "name", "NPS Log Viewer");
    config_set_string("app", "version", APP_VERSION);
    MigrateWindowConfig();
}

static int WindowConfigGetInt(const char* key, int default_val)
{
    return config_get_int(WINDOW_CONFIG_SECTION, key, default_val);
}

static void WindowConfigSetInt(const char* key, int val)
{
    config_set_int(WINDOW_CONFIG_SECTION, key, val);
}

static void ColumnConfigKey(const char* name, char* out, size_t out_size)
{
    size_t pos = 0;

    if (!out || out_size == 0) return;
    out[0] = '\0';

    if (!name || !*name) name = "column";
    for (size_t i = 0; name[i] && pos + 1 < out_size; i++) {
        unsigned char ch = (unsigned char)name[i];
        if (isalnum(ch)) {
            out[pos++] = (char)tolower(ch);
        } else {
            out[pos++] = '_';
        }
    }
    out[pos] = '\0';
}

static int SavedColumnWidth(const char* name, int default_width)
{
    char key[96];
    int width;

    ColumnConfigKey(name, key, sizeof(key));
    width = config_get_int("columns", key, default_width);
    if (width < 40) width = 40;
    if (width > 1200) width = 1200;
    return width;
}

static int IsColumnHidden(const char* name)
{
    char key[96];

    ColumnConfigKey(name, key, sizeof(key));
    return config_get_int("columns.hidden", key, 0);
}

static void SetColumnHidden(const char* name, int hidden)
{
    char key[96];

    ColumnConfigKey(name, key, sizeof(key));
    config_set_int("columns.hidden", key, hidden ? 1 : 0);
}

static void SaveColumnWidths(void)
{
    GList* cols;

    if (!g_treeview) return;
    cols = gtk_tree_view_get_columns(GTK_TREE_VIEW(g_treeview));
    for (GList* l = cols; l; l = l->next) {
        GtkTreeViewColumn* column = GTK_TREE_VIEW_COLUMN(l->data);
        const char* name = g_object_get_data(G_OBJECT(column), "field-name");
        char key[96];
        int width = gtk_tree_view_column_get_width(column);
        if (width <= 0 || !name) continue;
        ColumnConfigKey(name, key, sizeof(key));
        config_set_int("columns", key, width);
    }
    g_list_free(cols);
    config_save();
}

static int FindColumnDef(const char* name)
{
    for (int i = 0; i < num_col_defs; i++) {
        if (col_defs[i].name && strcasecmp(col_defs[i].name, name) == 0) return i;
    }
    return -1;
}

static void SaveColumnOrder(void)
{
    GList* cols;
    char buf[1024] = {0};
    size_t pos = 0;

    if (!g_treeview || g_restoring_columns) return;
    cols = gtk_tree_view_get_columns(GTK_TREE_VIEW(g_treeview));
    for (GList* l = cols; l; l = l->next) {
        const char* name = g_object_get_data(G_OBJECT(l->data), "field-name");
        if (!name) continue;
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s%s",
                        pos ? "," : "", name);
        if (pos >= sizeof(buf) - 1) break;
    }
    g_list_free(cols);
    config_set_string("columns", "order", buf);
    config_save();
}

static void RestoreColumnOrder(void)
{
    const char* saved = config_get_string("columns", "order", "");
    char work[1024];
    GtkTreeViewColumn* ordered[MAX_LIST_COLS];
    int used[MAX_LIST_COLS] = {0};
    int count = 0;

    if (!g_treeview || num_col_defs <= 1 || !saved || !*saved) return;
    snprintf(work, sizeof(work), "%s", saved);

    char* token = strtok(work, ",");
    while (token && count < num_col_defs) {
        int idx = FindColumnDef(token);
        if (idx >= 0 && !used[idx]) {
            ordered[count++] = gtk_tree_view_get_column(GTK_TREE_VIEW(g_treeview), idx);
            used[idx] = 1;
        }
        token = strtok(NULL, ",");
    }
    for (int i = 0; i < num_col_defs && count < num_col_defs; i++) {
        if (!used[i]) {
            ordered[count++] = gtk_tree_view_get_column(GTK_TREE_VIEW(g_treeview), i);
        }
    }
    for (int i = 0; i < count; i++) {
        gtk_tree_view_move_column_after(GTK_TREE_VIEW(g_treeview), ordered[i],
                                        i == 0 ? NULL : ordered[i - 1]);
    }
}

static gboolean on_column_save_timeout(gpointer data)
{
    (void)data;
    SaveColumnWidths();
    g_column_save_timeout = 0;
    return G_SOURCE_REMOVE;
}

static void on_column_width_changed(GObject* object, GParamSpec* pspec, gpointer data)
{
    (void)object; (void)pspec; (void)data;
    if (g_restoring_columns) return;
    if (!g_column_save_timeout) {
        g_column_save_timeout = g_timeout_add(250, on_column_save_timeout, NULL);
    }
}

static void on_column_clicked(GtkTreeViewColumn* column, gpointer data);

static void on_columns_changed(GtkTreeView* treeview, gpointer data)
{
    (void)treeview; (void)data;
    SaveColumnOrder();
}

enum {
    COL_DATA_0 = 0,
    COL_DATA_MAX = MAX_LIST_COLS,
    COL_REC_IDX = MAX_LIST_COLS,
    NUM_COLS
};

static const char* DecodeForDisplay(const char* field_name, const char* value)
{
    static char buf[512];

    if (!field_name || !value || !*value) return value;
    if (decode_radius_field(field_name, value, buf, sizeof(buf))) {
        return buf;
    }
    return value;
}

static const char* ExtractEapMethod(const char* friendly);

static int FieldColumnIndex(const char* field_name)
{
    int idx = 0;
    char extra = '\0';

    if (!field_name) return -1;
    if (sscanf(field_name, "Field %d%c", &idx, &extra) == 1 && idx > 0) {
        return idx - 1;
    }
    return -1;
}

static const char* RecordDisplayValue(const NpsLogRecord* rec, const char* field_name,
                                      int decode, char* out, size_t out_size)
{
    const char* val = "";

    if (!rec || !field_name) return "";
    if (rec->has_names) {
        for (int f = 0; f < rec->num_fields; f++) {
            if (strcasecmp(rec->names[f], field_name) == 0) {
                val = decode ? DecodeForDisplay(rec->names[f], rec->fields[f]) : rec->fields[f];
                break;
            }
        }
        if (strcasecmp(field_name, "Authentication-Type") == 0) {
            for (int f = 0; f < rec->num_fields; f++) {
                if (strcasecmp(rec->names[f], "EAP-Friendly-Name") == 0 && rec->fields[f][0]) {
                    const char* method = ExtractEapMethod(rec->fields[f]);
                    if (method) val = method;
                    break;
                }
            }
        }
    } else if (g_logfile.has_header) {
        for (int h = 0; h < g_logfile.header_count && h < rec->num_fields; h++) {
            if (strcasecmp(g_logfile.header[h], field_name) == 0) {
                val = decode ? DecodeForDisplay(g_logfile.header[h], rec->fields[h]) : rec->fields[h];
                break;
            }
        }
    } else {
        int field_idx = FieldColumnIndex(field_name);
        val = nps_get_field(rec, field_idx >= 0 ? field_idx : 0);
    }

    snprintf(out, out_size, "%s", val ? val : "");
    return out;
}

static void BuildColumnDefs(void)
{
    static const ColDef priority[] = {
        {"Timestamp",            "Time",          160, 0},
        {"User-Name",            "User",          140, 0},
        {"Packet-Type",          "Packet",        110, 1},
        {"Reason-Code",          "Result",        140, 1},
        {"NAS-IP-Address",       "NAS IP",        120, 0},
        {"Client-Friendly-Name", "Client",        120, 0},
        {"Calling-Station-Id",   "Calling Station",140, 0},
        {"Authentication-Type",  "Auth Type",     130, 1},
        {"NP-Policy-Name",       "Policy",        140, 0},
        {"NAS-Port-Type",        "Port Type",     130, 1},
        {"Service-Type",         "Service",       100, 1},
        {NULL, NULL, 0, 0}
    };

    num_col_defs = 0;

    if (!g_logfile.has_header) {
        int max_fields = 0;

        for (int r = 0; r < g_logfile.count; r++) {
            if (g_logfile.records[r].num_fields > max_fields) {
                max_fields = g_logfile.records[r].num_fields;
            }
        }
        if (max_fields <= 0) max_fields = 8;
        if (max_fields > MAX_LIST_COLS) max_fields = MAX_LIST_COLS;

        for (int i = 0; i < max_fields; i++) {
            snprintf(generic_col_names[i], sizeof(generic_col_names[i]), "Field %d", i + 1);
            col_defs[num_col_defs].name = generic_col_names[i];
            col_defs[num_col_defs].title = generic_col_names[i];
            col_defs[num_col_defs].width = 120;
            col_defs[num_col_defs].decode = 0;
            num_col_defs++;
        }
        return;
    }

    for (int p = 0; priority[p].name && num_col_defs < MAX_LIST_COLS; p++) {
        int exists = 0;
        if (g_logfile.has_header) {
            for (int h = 0; h < g_logfile.header_count; h++) {
                if (strcasecmp(g_logfile.header[h], priority[p].name) == 0) {
                    exists = 1;
                    break;
                }
            }
        }
        if (exists && !IsColumnHidden(priority[p].name)) {
            col_defs[num_col_defs++] = priority[p];
        }
    }

    if (g_logfile.has_header) {
        for (int h = 0; h < g_logfile.header_count && num_col_defs < MAX_LIST_COLS; h++) {
            int already = 0;
            for (int c = 0; c < num_col_defs; c++) {
                if (strcasecmp(col_defs[c].name, g_logfile.header[h]) == 0) {
                    already = 1;
                    break;
                }
            }
            if (!already && !IsColumnHidden(g_logfile.header[h])) {
                col_defs[num_col_defs].name = g_logfile.header[h];
                col_defs[num_col_defs].title = g_logfile.header[h];
                col_defs[num_col_defs].width = 110;
                col_defs[num_col_defs].decode = 0;
                num_col_defs++;
            }
        }
    }
}

static const char* MapFieldName(const char* name)
{
    if (!name) return "?";
    if (strcasecmp(name, "Record-Date") == 0 || strcasecmp(name, "Record_Date") == 0) return "Date";
    if (strcasecmp(name, "Record-Time") == 0 || strcasecmp(name, "Record_Time") == 0) return "Time";
    if (strcasecmp(name, "Packet-Type") == 0 || strcasecmp(name, "Packet_Type") == 0) return "Packet Type";
    if (strcasecmp(name, "User-Name") == 0 || strcasecmp(name, "User_Name") == 0) return "User Name";
    if (strcasecmp(name, "Called-Station-ID") == 0 || strcasecmp(name, "Called_Station_ID") == 0) return "Called Station";
    if (strcasecmp(name, "Calling-Station-ID") == 0 || strcasecmp(name, "Calling_Station_ID") == 0) return "Calling Station";
    if (strcasecmp(name, "NAS-IP-Address") == 0 || strcasecmp(name, "NAS_IP_Address") == 0) return "NAS IP";
    if (strcasecmp(name, "NAS-Identifier") == 0 || strcasecmp(name, "NAS_Identifier") == 0) return "NAS ID";
    if (strcasecmp(name, "Service-Type") == 0 || strcasecmp(name, "Service_Type") == 0) return "Service";
    if (strcasecmp(name, "Framed-Protocol") == 0 || strcasecmp(name, "Framed_Protocol") == 0) return "Protocol";
    if (strcasecmp(name, "Framed-IP-Address") == 0 || strcasecmp(name, "Framed_IP_Address") == 0) return "Framed IP";
    if (strcasecmp(name, "Acct-Status-Type") == 0 || strcasecmp(name, "Acct_Status_Type") == 0) return "Acct Status";
    if (strcasecmp(name, "Acct-Session-Time") == 0 || strcasecmp(name, "Acct_Session_Time") == 0) return "Session Time";
    if (strcasecmp(name, "Reason-Code") == 0 || strcasecmp(name, "Reason_Code") == 0) return "Reason";
    if (strcasecmp(name, "Authentication-Type") == 0 || strcasecmp(name, "Authentication_Type") == 0) return "Auth Type";
    if (strcasecmp(name, "Client-IP-Address") == 0 || strcasecmp(name, "Client_IP_Address") == 0) return "Client IP";
    if (strcasecmp(name, "Client-Friendly-Name") == 0 || strcasecmp(name, "Client_Friendly_Name") == 0) return "Client Name";
    if (strcasecmp(name, "Tunnel-Type") == 0 || strcasecmp(name, "Tunnel_Type") == 0) return "Tunnel";
    if (strcasecmp(name, "Policy-Name") == 0 || strcasecmp(name, "Policy_Name") == 0) return "Policy";
    return name;
}

static int TryDecode(const char* header_name, const char* value, char* out, size_t out_size)
{
    return decode_radius_field(header_name, value, out, out_size);
}

static void FormatDetails(const NpsLogRecord* rec, char* out, size_t out_size)
{
    size_t pos = 0;
    char decode_buf[512];

    pos += snprintf(out + pos, out_size - pos, "=== Record Details ===\n\n");

    for (int i = 0; i < rec->num_fields && pos < out_size - 256; i++) {
        const char* field_name = NULL;
        const char* display_name = NULL;

        if (rec->has_names) {
            field_name = rec->names[i];
            display_name = rec->names[i];
        } else if (g_logfile.has_header && i < g_logfile.header_count) {
            field_name = g_logfile.header[i];
            display_name = MapFieldName(field_name);
        } else {
            static char num_name[32];
            snprintf(num_name, sizeof(num_name), "Field %d", i + 1);
            display_name = num_name;
            field_name = num_name;
        }

        if (TryDecode(field_name, rec->fields[i], decode_buf, sizeof(decode_buf))) {
            pos += snprintf(out + pos, out_size - pos, "%s: %s\n", display_name, decode_buf);
        } else {
            pos += snprintf(out + pos, out_size - pos, "%s: %s\n", display_name, rec->fields[i]);
        }
    }
}

static const char* ExtractEapMethod(const char* friendly)
{
    static char buf[64];
    const char* start = strrchr(friendly, '(');
    const char* end = strrchr(friendly, ')');
    if (!start || !end || end <= start + 1) return NULL;
    int len = (int)(end - start - 1);
    if (len <= 0 || len >= (int)sizeof(buf)) return NULL;
    memcpy(buf, start + 1, len);
    buf[len] = '\0';
    return buf;
}

static int RowToRecord(int row)
{
    if (row < 0 || row >= g_visible_count) return -1;
    return g_visible_records[row];
}

static int IsFailureRecord(const NpsLogRecord* rec)
{
    const char* reason;

    if (!rec) return 0;
    reason = RecordFieldValue(rec, "Reason-Code");
    if (!reason) reason = RecordFieldValue(rec, "Reason_Code");
    return (reason && reason[0] && atoi(reason) != 0);
}

static const char* RecordFieldValue(const NpsLogRecord* rec, const char* name)
{
    if (!rec || !name) return NULL;

    if (rec->has_names) {
        for (int f = 0; f < rec->num_fields; f++) {
            if (strcasecmp(rec->names[f], name) == 0) {
                return rec->fields[f];
            }
        }
    } else if (g_logfile.has_header) {
        for (int h = 0; h < g_logfile.header_count && h < rec->num_fields; h++) {
            if (strcasecmp(g_logfile.header[h], name) == 0) {
                return rec->fields[h];
            }
        }
    }

    return NULL;
}

static int ContainsNoCase(const char* haystack, const char* needle)
{
    size_t needle_len;

    if (!needle || !*needle) return 1;
    if (!haystack) return 0;

    needle_len = strlen(needle);
    for (size_t i = 0; haystack[i]; i++) {
        size_t j = 0;
        while (j < needle_len &&
               haystack[i + j] &&
               tolower((unsigned char)haystack[i + j]) ==
               tolower((unsigned char)needle[j])) {
            j++;
        }
        if (j == needle_len) return 1;
    }
    return 0;
}

static int NormalizeMacAddress(const char* text, char out[13])
{
    if (!text || !out) return 0;

    for (size_t i = 0; text[i]; i++) {
        int pos = 0;

        for (size_t j = i; text[j]; j++) {
            unsigned char ch = (unsigned char)text[j];

            if (isxdigit(ch)) {
                out[pos++] = (char)tolower(ch);
                if (pos == 12) {
                    out[12] = '\0';
                    return 1;
                }
            } else if (ch == '-' || ch == ':' || ch == '.') {
                if (pos == 0) break;
            } else {
                break;
            }
        }
    }

    out[0] = '\0';
    return 0;
}

static int StationMatchesMacFilter(const NpsLogRecord* rec)
{
    static const char* station_fields[] = {
        "Calling-Station-Id",
        "Calling-Station-ID",
        "Calling_Station_ID",
        "Called-Station-Id",
        "Called-Station-ID",
        "Called_Station_ID",
        NULL
    };
    char filter_mac[13];
    char value_mac[13];

    if (!NormalizeMacAddress(g_filter_text, filter_mac)) return 0;

    for (int i = 0; station_fields[i]; i++) {
        const char* value = RecordFieldValue(rec, station_fields[i]);
        if (NormalizeMacAddress(value, value_mac) &&
            strcmp(filter_mac, value_mac) == 0) {
            return 1;
        }
    }

    return 0;
}

static int RecordMatchesUserFilter(const NpsLogRecord* rec)
{
    static const char* user_fields[] = {
        "User-Name",
        "SAM-Account-Name",
        "Fully-Qualifed-User-Name",
        "Fully-Qualified-User-Name",
        "Subject-User-Name",
        NULL
    };

    if (!g_filter_text[0]) return 1;
    if (StationMatchesMacFilter(rec)) return 1;

    for (int i = 0; user_fields[i]; i++) {
        const char* value = RecordFieldValue(rec, user_fields[i]);
        if (ContainsNoCase(value, g_filter_text)) return 1;
    }

    for (int f = 0; f < rec->num_fields; f++) {
        if (ContainsNoCase(rec->fields[f], g_filter_text)) return 1;
    }

    return 0;
}

static int SharePairKey(const NpsLogRecord* a, const NpsLogRecord* b)
{
    const char* a_class;
    const char* b_class;
    const char* a_acct;
    const char* b_acct;
    const char* a_time;
    const char* b_time;

    if (!a || !b) return 0;

    a_class = RecordFieldValue(a, "Class");
    b_class = RecordFieldValue(b, "Class");
    if (a_class && b_class && a_class[0] && b_class[0]) {
        return strcmp(a_class, b_class) == 0;
    }

    a_acct = RecordFieldValue(a, "Acct-Session-Id");
    b_acct = RecordFieldValue(b, "Acct-Session-Id");
    if (!a_acct || !b_acct || !a_acct[0] || !b_acct[0] ||
        strcmp(a_acct, b_acct) != 0) {
        return 0;
    }

    a_time = RecordFieldValue(a, "Timestamp");
    b_time = RecordFieldValue(b, "Timestamp");
    if (a_time && b_time && a_time[0] && b_time[0]) {
        return strcmp(a_time, b_time) == 0;
    }

    return 1;
}

static int IsAccessRequestRecord(const NpsLogRecord* rec)
{
    const char* packet = RecordFieldValue(rec, "Packet-Type");
    return packet && atoi(packet) == 1;
}

static int IsRejectedExchangeRecord(int rec_idx)
{
    const NpsLogRecord* rec;

    if (rec_idx < 0 || rec_idx >= g_logfile.count) return 0;
    rec = &g_logfile.records[rec_idx];
    if (IsFailureRecord(rec)) return 1;

    for (int i = 0; i < g_logfile.count; i++) {
        if (i != rec_idx &&
            IsFailureRecord(&g_logfile.records[i]) &&
            SharePairKey(rec, &g_logfile.records[i])) {
            return 1;
        }
    }
    return 0;
}

static int RecordIsFilterSeed(const NpsLogRecord* rec)
{
    if (g_filter_rejected_only && !IsFailureRecord(rec)) return 0;
    return RecordMatchesUserFilter(rec);
}

static int RecordPassesFilter(int rec_idx)
{
    const NpsLogRecord* rec;

    if (rec_idx < 0 || rec_idx >= g_logfile.count) return 0;
    if (!g_filter_text[0] && !g_filter_rejected_only) return 1;

    rec = &g_logfile.records[rec_idx];
    if (RecordIsFilterSeed(rec)) return 1;

    for (int i = 0; i < g_logfile.count; i++) {
        const NpsLogRecord* seed = &g_logfile.records[i];
        if (RecordIsFilterSeed(seed) && SharePairKey(rec, seed)) return 1;
    }

    return 0;
}

static void ApplyOrderingToVisibleRecords(void)
{
    int used[MAX_RECORDS] = {0};
    int temp[MAX_RECORDS];
    int out = 0;

    if (!g_recent_first || g_visible_count <= 1) return;

    for (int i = g_visible_count - 1; i >= 0 && out < MAX_RECORDS; i--) {
        int group[MAX_RECORDS] = {0};
        const NpsLogRecord* anchor;

        if (used[i]) continue;
        anchor = &g_logfile.records[g_visible_records[i]];

        for (int j = 0; j < g_visible_count; j++) {
            if (!used[j] &&
                (j == i ||
                 SharePairKey(anchor, &g_logfile.records[g_visible_records[j]]))) {
                group[j] = 1;
            }
        }

        for (int pass = 0; pass < 2; pass++) {
            for (int j = 0; j < g_visible_count && out < MAX_RECORDS; j++) {
                int rec_idx;
                int is_request;

                if (!group[j]) continue;
                rec_idx = g_visible_records[j];
                is_request = IsAccessRequestRecord(&g_logfile.records[rec_idx]);
                if ((pass == 0 && is_request) || (pass == 1 && !is_request)) {
                    temp[out++] = rec_idx;
                }
            }
        }

        for (int j = 0; j < g_visible_count; j++) {
            if (group[j]) used[j] = 1;
        }
    }

    for (int i = 0; i < out; i++) {
        g_visible_records[i] = temp[i];
    }
    g_visible_count = out;
}

static void BuildVisibleRecords(void)
{
    g_visible_count = 0;
    for (int i = 0; i < g_logfile.count && g_visible_count < MAX_RECORDS; i++) {
        if (RecordPassesFilter(i)) {
            g_visible_records[g_visible_count++] = i;
        }
    }
    ApplyOrderingToVisibleRecords();
}

static void cell_data_failure(GtkTreeViewColumn* col, GtkCellRenderer* renderer,
                               GtkTreeModel* model, GtkTreeIter* iter, gpointer data)
{
    (void)col; (void)data;
    gint rec_idx = 0;
    gtk_tree_model_get(model, iter, COL_REC_IDX, &rec_idx, -1);
    if (rec_idx >= 0 && rec_idx < g_logfile.count) {
        if (IsRejectedExchangeRecord(rec_idx)) {
            g_object_set(renderer,
                         "foreground", "#C80000",
                         "cell-background", "#FFEBEB",
                         NULL);
        } else {
            g_object_set(renderer,
                         "foreground", NULL,
                         "cell-background", NULL,
                         NULL);
        }
    }
}

static void SetListColumns(void)
{
    GList* cols = gtk_tree_view_get_columns(GTK_TREE_VIEW(g_treeview));
    g_restoring_columns = 1;
    for (GList* l = cols; l; l = l->next) {
        gtk_tree_view_remove_column(GTK_TREE_VIEW(g_treeview), GTK_TREE_VIEW_COLUMN(l->data));
    }
    g_list_free(cols);

    if (g_store) {
        gtk_list_store_clear(g_store);
    }

    BuildColumnDefs();

    for (int i = 0; i < num_col_defs; i++) {
        GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes(
            col_defs[i].title, renderer, "text", COL_DATA_0 + i, NULL);
        int width = SavedColumnWidth(col_defs[i].name, col_defs[i].width);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_column_set_clickable(column, TRUE);
        gtk_tree_view_column_set_reorderable(column, TRUE);
        gtk_tree_view_column_set_min_width(column, col_defs[i].width / 2);
        gtk_tree_view_column_set_fixed_width(column, width);
        gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
        gtk_tree_view_column_set_cell_data_func(column, renderer, cell_data_failure, NULL, NULL);
        g_object_set_data_full(G_OBJECT(column), "field-name",
                               g_strdup(col_defs[i].name), g_free);
        g_signal_connect(column, "notify::width", G_CALLBACK(on_column_width_changed), NULL);
        g_signal_connect(column, "clicked", G_CALLBACK(on_column_clicked), GINT_TO_POINTER(i));
        gtk_tree_view_append_column(GTK_TREE_VIEW(g_treeview), column);
    }

    if (num_col_defs == 0) {
        for (int i = 0; i < 8; i++) {
            char name[32];
            snprintf(name, sizeof(name), "Field %d", i + 1);
            GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
            GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes(
                name, renderer, "text", COL_DATA_0 + i, NULL);
            gtk_tree_view_column_set_resizable(column, TRUE);
            gtk_tree_view_append_column(GTK_TREE_VIEW(g_treeview), column);
        }
    }
    RestoreColumnOrder();
    g_restoring_columns = 0;
}

static void UpdateStatusBar(void);
static void PopulateList(void);
static void on_menu_recent_first(GtkMenuItem* item, gpointer data);

static void SaveSortSettings(void)
{
    config_set_int("settings", "recent_first", g_recent_first);
    config_set_string("settings", "sort_column", "");
    config_set_int("settings", "sort_ascending", 0);
    config_save();
}

static void on_column_clicked(GtkTreeViewColumn* column, gpointer data)
{
    int col = GPOINTER_TO_INT(data);
    (void)column;

    if (col >= 0 && col < num_col_defs &&
        strcasecmp(col_defs[col].name, "Timestamp") == 0) {
        g_recent_first = !g_recent_first;
        if (g_item_recent) {
            g_signal_handlers_block_by_func(g_item_recent, G_CALLBACK(on_menu_recent_first), NULL);
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_item_recent), g_recent_first);
            g_signal_handlers_unblock_by_func(g_item_recent, G_CALLBACK(on_menu_recent_first), NULL);
        }
        SaveSortSettings();
        PopulateList();
    }
}

static void PopulateList(void)
{
    if (!g_store) return;
    BuildVisibleRecords();
    gtk_list_store_clear(g_store);

    for (int row = 0; row < g_visible_count; row++) {
        int rec_idx = RowToRecord(row);
        NpsLogRecord* rec = &g_logfile.records[rec_idx];
        GtkTreeIter iter;
        gtk_list_store_append(g_store, &iter);

        for (int col = 0; col < num_col_defs; col++) {
            char val[512];
            RecordDisplayValue(rec, col_defs[col].name, col_defs[col].decode, val, sizeof(val));
            gtk_list_store_set(g_store, &iter, COL_DATA_0 + col, val, -1);
        }

        gtk_list_store_set(g_store, &iter, COL_REC_IDX, rec_idx, -1);
    }

    UpdateStatusBar();
}

static void UpdateStatusBar(void)
{
    const char* fname = g_current_file[0] ? g_current_file : "No file";
    const char* slash = strrchr(fname, '/');
    const char* cap = g_logfile.truncated ? " | WARNING: record limit reached" : "";
    char short_name[160];
    if (slash) fname = slash + 1;
    CopyTruncated(short_name, sizeof(short_name), fname);

    char buf[512];
    if (g_file_changed && g_current_file[0]) {
        snprintf(buf, sizeof(buf),
                 "%s | visible %d of %d | loaded %s in %ldms | size %ld KB | changed: yes%s",
                 short_name, g_visible_count, g_logfile.count,
                 g_load_time[0] ? g_load_time : "-", g_load_ms,
                 (g_load_file_size + 1023) / 1024, cap);
    } else if (g_current_file[0]) {
        snprintf(buf, sizeof(buf),
                 "%s | visible %d of %d | loaded %s in %ldms | size %ld KB | changed: no%s",
                 short_name, g_visible_count, g_logfile.count,
                 g_load_time[0] ? g_load_time : "-", g_load_ms,
                 (g_load_file_size + 1023) / 1024, cap);
    } else {
        snprintf(buf, sizeof(buf), "Ready");
    }
    gtk_label_set_text(GTK_LABEL(g_status), buf);
}

static void SetStatusTextNow(const char* text)
{
    if (!g_status) return;
    gtk_label_set_text(GTK_LABEL(g_status), text ? text : "");
    while (gtk_events_pending()) gtk_main_iteration();
}

static void SetLoadProgressStatus(const char* label, int step, int total)
{
    char buf[128];

    if (step < 1) step = 1;
    if (total < 1) total = 1;
    if (step > total) step = total;
    snprintf(buf, sizeof(buf), "%s: %d/%d", label ? label : "Loading", step, total);
    SetStatusTextNow(buf);
}

typedef struct {
    int last_step;
} LoadProgressCtx;

static void OnLoadProgress(long bytes_read, long total_bytes, void* userdata)
{
    LoadProgressCtx* ctx = (LoadProgressCtx*)userdata;
    int step;

    if (!ctx || total_bytes <= 0) return;
    step = (int)((bytes_read * 5L + total_bytes - 1) / total_bytes);
    if (step < 1) step = 1;
    if (step > 5) step = 5;
    if (step != ctx->last_step) {
        SetLoadProgressStatus("Reading file", step, 5);
        ctx->last_step = step;
    }
}

static void LoadRecentFiles(void)
{
    g_recent_count = 0;
    for (int i = 0; i < MAX_RECENT_FILES; i++) {
        char key[16];
        snprintf(key, sizeof(key), "file%d", i + 1);
        const char* val = config_get_string("recent", key, "");
        if (val && val[0]) {
            snprintf(g_recent_files[i], sizeof(g_recent_files[i]), "%s", val);
            g_recent_count++;
        } else {
            g_recent_files[i][0] = '\0';
        }
    }
}

static void SaveRecentFiles(void)
{
    for (int i = 0; i < MAX_RECENT_FILES; i++) {
        char key[16];
        snprintf(key, sizeof(key), "file%d", i + 1);
        config_set_string("recent", key, g_recent_files[i][0] ? g_recent_files[i] : "");
    }
    config_save();
}

static void AddRecentFile(const char* path)
{
    for (int i = 0; i < g_recent_count; i++) {
        if (strcmp(g_recent_files[i], path) == 0) {
            for (int j = i; j < g_recent_count - 1; j++) {
                snprintf(g_recent_files[j], sizeof(g_recent_files[j]), "%s", g_recent_files[j + 1]);
            }
            g_recent_count--;
            break;
        }
    }
    for (int i = g_recent_count - 1; i >= 0; i--) {
        if (i + 1 < MAX_RECENT_FILES) {
            snprintf(g_recent_files[i + 1], sizeof(g_recent_files[i + 1]), "%s", g_recent_files[i]);
        }
    }
    snprintf(g_recent_files[0], sizeof(g_recent_files[0]), "%s", path);
    if (g_recent_count < MAX_RECENT_FILES) {
        g_recent_count++;
    }
    SaveRecentFiles();
}

static void LoadFile(const char* path);
static void UpdateRecentMenu(void);
static void UpdateColumnMenu(void);

static void on_menu_recent(GtkMenuItem* item, gpointer data)
{
    (void)item;
    int idx = GPOINTER_TO_INT(data);
    if (idx < 0 || idx >= g_recent_count) return;
    if (access(g_recent_files[idx], F_OK) != 0) {
        GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(g_window),
                                                GTK_DIALOG_MODAL,
                                                GTK_MESSAGE_WARNING,
                                                GTK_BUTTONS_OK,
                                                "The file no longer exists at this location.");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
    } else {
        LoadFile(g_recent_files[idx]);
    }
}

static void UpdateRecentMenu(void)
{
    if (!g_menu_recent) return;
    /* Remove existing children */
    GList* children = gtk_container_get_children(GTK_CONTAINER(g_menu_recent));
    for (GList* l = children; l; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);

    if (g_recent_count == 0) {
        GtkWidget* item = gtk_menu_item_new_with_label("(No recent files)");
        gtk_widget_set_sensitive(item, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(g_menu_recent), item);
        gtk_widget_show(item);
        return;
    }

    for (int i = 0; i < g_recent_count; i++) {
        const char* display = g_recent_files[i];
        const char* slash = strrchr(display, '/');
        char short_display[160];
        if (slash) display = slash + 1;
        CopyTruncated(short_display, sizeof(short_display), display);
        char label[180];
        snprintf(label, sizeof(label), "_%d %s", i + 1, short_display);
        GtkWidget* item = gtk_menu_item_new_with_label(label);
        gtk_menu_shell_append(GTK_MENU_SHELL(g_menu_recent), item);
        g_signal_connect(item, "activate", G_CALLBACK(on_menu_recent), GINT_TO_POINTER(i));
        gtk_widget_show(item);
    }
}

static void on_menu_column(GtkCheckMenuItem* item, gpointer data)
{
    int idx = GPOINTER_TO_INT(data);
    if (idx < 0 || idx >= g_logfile.header_count) return;
    SetColumnHidden(g_logfile.header[idx],
                    !gtk_check_menu_item_get_active(item));
    config_save();
    UpdateColumnMenu();
    SetListColumns();
    PopulateList();
}

static void on_menu_columns_reset(GtkMenuItem* item, gpointer data)
{
    (void)item; (void)data;
    config_clear_section("columns.hidden");
    config_save();
    UpdateColumnMenu();
    SetListColumns();
    PopulateList();
}

static void UpdateColumnMenu(void)
{
    if (!g_menu_columns) return;

    GList* children = gtk_container_get_children(GTK_CONTAINER(g_menu_columns));
    for (GList* l = children; l; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);

    if (!g_logfile.has_header) {
        GtkWidget* item = gtk_menu_item_new_with_label("(Open a log first)");
        gtk_widget_set_sensitive(item, FALSE);
        gtk_menu_shell_append(GTK_MENU_SHELL(g_menu_columns), item);
        gtk_widget_show(item);
        return;
    }

    for (int i = 0; i < g_logfile.header_count && i < MAX_FIELDS; i++) {
        GtkWidget* item = gtk_check_menu_item_new_with_label(g_logfile.header[i]);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),
                                       !IsColumnHidden(g_logfile.header[i]));
        gtk_menu_shell_append(GTK_MENU_SHELL(g_menu_columns), item);
        g_signal_connect(item, "toggled", G_CALLBACK(on_menu_column), GINT_TO_POINTER(i));
        gtk_widget_show(item);
    }

    GtkWidget* sep = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(g_menu_columns), sep);
    gtk_widget_show(sep);

    GtkWidget* reset = gtk_menu_item_new_with_label("Reset Columns");
    gtk_menu_shell_append(GTK_MENU_SHELL(g_menu_columns), reset);
    g_signal_connect(reset, "activate", G_CALLBACK(on_menu_columns_reset), NULL);
    gtk_widget_show(reset);
}

static void LoadFile(const char* path)
{
    struct stat st;
    FILE* fp;
    long size;
    LoadProgressCtx progress = {0};
    gint64 start_us = g_get_monotonic_time();

    if (stat(path, &st) != 0) {
        GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(g_window),
                                                GTK_DIALOG_MODAL,
                                                GTK_MESSAGE_ERROR,
                                                GTK_BUTTONS_OK,
                                                "Failed to open file.");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return;
    }

    size = st.st_size;
    if (size <= 0) {
        GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(g_window),
                                                GTK_DIALOG_MODAL,
                                                GTK_MESSAGE_ERROR,
                                                GTK_BUTTONS_OK,
                                                "File is empty or too large.");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return;
    }

    fp = fopen(path, "rb");
    if (!fp) {
        GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(g_window),
                                                GTK_DIALOG_MODAL,
                                                GTK_MESSAGE_ERROR,
                                                GTK_BUTTONS_OK,
                                                "Failed to open file.");
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return;
    }

    SetLoadProgressStatus("Reading file", 1, 5);

    strncpy(g_current_file, path, sizeof(g_current_file) - 1);
    g_current_file[sizeof(g_current_file) - 1] = '\0';
    g_last_file_size = size;
    g_load_file_size = size;
    g_file_changed = 0;

    nps_logfile_free(&g_logfile);
    nps_parse_stream(fp, size, &g_logfile, OnLoadProgress, &progress);
    fclose(fp);
    SetStatusTextNow("Building view...");
    g_load_ms = (long)((g_get_monotonic_time() - start_us) / 1000);
    {
        time_t now = time(NULL);
        struct tm* tm_now = localtime(&now);
        if (tm_now) {
            strftime(g_load_time, sizeof(g_load_time), "%Y-%m-%d %H:%M:%S", tm_now);
        } else {
            g_load_time[0] = '\0';
        }
    }

    UpdateColumnMenu();
    SetListColumns();
    PopulateList();

    GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_details));
    gtk_text_buffer_set_text(buf,
        "Select a record to view decoded details.", -1);

    AddRecentFile(path);
    UpdateRecentMenu();
}

static void OnListSelect(GtkTreeSelection* sel, gpointer data)
{
    (void)data;
    static char details[32768];
    GtkTreeIter iter;
    GtkTreeModel* model;
    gint rec_idx = 0;

    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) return;
    gtk_tree_model_get(model, &iter, COL_REC_IDX, &rec_idx, -1);

    if (rec_idx < 0 || rec_idx >= g_logfile.count) return;
    g_selected_rec_idx = rec_idx;
    FormatDetails(&g_logfile.records[rec_idx], details, sizeof(details));

    GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_details));
    gtk_text_buffer_set_text(buf, details, -1);

    if (IsRejectedExchangeRecord(rec_idx)) {
        GtkTextIter start, end;
        gtk_text_buffer_get_start_iter(buf, &start);
        gtk_text_buffer_get_end_iter(buf, &end);
        GtkTextTagTable* table = gtk_text_buffer_get_tag_table(buf);
        GtkTextTag* tag = gtk_text_tag_table_lookup(table, "failure-red");
        if (!tag) {
            tag = gtk_text_buffer_create_tag(buf, "failure-red",
                                              "foreground", "#DC0000", NULL);
        }
        gtk_text_buffer_apply_tag(buf, tag, &start, &end);
    }
}

static void OpenFileDialog(GtkWidget* parent)
{
    GtkWidget* dlg = gtk_file_chooser_dialog_new("Open NPS Log",
                                                  GTK_WINDOW(parent),
                                                  GTK_FILE_CHOOSER_ACTION_OPEN,
                                                  "_Cancel", GTK_RESPONSE_CANCEL,
                                                  "_Open", GTK_RESPONSE_ACCEPT,
                                                  NULL);

    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "NPS Log Files (*.log;*.txt;*.csv)");
    gtk_file_filter_add_pattern(filter, "*.log");
    gtk_file_filter_add_pattern(filter, "*.txt");
    gtk_file_filter_add_pattern(filter, "*.csv");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), filter);

    GtkFileFilter* all = gtk_file_filter_new();
    gtk_file_filter_set_name(all, "All Files");
    gtk_file_filter_add_pattern(all, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), all);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (filename) {
            LoadFile(filename);
            g_free(filename);
        }
    }
    gtk_widget_destroy(dlg);
}

static gboolean on_file_watch(gpointer data)
{
    (void)data;
    if (!g_current_file[0]) return G_SOURCE_CONTINUE;

    struct stat st;
    if (stat(g_current_file, &st) == 0) {
        if (st.st_size != g_last_file_size) {
            if (!g_file_changed) {
                g_file_changed = 1;
                UpdateStatusBar();
            }
        }
    }
    return G_SOURCE_CONTINUE;
}

static void on_menu_open(GtkMenuItem* item, gpointer data)
{
    (void)item; (void)data;
    OpenFileDialog(g_window);
}

static void WriteCsvCell(FILE* fp, const char* text)
{
    fputc('"', fp);
    for (const char* p = text ? text : ""; *p; p++) {
        if (*p == '"') fputc('"', fp);
        fputc(*p, fp);
    }
    fputc('"', fp);
}

static void WriteHtmlEscaped(FILE* fp, const char* text)
{
    for (const char* p = text ? text : ""; *p; p++) {
        if (*p == '&') fputs("&amp;", fp);
        else if (*p == '<') fputs("&lt;", fp);
        else if (*p == '>') fputs("&gt;", fp);
        else if (*p == '"') fputs("&quot;", fp);
        else fputc(*p, fp);
    }
}

static void ExportVisibleRecords(const char* title, const char* pattern, int format)
{
    GtkWidget* dlg = gtk_file_chooser_dialog_new(title, GTK_WINDOW(g_window),
        GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Save", GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), pattern);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        FILE* fp = path ? fopen(path, "w") : NULL;
        if (!fp) {
            GtkWidget* msg = gtk_message_dialog_new(GTK_WINDOW(g_window), GTK_DIALOG_MODAL,
                GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "Failed to create export file.");
            gtk_dialog_run(GTK_DIALOG(msg));
            gtk_widget_destroy(msg);
        } else {
            if (format == 2) {
                fputs("<!doctype html><html><head><meta charset=\"utf-8\"><title>NPS Log Export</title>"
                      "<style>body{font-family:sans-serif}table{border-collapse:collapse}"
                      "td,th{border:1px solid #bbb;padding:4px 6px}tr.rejected{color:#c80000;background:#ffebeb}</style>"
                      "</head><body><table><thead><tr>", fp);
                for (int c = 0; c < num_col_defs; c++) {
                    fputs("<th>", fp);
                    WriteHtmlEscaped(fp, col_defs[c].title);
                    fputs("</th>", fp);
                }
                fputs("</tr></thead><tbody>\n", fp);
            } else if (format == 0) {
                for (int c = 0; c < num_col_defs; c++) {
                    if (c) fputc(',', fp);
                    WriteCsvCell(fp, col_defs[c].title);
                }
                fputc('\n', fp);
            }

            for (int row = 0; row < g_visible_count; row++) {
                int rec_idx = RowToRecord(row);
                char value[512];
                if (format == 2) {
                    fputs(IsRejectedExchangeRecord(rec_idx) ? "<tr class=\"rejected\">" : "<tr>", fp);
                } else if (format == 1) {
                    fprintf(fp, "=== Record %d ===\n", row + 1);
                }
                for (int c = 0; c < num_col_defs; c++) {
                    RecordDisplayValue(&g_logfile.records[rec_idx], col_defs[c].name,
                                       col_defs[c].decode, value, sizeof(value));
                    if (format == 2) {
                        fputs("<td>", fp);
                        WriteHtmlEscaped(fp, value);
                        fputs("</td>", fp);
                    } else if (format == 0) {
                        if (c) fputc(',', fp);
                        WriteCsvCell(fp, value);
                    } else {
                        fprintf(fp, "%s: %s\n", col_defs[c].title, value);
                    }
                }
                if (format == 2) fputs("</tr>\n", fp);
                else fputc('\n', fp);
            }
            if (format == 2) fputs("</tbody></table></body></html>\n", fp);
            fclose(fp);
            gtk_label_set_text(GTK_LABEL(g_status), "Export complete.");
        }
        g_free(path);
    }
    gtk_widget_destroy(dlg);
}

static void on_menu_export_csv(GtkMenuItem* item, gpointer data)
{
    (void)item; (void)data;
    ExportVisibleRecords("Export CSV", "nps-logview-export.csv", 0);
}

static void on_menu_export_text(GtkMenuItem* item, gpointer data)
{
    (void)item; (void)data;
    ExportVisibleRecords("Export Text", "nps-logview-export.txt", 1);
}

static void on_menu_export_html(GtkMenuItem* item, gpointer data)
{
    (void)item; (void)data;
    ExportVisibleRecords("Export HTML", "nps-logview-export.html", 2);
}

static void on_menu_exit(GtkMenuItem* item, gpointer data)
{
    (void)item; (void)data;
    gtk_widget_destroy(g_window);
}

static void ApplyFilter(void)
{
    const char* text = "";

    if (g_filter_entry) {
        text = gtk_entry_get_text(GTK_ENTRY(g_filter_entry));
    }
    snprintf(g_filter_text, sizeof(g_filter_text), "%s", text ? text : "");
    g_filter_rejected_only =
        g_filter_rejected &&
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_filter_rejected));

    PopulateList();
    g_selected_rec_idx = -1;
    if (g_details) {
        GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_details));
        gtk_text_buffer_set_text(buf,
            "Select a record to view decoded details.", -1);
    }
}

static void on_filter_changed(GtkWidget* widget, gpointer data)
{
    (void)widget; (void)data;
    ApplyFilter();
}

static void on_filter_clear(GtkButton* button, gpointer data)
{
    (void)button; (void)data;
    if (g_filter_entry) {
        gtk_entry_set_text(GTK_ENTRY(g_filter_entry), "");
    }
    if (g_filter_rejected) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_filter_rejected), FALSE);
    }
    ApplyFilter();
}

static void on_menu_copy(GtkMenuItem* item, gpointer data)
{
    (void)item; (void)data;
    if (!g_details) return;
    GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_details));
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(buf, &start);
    gtk_text_buffer_get_end_iter(buf, &end);
    gchar* details = gtk_text_buffer_get_text(buf, &start, &end, FALSE);
    if (!details) return;

    const char* raw = NULL;
    if (g_selected_rec_idx >= 0 && g_selected_rec_idx < g_logfile.count) {
        raw = g_logfile.records[g_selected_rec_idx].raw_line;
    }

    if (raw) {
        gchar* combined = g_strdup_printf(
            "=== Original Record ===\n\n%s\n\n=== Decoded Record ===\n\n%s",
            raw, details);
        GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_text(cb, combined, -1);
        g_free(combined);
    } else {
        GtkClipboard* cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_text(cb, details, -1);
    }
    g_free(details);
}

static gboolean on_details_button_press(GtkWidget* widget, GdkEventButton* event, gpointer data)
{
    (void)widget; (void)data;
    if (event->button == 3) { /* right click */
        GtkWidget* menu = gtk_menu_new();
        GtkWidget* item = gtk_menu_item_new_with_label("Copy Record");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        g_signal_connect(item, "activate", G_CALLBACK(on_menu_copy), NULL);
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
        return TRUE;
    }
    return FALSE;
}

static gboolean on_treeview_button_press(GtkWidget* widget, GdkEventButton* event, gpointer data)
{
    (void)data;

    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        GtkTreePath* path = NULL;

        if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
                                           (gint)event->x, (gint)event->y,
                                           &path, NULL, NULL, NULL)) {
            return FALSE;
        }

        gtk_tree_selection_select_path(
            gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)), path);
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget), path, NULL, FALSE);
        gtk_tree_path_free(path);

        GtkWidget* menu = gtk_menu_new();
        GtkWidget* item = gtk_menu_item_new_with_label("Copy Record");
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        g_signal_connect(item, "activate", G_CALLBACK(on_menu_copy), NULL);
        gtk_widget_show_all(menu);
        gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent*)event);
        return TRUE;
    }

    return FALSE;
}

static void on_menu_recent_first(GtkMenuItem* item, gpointer data)
{
    (void)item; (void)data;
    g_recent_first = !g_recent_first;
    SaveSortSettings();
    PopulateList();
}

static void show_reference_dialog(const char* title, const char* text)
{
    GdkDisplay* display;
    GdkMonitor* monitor = NULL;
    GdkRectangle geometry;
    int max_height = 400;
    int dialog_height;
    int dialog_width;

    display = gdk_display_get_default();
    if (display) {
        monitor = gdk_display_get_primary_monitor(display);
        if (!monitor) monitor = gdk_display_get_monitor(display, 0);
    }
    if (monitor) {
        gdk_monitor_get_geometry(monitor, &geometry);
        max_height = geometry.height / 2;
    }
    dialog_height = max_height;
    if (dialog_height > 480) dialog_height = 480;
    dialog_width = 560;
    if (monitor && dialog_width > geometry.width - 80) {
        dialog_width = geometry.width - 80;
    }
    if (dialog_width < 280 && monitor) {
        dialog_width = geometry.width > 40 ? geometry.width - 20 : geometry.width;
    }

    GtkWidget* dlg = gtk_dialog_new_with_buttons(title,
                                                 GTK_WINDOW(g_window),
                                                 GTK_DIALOG_MODAL |
                                                 GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 "_OK", GTK_RESPONSE_OK,
                                                 NULL);
    gtk_window_set_title(GTK_WINDOW(dlg), title);
    gtk_window_set_default_size(GTK_WINDOW(dlg), dialog_width, dialog_height);

    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget* scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled, dialog_width - 40,
                                dialog_height > 120 ? dialog_height - 90 : dialog_height);

    GtkWidget* text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text_view), GTK_WRAP_NONE);
    ApplyMonospaceFont(text_view);

    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    gtk_text_buffer_set_text(buffer, text, -1);
    gtk_container_add(GTK_CONTAINER(scrolled), text_view);
    gtk_box_pack_start(GTK_BOX(area), scrolled, TRUE, TRUE, 8);
    gtk_widget_show_all(dlg);
    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static void on_menu_radius(GtkMenuItem* item, gpointer data)
{
    (void)item; (void)data;
    static char radius_help[24576];
    size_t rp = 0;

    rp += snprintf(radius_help + rp, sizeof(radius_help) - rp,
                   "RADIUS Attribute Codes Reference\n"
                   "=================================\n\n");
    for (int i = 1; i <= 255; i++) {
        const char* attr_name = radius_attr_name(i);
        if (strcmp(attr_name, "Unknown") != 0) {
            rp += snprintf(radius_help + rp, sizeof(radius_help) - rp,
                           "%3d = %s\n", i, attr_name);
        }
    }

    show_reference_dialog("RADIUS Attribute Codes", radius_help);
}

static void on_menu_vendor(GtkMenuItem* item, gpointer data)
{
    (void)item; (void)data;
    static char vendor_help[24576];
    static const unsigned int vendors[] = {
        9, 311, 2636, 6889, 12356, 25461, 29671
    };
    size_t vp = 0;

    vp += snprintf(vendor_help + vp, sizeof(vendor_help) - vp,
                   "Vendor-Specific Attribute Codes Reference\n"
                   "=========================================\n\n");
    for (size_t v = 0; v < sizeof(vendors) / sizeof(vendors[0]); v++) {
        unsigned int vendor_id = vendors[v];
        vp += snprintf(vendor_help + vp, sizeof(vendor_help) - vp,
                       "%s (%u)\n", radius_vendor_name(vendor_id), vendor_id);
        for (unsigned int type = 1; type <= 255; type++) {
            const char* name = radius_vendor_attr_name(vendor_id, type);
            if (name) {
                vp += snprintf(vendor_help + vp, sizeof(vendor_help) - vp,
                               "  %3u = %s\n", type, name);
            }
        }
        vp += snprintf(vendor_help + vp, sizeof(vendor_help) - vp, "\n");
    }

    show_reference_dialog("Vendor-Specific Attribute Codes", vendor_help);
}

static void on_menu_about(GtkMenuItem* item, gpointer data)
{
    (void)item; (void)data;
    char comments[2048];
    GtkWidget* dlg = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dlg), "NPS Log Viewer");
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dlg), APP_VERSION);
    gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(dlg),
        "(C) 2026, Eugene Nesterenko - eenest@eenest.net");
    snprintf(comments, sizeof(comments),
        "Decodes Microsoft NPS/IAS RADIUS logs.\n"
        "Supports XML and CSV log formats.\n"
        "RADIUS attribute codes and common option values\n"
        "are automatically translated to human-readable text.\n\n"
        "NPS Log Viewer is free software. If it saves you time,\n"
        "coffee-money tips are appreciated but never required.\n"
        "The GPL license remains unchanged.\n\n"
        "Settings file:\n%s",
        config_get_path());
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dlg), comments);
    gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(dlg), SUPPORT_URL);
    gtk_about_dialog_set_website_label(GTK_ABOUT_DIALOG(dlg), "Support the project");

    char exe_path[1024];
    char icon_path[2048];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        char* last_slash = strrchr(exe_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            snprintf(icon_path, sizeof(icon_path),
                     "%s/icons/icon_48x48.png", exe_path);
            if (access(icon_path, F_OK) == 0) {
                GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file(icon_path, NULL);
                if (pixbuf) {
                    gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(dlg), pixbuf);
                    g_object_unref(pixbuf);
                }
            }
        }
    }

    gtk_dialog_run(GTK_DIALOG(dlg));
    gtk_widget_destroy(dlg);
}

static void on_menu_support(GtkMenuItem* item, gpointer data)
{
    GError* error = NULL;
    (void)item; (void)data;

    if (!gtk_show_uri_on_window(GTK_WINDOW(g_window), SUPPORT_URL, GDK_CURRENT_TIME, &error)) {
        GtkWidget* dlg = gtk_message_dialog_new(GTK_WINDOW(g_window),
            GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK,
            "Could not open the support page.\n\nSupport URL:\n%s",
            SUPPORT_URL);
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        if (error) g_error_free(error);
    }
}

static gboolean on_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data)
{
    (void)widget; (void)data;
    if ((event->state & GDK_CONTROL_MASK) && (event->keyval == GDK_KEY_o || event->keyval == GDK_KEY_O)) {
        OpenFileDialog(g_window);
        return TRUE;
    }
    if ((event->state & GDK_CONTROL_MASK) && (event->keyval == GDK_KEY_c || event->keyval == GDK_KEY_C)) {
        on_menu_copy(NULL, NULL);
        return TRUE;
    }
    return FALSE;
}

static GtkWidget* BuildMenuBar(void)
{
    GtkWidget* menubar = gtk_menu_bar_new();

    GtkWidget* item_file = gtk_menu_item_new_with_mnemonic("_File");
    GtkWidget* menu_file = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_file), menu_file);

    GtkWidget* item_open = gtk_menu_item_new_with_mnemonic("_Open");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), item_open);
    g_signal_connect(item_open, "activate", G_CALLBACK(on_menu_open), NULL);

    GtkWidget* sep_export = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), sep_export);

    GtkWidget* item_export_csv = gtk_menu_item_new_with_mnemonic("Export _CSV");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), item_export_csv);
    g_signal_connect(item_export_csv, "activate", G_CALLBACK(on_menu_export_csv), NULL);

    GtkWidget* item_export_text = gtk_menu_item_new_with_mnemonic("Export _Text");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), item_export_text);
    g_signal_connect(item_export_text, "activate", G_CALLBACK(on_menu_export_text), NULL);

    GtkWidget* item_export_html = gtk_menu_item_new_with_mnemonic("Export _HTML");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), item_export_html);
    g_signal_connect(item_export_html, "activate", G_CALLBACK(on_menu_export_html), NULL);

    GtkWidget* item_recent = gtk_menu_item_new_with_mnemonic("Recent _Files");
    g_menu_recent = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_recent), g_menu_recent);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), item_recent);

    GtkWidget* sep1 = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), sep1);

    GtkWidget* item_exit = gtk_menu_item_new_with_mnemonic("E_xit");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), item_exit);
    g_signal_connect(item_exit, "activate", G_CALLBACK(on_menu_exit), NULL);

    GtkWidget* item_edit = gtk_menu_item_new_with_mnemonic("_Edit");
    GtkWidget* menu_edit = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_edit), menu_edit);

    GtkWidget* item_copy = gtk_menu_item_new_with_mnemonic("_Copy Record");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_edit), item_copy);
    g_signal_connect(item_copy, "activate", G_CALLBACK(on_menu_copy), NULL);

    GtkWidget* item_settings = gtk_menu_item_new_with_mnemonic("_Settings");
    GtkWidget* menu_settings = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_settings), menu_settings);

    g_item_recent = gtk_check_menu_item_new_with_mnemonic("Most Recent _First");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_settings), g_item_recent);
    g_signal_connect(g_item_recent, "activate", G_CALLBACK(on_menu_recent_first), NULL);

    GtkWidget* item_columns = gtk_menu_item_new_with_mnemonic("_Columns");
    g_menu_columns = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_columns), g_menu_columns);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_settings), item_columns);

    GtkWidget* item_help = gtk_menu_item_new_with_mnemonic("_Help");
    GtkWidget* menu_help = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item_help), menu_help);

    GtkWidget* item_radius = gtk_menu_item_new_with_mnemonic("_RADIUS Attribute Codes");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_help), item_radius);
    g_signal_connect(item_radius, "activate", G_CALLBACK(on_menu_radius), NULL);

    GtkWidget* item_vendor = gtk_menu_item_new_with_mnemonic("_Vendor-Specific Attribute Codes");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_help), item_vendor);
    g_signal_connect(item_vendor, "activate", G_CALLBACK(on_menu_vendor), NULL);

    GtkWidget* sep2 = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_help), sep2);

    GtkWidget* item_support = gtk_menu_item_new_with_mnemonic("_Support Project");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_help), item_support);
    g_signal_connect(item_support, "activate", G_CALLBACK(on_menu_support), NULL);

    GtkWidget* item_about = gtk_menu_item_new_with_mnemonic("_About");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_help), item_about);
    g_signal_connect(item_about, "activate", G_CALLBACK(on_menu_about), NULL);

    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), item_file);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), item_edit);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), item_settings);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), item_help);

    return menubar;
}

static GtkWidget* BuildListView(void)
{
    GType types[NUM_COLS];
    for (int i = 0; i < NUM_COLS; i++) {
        types[i] = G_TYPE_STRING;
    }
    types[COL_REC_IDX] = G_TYPE_INT;

    g_store = gtk_list_store_newv(NUM_COLS, types);

    GtkWidget* treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(g_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(treeview), TRUE);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(treeview), GTK_TREE_VIEW_GRID_LINES_BOTH);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)),
                                 GTK_SELECTION_SINGLE);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview)),
                     "changed", G_CALLBACK(OnListSelect), NULL);
    g_signal_connect(treeview, "button-press-event",
                     G_CALLBACK(on_treeview_button_press), NULL);
    g_signal_connect(treeview, "columns-changed",
                     G_CALLBACK(on_columns_changed), NULL);

    return treeview;
}

static guint g_resize_timeout = 0;

static gboolean on_resize_timeout(gpointer data)
{
    (void)data;
    int w, h;
    gtk_window_get_size(GTK_WINDOW(g_window), &w, &h);
    WindowConfigSetInt("width", w);
    WindowConfigSetInt("height", h);
    if (g_hpane) {
        WindowConfigSetInt("paned", SnapPanedPosition(
            gtk_paned_get_position(GTK_PANED(g_hpane))));
    }
    config_save();
    g_resize_timeout = 0;
    return G_SOURCE_REMOVE;
}

static void on_paned_position_notify(GObject* object, GParamSpec* pspec, gpointer data)
{
    (void)object; (void)pspec; (void)data;
    if (g_snapping_paned) return;
    if (g_hpane) {
        int pos = gtk_paned_get_position(GTK_PANED(g_hpane));
        int snapped = SnapPanedPosition(pos);
        if (snapped != pos) {
            g_snapping_paned = 1;
            gtk_paned_set_position(GTK_PANED(g_hpane), snapped);
            g_snapping_paned = 0;
        }
    }
    if (g_resize_timeout) {
        g_source_remove(g_resize_timeout);
    }
    g_resize_timeout = g_timeout_add(500, on_resize_timeout, NULL);
}

static gboolean on_configure(GtkWidget* widget, GdkEventConfigure* event, gpointer data)
{
    (void)widget; (void)event; (void)data;
    if (g_resize_timeout) {
        g_source_remove(g_resize_timeout);
    }
    g_resize_timeout = g_timeout_add(500, on_resize_timeout, NULL);
    return FALSE;
}

static void on_destroy(GtkWidget* widget, gpointer data)
{
    (void)widget; (void)data;
    if (g_resize_timeout) {
        g_source_remove(g_resize_timeout);
        g_resize_timeout = 0;
    }
    if (g_column_save_timeout) {
        g_source_remove(g_column_save_timeout);
        g_column_save_timeout = 0;
    }
    SaveColumnWidths();
    SaveColumnOrder();
    config_save();
    gtk_main_quit();
}

int main(int argc, char* argv[])
{
    gtk_init(&argc, &argv);

    g_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(g_window), "NPS Log Viewer " APP_VERSION);
    gtk_window_set_default_size(GTK_WINDOW(g_window), 1200, 750);
    g_signal_connect(g_window, "destroy", G_CALLBACK(on_destroy), NULL);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(g_window), vbox);

    GtkWidget* menubar = BuildMenuBar();
    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);

    GtkWidget* filter_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(filter_box), 6);
    gtk_box_pack_start(GTK_BOX(vbox), filter_box, FALSE, FALSE, 0);

    GtkWidget* filter_label = gtk_label_new("Filter:");
    gtk_box_pack_start(GTK_BOX(filter_box), filter_label, FALSE, FALSE, 0);

    g_filter_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(g_filter_entry),
                                   "User, account, MAC, IP, or any value");
    gtk_box_pack_start(GTK_BOX(filter_box), g_filter_entry, TRUE, TRUE, 0);
    g_signal_connect(g_filter_entry, "changed", G_CALLBACK(on_filter_changed), NULL);

    g_filter_rejected = gtk_check_button_new_with_label("Rejected only");
    gtk_box_pack_start(GTK_BOX(filter_box), g_filter_rejected, FALSE, FALSE, 0);
    g_signal_connect(g_filter_rejected, "toggled", G_CALLBACK(on_filter_changed), NULL);

    GtkWidget* filter_clear = gtk_button_new_with_label("Clear");
    gtk_box_pack_start(GTK_BOX(filter_box), filter_clear, FALSE, FALSE, 0);
    g_signal_connect(filter_clear, "clicked", G_CALLBACK(on_filter_clear), NULL);

    g_hpane = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(vbox), g_hpane, TRUE, TRUE, 0);
    gtk_paned_set_position(GTK_PANED(g_hpane), 500);
    g_signal_connect(g_hpane, "notify::position", G_CALLBACK(on_paned_position_notify), NULL);

    GtkWidget* scrolled_list = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_list),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    g_treeview = BuildListView();
    gtk_container_add(GTK_CONTAINER(scrolled_list), g_treeview);
    gtk_paned_pack1(GTK_PANED(g_hpane), scrolled_list, TRUE, FALSE);

    GtkWidget* scrolled_details = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_details),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    g_details = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(g_details), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(g_details), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(g_details), GTK_WRAP_WORD);

    ApplyMonospaceFont(g_details);

    g_signal_connect(g_details, "button-press-event", G_CALLBACK(on_details_button_press), NULL);

    gtk_container_add(GTK_CONTAINER(scrolled_details), g_details);
    gtk_paned_pack2(GTK_PANED(g_hpane), scrolled_details, FALSE, FALSE);

    g_status = gtk_label_new("Ready");
    gtk_widget_set_halign(g_status, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), g_status, FALSE, FALSE, 4);

    /* Try to set window icon from file relative to executable */
    {
        char exe_path[1024];
        char icon_path[2048];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len != -1) {
            exe_path[len] = '\0';
            char* last_slash = strrchr(exe_path, '/');
            if (last_slash) {
                *last_slash = '\0';
                snprintf(icon_path, sizeof(icon_path),
                         "%s/icons/icon_48x48.png", exe_path);
                if (access(icon_path, F_OK) == 0) {
                    gtk_window_set_icon_from_file(GTK_WINDOW(g_window), icon_path, NULL);
                }
            }
        }
    }

    gtk_widget_show_all(g_window);

    InitAppConfig();
    {
        int w = WindowConfigGetInt("width", 1200);
        int h = WindowConfigGetInt("height", 750);
        gtk_window_resize(GTK_WINDOW(g_window), w, h);
    }
    if (g_hpane) {
        int paned = WindowConfigGetInt("paned", 500);
        gtk_paned_set_position(GTK_PANED(g_hpane), SnapPanedPosition(paned));
    }
    g_recent_first = config_get_int("settings", "recent_first", 0);
    if (g_item_recent) {
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(g_item_recent), g_recent_first);
    }

    LoadRecentFiles();
    UpdateRecentMenu();
    UpdateColumnMenu();

    g_signal_connect(g_window, "configure-event", G_CALLBACK(on_configure), NULL);
    g_signal_connect(g_window, "key-press-event", G_CALLBACK(on_key_press), NULL);

    g_watch_id = g_timeout_add(FILE_WATCH_INTERVAL_MS, on_file_watch, NULL);

    GtkTextBuffer* buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_details));
    gtk_text_buffer_set_text(buf,
        "NPS Log Viewer\n"
        "================\n\n"
        "Use File > Open to load an NPS/IAS log file.\n\n"
        "Supported formats:\n"
        "  - NPS XML format (.log files from Windows Server NPS)\n"
        "  - Database-compatible CSV (quoted fields)\n"
        "  - IAS text format (comma or tab delimited)\n\n"
        "Select a record in the list above to see decoded RADIUS attributes.\n"
        "Numeric option codes are automatically translated to human-readable names.\n"
        "View Help > RADIUS Attribute Codes for the full reference table.", -1);

    if (argc > 1) {
        LoadFile(argv[1]);
    }

    gtk_main();

    if (g_watch_id) {
        g_source_remove(g_watch_id);
    }
    nps_logfile_free(&g_logfile);
    if (g_store) {
        g_object_unref(g_store);
    }

    return 0;
}
