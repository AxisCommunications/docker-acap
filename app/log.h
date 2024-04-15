#pragma once
#include <glib.h>
#include <stdbool.h>

enum log_destination { log_dest_stdout, log_dest_syslog };

struct log_settings {
    enum log_destination destination;
};

// Set up g_log to log to either stdout or syslog.
// The log destination cannot be changed after this call, but the debug level
// can be adjusted at any time by changing the 'debug' member of the struct. A
// pointer to the log_settings struct will be passed to g_log_set_handler(), so
// the struct must live until the process exits.
void log_init(struct log_settings* settings);

void log_debug_set(bool enabled);

// Replacement for G_LOG_LEVEL_ERROR, which is fatal.
#define G_LOG_LEVEL_NON_FATAL_ERROR (1 << G_LOG_LEVEL_USER_SHIFT)

#define log_debug(format, ...)   g_debug(format, ##__VA_ARGS__)
#define log_info(format, ...)    g_info(format, ##__VA_ARGS__)
#define log_warning(format, ...) g_warning(format, ##__VA_ARGS__)

#define log_error(format, ...) \
    g_log(G_LOG_DOMAIN, G_LOG_LEVEL_NON_FATAL_ERROR, format, ##__VA_ARGS__)
