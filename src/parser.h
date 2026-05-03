/*
 * NPS Log Viewer
 * (C) 2026, Eugene Nesterenko - eenest@eenest.net
 */

#ifndef NPS_PARSER_H
#define NPS_PARSER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdio.h>

#define MAX_FIELDS 64
#define MAX_FIELD_LEN 512
#define MAX_NAME_LEN 64
#define MAX_RECORDS 4096
#define MAX_RAW_LINE_LEN 8192
#define MAX_PHYSICAL_LINE_LEN (8 * 1024 * 1024)

typedef struct {
    char names[MAX_FIELDS][MAX_NAME_LEN];
    char fields[MAX_FIELDS][MAX_FIELD_LEN];
    int num_fields;
    int has_names;   /* 1 if names[] are populated (e.g. XML format) */
    char* raw_line;  /* Original line text (dynamically allocated) */
} NpsLogRecord;

typedef struct {
    NpsLogRecord* records;
    int count;
    int capacity;
    int has_header;
    char header[MAX_FIELDS][MAX_NAME_LEN];
    int header_count;
    int truncated;  /* 1 if parseable records were skipped after MAX_RECORDS */
} NpsLogFile;

typedef void (*NpsParseProgressCb)(long bytes_read, long total_bytes, void* userdata);

/* Initialize/clear a log file structure (allocates records array) */
void nps_logfile_init(NpsLogFile* logfile);

/* Free dynamically allocated memory */
void nps_logfile_free(NpsLogFile* logfile);

/* Parse a single line - handles quoted CSV and simple delimited */
int nps_parse_line(const char* line, NpsLogRecord* record);

/* Parse entire file content (auto-detects CSV vs XML).
 * Initializes logfile; caller should not call nps_logfile_init() first.
 * Caller must release records with nps_logfile_free().
 */
int nps_parse_file(const char* text, NpsLogFile* logfile);

/* Parse file content from an already opened binary/text stream.
 * This avoids allocating the whole file and reports read progress when a
 * callback is provided. The caller owns and closes the stream.
 */
int nps_parse_stream(FILE* fp, long total_bytes, NpsLogFile* logfile,
                     NpsParseProgressCb progress_cb, void* userdata);

/* Get field value safely */
const char* nps_get_field(const NpsLogRecord* rec, int index);

/* Get field name safely */
const char* nps_get_name(const NpsLogRecord* rec, int index);

/* Try to find column index by name */
int nps_find_column(const NpsLogFile* logfile, const char* name);

/* Find field index in a record by name (for XML/records with names) */
int nps_find_field(const NpsLogRecord* rec, const char* name);

#ifdef __cplusplus
}
#endif

#endif
