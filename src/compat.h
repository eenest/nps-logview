/*
 * NPS Log Viewer
 * (C) 2026, Eugene Nesterenko - eenest@eenest.net
 */

#ifndef NPS_COMPAT_H
#define NPS_COMPAT_H

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static inline int nps_strcasecmp(const char* a, const char* b)
{
    unsigned char ca;
    unsigned char cb;

    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;

    while (*a && *b) {
        ca = (unsigned char)tolower((unsigned char)*a);
        cb = (unsigned char)tolower((unsigned char)*b);
        if (ca != cb) return (int)ca - (int)cb;
        a++;
        b++;
    }

    ca = (unsigned char)tolower((unsigned char)*a);
    cb = (unsigned char)tolower((unsigned char)*b);
    return (int)ca - (int)cb;
}

static inline char* nps_strdup(const char* str)
{
    size_t len;
    char* copy;

    if (!str) return NULL;
    len = strlen(str) + 1;
    copy = (char*)malloc(len);
    if (!copy) return NULL;
    memcpy(copy, str, len);
    return copy;
}

#define strcasecmp nps_strcasecmp
#define strdup nps_strdup

#endif
