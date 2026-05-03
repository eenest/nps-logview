/*
 * NPS Log Viewer
 * (C) 2026, Eugene Nesterenko - eenest@eenest.net
 *
 * Simple INI-style configuration file handler.
 */

#include "config.h"
#include "compat.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

#define MAX_ENTRIES 512
#define MAX_SECTION_LEN 128
#define MAX_KEY_LEN     128
#define MAX_VALUE_LEN   4096
#define MAX_CONFIG_PATH 4096

typedef struct {
    char section[MAX_SECTION_LEN];
    char key[MAX_KEY_LEN];
    char value[MAX_VALUE_LEN];
} ConfigEntry;

static char g_config_path[MAX_CONFIG_PATH] = {0};
static ConfigEntry g_entries[MAX_ENTRIES];
static int g_entry_count = 0;
static int g_loaded = 0;

static void strip_newline(char* str)
{
    char* p = str;
    while (*p) {
        if (*p == '\r' || *p == '\n') {
            *p = '\0';
            break;
        }
        p++;
    }
}

static char* trim(char* str)
{
    char* end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

static int find_entry(const char* section, const char* key)
{
    for (int i = 0; i < g_entry_count; i++) {
        if (strcasecmp(g_entries[i].section, section) == 0 &&
            strcasecmp(g_entries[i].key, key) == 0) {
            return i;
        }
    }
    return -1;
}

static void set_entry_value(const char* section, const char* key, const char* val)
{
    int idx;

    if (!section) section = "";
    if (!key) key = "";
    if (!val) val = "";
    idx = find_entry(section, key);

    if (idx >= 0) {
        strncpy(g_entries[idx].value, val, sizeof(g_entries[idx].value) - 1);
        g_entries[idx].value[sizeof(g_entries[idx].value) - 1] = '\0';
        return;
    }

    if (g_entry_count < MAX_ENTRIES) {
        ConfigEntry* e = &g_entries[g_entry_count++];
        strncpy(e->section, section, sizeof(e->section) - 1);
        e->section[sizeof(e->section) - 1] = '\0';
        strncpy(e->key, key, sizeof(e->key) - 1);
        e->key[sizeof(e->key) - 1] = '\0';
        strncpy(e->value, val, sizeof(e->value) - 1);
        e->value[sizeof(e->value) - 1] = '\0';
    }
}

static int section_has_entries(const char* section)
{
    for (int i = 0; i < g_entry_count; i++) {
        if (strcasecmp(g_entries[i].section, section) == 0) {
            return 1;
        }
    }
    return 0;
}

static int section_is_named(const char* section, const char* const* sections, int count)
{
    for (int i = 0; i < count; i++) {
        if (strcasecmp(section, sections[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static void write_section(FILE* fp, const char* section)
{
    if (!section_has_entries(section)) return;

    fprintf(fp, "\n[%s]\n", section);
    for (int i = 0; i < g_entry_count; i++) {
        if (strcasecmp(g_entries[i].section, section) == 0) {
            fprintf(fp, "%s=%s\n", g_entries[i].key, g_entries[i].value);
        }
    }
}

static void copy_path(char* out, size_t out_size, const char* path)
{
    size_t len;

    if (!out || out_size == 0) return;
    if (!path) path = "";
    len = strlen(path);
    if (len >= out_size) len = out_size - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

static void append_text(char* out, size_t out_size, const char* text)
{
    size_t len;

    if (!out || out_size == 0) return;
    if (!text) text = "";
    len = strlen(out);
    if (len >= out_size) return;
    copy_path(out + len, out_size - len, text);
}

static void make_ini_name(char* out, size_t out_size, const char* app_name)
{
    copy_path(out, out_size, app_name && *app_name ? app_name : "nps-logview");
    append_text(out, out_size, ".ini");
}

static int path_has_trailing_sep(const char* path, char sep)
{
    size_t len;

    if (!path || !*path) return 0;
    len = strlen(path);
    if (path[len - 1] == sep) return 1;
#ifdef _WIN32
    if (path[len - 1] == '\\' || path[len - 1] == '/') return 1;
#endif
    return 0;
}

static void join_path(char* out, size_t out_size, const char* dir,
                      char sep, const char* leaf)
{
    copy_path(out, out_size, dir && *dir ? dir : ".");
    if (!path_has_trailing_sep(out, sep)) {
        char sep_text[2];
        sep_text[0] = sep;
        sep_text[1] = '\0';
        append_text(out, out_size, sep_text);
    }
    append_text(out, out_size, leaf);
}

static void ensure_dir(const char* path)
{
    char dir[MAX_CONFIG_PATH];
    const char* last_slash;
    char* p;

#ifdef _WIN32
    const char* slash_a = strrchr(path, '\\');
    const char* slash_b = strrchr(path, '/');
    last_slash = (!slash_a || (slash_b && slash_b > slash_a)) ? slash_b : slash_a;
#else
    last_slash = strrchr(path, '/');
#endif
    if (!last_slash) return;

    size_t len = (size_t)(last_slash - path);
    if (len >= sizeof(dir)) len = sizeof(dir) - 1;
    memcpy(dir, path, len);
    dir[len] = '\0';

#ifdef _WIN32
    p = dir;
    if (isalpha((unsigned char)p[0]) && p[1] == ':' && (p[2] == '\\' || p[2] == '/')) {
        p += 3;
    } else if ((p[0] == '\\' || p[0] == '/') && (p[1] == '\\' || p[1] == '/')) {
        p += 2;
        while (*p && *p != '\\' && *p != '/') p++;
        if (*p) p++;
        while (*p && *p != '\\' && *p != '/') p++;
        if (*p) p++;
    }
    for (; *p; p++) {
        if (*p == '\\' || *p == '/') {
            char saved = *p;
            *p = '\0';
            if (dir[0]) CreateDirectoryA(dir, NULL);
            *p = saved;
        }
    }
    CreateDirectoryA(dir, NULL);
#else
    p = dir;
    if (*p == '/') p++;
    for (; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (dir[0]) mkdir(dir, 0755);
            *p = '/';
        }
    }
    mkdir(dir, 0755);
#endif
}

static void resolve_config_path(const char* app_name, char* out, size_t out_size)
{
    char exec_dir[MAX_CONFIG_PATH];
    char path_buf[MAX_CONFIG_PATH];
    char dir_buf[MAX_CONFIG_PATH];
    char ini_name[MAX_KEY_LEN + 8];

    make_ini_name(ini_name, sizeof(ini_name), app_name);
#ifdef _WIN32
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
    char* last_slash = strrchr(exe_path, '\\');
    if (last_slash) {
        *last_slash = '\0';
        strncpy(exec_dir, exe_path, sizeof(exec_dir) - 1);
    } else {
        strncpy(exec_dir, ".", sizeof(exec_dir) - 1);
    }
    exec_dir[sizeof(exec_dir) - 1] = '\0';

    char test_path[MAX_CONFIG_PATH];
    join_path(test_path, sizeof(test_path), exec_dir, '\\', ".writetest");
    FILE* fp = fopen(test_path, "w");
    if (fp) {
        fclose(fp);
        remove(test_path);
        join_path(path_buf, sizeof(path_buf), exec_dir, '\\', ini_name);
        copy_path(out, out_size, path_buf);
        return;
    }

    const char* local_appdata = getenv("LOCALAPPDATA");
    if (!local_appdata) local_appdata = getenv("APPDATA");
    if (!local_appdata) local_appdata = ".";
    join_path(dir_buf, sizeof(dir_buf), local_appdata, '\\', app_name);
    join_path(path_buf, sizeof(path_buf), dir_buf, '\\', ini_name);
    copy_path(out, out_size, path_buf);
#else
    char exe_path[MAX_CONFIG_PATH];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        char* last_slash = strrchr(exe_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            strncpy(exec_dir, exe_path, sizeof(exec_dir) - 1);
        } else {
            strncpy(exec_dir, ".", sizeof(exec_dir) - 1);
        }
    } else {
        strncpy(exec_dir, ".", sizeof(exec_dir) - 1);
    }
    exec_dir[sizeof(exec_dir) - 1] = '\0';

    char test_path[MAX_CONFIG_PATH];
    join_path(test_path, sizeof(test_path), exec_dir, '/', ".writetest");
    FILE* fp = fopen(test_path, "w");
    if (fp) {
        fclose(fp);
        remove(test_path);
        join_path(path_buf, sizeof(path_buf), exec_dir, '/', ini_name);
        copy_path(out, out_size, path_buf);
        return;
    }

    const char* home = getenv("HOME");
    if (!home) home = ".";
    join_path(dir_buf, sizeof(dir_buf), home, '/', ".config");
    join_path(path_buf, sizeof(path_buf), dir_buf, '/', app_name);
    join_path(dir_buf, sizeof(dir_buf), path_buf, '/', ini_name);
    copy_path(path_buf, sizeof(path_buf), dir_buf);
    copy_path(out, out_size, path_buf);
#endif
}

void config_init(const char* app_name)
{
    if (g_loaded) return;

    g_entry_count = 0;
    resolve_config_path(app_name, g_config_path, sizeof(g_config_path));

    FILE* fp = fopen(g_config_path, "r");
    if (!fp) {
        g_loaded = 1;
        return;
    }

    char line[MAX_VALUE_LEN + MAX_KEY_LEN + 8];
    char current_section[MAX_SECTION_LEN] = "";

    while (fgets(line, sizeof(line), fp) && g_entry_count < MAX_ENTRIES) {
        strip_newline(line);
        char* trimmed = trim(line);

        if (*trimmed == '\0' || *trimmed == '#' || *trimmed == ';')
            continue;

        if (*trimmed == '[') {
            char* end = strchr(trimmed + 1, ']');
            if (end) {
                *end = '\0';
                strncpy(current_section, trimmed + 1, sizeof(current_section) - 1);
                current_section[sizeof(current_section) - 1] = '\0';
            }
            continue;
        }

        char* eq = strchr(trimmed, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = trim(trimmed);
        char* val = trim(eq + 1);

        set_entry_value(current_section, key, val);
    }

    fclose(fp);
    g_loaded = 1;
}

void config_save(void)
{
    static const char* section_order[] = {
        "app", "window.win64", "window.win32", "window.gtk",
        "settings", "recent", "columns", "columns.hidden"
    };

    if (!g_loaded || g_config_path[0] == '\0') return;

    ensure_dir(g_config_path);

    FILE* fp = fopen(g_config_path, "w");
    if (!fp) return;

    for (int i = 0; i < (int)(sizeof(section_order) / sizeof(section_order[0])); i++) {
        write_section(fp, section_order[i]);
    }

    for (int i = 0; i < g_entry_count; i++) {
        int section_seen = 0;
        if (section_is_named(g_entries[i].section, section_order,
                             (int)(sizeof(section_order) / sizeof(section_order[0])))) {
            continue;
        }
        for (int prev = 0; prev < i; prev++) {
            if (strcasecmp(g_entries[prev].section, g_entries[i].section) == 0) {
                section_seen = 1;
                break;
            }
        }
        if (section_seen) continue;

        write_section(fp, g_entries[i].section);
    }

    fclose(fp);
}

const char* config_get_path(void)
{
    return g_config_path;
}

const char* config_get_string(const char* section, const char* key, const char* default_val)
{
    int idx = find_entry(section, key);
    if (idx >= 0) return g_entries[idx].value;
    return default_val;
}

void config_set_string(const char* section, const char* key, const char* val)
{
    set_entry_value(section, key, val);
}

void config_clear_section(const char* section)
{
    int write_pos = 0;

    if (!section) return;
    for (int read_pos = 0; read_pos < g_entry_count; read_pos++) {
        if (strcasecmp(g_entries[read_pos].section, section) == 0) {
            continue;
        }
        if (write_pos != read_pos) {
            g_entries[write_pos] = g_entries[read_pos];
        }
        write_pos++;
    }
    g_entry_count = write_pos;
}

int config_get_int(const char* section, const char* key, int default_val)
{
    const char* str = config_get_string(section, key, NULL);
    if (!str) return default_val;
    return atoi(str);
}

void config_set_int(const char* section, const char* key, int val)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", val);
    config_set_string(section, key, buf);
}
