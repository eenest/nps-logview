/*
 * NPS Log Viewer
 * (C) 2026, Eugene Nesterenko - eenest@eenest.net
 *
 * Simple CLI test for Linux - tests parser and dictionary without Win32 GUI
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "radius_dict.h"
#include "compat.h"

static void print_decoded(const char* name, const char* value)
{
    char decode_buf[512];

    if (!name) name = "?";
    if (!value || !*value) {
        printf("%s: %s\n", name, value ? value : "");
        return;
    }

    if (decode_radius_field(name, value, decode_buf, sizeof(decode_buf))) {
        printf("%s: %s\n", name, decode_buf);
    } else {
        printf("%s: %s\n", name, value);
    }
}

int main(int argc, char* argv[])
{
    const char* filename;
    FILE* fp;
    long size;
    NpsLogFile logfile = {0};
    int count;

    printf("NPS Log Viewer - CLI Test\n");
    printf("=========================\n\n");

    if (argc < 2) {
        printf("Usage: %s <nps-log-file>\n", argv[0]);
        printf("\nSupports NPS XML logs and database-compatible CSV.\n\n");
        return 1;
    }

    filename = argv[1];
    fp = fopen(filename, "rb");
    if (!fp) {
        printf("Error: Cannot open file '%s'\n", filename);
        return 1;
    }

    fseek(fp, 0, SEEK_END);
    size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (size <= 0) {
        printf("Error: File is empty\n");
        fclose(fp);
        return 1;
    }

    count = nps_parse_stream(fp, size, &logfile, NULL, NULL);
    fclose(fp);

    printf("Parsed %d records (format: %s).\n\n", count,
           logfile.has_header ? "with headers" : "no headers");

    for (int r = 0; r < logfile.count && r < 50; r++) {
        NpsLogRecord* rec = &logfile.records[r];
        printf("--- Record %d ---\n", r + 1);
        for (int f = 0; f < rec->num_fields; f++) {
            const char* name = rec->has_names ? rec->names[f]
                : ((logfile.has_header && f < logfile.header_count)
                   ? logfile.header[f] : "Field");
            print_decoded(name, rec->fields[f]);
        }
        printf("\n");
    }

    if (logfile.count > 50) {
        printf("... (%d more records not shown)\n", logfile.count - 50);
    }

    printf("\n=== Common RADIUS Attribute Codes ===\n");
    for (int i = 1; i <= 40; i++) {
        const char* name = radius_attr_name(i);
        if (strcmp(name, "Unknown") != 0) {
            printf("%2d = %s\n", i, name);
        }
    }

    nps_logfile_free(&logfile);
    return 0;
}
