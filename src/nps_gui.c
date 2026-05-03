/*
 * NPS Log Viewer
 * (C) 2026, Eugene Nesterenko - eenest@eenest.net
 */

#ifndef _WIN32
#error "This file is for Windows builds only"
#endif

#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "parser.h"
#include "radius_dict.h"
#include "config.h"
#include "compat.h"

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "comdlg32.lib")
#endif

#define IDC_LISTVIEW  1001
#define IDC_DETAILS   1002
#define IDC_STATUS    1003
#define IDC_RADIUS_TEXT 1004
#define IDC_RADIUS_OK   1005
#define IDC_FILTER_LABEL 1006
#define IDC_FILTER_EDIT  1007
#define IDC_FILTER_REJECTED 1008
#define IDC_FILTER_CLEAR 1009
#define IDM_FILE_EXPORT_CSV 4010
#define IDM_FILE_EXPORT_TXT 4011
#define IDM_FILE_EXPORT_HTML 4012
#define IDM_FILE_OPEN 4001
#define IDM_FILE_EXIT 4002
#define IDM_EDIT_COPY 4006
#define IDM_SETTINGS_RECENT_FIRST 4005
#define IDM_HELP_ABOUT 4003
#define IDM_HELP_RADIUS 4004
#define IDM_HELP_VENDOR 4007
#define IDM_HELP_SUPPORT 4008
#define IDM_COLUMNS_BASE 4300
#define IDM_COLUMNS_RESET 4399
#define IDM_RECENT_BASE 4100
#define MAX_RECENT_FILES 8
#define IDD_ABOUT     100
#define IDC_ABOUT_ICON 101
#define IDC_ABOUT_SUPPORT 102
#define IDC_ABOUT_CONFIG 103

#define MAX_LIST_COLS 16
#define IDT_FILE_WATCH 5001
#define FILE_WATCH_INTERVAL_MS 2000
#define APP_VERSION "v0.22"
#define SUPPORT_URL "https://ko-fi.com/eenest"
#ifdef _WIN64
#define WINDOW_CONFIG_SECTION "window.win64"
#else
#define WINDOW_CONFIG_SECTION "window.win32"
#endif

static HWND hWndList = NULL;
static HWND hWndDetails = NULL;
static HWND hWndStatus = NULL;
static HWND hWndSplitter = NULL;
static HWND hWndFilterLabel = NULL;
static HWND hWndFilterEdit = NULL;
static HWND hWndFilterRejected = NULL;
static HWND hWndFilterClear = NULL;
static HMENU g_h_columns_menu = NULL;
static WNDPROC g_old_splitter_proc = NULL;
static HFONT hFontMono = NULL;
static NpsLogFile g_logfile = {0};
static int g_recent_first = 0;
static char g_current_file[MAX_PATH] = {0};
static DWORD g_last_file_size = 0;
static int g_file_changed = 0;
static char g_recent_files[MAX_RECENT_FILES][MAX_PATH] = {{0}};
static int g_recent_count = 0;
static int g_splitter_pos = 0;
static int g_dragging = 0;
static int g_details_red = 0;
static int g_selected_rec_idx = -1;
static char g_filter_text[128] = {0};
static int g_filter_rejected_only = 0;
static int g_visible_records[MAX_RECORDS];
static int g_visible_count = 0;
static char g_load_time[32] = {0};
static DWORD g_load_ms = 0;
static DWORD g_load_file_size = 0;
#define SPLITTER_HIT_ZONE 6
#define SPLITTER_H 4
#define MIN_PANE_H 60
#define STATUS_H 24
#define FILTER_H 32

static void OnListSelect(int index);
static void OpenSupportPage(HWND hwnd);
static const char* RecordFieldValue(const NpsLogRecord* rec, const char* name);

static LRESULT CALLBACK SplitterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void MigrateWindowConfig(void)
{
    static const char* keys[] = {
        "width", "height", "has_position", "x", "y", "splitter", NULL
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

static void DoLayout(int width, int height)
{
    int list_y = 4 + FILTER_H;
    int min_pos = list_y + MIN_PANE_H;
    int max_pos = height - STATUS_H - MIN_PANE_H;
    int edit_w = width - 360;
    if (edit_w < 120) edit_w = 120;
    if (max_pos < min_pos) max_pos = min_pos;
    if (g_splitter_pos < min_pos) g_splitter_pos = min_pos;
    if (g_splitter_pos > max_pos) g_splitter_pos = max_pos;

    int list_h = g_splitter_pos - list_y;
    int details_y = g_splitter_pos + SPLITTER_H;
    int details_h = height - details_y - STATUS_H - 4;
    if (list_h < MIN_PANE_H) list_h = MIN_PANE_H;
    if (details_h < MIN_PANE_H) details_h = MIN_PANE_H;

    HDWP hdwp = BeginDeferWindowPos(8);
    if (hdwp) {
        hdwp = DeferWindowPos(hdwp, hWndFilterLabel, NULL, 8, 8, 70, 22, SWP_NOZORDER);
        hdwp = DeferWindowPos(hdwp, hWndFilterEdit, NULL, 82, 6, edit_w, 24, SWP_NOZORDER);
        hdwp = DeferWindowPos(hdwp, hWndFilterRejected, NULL, width - 270, 7, 150, 24, SWP_NOZORDER);
        hdwp = DeferWindowPos(hdwp, hWndFilterClear, NULL, width - 110, 6, 96, 24, SWP_NOZORDER);
        hdwp = DeferWindowPos(hdwp, hWndList, NULL, 4, list_y, width - 8, list_h, SWP_NOZORDER);
        hdwp = DeferWindowPos(hdwp, hWndSplitter, NULL, 4, g_splitter_pos, width - 8, SPLITTER_H, SWP_NOZORDER);
        hdwp = DeferWindowPos(hdwp, hWndDetails, NULL, 4, details_y, width - 8, details_h, SWP_NOZORDER);
        hdwp = DeferWindowPos(hdwp, hWndStatus, NULL, 4, height - STATUS_H, width - 8, STATUS_H - 4, SWP_NOZORDER);
        EndDeferWindowPos(hdwp);
    }
}

static int ListRowHeight(void)
{
    RECT rc;

    if (hWndList && ListView_GetItemCount(hWndList) > 0 &&
        ListView_GetItemRect(hWndList, 0, &rc, LVIR_BOUNDS)) {
        int row_h = rc.bottom - rc.top;
        if (row_h > 0) return row_h;
    }
    return 20;
}

static int SnapSplitterPos(int pos, int height)
{
    int list_y = 4 + FILTER_H;
    int min_pos = list_y + MIN_PANE_H;
    int max_pos = height - STATUS_H - MIN_PANE_H;
    int row_h = ListRowHeight();
    int rows;

    if (max_pos < min_pos) max_pos = min_pos;
    if (pos < min_pos) pos = min_pos;
    if (pos > max_pos) pos = max_pos;
    if (row_h <= 0) return pos;

    rows = (pos - list_y) / row_h;
    if (rows < 1) rows = 1;
    pos = list_y + rows * row_h;
    if (pos < min_pos) pos = min_pos;
    if (pos > max_pos) pos = max_pos;
    return pos;
}

/* Column definition: name = XML/CSV field name, title = display header, decode = 1 if numeric value should be decoded */
typedef struct {
    const char* name;
    const char* title;
    int width;
    int decode;
} ColDef;

/* Priority columns for the list view — these fields are shown first so success/failure is visible at a glance */
static ColDef col_defs[MAX_LIST_COLS];
static int num_col_defs = 0;
static char generic_col_names[MAX_LIST_COLS][32];

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
    for (int i = 0; i < num_col_defs; i++) {
        char key[96];
        int width;

        if (!hWndList || !col_defs[i].name) continue;
        width = ListView_GetColumnWidth(hWndList, i);
        if (width <= 0) continue;
        ColumnConfigKey(col_defs[i].name, key, sizeof(key));
        config_set_int("columns", key, width);
    }
    config_save();
}

static void SaveColumnOrder(void)
{
    int order[MAX_LIST_COLS];
    char buf[1024] = {0};
    size_t pos = 0;

    if (!hWndList || num_col_defs <= 0) return;
    if (!ListView_GetColumnOrderArray(hWndList, num_col_defs, order)) return;

    for (int i = 0; i < num_col_defs; i++) {
        int idx = order[i];
        if (idx < 0 || idx >= num_col_defs || !col_defs[idx].name) continue;
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s%s",
                        pos ? "," : "", col_defs[idx].name);
        if (pos >= sizeof(buf) - 1) break;
    }
    config_set_string("columns", "order", buf);
    config_save();
}

static int FindColumnDef(const char* name)
{
    for (int i = 0; i < num_col_defs; i++) {
        if (col_defs[i].name && strcasecmp(col_defs[i].name, name) == 0) return i;
    }
    return -1;
}

static void RestoreColumnOrder(void)
{
    const char* saved = config_get_string("columns", "order", "");
    int order[MAX_LIST_COLS];
    int used[MAX_LIST_COLS] = {0};
    int count = 0;
    char work[1024];

    if (!hWndList || num_col_defs <= 1 || !saved || !*saved) return;
    snprintf(work, sizeof(work), "%s", saved);

    char* token = strtok(work, ",");
    while (token && count < num_col_defs) {
        int idx = FindColumnDef(token);
        if (idx >= 0 && !used[idx]) {
            order[count++] = idx;
            used[idx] = 1;
        }
        token = strtok(NULL, ",");
    }
    for (int i = 0; i < num_col_defs && count < num_col_defs; i++) {
        if (!used[i]) order[count++] = i;
    }
    if (count == num_col_defs) {
        ListView_SetColumnOrderArray(hWndList, num_col_defs, order);
    }
}

static void UpdateColumnMenu(void)
{
    if (!g_h_columns_menu) return;
    while (GetMenuItemCount(g_h_columns_menu) > 0) {
        RemoveMenu(g_h_columns_menu, 0, MF_BYPOSITION);
    }

    if (!g_logfile.has_header) {
        AppendMenuA(g_h_columns_menu, MF_STRING | MF_GRAYED, 0, "(Open a log first)");
        return;
    }

    for (int i = 0; i < g_logfile.header_count && i < MAX_FIELDS; i++) {
        UINT flags = MF_STRING;
        if (!IsColumnHidden(g_logfile.header[i])) flags |= MF_CHECKED;
        AppendMenuA(g_h_columns_menu, flags, IDM_COLUMNS_BASE + i, g_logfile.header[i]);
    }
    AppendMenuA(g_h_columns_menu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(g_h_columns_menu, MF_STRING, IDM_COLUMNS_RESET, "&Reset Columns");
}

static void SaveSortSettings(void)
{
    config_set_int("settings", "recent_first", g_recent_first);
    config_set_string("settings", "sort_column", "");
    config_set_int("settings", "sort_ascending", 0);
    config_save();
}

static void BuildRadiusHelpText(char* out, size_t out_size)
{
    size_t pos = 0;
    pos += snprintf(out + pos, out_size - pos,
        "RADIUS Attribute Codes Reference\r\n"
        "=================================\r\n\r\n");
    for (int i = 1; i <= 255 && pos < out_size - 96; i++) {
        const char* attr_name = radius_attr_name(i);
        if (strcmp(attr_name, "Unknown") != 0) {
            pos += snprintf(out + pos, out_size - pos,
                "%3d = %s\r\n", i, attr_name);
        }
    }
}

static void BuildVendorHelpText(char* out, size_t out_size)
{
    static const unsigned int vendors[] = {
        9, 311, 2636, 6889, 12356, 25461, 29671
    };
    size_t pos = 0;

    pos += snprintf(out + pos, out_size - pos,
        "Vendor-Specific Attribute Codes Reference\r\n"
        "=========================================\r\n\r\n");
    for (size_t v = 0; v < sizeof(vendors) / sizeof(vendors[0]) && pos < out_size - 128; v++) {
        unsigned int vendor_id = vendors[v];
        pos += snprintf(out + pos, out_size - pos,
            "%s (%u)\r\n", radius_vendor_name(vendor_id), vendor_id);
        for (unsigned int type = 1; type <= 255 && pos < out_size - 96; type++) {
            const char* name = radius_vendor_attr_name(vendor_id, type);
            if (name) {
                pos += snprintf(out + pos, out_size - pos,
                    "  %3u = %s\r\n", type, name);
            }
        }
        pos += snprintf(out + pos, out_size - pos, "\r\n");
    }
}

static LRESULT CALLBACK RadiusRefWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_CREATE: {
            const char* text = (const char*)((CREATESTRUCT*)lParam)->lpCreateParams;
            HWND hEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", text,
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
                ES_AUTOVSCROLL | WS_VSCROLL,
                0, 0, 0, 0, hwnd, (HMENU)IDC_RADIUS_TEXT,
                GetModuleHandleA(NULL), NULL);
            HWND hOk = CreateWindowExA(0, "BUTTON", "OK",
                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                0, 0, 0, 0, hwnd, (HMENU)IDC_RADIUS_OK,
                GetModuleHandleA(NULL), NULL);
            if (hFontMono) {
                SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontMono, TRUE);
            }
            SendMessage(hOk, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            return 0;
        }
        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            int button_w = 90;
            int button_h = 28;
            int margin = 10;
            HWND hEdit = GetDlgItem(hwnd, IDC_RADIUS_TEXT);
            HWND hOk = GetDlgItem(hwnd, IDC_RADIUS_OK);
            MoveWindow(hEdit, margin, margin, width - margin * 2,
                       height - button_h - margin * 3, TRUE);
            MoveWindow(hOk, width - button_w - margin,
                       height - button_h - margin, button_w, button_h, TRUE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_RADIUS_OK) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static void ShowReferenceWindow(HWND parent, const char* title, int vendor_reference)
{
    static char radius_help[24576];
    WNDCLASSA wc = {0};
    HINSTANCE hInst = GetModuleHandleA(NULL);
    const char* class_name = "NpsRadiusReferenceWindow";
    int screen_w;
    int screen_h;
    int max_height;
    int width;
    int height;
    int x;
    int y;
    RECT rc_parent;

    if (vendor_reference) {
        BuildVendorHelpText(radius_help, sizeof(radius_help));
    } else {
        BuildRadiusHelpText(radius_help, sizeof(radius_help));
    }

    wc.lpfnWndProc = RadiusRefWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = class_name;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    screen_w = GetSystemMetrics(SM_CXSCREEN);
    screen_h = GetSystemMetrics(SM_CYSCREEN);
    width = screen_w < 700 ? screen_w - 40 : 640;
    if (width < 280) width = screen_w > 40 ? screen_w - 20 : screen_w;
    max_height = screen_h / 2;
    height = max_height;
    if (height > 480) height = 480;

    x = (screen_w - width) / 2;
    y = (screen_h - height) / 2;
    if (parent && GetWindowRect(parent, &rc_parent)) {
        x = rc_parent.left + ((rc_parent.right - rc_parent.left) - width) / 2;
        y = rc_parent.top + ((rc_parent.bottom - rc_parent.top) - height) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x + width > screen_w) x = screen_w - width;
        if (y + height > screen_h) y = screen_h - height;
    }

    HWND hRef = CreateWindowExA(WS_EX_DLGMODALFRAME, class_name,
        title, WS_OVERLAPPED | WS_CAPTION |
        WS_SYSMENU | WS_SIZEBOX,
        x, y, width, height, parent, NULL, hInst, radius_help);
    if (hRef) {
        ShowWindow(hRef, SW_SHOW);
        UpdateWindow(hRef);
    }
}

static void ShowRadiusReferenceWindow(HWND parent)
{
    ShowReferenceWindow(parent, "RADIUS Attribute Codes", 0);
}

static void ShowVendorReferenceWindow(HWND parent)
{
    ShowReferenceWindow(parent, "Vendor-Specific Attribute Codes", 1);
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

static const char* DecodeForDisplay(const char* field_name, const char* value)
{
    static char buf[512];

    if (!field_name || !value || !*value) return value;
    if (decode_radius_field(field_name, value, buf, sizeof(buf))) {
        return buf;
    }
    return value;
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
    /* Default priority columns that make success/failure visible at a glance */
    static const ColDef priority[] = {
        {"Timestamp",           "Time",               160, 0},
        {"User-Name",           "User",               140, 0},
        {"Packet-Type",         "Packet",             110, 1},
        {"Reason-Code",         "Result",             140, 1},
        {"NAS-IP-Address",      "NAS IP",             120, 0},
        {"Client-Friendly-Name","Client",             120, 0},
        {"Calling-Station-Id",  "Calling Station",    140, 0},
        {"Authentication-Type", "Auth Type",          130, 1},
        {"NP-Policy-Name",      "Policy",             140, 0},
        {"NAS-Port-Type",       "Port Type",          130, 1},
        {"Service-Type",        "Service",            100, 1},
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

    /* Add priority columns that actually exist in the data */
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

    /* Fill remaining slots with other header fields not already included */
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

    pos += snprintf(out + pos, out_size - pos, "=== Record Details ===\r\n\r\n");

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
            pos += snprintf(out + pos, out_size - pos, "%s: %s\r\n", display_name, decode_buf);
        } else {
            pos += snprintf(out + pos, out_size - pos, "%s: %s\r\n", display_name, rec->fields[i]);
        }
    }
}

static void SetListColumns(void)
{
    LVCOLUMN lvc = {0};
    lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    ListView_DeleteAllItems(hWndList);
    while (ListView_DeleteColumn(hWndList, 0)) {}

    BuildColumnDefs();

    for (int i = 0; i < num_col_defs; i++) {
        lvc.iSubItem = i;
        lvc.pszText = (char*)col_defs[i].title;
        lvc.cx = SavedColumnWidth(col_defs[i].name, col_defs[i].width);
        ListView_InsertColumn(hWndList, i, &lvc);
    }
    RestoreColumnOrder();

    /* If no columns were created, fall back to generic */
    if (num_col_defs == 0) {
        for (int i = 0; i < 8; i++) {
            char name[32];
            snprintf(name, sizeof(name), "Field %d", i + 1);
            lvc.iSubItem = i;
            lvc.pszText = name;
            lvc.cx = 120;
            ListView_InsertColumn(hWndList, i, &lvc);
        }
    }
}

/* Extract short EAP method name from EAP-Friendly-Name (e.g. 'EAP-MSCHAP v2' from 'Microsoft: Secured password (EAP-MSCHAP v2)') */
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

/* Map visible list row index to actual record index based on sort order */
static int RowToRecord(int row)
{
    if (row < 0 || row >= g_visible_count) return -1;
    return g_visible_records[row];
}

static void UpdateStatusBar(void);
static void PopulateList(void);
static void BuildVisibleRecords(void);

static void PumpPaintMessages(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, WM_PAINT, WM_PAINT, PM_REMOVE)) {
        DispatchMessageA(&msg);
    }
}

static void ApplyFilter(void)
{
    if (hWndFilterEdit) {
        GetWindowTextA(hWndFilterEdit, g_filter_text, sizeof(g_filter_text));
        g_filter_text[sizeof(g_filter_text) - 1] = '\0';
    }
    if (hWndFilterRejected) {
        g_filter_rejected_only =
            (SendMessage(hWndFilterRejected, BM_GETCHECK, 0, 0) == BST_CHECKED);
    }

    PopulateList();
    g_selected_rec_idx = -1;
    g_details_red = 0;
    if (hWndDetails) {
        SetWindowText(hWndDetails, "Select a record to view decoded details.");
    }
}

static void PopulateList(void)
{
    LVITEM lvi = {0};
    char buf[512];

    BuildVisibleRecords();
    ListView_DeleteAllItems(hWndList);

    for (int row = 0; row < g_visible_count; row++) {
        int rec_idx = RowToRecord(row);
        NpsLogRecord* rec = &g_logfile.records[rec_idx];

        lvi.mask = LVIF_TEXT;
        lvi.iItem = row;
        lvi.iSubItem = 0;

        RecordDisplayValue(rec, col_defs[0].name, col_defs[0].decode, buf, sizeof(buf));
        lvi.pszText = buf;
        ListView_InsertItem(hWndList, &lvi);

        /* Remaining columns */
        for (int col = 1; col < num_col_defs; col++) {
            lvi.iSubItem = col;
            RecordDisplayValue(rec, col_defs[col].name, col_defs[col].decode, buf, sizeof(buf));
            lvi.pszText = buf;
            ListView_SetItem(hWndList, &lvi);
        }
    }

    UpdateStatusBar();
}

static void UpdateStatusBar(void)
{
    char buf[256];
    const char* fname = g_current_file[0] ? g_current_file : "No file";
    const char* cap = g_logfile.truncated ? " | WARNING: record limit reached" : "";
    const char* slash = strrchr(fname, '\\');
    if (!slash) slash = strrchr(fname, '/');
    if (slash) fname = slash + 1;

    if (g_file_changed && g_current_file[0]) {
        snprintf(buf, sizeof(buf),
                 "%s | visible %d of %d | loaded %s in %lums | size %lu KB | changed: yes%s",
                 fname, g_visible_count, g_logfile.count,
                 g_load_time[0] ? g_load_time : "-", (unsigned long)g_load_ms,
                 (unsigned long)((g_load_file_size + 1023) / 1024), cap);
    } else if (g_current_file[0]) {
        snprintf(buf, sizeof(buf),
                 "%s | visible %d of %d | loaded %s in %lums | size %lu KB | changed: no%s",
                 fname, g_visible_count, g_logfile.count,
                 g_load_time[0] ? g_load_time : "-", (unsigned long)g_load_ms,
                 (unsigned long)((g_load_file_size + 1023) / 1024), cap);
    } else {
        snprintf(buf, sizeof(buf), "Ready");
    }
    SetWindowText(hWndStatus, buf);
}

static void SetStatusTextNow(const char* text)
{
    if (!hWndStatus) return;
    SetWindowTextA(hWndStatus, text ? text : "");
    UpdateWindow(hWndStatus);
    PumpPaintMessages();
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
    DWORD last_paint_tick;
} LoadProgressCtx;

static void OnLoadProgress(long bytes_read, long total_bytes, void* userdata)
{
    LoadProgressCtx* ctx = (LoadProgressCtx*)userdata;
    int step;

    if (!ctx || total_bytes <= 0) return;
    step = (int)(((long long)bytes_read * 5LL + total_bytes - 1) / total_bytes);
    if (step < 1) step = 1;
    if (step > 5) step = 5;
    if (step != ctx->last_step) {
        SetLoadProgressStatus("Reading file", step, 5);
        ctx->last_step = step;
        ctx->last_paint_tick = GetTickCount();
    } else {
        DWORD now = GetTickCount();
        if (now - ctx->last_paint_tick > 250) {
            PumpPaintMessages();
            ctx->last_paint_tick = now;
        }
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
            strncpy(g_recent_files[i], val, MAX_PATH - 1);
            g_recent_files[i][MAX_PATH - 1] = '\0';
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
        if (g_recent_files[i][0]) {
            config_set_string("recent", key, g_recent_files[i]);
        } else {
            config_set_string("recent", key, "");
        }
    }
    config_save();
}

static void AddRecentFile(const char* path)
{
    /* Remove if already in list */
    for (int i = 0; i < g_recent_count; i++) {
        if (strcasecmp(g_recent_files[i], path) == 0) {
            /* Shift items down to remove duplicate */
            for (int j = i; j < g_recent_count - 1; j++) {
                strncpy(g_recent_files[j], g_recent_files[j + 1], MAX_PATH);
            }
            g_recent_count--;
            break;
        }
    }
    /* Shift everything down to make room at top */
    for (int i = g_recent_count - 1; i >= 0; i--) {
        if (i + 1 < MAX_RECENT_FILES) {
            strncpy(g_recent_files[i + 1], g_recent_files[i], MAX_PATH);
        }
    }
    /* Insert at top */
    strncpy(g_recent_files[0], path, MAX_PATH - 1);
    g_recent_files[0][MAX_PATH - 1] = '\0';
    if (g_recent_count < MAX_RECENT_FILES) {
        g_recent_count++;
    }
    SaveRecentFiles();
}

static void UpdateRecentMenu(HMENU hFileMenu)
{
    /* Remove old recent items (ids IDM_RECENT_BASE and up) */
    int item_count = GetMenuItemCount(hFileMenu);
    for (int i = item_count - 1; i >= 0; i--) {
        UINT id = GetMenuItemID(hFileMenu, i);
        if (id >= IDM_RECENT_BASE && id < IDM_RECENT_BASE + MAX_RECENT_FILES) {
            RemoveMenu(hFileMenu, i, MF_BYPOSITION);
        }
    }
    /* Insert after position 1 (Open is 0, separator after it is 1) */
    int insert_pos = 2;
    if (g_recent_count > 0) {
        /* Remove any existing "Recent Files" popup submenu */
        for (int i = GetMenuItemCount(hFileMenu) - 1; i >= 0; i--) {
            if (GetSubMenu(hFileMenu, i) && GetMenuItemID(hFileMenu, i) == (UINT)-1) {
                /* Could be our submenu - check if it has recent items */
                HMENU sub = GetSubMenu(hFileMenu, i);
                if (sub && GetMenuItemID(sub, 0) >= IDM_RECENT_BASE) {
                    RemoveMenu(hFileMenu, i, MF_BYPOSITION);
                    break;
                }
            }
        }
        HMENU hRecent = CreatePopupMenu();
        for (int i = 0; i < g_recent_count; i++) {
            /* Extract just the filename for display, keep full path as data */
            const char* display = g_recent_files[i];
            const char* slash = strrchr(display, '\\');
            if (!slash) slash = strrchr(display, '/');
            if (slash) display = slash + 1;
            char label[MAX_PATH + 8];
            snprintf(label, sizeof(label), "&%d %s", i + 1, display);
            AppendMenuA(hRecent, MF_STRING, IDM_RECENT_BASE + i, label);
        }
        InsertMenuA(hFileMenu, insert_pos, MF_BYPOSITION | MF_POPUP,
                    (UINT_PTR)hRecent, "Recent &Files");
    }
}

static void CopyToClipboard(HWND hwnd)
{
    if (!hWndDetails) return;

    int details_len = GetWindowTextLengthA(hWndDetails);
    if (details_len <= 0) return;

    char* details = (char*)malloc(details_len + 1);
    if (!details) return;
    GetWindowTextA(hWndDetails, details, details_len + 1);

    const char* raw = NULL;
    if (g_selected_rec_idx >= 0 && g_selected_rec_idx < g_logfile.count) {
        raw = g_logfile.records[g_selected_rec_idx].raw_line;
    }

    int total = details_len + 1;
    if (raw) {
        total += (int)strlen("=== Original Record ===\r\n\r\n")
               + (int)strlen(raw)
               + (int)strlen("\r\n\r\n=== Decoded Record ===\r\n\r\n")
               + 1;
    }

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, total);
    if (!hMem) {
        free(details);
        return;
    }

    char* ptr = (char*)GlobalLock(hMem);
    if (!ptr) {
        GlobalFree(hMem);
        free(details);
        return;
    }

    if (raw) {
        snprintf(ptr, total,
            "=== Original Record ===\r\n\r\n%s\r\n\r\n=== Decoded Record ===\r\n\r\n%s",
            raw, details);
    } else {
        memcpy(ptr, details, details_len + 1);
    }
    GlobalUnlock(hMem);
    free(details);

    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        SetClipboardData(CF_TEXT, hMem);
        CloseClipboard();
    } else {
        GlobalFree(hMem);
    }
}

static void ShowCopyContextMenu(HWND hwnd, POINT pt)
{
    HMENU hPopup = CreatePopupMenu();
    if (!hPopup) return;

    AppendMenuA(hPopup, MF_STRING, IDM_EDIT_COPY, "&Copy Record");
    TrackPopupMenu(hPopup, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
                   pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hPopup);
}

static int SelectListRowAtPoint(POINT screen_pt)
{
    LVHITTESTINFO hit = {0};
    POINT list_pt = screen_pt;

    if (!hWndList) return 0;
    ScreenToClient(hWndList, &list_pt);
    hit.pt = list_pt;

    int item = ListView_HitTest(hWndList, &hit);
    if (item < 0 || item >= g_visible_count) return 0;

    ListView_SetItemState(hWndList, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(hWndList, item, LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(hWndList, item, FALSE);
    OnListSelect(item);
    return 1;
}

static void BeginSplitterDrag(void)
{
    POINT pt;
    RECT rc;
    HWND parent = GetParent(hWndSplitter);

    if (!parent) return;
    GetCursorPos(&pt);
    ScreenToClient(parent, &pt);
    GetClientRect(parent, &rc);

    g_splitter_pos = SnapSplitterPos(pt.y, rc.bottom - rc.top);
    g_dragging = 1;
    SetCapture(parent);
}

static void LoadFile(const char* path)
{
    HANDLE hFile;
    DWORD size;
    int fd;
    FILE* fp;
    LoadProgressCtx progress = {0};
    DWORD start_tick = GetTickCount();

    hFile = CreateFileA(path, GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_SHARING_VIOLATION) {
            MessageBoxA(NULL,
                "The file is locked by another process (likely the NPS service).\r\n\r\n"
                "To view this log:\r\n"
                "  1. Stop the NPS service temporarily, or\r\n"
                "  2. Copy the log file to another location and open the copy.",
                "File Locked", MB_OK | MB_ICONWARNING);
        } else {
            MessageBoxA(NULL, "Failed to open file.", "Error", MB_OK | MB_ICONERROR);
        }
        return;
    }

    size = GetFileSize(hFile, NULL);
    if (size == INVALID_FILE_SIZE || size == 0) {
        CloseHandle(hFile);
        MessageBoxA(NULL, "File is empty or too large.", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    fd = _open_osfhandle((intptr_t)hFile, _O_RDONLY | _O_BINARY);
    if (fd < 0) {
        CloseHandle(hFile);
        MessageBoxA(NULL, "Failed to prepare file stream.", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    fp = _fdopen(fd, "rb");
    if (!fp) {
        _close(fd);
        MessageBoxA(NULL, "Failed to open file stream.", "Error", MB_OK | MB_ICONERROR);
        return;
    }

    SetLoadProgressStatus("Reading file", 1, 5);

    /* Store file info for monitoring */
    strncpy(g_current_file, path, sizeof(g_current_file) - 1);
    g_current_file[sizeof(g_current_file) - 1] = '\0';
    g_last_file_size = (DWORD)size;
    g_load_file_size = (DWORD)size;
    g_file_changed = 0;

    nps_logfile_free(&g_logfile);
    nps_parse_stream(fp, (long)size, &g_logfile, OnLoadProgress, &progress);
    fclose(fp);
    SetStatusTextNow("Building view...");
    g_load_ms = GetTickCount() - start_tick;
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

    /* Start/restart file watch timer */
    if (hWndStatus) {
        HWND hwnd = GetParent(hWndStatus);
        if (hwnd) {
            SetTimer(hwnd, IDT_FILE_WATCH, FILE_WATCH_INTERVAL_MS, NULL);
        }
    }

    SetWindowText(hWndDetails, "Select a record to view decoded details.");

    AddRecentFile(path);
    {
        HMENU hMenu = GetMenu(GetParent(hWndStatus));
        if (hMenu) {
            HMENU hFile = GetSubMenu(hMenu, 0);
            if (hFile) {
                UpdateRecentMenu(hFile);
                DrawMenuBar(GetParent(hWndStatus));
            }
        }
    }
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

static void OnListSelect(int index)
{
    static char details[32768];
    int rec_idx = RowToRecord(index);
    if (rec_idx < 0 || rec_idx >= g_logfile.count) return;
    g_selected_rec_idx = rec_idx;
    FormatDetails(&g_logfile.records[rec_idx], details, sizeof(details));
    g_details_red = IsRejectedExchangeRecord(rec_idx);
    SetWindowText(hWndDetails, details);
    InvalidateRect(hWndDetails, NULL, TRUE);
}

static void OpenFileDialog(HWND hwnd)
{
    OPENFILENAMEA ofn = {0};
    char szFile[MAX_PATH] = {0};

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "NPS Log Files (*.log;*.txt;*.csv)\0*.log;*.txt;*.csv\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        LoadFile(szFile);
    }
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

static void ExportVisibleRecords(HWND hwnd, int format)
{
    OPENFILENAMEA ofn = {0};
    char path[MAX_PATH] = {0};
    FILE* fp;

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = path;
    ofn.nMaxFile = sizeof(path);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (format == IDM_FILE_EXPORT_CSV) {
        ofn.lpstrFilter = "CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
        strncpy(path, "nps-logview-export.csv", sizeof(path) - 1);
    } else if (format == IDM_FILE_EXPORT_HTML) {
        ofn.lpstrFilter = "HTML Files (*.html)\0*.html\0All Files (*.*)\0*.*\0";
        strncpy(path, "nps-logview-export.html", sizeof(path) - 1);
    } else {
        ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
        strncpy(path, "nps-logview-export.txt", sizeof(path) - 1);
    }

    if (!GetSaveFileNameA(&ofn)) return;
    fp = fopen(path, "w");
    if (!fp) {
        MessageBoxA(hwnd, "Failed to create export file.", "Export", MB_OK | MB_ICONERROR);
        return;
    }

    if (format == IDM_FILE_EXPORT_HTML) {
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
    } else if (format == IDM_FILE_EXPORT_CSV) {
        for (int c = 0; c < num_col_defs; c++) {
            if (c) fputc(',', fp);
            WriteCsvCell(fp, col_defs[c].title);
        }
        fputc('\n', fp);
    }

    for (int row = 0; row < g_visible_count; row++) {
        int rec_idx = RowToRecord(row);
        char value[512];
        if (format == IDM_FILE_EXPORT_HTML) {
            fputs(IsRejectedExchangeRecord(rec_idx) ? "<tr class=\"rejected\">" : "<tr>", fp);
        } else if (format == IDM_FILE_EXPORT_TXT) {
            fprintf(fp, "=== Record %d ===\n", row + 1);
        }
        for (int c = 0; c < num_col_defs; c++) {
            RecordDisplayValue(&g_logfile.records[rec_idx], col_defs[c].name,
                               col_defs[c].decode, value, sizeof(value));
            if (format == IDM_FILE_EXPORT_HTML) {
                fputs("<td>", fp);
                WriteHtmlEscaped(fp, value);
                fputs("</td>", fp);
            } else if (format == IDM_FILE_EXPORT_CSV) {
                if (c) fputc(',', fp);
                WriteCsvCell(fp, value);
            } else {
                fprintf(fp, "%s: %s\n", col_defs[c].title, value);
            }
        }
        if (format == IDM_FILE_EXPORT_HTML) fputs("</tr>\n", fp);
        else fputc('\n', fp);
    }
    if (format == IDM_FILE_EXPORT_HTML) fputs("</tbody></table></body></html>\n", fp);
    fclose(fp);
    SetWindowTextA(hWndStatus, "Export complete.");
}

static INT_PTR CALLBACK AboutDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_INITDIALOG: {
            HINSTANCE hInst = GetModuleHandleA(NULL);
            HICON hIcon = LoadImageA(hInst, MAKEINTRESOURCEA(1), IMAGE_ICON, 128, 128, LR_DEFAULTCOLOR);
            if (hIcon) {
                SendDlgItemMessageA(hDlg, IDC_ABOUT_ICON, STM_SETICON, (WPARAM)hIcon, 0);
            }
            SetDlgItemTextA(hDlg, IDC_ABOUT_CONFIG, config_get_path());
            return TRUE;
        }
        case WM_NOTIFY: {
            LPNMHDR hdr = (LPNMHDR)lParam;
            if (hdr && hdr->idFrom == IDC_ABOUT_SUPPORT &&
                (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
                OpenSupportPage(hDlg);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                EndDialog(hDlg, LOWORD(wParam));
                return TRUE;
            }
            break;
    }
    return FALSE;
}

static void OpenSupportPage(HWND hwnd)
{
    HINSTANCE result = ShellExecuteA(hwnd, "open", SUPPORT_URL, NULL, NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        MessageBoxA(hwnd,
            "Could not open the support page.\r\n\r\n"
            "Support URL:\r\n" SUPPORT_URL,
            "NPS Log Viewer", MB_OK | MB_ICONINFORMATION);
    }
}

static LRESULT CALLBACK SplitterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_SETCURSOR:
            SetCursor(LoadCursor(NULL, IDC_SIZENS));
            return TRUE;
        case WM_LBUTTONDOWN:
            BeginSplitterDrag();
            return 0;
    }

    return CallWindowProcA(g_old_splitter_proc, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_CREATE: {
            INITCOMMONCONTROLSEX icex = {0};
            icex.dwSize = sizeof(icex);
            icex.dwICC = ICC_LISTVIEW_CLASSES;
            InitCommonControlsEx(&icex);

            RECT rc;
            GetClientRect(hwnd, &rc);
            int width = rc.right - rc.left;
            int height = rc.bottom - rc.top;
            InitAppConfig();
            g_splitter_pos = WindowConfigGetInt("splitter", height * 2 / 3);

            hWndStatus = CreateWindowA("STATIC", "Ready",
                WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | WS_CLIPSIBLINGS,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_STATUS, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

            hWndFilterLabel = CreateWindowA("STATIC", "Filter:",
                WS_CHILD | WS_VISIBLE | SS_LEFT | WS_CLIPSIBLINGS,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_FILTER_LABEL, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

            hWndFilterEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP | WS_CLIPSIBLINGS,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_FILTER_EDIT, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
            SendMessageW(hWndFilterEdit, EM_SETCUEBANNER, FALSE,
                         (LPARAM)L"User, account, MAC, IP, or any value");

            hWndFilterRejected = CreateWindowA("BUTTON", "Rejected only",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP | WS_CLIPSIBLINGS,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_FILTER_REJECTED, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

            hWndFilterClear = CreateWindowA("BUTTON", "Clear",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP | WS_CLIPSIBLINGS,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_FILTER_CLEAR, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

            SendMessage(hWndFilterLabel, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            SendMessage(hWndFilterEdit, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            SendMessage(hWndFilterRejected, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
            SendMessage(hWndFilterClear, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);

            hFontMono = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

            hWndList = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEW, "",
                WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS |
                LVS_SINGLESEL | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | WS_CLIPSIBLINGS,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_LISTVIEW, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
            ListView_SetExtendedListViewStyle(hWndList,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER |
                LVS_EX_HEADERDRAGDROP);

            hWndSplitter = CreateWindowExA(0, "STATIC", "",
                WS_CHILD | WS_VISIBLE | SS_NOTIFY | WS_CLIPSIBLINGS,
                0, 0, 0, 0,
                hwnd, NULL, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
            g_old_splitter_proc = (WNDPROC)SetWindowLongPtrA(
                hWndSplitter, GWLP_WNDPROC, (LONG_PTR)SplitterProc);

            hWndDetails = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
                WS_VSCROLL | WS_HSCROLL | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_CLIPSIBLINGS,
                0, 0, 0, 0,
                hwnd, (HMENU)IDC_DETAILS, ((LPCREATESTRUCT)lParam)->hInstance, NULL);
            SendMessage(hWndDetails, WM_SETFONT, (WPARAM)hFontMono, TRUE);
            SendMessage(hWndDetails, EM_SETREADONLY, TRUE, 0);

            DoLayout(width, height);

            HMENU hMenu = CreateMenu();
            HMENU hFile = CreatePopupMenu();
            AppendMenuA(hFile, MF_STRING, IDM_FILE_OPEN, "&Open\tCtrl+O");
            AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hFile, MF_STRING, IDM_FILE_EXPORT_CSV, "Export &CSV");
            AppendMenuA(hFile, MF_STRING, IDM_FILE_EXPORT_TXT, "Export &Text");
            AppendMenuA(hFile, MF_STRING, IDM_FILE_EXPORT_HTML, "Export &HTML");
            AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hFile, MF_STRING, IDM_FILE_EXIT, "E&xit\tAlt+F4");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hFile, "&File");

            LoadRecentFiles();
            UpdateRecentMenu(hFile);

            HMENU hEdit = CreatePopupMenu();
            AppendMenuA(hEdit, MF_STRING, IDM_EDIT_COPY, "&Copy Record\tCtrl+C");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hEdit, "&Edit");

            HMENU hSettings = CreatePopupMenu();
            AppendMenuA(hSettings, MF_STRING, IDM_SETTINGS_RECENT_FIRST, "Most Recent &First");
            g_h_columns_menu = CreatePopupMenu();
            UpdateColumnMenu();
            AppendMenuA(hSettings, MF_POPUP, (UINT_PTR)g_h_columns_menu, "&Columns");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hSettings, "&Settings");

            HMENU hHelp = CreatePopupMenu();
            AppendMenuA(hHelp, MF_STRING, IDM_HELP_RADIUS, "&RADIUS Attribute Codes");
            AppendMenuA(hHelp, MF_STRING, IDM_HELP_VENDOR, "&Vendor-Specific Attribute Codes");
            AppendMenuA(hHelp, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hHelp, MF_STRING, IDM_HELP_SUPPORT, "&Support Project");
            AppendMenuA(hHelp, MF_STRING, IDM_HELP_ABOUT, "&About");
            AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hHelp, "&Help");

            SetMenu(hwnd, hMenu);

            g_recent_first = config_get_int("settings", "recent_first", 0);
            CheckMenuItem(hSettings, IDM_SETTINGS_RECENT_FIRST,
                g_recent_first ? MF_CHECKED : MF_UNCHECKED);

            SetWindowTextA(hWndDetails,
                "NPS Log Viewer\r\n"
                "================\r\n\r\n"
                "Use File > Open to load an NPS/IAS log file.\r\n\r\n"
                "Supported formats:\r\n"
                "  - NPS XML format (.log files from Windows Server NPS)\r\n"
                "  - Database-compatible CSV (quoted fields)\r\n"
                "  - IAS text format (comma or tab delimited)\r\n\r\n"
                "Select a record in the list above to see decoded RADIUS attributes.\r\n"
                "Numeric option codes are automatically translated to human-readable names.\r\n"
                "View Help > RADIUS Attribute Codes for the full reference table.");
            return 0;
        }

        case WM_SIZE: {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            if (g_splitter_pos == 0) g_splitter_pos = height * 2 / 3;
            DoLayout(width, height);
            return 0;
        }

        case WM_SETCURSOR: {
            if ((HWND)wParam == hwnd || (HWND)wParam == hWndSplitter) {
                POINT pt;
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                int splitter_top = g_splitter_pos;
                int splitter_bottom = g_splitter_pos + SPLITTER_H;
                if (pt.y >= splitter_top - SPLITTER_HIT_ZONE && pt.y <= splitter_bottom + SPLITTER_HIT_ZONE) {
                    SetCursor(LoadCursor(NULL, IDC_SIZENS));
                    return TRUE;
                }
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            int y = HIWORD(lParam);
            int splitter_top = g_splitter_pos;
            int splitter_bottom = g_splitter_pos + SPLITTER_H;
            if (y >= splitter_top - SPLITTER_HIT_ZONE && y <= splitter_bottom + SPLITTER_HIT_ZONE) {
                BeginSplitterDrag();
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            int y = HIWORD(lParam);
            if (g_dragging) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                int width = rc.right - rc.left;
                int height = rc.bottom - rc.top;
                g_splitter_pos = SnapSplitterPos(y, height);
                DoLayout(width, height);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (g_dragging) {
                g_dragging = 0;
                ReleaseCapture();
                WindowConfigSetInt("splitter", g_splitter_pos);
                config_save();
            }
            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            if ((HWND)lParam == hWndSplitter) {
                SetBkColor((HDC)wParam, GetSysColor(COLOR_BTNFACE));
                return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
            }
            if ((HWND)lParam == hWndDetails) {
                if (g_details_red) {
                    SetTextColor((HDC)wParam, RGB(220, 0, 0));
                } else {
                    SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
                }
                SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
                return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
            }
            break;
        }

        case WM_CAPTURECHANGED: {
            g_dragging = 0;
            return 0;
        }

        case WM_NOTIFY: {
            LPNMHDR pnmh = (LPNMHDR)lParam;
            if (pnmh->code == HDN_ENDTRACKA || pnmh->code == HDN_ENDTRACKW) {
                SaveColumnWidths();
                return 0;
            }
            if (pnmh->code == HDN_ENDDRAG) {
                SaveColumnOrder();
                return 0;
            }
            if (pnmh->idFrom == IDC_LISTVIEW) {
                if (pnmh->code == LVN_ITEMCHANGED) {
                    LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
                    if (pnmv->uNewState & LVIS_SELECTED) {
                        OnListSelect(pnmv->iItem);
                    }
                } else if (pnmh->code == LVN_COLUMNCLICK) {
                    LPNMLISTVIEW pnmv = (LPNMLISTVIEW)lParam;
                    if (pnmv->iSubItem >= 0 && pnmv->iSubItem < num_col_defs) {
                        const char* name = col_defs[pnmv->iSubItem].name;
                        if (strcasecmp(name, "Timestamp") == 0) {
                            g_recent_first = !g_recent_first;
                            {
                                HMENU hMenu = GetMenu(hwnd);
                                HMENU hSettings = GetSubMenu(hMenu, 2);
                                CheckMenuItem(hSettings, IDM_SETTINGS_RECENT_FIRST,
                                    g_recent_first ? MF_CHECKED : MF_UNCHECKED);
                            }
                            SaveSortSettings();
                            PopulateList();
                        }
                    }
                    return 0;
                } else if (pnmh->code == NM_CUSTOMDRAW) {
                    LPNMLVCUSTOMDRAW lplvcd = (LPNMLVCUSTOMDRAW)lParam;
                    switch (lplvcd->nmcd.dwDrawStage) {
                        case CDDS_PREPAINT:
                            return CDRF_NOTIFYITEMDRAW;
                        case CDDS_ITEMPREPAINT:
                            {
                                int idx = (int)lplvcd->nmcd.dwItemSpec;
                                if (idx >= 0 && idx < g_visible_count) {
                                    int rec_idx = RowToRecord(idx);
                                    if (IsRejectedExchangeRecord(rec_idx)) {
                                        lplvcd->clrText = RGB(200, 0, 0);
                                        lplvcd->clrTextBk = RGB(255, 235, 235);
                                    }
                                }
                            }
                            return CDRF_NEWFONT;
                    }
                    return CDRF_DODEFAULT;
                }
            }
            return 0;
        }

        case WM_COMMAND: {
            if (LOWORD(wParam) == IDC_FILTER_EDIT && HIWORD(wParam) == EN_CHANGE) {
                ApplyFilter();
                return 0;
            }
            if (LOWORD(wParam) == IDC_FILTER_REJECTED && HIWORD(wParam) == BN_CLICKED) {
                ApplyFilter();
                return 0;
            }
            if (LOWORD(wParam) == IDC_FILTER_CLEAR && HIWORD(wParam) == BN_CLICKED) {
                SetWindowTextA(hWndFilterEdit, "");
                SendMessage(hWndFilterRejected, BM_SETCHECK, BST_UNCHECKED, 0);
                ApplyFilter();
                return 0;
            }
            switch (LOWORD(wParam)) {
                case IDM_FILE_OPEN:
                    OpenFileDialog(hwnd);
                    return 0;
                case IDM_FILE_EXPORT_CSV:
                case IDM_FILE_EXPORT_TXT:
                case IDM_FILE_EXPORT_HTML:
                    ExportVisibleRecords(hwnd, LOWORD(wParam));
                    return 0;
                case IDM_FILE_EXIT:
                    DestroyWindow(hwnd);
                    return 0;
                case IDM_HELP_RADIUS:
                    ShowRadiusReferenceWindow(hwnd);
                    return 0;
                case IDM_HELP_VENDOR:
                    ShowVendorReferenceWindow(hwnd);
                    return 0;
                case IDM_SETTINGS_RECENT_FIRST:
                    g_recent_first = !g_recent_first;
                    {
                        HMENU hMenu = GetMenu(hwnd);
                        HMENU hSettings = GetSubMenu(hMenu, 2);
                        CheckMenuItem(hSettings, IDM_SETTINGS_RECENT_FIRST,
                            g_recent_first ? MF_CHECKED : MF_UNCHECKED);
                    }
                    SaveSortSettings();
                    PopulateList();
                    return 0;
                case IDM_COLUMNS_RESET:
                    config_clear_section("columns.hidden");
                    config_save();
                    UpdateColumnMenu();
                    SetListColumns();
                    PopulateList();
                    return 0;
                case IDM_EDIT_COPY:
                    CopyToClipboard(hwnd);
                    return 0;
                case IDM_HELP_ABOUT:
                    DialogBoxParamA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(IDD_ABOUT), hwnd, AboutDlgProc, 0);
                    return 0;
                case IDM_HELP_SUPPORT:
                    OpenSupportPage(hwnd);
                    return 0;
                default:
                    if (LOWORD(wParam) >= IDM_RECENT_BASE &&
                        LOWORD(wParam) < IDM_RECENT_BASE + MAX_RECENT_FILES) {
                        int idx = LOWORD(wParam) - IDM_RECENT_BASE;
                        if (idx < g_recent_count && g_recent_files[idx][0]) {
                            /* Check file still exists */
                            DWORD attribs = GetFileAttributesA(g_recent_files[idx]);
                            if (attribs == INVALID_FILE_ATTRIBUTES) {
                                MessageBoxA(hwnd,
                                    "The file no longer exists at this location.",
                                    "File Not Found", MB_OK | MB_ICONWARNING);
                            } else {
                                LoadFile(g_recent_files[idx]);
                            }
                        }
                        return 0;
                    }
                    if (LOWORD(wParam) >= IDM_COLUMNS_BASE &&
                        LOWORD(wParam) < IDM_COLUMNS_BASE + MAX_FIELDS) {
                        int idx = LOWORD(wParam) - IDM_COLUMNS_BASE;
                        if (idx >= 0 && idx < g_logfile.header_count) {
                            int hidden = IsColumnHidden(g_logfile.header[idx]);
                            SetColumnHidden(g_logfile.header[idx], !hidden);
                            config_save();
                            UpdateColumnMenu();
                            SetListColumns();
                            PopulateList();
                        }
                        return 0;
                    }
                    break;
            }
            return 0;
        }

        case WM_CONTEXTMENU: {
            if ((HWND)wParam == hWndDetails || (HWND)wParam == hWndList) {
                POINT pt;
                pt.x = (int)(short)LOWORD(lParam);
                pt.y = (int)(short)HIWORD(lParam);
                if (pt.x == -1 && pt.y == -1) {
                    /* Keyboard-invoked context menu */
                    RECT rc;
                    GetWindowRect((HWND)wParam, &rc);
                    pt.x = rc.left;
                    pt.y = rc.top;
                } else if ((HWND)wParam == hWndList && !SelectListRowAtPoint(pt)) {
                    return 0;
                }
                ShowCopyContextMenu(hwnd, pt);
                return 0;
            }
            break;
        }

        case WM_KEYDOWN: {
            if (wParam == 'O' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                OpenFileDialog(hwnd);
                return 0;
            }
            if (wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                CopyToClipboard(hwnd);
                return 0;
            }
            return 0;
        }

        case WM_TIMER: {
            if (wParam == IDT_FILE_WATCH && g_current_file[0]) {
                WIN32_FILE_ATTRIBUTE_DATA fad;
                if (GetFileAttributesExA(g_current_file, GetFileExInfoStandard, &fad)) {
                    DWORD new_size = fad.nFileSizeLow;
                    if (new_size != g_last_file_size) {
                        if (!g_file_changed) {
                            g_file_changed = 1;
                            UpdateStatusBar();
                        }
                    }
                }
            }
            return 0;
        }

        case WM_EXITSIZEMOVE: {
            RECT rc;
            GetWindowRect(hwnd, &rc);
            WindowConfigSetInt("has_position", 1);
            WindowConfigSetInt("x", rc.left);
            WindowConfigSetInt("y", rc.top);
            WindowConfigSetInt("width", rc.right - rc.left);
            WindowConfigSetInt("height", rc.bottom - rc.top);
            WindowConfigSetInt("splitter", g_splitter_pos);
            config_save();
            return 0;
        }

        case WM_DESTROY:
            KillTimer(hwnd, IDT_FILE_WATCH);
            SaveColumnWidths();
            SaveColumnOrder();
            if (hFontMono) DeleteObject(hFontMono);
            {
                RECT rc;
                GetWindowRect(hwnd, &rc);
                WindowConfigSetInt("has_position", 1);
                WindowConfigSetInt("x", rc.left);
                WindowConfigSetInt("y", rc.top);
                WindowConfigSetInt("width", rc.right - rc.left);
                WindowConfigSetInt("height", rc.bottom - rc.top);
                config_save();
            }
            nps_logfile_free(&g_logfile);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSEXA wc = {0};
    HWND hwnd;
    MSG msg;

    (void)hPrevInstance;
    (void)lpCmdLine;

    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "NpsLogViewerClass";
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExA(&wc)) {
        MessageBoxA(NULL, "Window Registration Failed!", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    hwnd = CreateWindowExA(0, "NpsLogViewerClass", "NPS Log Viewer " APP_VERSION,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1200, 750,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        MessageBoxA(NULL, "Window Creation Failed!", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    InitAppConfig();
    {
        int has_pos = WindowConfigGetInt("has_position", 0);
        int w = WindowConfigGetInt("width", 1200);
        int h = WindowConfigGetInt("height", 750);
        if (has_pos) {
            int x = WindowConfigGetInt("x", 100);
            int y = WindowConfigGetInt("y", 100);
            SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER);
        } else {
            SetWindowPos(hwnd, NULL, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
        }
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
