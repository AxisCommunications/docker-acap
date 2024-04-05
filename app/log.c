#include "log.h"
#include <glib.h>
#include <stdio.h>
#include <syslog.h>

static int
log_level_to_syslog_priority(GLogLevelFlags log_level)
{
  if (log_level == G_LOG_LEVEL_NON_FATAL_ERROR)
    log_level = G_LOG_LEVEL_ERROR;

  switch (log_level) {
    case G_LOG_LEVEL_DEBUG:
      return LOG_INFO; // ... since LOG_DEBUG doesn't show up in syslog
    case G_LOG_LEVEL_INFO:
      return LOG_INFO;
    case G_LOG_LEVEL_WARNING:
      return LOG_WARNING;
    case G_LOG_LEVEL_ERROR:
      return LOG_ERR;
    case G_LOG_LEVEL_CRITICAL:
      return LOG_CRIT;
    default:
      return LOG_NOTICE;
  }
}

// String representation has been chosen to match that of dockerd
static const char *
log_level_to_string(GLogLevelFlags log_level)
{
  if (log_level == G_LOG_LEVEL_NON_FATAL_ERROR)
    log_level = G_LOG_LEVEL_ERROR;

  switch (log_level) {
    case G_LOG_LEVEL_DEBUG:
      return "DEBU";
    case G_LOG_LEVEL_INFO:
      return "INFO";
    case G_LOG_LEVEL_WARNING:
      return "WARN";
    case G_LOG_LEVEL_ERROR:
      return "ERRO";
    case G_LOG_LEVEL_CRITICAL:
      return "CRIT";
    default:
      return "?";
  }
}

static bool
log_threshold_met(GLogLevelFlags log_level, const struct log_settings *settings)
{
  return settings->debug || (log_level & ~G_LOG_LEVEL_DEBUG);
}

static void
log_to_syslog(__attribute__((unused)) const char *log_domain,
              GLogLevelFlags log_level,
              const char *message,
              gpointer settings_void_ptr)
{
  if (log_threshold_met(log_level, settings_void_ptr))
    syslog(log_level_to_syslog_priority(log_level), "%s", message);
}

// Timestamp format and log level have been chosen to match that of dockerd
static void
log_to_stdout(__attribute__((unused)) const char *log_domain,
              GLogLevelFlags log_level,
              const char *message,
              gpointer settings_void_ptr)
{
  if (log_threshold_met(log_level, settings_void_ptr)) {
    GDateTime *now = g_date_time_new_now_local();
    g_autofree char *now_text = g_date_time_format(now, "%Y-%m-%dT%T.%f000%:z");
    g_date_time_unref(now);
    printf("%s[%s] %s\n", log_level_to_string(log_level), now_text, message);
  }
}

void
log_init(struct log_settings *settings)
{
  if (settings->destination == log_dest_syslog)
    openlog(NULL, LOG_PID, LOG_USER);

  g_log_set_handler(
      NULL,
      G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION | G_LOG_LEVEL_MASK,
      settings->destination == log_dest_syslog ? log_to_syslog : log_to_stdout,
      settings);
}
