/**
 * Copyright (C) 2021, Axis Communications AB, Lund, Sweden
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

#include <axsdk/ax_parameter.h>
#include <errno.h>
#include <glib.h>
#include <mntent.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

/**
 * @brief Callback called when the dockerd process exits.
 */
static void dockerd_process_exited_callback(__attribute__((unused)) GPid pid,
                                            gint status,
                                            __attribute__((unused))
                                            gpointer user_data);

// Loop run on the main process
static GMainLoop *loop = NULL;

// Exit code
static int exit_code = 0;

// Pid of the running dockerd process
static pid_t dockerd_process_pid = -1;

// Full path to the SD card
static const char *sd_card_path = "/var/spool/storage/SD_DISK";

// True if the dockerd_exited_callback should restart dockerd
static bool restart_dockerd = false;

/**
 * @brief Signals handling
 *
 * @param signal_num Signal number.
 */
static void
handle_signals(__attribute__((unused)) int signal_num)
{
  switch (signal_num) {
    case SIGINT:
    case SIGTERM:
    case SIGQUIT:
      g_main_loop_quit(loop);
  }
}

/**
 * @brief Initialize signals
 */
static void
init_signals(void)
{
  struct sigaction sa;

  sa.sa_flags = 0;

  sigemptyset(&sa.sa_mask);
  sa.sa_handler = handle_signals;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGQUIT, &sa, NULL);
}

/**
 * @brief Checks if the given process is alive.
 *
 * @return True if alive. False if dead or exited.
 */
static bool
is_process_alive(int pid)
{
  int status;
  pid_t return_pid = waitpid(pid, &status, WNOHANG);
  if (return_pid == -1) {
    // Report errors as dead.
    return false;
  } else if (return_pid == dockerd_process_pid) {
    // Child is already exited, so not alive.
    return false;
  }
  return true;
}

/**
 * @brief Fetch the value of the parameter as a string
 *
 * @return The value of the parameter as string if successful, NULL otherwise
 */
static char *
get_parameter_value(const char *parameter_name)
{
  GError *error = NULL;
  AXParameter *ax_parameter =
      ax_parameter_new("dockerdwrapperwithcompose", &error);
  char *parameter_value = NULL;

  if (ax_parameter == NULL) {
    syslog(LOG_ERR, "Error when creating axparameter: %s", error->message);
    goto end;
  }

  if (!ax_parameter_get(
          ax_parameter, parameter_name, &parameter_value, &error)) {
    syslog(LOG_ERR,
           "Failed to fetch parameter value of %s. Error: %s",
           parameter_name,
           error->message);

    free(parameter_value);
    parameter_value = NULL;
  }

end:
  if (ax_parameter != NULL) {
    ax_parameter_free(ax_parameter);
  }
  g_clear_error(&error);

  return parameter_value;
}

/**
 * @brief Retrieve the file system type of the SD card as a string.
 *
 * @return The file system type as a string (ext4/ext3/vfat etc...) if
 * successful, NULL otherwise.
 */
static char *
get_sd_filesystem(void)
{
  char buf[PATH_MAX];
  struct stat sd_card_stat;
  int stat_result = stat(sd_card_path, &sd_card_stat);
  if (stat_result != 0) {
    syslog(LOG_ERR,
           "Cannot store data on the SD card, no storage exists at %s",
           sd_card_path);
    return NULL;
  }

  FILE *fp;
  dev_t dev;

  dev = sd_card_stat.st_dev;

  if ((fp = setmntent("/proc/mounts", "r")) == NULL) {
    return NULL;
  }

  struct mntent mnt;
  while (getmntent_r(fp, &mnt, buf, PATH_MAX)) {
    if (stat(mnt.mnt_dir, &sd_card_stat) != 0) {
      continue;
    }

    if (sd_card_stat.st_dev == dev) {
      endmntent(fp);
      char *return_value = strdup(mnt.mnt_type);
      return return_value;
    }
  }

  endmntent(fp);

  // Should never reach here.
  errno = EINVAL;
  return NULL;
}

/**
 * @brief Setup the sd card.
 *
 * @return True if successful, false if setup failed.
 */
static bool
setup_sdcard(void)
{
  const char *data_root = "/var/spool/storage/SD_DISK/dockerd/data";
  const char *exec_root = "/var/spool/storage/SD_DISK/dockerd/exec";
  char *create_droot_command = g_strdup_printf("mkdir -p %s", data_root);
  char *create_eroot_command = g_strdup_printf("mkdir -p %s", exec_root);
  int res = system(create_droot_command);
  if (res != 0) {
    syslog(LOG_ERR,
           "Failed to create data_root folder at: %s. Error code: %d",
           data_root,
           res);
    goto end;
  }
  res = system(create_eroot_command);
  if (res != 0) {
    syslog(LOG_ERR,
           "Failed to create exec_root folder at: %s. Error code: %d",
           exec_root,
           res);
    goto end;
  }

  res = 0;

end:

  free(create_droot_command);
  free(create_eroot_command);

  return res == 0;
}

/**
 * @brief Start a new dockerd process.
 *
 * @return True if successful, false otherwise
 */
static bool
start_dockerd(void)
{
  GError *error = NULL;

  bool return_value = false;
  bool result = false;

  gsize args_len = 1024;
  gsize msg_len = 128;
  gchar args[args_len];
  gchar msg[msg_len];
  guint args_offset = 0;
  gchar **args_split = NULL;

  // Read parameters
  char *use_sd_card_value = get_parameter_value("SDCardSupport");
  char *use_tls_value = get_parameter_value("UseTLS");
  char *use_ipc_socket_value = get_parameter_value("IPCSocket");
  if (use_sd_card_value == NULL || use_tls_value == NULL ||
      use_ipc_socket_value == NULL) {
    goto end;
  }
  bool use_sdcard = strcmp(use_sd_card_value, "yes") == 0;
  bool use_tls = strcmp(use_tls_value, "yes") == 0;
  bool use_ipc_socket = strcmp(use_ipc_socket_value, "yes") == 0;

  if (use_sdcard) {
    // Confirm that the SD card is usable
    char *sd_file_system = get_sd_filesystem();
    if (sd_file_system == NULL) {
      syslog(LOG_ERR,
             "Couldn't identify the file system of the SD card at %s",
             sd_card_path);
      goto end;
    }

    if (strcmp(sd_file_system, "vfat") == 0 ||
        strcmp(sd_file_system, "exfat") == 0) {
      syslog(LOG_ERR,
             "The SD card at %s uses file system %s which does not support "
             "Unix file permissions. Please reformat to a file system that "
             "support Unix file permissions, such as ext4 or xfs.",
             sd_card_path,
             sd_file_system);
      goto end;
    }

    if (!setup_sdcard()) {
      syslog(LOG_ERR, "Failed to setup SD card.");
      goto end;
    }
  }
  args_offset += g_snprintf(
      args + args_offset,
      args_len - args_offset,
      "%s %s",
      "dockerd",
      "--config-file "
      "/usr/local/packages/dockerdwrapperwithcompose/localdata/daemon.json");

  g_strlcpy(msg, "Starting dockerd", msg_len);

  if (use_tls) {
    const char *ca_path =
        "/usr/local/packages/dockerdwrapperwithcompose/ca.pem";
    const char *cert_path =
        "/usr/local/packages/dockerdwrapperwithcompose/server-cert.pem";
    const char *key_path =
        "/usr/local/packages/dockerdwrapperwithcompose/server-key.pem";

    bool ca_exists = access(ca_path, F_OK) == 0;
    bool cert_exists = access(cert_path, F_OK) == 0;
    bool key_exists = access(key_path, F_OK) == 0;

    if (!ca_exists) {
      syslog(LOG_ERR,
             "Cannot start using TLS, no CA certificate found at %s",
             ca_path);
    }
    if (!cert_exists) {
      syslog(LOG_ERR,
             "Cannot start using TLS, no server certificate found at %s",
             cert_path);
    }
    if (!key_exists) {
      syslog(LOG_ERR,
             "Cannot start using TLS, no server key found at %s",
             key_path);
    }

    if (!ca_exists || !cert_exists || !key_exists) {
      goto end;
    }

    args_offset += g_snprintf(args + args_offset,
                              args_len - args_offset,
                              " %s %s %s %s %s %s %s %s",
                              "-H tcp://0.0.0.0:2376",
                              "--tlsverify",
                              "--tlscacert",
                              ca_path,
                              "--tlscert",
                              cert_path,
                              "--tlskey",
                              key_path);

    g_strlcat(msg, " in TLS mode", msg_len);
  } else {
    args_offset += g_snprintf(args + args_offset,
                              args_len - args_offset,
                              " %s %s",
                              "-H tcp://0.0.0.0:2375",
                              "--tls=false");

    g_strlcat(msg, " in unsecured mode", msg_len);
  }

  if (use_sdcard) {
    args_offset +=
        g_snprintf(args + args_offset,
                   args_len - args_offset,
                   " %s",
                   "--data-root /var/spool/storage/SD_DISK/dockerd/data");

    g_strlcat(msg, " using SD card as storage", msg_len);
  } else {
    g_strlcat(msg, " using internal storage", msg_len);
  }

  if (use_ipc_socket) {
    args_offset += g_snprintf(args + args_offset,
                              args_len - args_offset,
                              " %s",
                              "-H unix:///var/run/docker.sock");

    g_strlcat(msg, " with IPC socket.", msg_len);
  } else {
    g_strlcat(msg, " without IPC socket.", msg_len);
  }

  // Log startup information to syslog.
  syslog(LOG_INFO, "%s", msg);

  args_split = g_strsplit(args, " ", 0);
  result = g_spawn_async(NULL,
                         args_split,
                         NULL,
                         G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
                         NULL,
                         NULL,
                         &dockerd_process_pid,
                         &error);
  if (!result) {
    syslog(LOG_ERR,
           "Could not execv the dockerd process. Return value: %d, error: %s",
           result,
           error->message);
    goto end;
  }

  // Watch the child process.
  g_child_watch_add(dockerd_process_pid, dockerd_process_exited_callback, NULL);

  if (!is_process_alive(dockerd_process_pid)) {
    // The process died during adding of callback, tell loop to quit.
    exit_code = -1;
    g_main_loop_quit(loop);
    goto end;
  }

  return_value = true;

end:
  g_strfreev(args_split);
  free(use_sd_card_value);
  free(use_tls_value);
  free(use_ipc_socket_value);
  g_clear_error(&error);

  return return_value;
}

/**
 * @brief Stop the currently running dockerd process.
 *
 * @return True if successful, false otherwise
 */
static bool
stop_dockerd(void)
{
  bool killed = false;
  if (dockerd_process_pid == -1) {
    // Nothing to stop.
    killed = true;
    goto end;
  }

  // Send SIGTERM to the process
  bool sigterm_successfully_sent = kill(dockerd_process_pid, SIGTERM) == 0;
  if (!sigterm_successfully_sent) {
    syslog(
        LOG_ERR, "Failed to send SIGTERM to child. Error: %s", strerror(errno));
    errno = 0;
  }

  // Wait before sending a SIGKILL.
  // The sleep will be interrupted when the dockerd_process_callback arrives,
  // so we will essentially sleep until dockerd has shut down or 10 seconds
  // passed.
  sleep(10);

  if (dockerd_process_pid == -1) {
    killed = true;
    goto end;
  }

  // SIGTERM failed, let's try SIGKILL
  killed = kill(dockerd_process_pid, SIGKILL) == 0;
  if (!killed) {
    syslog(
        LOG_ERR, "Failed to send SIGKILL to child. Error: %s", strerror(errno));
  }
end:
  return killed;
}

/**
 * @brief Callback called when the dockerd process exits.
 */
static void
dockerd_process_exited_callback(__attribute__((unused)) GPid pid,
                                gint status,
                                __attribute__((unused)) gpointer user_data)
{
  GError *error = NULL;
  if (!g_spawn_check_wait_status(status, &error)) {
    syslog(LOG_ERR, "Dockerd process exited with error: %d", status);
    g_clear_error(&error);

    exit_code = -1;
  }

  dockerd_process_pid = -1;
  g_spawn_close_pid(pid);

  // The lockfile might have been left behind if dockerd shut down in a bad
  // manner. Remove it manually.
  remove("/var/run/docker.pid");

  if (restart_dockerd) {
    restart_dockerd = false;
    if (!start_dockerd()) {
      syslog(LOG_ERR, "Failed to restart dockerd, exiting.");
      exit_code = -1;
      g_main_loop_quit(loop);
    }
  } else {
    // We shouldn't restart, stop instead.
    g_main_loop_quit(loop);
  }
}

/**
 * @brief Callback function called when the SDCardSupport parameter
 * changes. Will restart the dockerd process with the new setting.
 *
 * @param name Name of the updated parameter.
 * @param value Value of the updated parameter.
 */
static void
parameter_changed_callback(const gchar *name,
                           const gchar *value,
                           __attribute__((unused)) gpointer data)
{
  const gchar *parname = name += strlen("root.dockerdwrapperwithcompose.");
  // bool dockerd_started_correctly = false;
  if (strcmp(parname, "SDCardSupport") == 0) {
    syslog(LOG_INFO, "SDCardSupport changed to: %s", value);
    restart_dockerd = true;
  } else if (strcmp(parname, "UseTLS") == 0) {
    syslog(LOG_INFO, "UseTLS changed to: %s", value);
    restart_dockerd = true;
  } else {
    syslog(LOG_WARNING, "Parameter %s is not recognized", name);
    restart_dockerd = false;

    // No known parameter was changed, do not restart.
    return;
  }

  // Stop the currently running process.
  if (!stop_dockerd()) {
    syslog(LOG_ERR,
           "Failed to stop dockerd process. Please restart the acap "
           "manually.");
    exit_code = -1;
  }
}

static AXParameter *
setup_axparameter(void)
{
  bool success = false;
  GError *error = NULL;
  AXParameter *ax_parameter =
      ax_parameter_new("dockerdwrapperwithcompose", &error);
  if (ax_parameter == NULL) {
    syslog(LOG_ERR, "Error when creating AXParameter: %s", error->message);
    goto end;
  }

  gboolean geresult = ax_parameter_register_callback(
      ax_parameter,
      "root.dockerdwrapperwithcompose.SDCardSupport",
      parameter_changed_callback,
      NULL,
      &error);

  if (geresult == FALSE) {
    syslog(LOG_ERR,
           "Could not register SDCardSupport callback. Error: %s",
           error->message);
    goto end;
  }

  geresult =
      ax_parameter_register_callback(ax_parameter,
                                     "root.dockerdwrapperwithcompose.UseTLS",
                                     parameter_changed_callback,
                                     NULL,
                                     &error);

  if (geresult == FALSE) {
    syslog(LOG_ERR,
           "Could not register UseTLS callback. Error: %s",
           error->message);
    ax_parameter_unregister_callback(
        ax_parameter, "root.dockerdwrapperwithcompose.SDCardSupport");
    goto end;
  }

  success = true;

end:
  if (!success && ax_parameter != NULL) {
    ax_parameter_free(ax_parameter);
  }
  return ax_parameter;
}

int
main(void)
{
  GError *error = NULL;
  AXParameter *ax_parameter = NULL;
  exit_code = 0;

  openlog(NULL, LOG_PID, LOG_USER);
  syslog(LOG_INFO, "Started logging.");

  // Setup signal handling.
  init_signals();

  // Setup ax_parameter
  ax_parameter = setup_axparameter();
  if (ax_parameter == NULL) {
    syslog(LOG_ERR, "Error in setup_axparameter: %s", error->message);
    goto end;
  }

  /* Create the GLib event loop. */
  loop = g_main_loop_new(NULL, FALSE);
  loop = g_main_loop_ref(loop);

  if (!start_dockerd()) {
    syslog(LOG_ERR, "Starting dockerd failed");
    exit_code = -1;
    goto end;
  }

  /* Run the GLib event loop. */
  g_main_loop_run(loop);
  g_main_loop_unref(loop);

end:
  if (stop_dockerd()) {
    syslog(LOG_INFO, "Shutting down. dockerd shut down successfully.");
  } else {
    syslog(LOG_WARNING, "Shutting down. Failed to shut down dockerd.");
  }

  if (ax_parameter != NULL) {
    ax_parameter_unregister_callback(
        ax_parameter, "root.dockerdwrapperwithcompose.SDCardSupport");
    ax_parameter_unregister_callback(ax_parameter,
                                     "root.dockerdwrapperwithcompose.UseTLS");
    ax_parameter_free(ax_parameter);
  }

  g_clear_error(&error);
  return exit_code;
}
