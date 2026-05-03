/*
 * NPS Log Viewer
 * (C) 2026, Eugene Nesterenko - eenest@eenest.net
 */

#include "parser.h"
#include "compat.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>

#define STREAM_READ_BUF_LEN 65536

typedef struct {
    FILE* fp;
    unsigned char buf[STREAM_READ_BUF_LEN];
    size_t pos;
    size_t len;
    long bytes_read;
} NpsStreamReader;

static char* trim(char* str)
{
    char* end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

static int has_unquoted_delimiter(const char* line, char delim)
{
    int in_quotes = 0;

    for (const char* p = line; p && *p; p++) {
        if (*p == '"') {
            if (in_quotes && *(p + 1) == '"') {
                p++;
            } else {
                in_quotes = !in_quotes;
            }
            continue;
        }
        if (!in_quotes && *p == delim) return 1;
    }
    return 0;
}

static char* duplicate_raw_line(const char* line)
{
    static const char suffix[] = " ... [raw record truncated]";
    size_t len;
    size_t suffix_len = sizeof(suffix) - 1;
    char* copy;

    if (!line) line = "";
    len = strlen(line);
    if (len <= MAX_RAW_LINE_LEN) return strdup(line);

    copy = (char*)malloc(MAX_RAW_LINE_LEN + suffix_len + 1);
    if (!copy) return NULL;
    memcpy(copy, line, MAX_RAW_LINE_LEN);
    memcpy(copy + MAX_RAW_LINE_LEN, suffix, suffix_len + 1);
    return copy;
}

static int grow_line_buffer(char** buffer, size_t* buffer_cap)
{
    size_t max_cap = (size_t)MAX_PHYSICAL_LINE_LEN + 1;
    size_t new_cap;
    char* new_buffer;

    if (!buffer || !*buffer || !buffer_cap) return 0;
    if (*buffer_cap >= max_cap) return 0;

    new_cap = (*buffer_cap) * 2;
    if (new_cap < *buffer_cap || new_cap > max_cap) new_cap = max_cap;

    new_buffer = (char*)realloc(*buffer, new_cap);
    if (!new_buffer) return 0;

    *buffer = new_buffer;
    *buffer_cap = new_cap;
    return 1;
}

void nps_logfile_init(NpsLogFile* logfile)
{
    if (!logfile) return;
    memset(logfile, 0, sizeof(NpsLogFile));
    logfile->capacity = 256;
    logfile->records = (NpsLogRecord*)calloc(logfile->capacity, sizeof(NpsLogRecord));
    if (!logfile->records) {
        logfile->capacity = 0;
    }
}

void nps_logfile_free(NpsLogFile* logfile)
{
    if (!logfile) return;
    if (logfile->records) {
        for (int i = 0; i < logfile->count; i++) {
            if (logfile->records[i].raw_line) {
                free(logfile->records[i].raw_line);
                logfile->records[i].raw_line = NULL;
            }
        }
        free(logfile->records);
        logfile->records = NULL;
    }
    logfile->count = 0;
    logfile->capacity = 0;
    logfile->has_header = 0;
    logfile->header_count = 0;
    logfile->truncated = 0;
}

/* Parse a CSV line, handling quoted fields */
int nps_parse_line(const char* line, NpsLogRecord* record)
{
    const char* p = line;
    int field = 0;
    int in_quotes = 0;
    int len = 0;
    int split_on_space;

    record->num_fields = 0;
    record->has_names = 0;

    if (!line || !*line) return 0;
    split_on_space = !has_unquoted_delimiter(line, ',') &&
                     !has_unquoted_delimiter(line, '\t');
    if (split_on_space) {
        while (isspace((unsigned char)*p)) p++;
    }

    while (*p && field < MAX_FIELDS) {
        if (*p == '"') {
            if (in_quotes && *(p + 1) == '"') {
                /* Escaped quote */
                if (len < MAX_FIELD_LEN - 1) {
                    record->fields[field][len++] = '"';
                }
                p += 2;
                continue;
            } else {
                in_quotes = !in_quotes;
                p++;
                continue;
            }
        }

        if (!in_quotes &&
            ((*p == ',' || *p == '\t') ||
             (split_on_space && isspace((unsigned char)*p)))) {
            record->fields[field][len] = '\0';
            field++;
            len = 0;
            p++;
            if (split_on_space) {
                while (isspace((unsigned char)*p)) p++;
            }
            continue;
        }

        if (len < MAX_FIELD_LEN - 1) {
            record->fields[field][len++] = *p;
        }
        p++;
    }

    /* Last field */
    if (field < MAX_FIELDS) {
        record->fields[field][len] = '\0';
        field++;
    }
    record->num_fields = field;

    /* Trim all fields */
    for (int i = 0; i < field; i++) {
        char* trimmed = trim(record->fields[i]);
        if (trimmed != record->fields[i]) {
            memmove(record->fields[i], trimmed, strlen(trimmed) + 1);
        }
    }

    return field;
}

static int hex_digit_value(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static int decode_numeric_entity(const char* text, int* consumed)
{
    int base = 10;
    int value = 0;
    int digits = 0;
    const char* p = text;

    if (p[0] != '&' || p[1] != '#') return -1;
    p += 2;
    if (*p == 'x' || *p == 'X') {
        base = 16;
        p++;
    }

    while (*p && *p != ';') {
        int digit = base == 16 ? hex_digit_value(*p) :
                    (isdigit((unsigned char)*p) ? *p - '0' : -1);
        if (digit < 0 || digit >= base) return -1;
        if (value < 256) value = value * base + digit;
        digits++;
        p++;
    }

    if (*p != ';' || digits == 0) return -1;
    *consumed = (int)(p - text + 1);
    return value <= 255 ? value : '?';
}

static void decode_xml_entities(const char* src, int src_len,
                                char* out, size_t out_size)
{
    size_t pos = 0;

    if (!out || out_size == 0) return;
    out[0] = '\0';
    if (!src || src_len <= 0) return;

    for (int i = 0; i < src_len && pos + 1 < out_size; i++) {
        if (src[i] == '&') {
            int consumed = 0;
            int decoded = decode_numeric_entity(src + i, &consumed);

            if (decoded >= 0) {
                out[pos++] = (char)decoded;
                i += consumed - 1;
                continue;
            }
            if (i + 5 <= src_len && strncmp(src + i, "&amp;", 5) == 0) {
                out[pos++] = '&';
                i += 4;
                continue;
            }
            if (i + 4 <= src_len && strncmp(src + i, "&lt;", 4) == 0) {
                out[pos++] = '<';
                i += 3;
                continue;
            }
            if (i + 4 <= src_len && strncmp(src + i, "&gt;", 4) == 0) {
                out[pos++] = '>';
                i += 3;
                continue;
            }
            if (i + 6 <= src_len && strncmp(src + i, "&quot;", 6) == 0) {
                out[pos++] = '"';
                i += 5;
                continue;
            }
            if (i + 6 <= src_len && strncmp(src + i, "&apos;", 6) == 0) {
                out[pos++] = '\'';
                i += 5;
                continue;
            }
        }
        out[pos++] = src[i];
    }
    out[pos] = '\0';
}

/* Simple XML event parser for NPS log format */
static int parse_xml_event(const char* line, NpsLogRecord* record)
{
    const char* p = line;
    record->num_fields = 0;
    record->has_names = 0;

    if (!line || !*line) return 0;

    /* Find <Event> or <Event ...> */
    const char* event_start = strstr(p, "<Event");
    if (!event_start) return 0;
    p = event_start + 6;
    while (*p && *p != '>') p++;
    if (*p == '>') p++;

    while (*p) {
        /* Skip whitespace */
        while (isspace((unsigned char)*p)) p++;
        if (*p != '<') break;
        if (strncmp(p, "</Event>", 8) == 0) break;

        /* Parse opening tag */
        p++; /* skip '<' */
        const char* tag_start = p;
        while (*p && *p != '>' && *p != ' ') p++;
        int tag_len = (int)(p - tag_start);

        /* Skip attributes */
        while (*p && *p != '>') p++;
        if (*p == '>') p++;

        if (tag_len <= 0 || tag_len >= MAX_NAME_LEN) {
            /* Skip until next tag */
            while (*p && *p != '<') p++;
            continue;
        }

        /* Get tag name */
        char tag_name[MAX_NAME_LEN];
        memcpy(tag_name, tag_start, tag_len);
        tag_name[tag_len] = '\0';

        /* Parse value until closing tag */
        const char* val_start = p;
        while (*p && *p != '<') p++;
        int val_len = (int)(p - val_start);

        /* Skip closing tag </Tag> */
        if (*p == '<') {
            p++;
            if (*p == '/') {
                p++;
                while (*p && *p != '>') p++;
                if (*p == '>') p++;
            }
        }

        /* Store */
        if (record->num_fields < MAX_FIELDS) {
            memcpy(record->names[record->num_fields], tag_name, tag_len + 1);
            if (val_len > 0) {
                decode_xml_entities(val_start, val_len,
                                    record->fields[record->num_fields],
                                    sizeof(record->fields[record->num_fields]));
            } else {
                record->fields[record->num_fields][0] = '\0';
            }
            record->num_fields++;
        }
    }

    if (record->num_fields > 0) record->has_names = 1;
    return record->num_fields;
}

static int looks_like_xml(const char* text)
{
    const char* p = text;
    while (*p && isspace((unsigned char)*p)) p++;
    return (strncmp(p, "<?xml", 5) == 0 || strncmp(p, "<Event", 6) == 0);
}

static int store_parsed_record(NpsLogFile* logfile, NpsLogRecord* rec,
                               const char* trimmed, int is_xml, int* line_count)
{
    if (!logfile || !rec || !trimmed || !line_count) return 0;

    /* For CSV: heuristic header detection */
    if (!is_xml && *line_count == 0) {
        int looks_like_header = 0;
        int has_nps_data_marker = 0;

        if (strcmp(rec->fields[0], "NPAS") == 0 ||
            strcmp(rec->fields[0], "NPS") == 0 ||
            strcmp(rec->fields[0], "IAS") == 0) {
            has_nps_data_marker = 1;
        }

        if (!has_nps_data_marker) {
            for (int i = 0; i < rec->num_fields; i++) {
                if (strchr(rec->fields[i], '/')) continue;
                if (strchr(rec->fields[i], ':')) continue;
                if (isdigit((unsigned char)rec->fields[i][0])) continue;
                looks_like_header++;
            }
        }

        if (looks_like_header > rec->num_fields / 2 && rec->num_fields > 3) {
            logfile->has_header = 1;
            logfile->header_count = rec->num_fields;
            for (int i = 0; i < rec->num_fields && i < MAX_FIELDS; i++) {
                strncpy(logfile->header[i], rec->fields[i], MAX_NAME_LEN - 1);
                logfile->header[i][MAX_NAME_LEN - 1] = '\0';
            }
            (*line_count)++;
            return 1;
        }
    }

    /* For XML: use first record's tag names as headers */
    if (is_xml && logfile->count == 0) {
        logfile->has_header = 1;
        logfile->header_count = rec->num_fields;
        for (int i = 0; i < rec->num_fields && i < MAX_FIELDS; i++) {
            strncpy(logfile->header[i], rec->names[i], MAX_NAME_LEN - 1);
            logfile->header[i][MAX_NAME_LEN - 1] = '\0';
        }
    }

    /* Grow array if needed */
    if (logfile->count >= logfile->capacity) {
        int new_cap = logfile->capacity > 0 ? logfile->capacity * 2 : 256;
        NpsLogRecord* new_rec;

        if (new_cap > MAX_RECORDS) new_cap = MAX_RECORDS;
        if (new_cap <= logfile->capacity) {
            logfile->truncated = 1;
            return 0;
        }

        new_rec = (NpsLogRecord*)realloc(logfile->records,
                                         new_cap * sizeof(NpsLogRecord));
        if (!new_rec) return 0;
        logfile->records = new_rec;
        logfile->capacity = new_cap;
    }

    rec->raw_line = duplicate_raw_line(trimmed);
    memcpy(&logfile->records[logfile->count], rec, sizeof(NpsLogRecord));
    logfile->count++;
    (*line_count)++;
    return 1;
}

static int parse_and_store_line(const char* line, NpsLogFile* logfile,
                                int is_xml, int* line_count)
{
    NpsLogRecord rec;
    int fields;

    if (!line || !*line) return 1;

    memset(&rec, 0, sizeof(rec));
    fields = is_xml ? parse_xml_event(line, &rec) : nps_parse_line(line, &rec);
    if (fields == 0) return 1;

    return store_parsed_record(logfile, &rec, line, is_xml, line_count);
}

int nps_parse_file(const char* text, NpsLogFile* logfile)
{
    char* buffer;
    size_t buffer_cap = 4096;
    const char* p = text;
    size_t line_len = 0;
    int line_count = 0;
    int is_xml;

    if (!logfile) return 0;

    nps_logfile_init(logfile);

    if (!text || !*text) return 0;
    is_xml = looks_like_xml(text);

    buffer = (char*)malloc(buffer_cap);
    if (!buffer) return 0;
    if (!logfile->records) {
        free(buffer);
        return 0;
    }

    while (*p) {
        line_len = 0;
        while (*p && *p != '\n') {
            if (line_len + 1 >= buffer_cap) {
                if (!grow_line_buffer(&buffer, &buffer_cap)) {
                    while (*p && *p != '\n') p++;
                    break;
                }
            }
            buffer[line_len++] = *p++;
        }
        buffer[line_len] = '\0';
        if (*p == '\n') p++;

        /* Skip empty lines */
        char* trimmed = trim(buffer);
        if (!*trimmed) continue;

        if (!parse_and_store_line(trimmed, logfile, is_xml, &line_count)) break;
    }

    free(buffer);
    return logfile->count;
}

static int stream_reader_getc(NpsStreamReader* reader, int* out_ch)
{
    if (!reader || !reader->fp || !out_ch) return -1;

    if (reader->pos >= reader->len) {
        reader->len = fread(reader->buf, 1, sizeof(reader->buf), reader->fp);
        reader->pos = 0;
        if (reader->len == 0) {
            if (ferror(reader->fp)) return -1;
            return 0;
        }
    }

    *out_ch = reader->buf[reader->pos++];
    reader->bytes_read++;
    return 1;
}

static int read_stream_line(NpsStreamReader* reader, char** buffer,
                            size_t* buffer_cap)
{
    size_t line_len = 0;
    int ch;
    int read_result;

    while ((read_result = stream_reader_getc(reader, &ch)) > 0) {
        if (line_len + 1 >= *buffer_cap) {
            if (!grow_line_buffer(buffer, buffer_cap)) {
                while (ch != '\n' &&
                       (read_result = stream_reader_getc(reader, &ch)) > 0) {
                    if (ch == '\n') break;
                }
                break;
            }
        }
        (*buffer)[line_len++] = (char)ch;
        if (ch == '\n') break;
    }

    if (read_result < 0) return -1;
    if (line_len == 0 && read_result == 0) return 0;

    (*buffer)[line_len] = '\0';
    return 1;
}

int nps_parse_stream(FILE* fp, long total_bytes, NpsLogFile* logfile,
                     NpsParseProgressCb progress_cb, void* userdata)
{
    char* buffer;
    size_t buffer_cap = 4096;
    NpsStreamReader reader;
    int line_count = 0;
    int is_xml = -1;

    if (!logfile || !fp) return 0;
    nps_logfile_init(logfile);
    buffer = (char*)malloc(buffer_cap);
    if (!buffer) return 0;
    if (!logfile->records) {
        free(buffer);
        return 0;
    }

    memset(&reader, 0, sizeof(reader));
    reader.fp = fp;

    while (1) {
        int read_result = read_stream_line(&reader, &buffer, &buffer_cap);
        char* trimmed;

        if (read_result < 0) break;
        if (read_result == 0) break;

        trimmed = trim(buffer);
        if (!*trimmed) {
            if (progress_cb) progress_cb(reader.bytes_read, total_bytes, userdata);
            continue;
        }

        if (is_xml < 0) is_xml = looks_like_xml(trimmed);
        if (!parse_and_store_line(trimmed, logfile, is_xml, &line_count)) break;

        if (progress_cb) progress_cb(reader.bytes_read, total_bytes, userdata);
    }

    free(buffer);
    if (progress_cb) progress_cb(total_bytes > reader.bytes_read ? reader.bytes_read : total_bytes,
                                 total_bytes, userdata);
    return logfile->count;
}

const char* nps_get_field(const NpsLogRecord* rec, int index)
{
    if (!rec || index < 0 || index >= rec->num_fields) return "";
    return rec->fields[index];
}

const char* nps_get_name(const NpsLogRecord* rec, int index)
{
    if (!rec || index < 0 || index >= rec->num_fields) return "";
    return rec->names[index];
}

int nps_find_column(const NpsLogFile* logfile, const char* name)
{
    if (!logfile || !logfile->has_header || !name) return -1;
    for (int i = 0; i < logfile->header_count; i++) {
        if (strcasecmp(logfile->header[i], name) == 0) return i;
    }
    return -1;
}

int nps_find_field(const NpsLogRecord* rec, const char* name)
{
    if (!rec || !name) return -1;
    for (int i = 0; i < rec->num_fields; i++) {
        if (strcasecmp(rec->names[i], name) == 0) return i;
    }
    return -1;
}
