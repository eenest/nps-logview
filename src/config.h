/*
 * NPS Log Viewer
 * (C) 2026, Eugene Nesterenko - eenest@eenest.net
 *
 * Simple INI-style configuration file handler.
 */

#ifndef CONFIG_H
#define CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize config: determine file path and load existing settings.
 * Call once at application startup.
 */
void config_init(const char* app_name);

/* Save current settings to disk. Call before application exit. */
void config_save(void);

/* Get the resolved INI path selected by config_init(). */
const char* config_get_path(void);

/* Get integer value (returns default if not found). */
int config_get_int(const char* section, const char* key, int default_val);

/* Set integer value. */
void config_set_int(const char* section, const char* key, int val);

/* Get string value (returns default if not found).
 * Returned pointer is valid until next config_set_string or config_save.
 */
const char* config_get_string(const char* section, const char* key, const char* default_val);

/* Set string value. */
void config_set_string(const char* section, const char* key, const char* val);

/* Remove all keys from a section. */
void config_clear_section(const char* section);

#ifdef __cplusplus
}
#endif

#endif
