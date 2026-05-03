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
#define MAX_STRLEN  256

typedef struct {
    char section[MAX_STRLEN];
    char key[MAX_STRLEN];
    char value[MAX_STRLEN];
} ConfigEntry;

static char g_config_path[1024] = {0};
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
    int idx = find_entry(section, key);
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

static void ensure_dir(const char* path)
{
    char dir[1024];
    const char* last_slash;

#ifdef _WIN32
    last_slash = strrchr(path, '\\');
#else
    last_slash = strrchr(path, '/');
#endif
    if (!last_slash) return;

    size_t len = (size_t)(last_slash - path);
    if (len >= sizeof(dir)) len = sizeof(dir) - 1;
    memcpy(dir, path, len);
    dir[len] = '\0';

#ifdef _WIN32
    CreateDirectoryA(dir, NULL);
#else
    mkdir(dir, 0755);
#endif
}

static void resolve_config_path(const char* app_name, char* out, size_t out_size)
{
    char exec_dir[1024];
    char path_buf[2048];

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

    char test_path[2048];
    snprintf(test_path, sizeof(test_path), "%s\\.writetest", exec_dir);
    FILE* fp = fopen(test_path, "w");
    if (fp) {
        fclose(fp);
        remove(test_path);
        snprintf(path_buf, sizeof(path_buf), "%s\\%s.ini", exec_dir, app_name);
        copy_path(out, out_size, path_buf);
        return;
    }

    const char* local_appdata = getenv("LOCALAPPDATA");
    if (!local_appdata) local_appdata = getenv("APPDATA");
    if (!local_appdata) local_appdata = ".";
    snprintf(path_buf, sizeof(path_buf), "%s\\%s\\%s.ini", local_appdata, app_name, app_name);
    copy_path(out, out_size, path_buf);
#else
    char exe_path[1024];
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

    char test_path[2048];
    snprintf(test_path, sizeof(test_path), "%s/.writetest", exec_dir);
    FILE* fp = fopen(test_path, "w");
    if (fp) {
        fclose(fp);
        remove(test_path);
        snprintf(path_buf, sizeof(path_buf), "%s/%s.ini", exec_dir, app_name);
        copy_path(out, out_size, path_buf);
        return;
    }

    const char* home = getenv("HOME");
    if (!home) home = ".";
    snprintf(path_buf, sizeof(path_buf), "%s/.config/%s/%s.ini", home, app_name, app_name);
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

    char line[1024];
    char current_section[MAX_STRLEN] = "";

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
