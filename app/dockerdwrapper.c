#include <axsdk/ax_parameter.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <linux/limits.h>
#include <mntent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

// Loop run on the main process.
static GMainLoop *loop = NULL;

// Exit code.
static int exit_code = 0;

// Pid of the running dockerd process.
static pid_t dockerd_process_pid = -1;

// Full path to the SD card
static const char *sd_card_path = "/var/spool/storage/SD_DISK";

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
    // Child is alread exited, so not alive.
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
  char *parameter_value = NULL;
  AXParameter *ax_parameter = ax_parameter_new("dockerdwrapper", &error);
  if (ax_parameter == NULL) {
    syslog(LOG_ERR, "Error when creating AXParameter: %s", error->message);
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
  bool return_value = false;
  int result = 0;

  // Read parameters
  char *use_sd_card_value = get_parameter_value("SDCardSupport");
  char *use_tls_value = get_parameter_value("UseTLS");
  if (use_sd_card_value == NULL || use_tls_value == NULL) {
    goto end;
  }
  bool use_sdcard = strcmp(use_sd_card_value, "yes") == 0;
  bool use_tls = strcmp(use_tls_value, "yes") == 0;

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

  if (use_tls) {
    const char *ca_path = "/usr/local/packages/dockerdwrapper/ca.pem";
    const char *cert_path =
        "/usr/local/packages/dockerdwrapper/server-cert.pem";
    const char *key_path = "/usr/local/packages/dockerdwrapper/server-key.pem";

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

    if (use_sdcard) {
      syslog(LOG_INFO,
             "Starting dockerd in TLS mode using SD card as storage.");
      result = execv(
          "/usr/local/packages/dockerdwrapper/dockerd",
          (char *[]){
              "dockerd",
              "-H",
              "tcp://0.0.0.0:2376",
              "-H",
              "unix:///var/run/docker.sock",
              "--data-root",
              "/var/spool/storage/SD_DISK/dockerd/data",
              "--exec-root",
              "/var/spool/storage/SD_DISK/dockerd/exec",
              "--tlsverify",
              "--tlscacert=/usr/local/packages/dockerdwrapper/ca.pem",
              "--tlscert=/usr/local/packages/dockerdwrapper/server-cert.pem",
              "--tlskey=/usr/local/packages/dockerdwrapper/server-key.pem",
              (char *)NULL});
      if (result != 0) {
        syslog(
            LOG_ERR,
            "Could not execv the dockerd process. Return value: %d, error: %s",
            result,
            strerror(errno));
        goto end;
      }
    } else {
      syslog(LOG_INFO, "Starting dockerd in TLS mode using internal storage.");
      result = execv(
          "/usr/local/packages/dockerdwrapper/dockerd",
          (char *[]){
              "dockerd",
              "-H",
              "tcp://0.0.0.0:2376",
              "-H",
              "unix:///var/run/docker.sock",
              "--tlsverify",
              "--tlscacert=/usr/local/packages/dockerdwrapper/ca.pem",
              "--tlscert=/usr/local/packages/dockerdwrapper/server-cert.pem",
              "--tlskey=/usr/local/packages/dockerdwrapper/server-key.pem",
              (char *)NULL});
      if (result != 0) {
        syslog(
            LOG_ERR,
            "Could not execv the dockerd process. Return value: %d, error: %s",
            result,
            strerror(errno));
      }
      goto end;
    }
  } else {
    if (use_sdcard) {
      syslog(LOG_INFO, "Starting unsecured dockerd using SD card as storage.");
      result = execv("/usr/local/packages/dockerdwrapper/dockerd",
                     (char *[]){"dockerd",
                                "-H",
                                "unix:///var/run/docker.sock",
                                "-H",
                                "tcp://0.0.0.0:2375",
                                "--data-root",
                                "/var/spool/storage/SD_DISK/dockerd/data",
                                "--exec-root",
                                "/var/spool/storage/SD_DISK/dockerd/exec",
                                (char *)NULL});
      if (result != 0) {
        syslog(
            LOG_ERR,
            "Could not execv the dockerd process. Return value: %d, error: %s",
            result,
            strerror(errno));
        goto end;
      }
    } else {
      syslog(LOG_INFO, "Starting unsecured dockerd using internal storage.");
      result = execv("/usr/local/packages/dockerdwrapper/dockerd",
                     (char *[]){"dockerd",
                                "-H",
                                "unix:///var/run/docker.sock",
                                "-H",
                                "tcp://0.0.0.0:2375",
                                (char *)NULL});
      if (result != 0) {
        syslog(
            LOG_ERR,
            "Could not execv the dockerd process. Return value: %d, error: %s",
            result,
            strerror(errno));
        goto end;
      }
    }
  }

  return_value = true;

end:
  free(use_sd_card_value);
  free(use_tls_value);

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

  // Send SIGTERM to the process
  bool sigterm_successfully_sent = kill(dockerd_process_pid, SIGTERM) == 0;
  if (!sigterm_successfully_sent) {
    syslog(
        LOG_ERR, "Failed to send SIGTERM to child. Error: %s", strerror(errno));
    errno = 0;
  }

  if (sigterm_successfully_sent) {
    // Give the process 20 seconds to shut down
    for (int i = 0; i < 20; i++) {
      int result = waitpid(dockerd_process_pid, NULL, WNOHANG);
      if (result == dockerd_process_pid) {
        killed = true;
        break;
      }
      sleep(1);
    }
  }

  if (!killed) {
    killed = kill(dockerd_process_pid, SIGKILL) == 0;
    if (!killed) {
      syslog(LOG_ERR,
             "Failed to send SIGKILL to child. Error: %s",
             strerror(errno));
    } else {
      int result = waitpid(dockerd_process_pid, NULL, 0);
      if (result != dockerd_process_pid) {
        syslog(LOG_ERR, "Failed to wait for process.");
      }
    }
  }

  // The lockfile might have been left behind if dockerd shut down in a bad
  // manner. Remove it manually.
  remove("/var/run/docker.pid");
  dockerd_process_pid = -1;
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
  if (!g_spawn_check_exit_status(status, &error)) {
    syslog(LOG_ERR, "Dockerd process exited with error: %d", status);
    g_clear_error(&error);

    // There has been an error, quit the main loop.
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
  const gchar *parname = name += strlen("root.dockerdwrapper.");
  bool dockerd_started_correctly = false;
  if (strcmp(parname, "SDCardSupport") == 0) {
    syslog(LOG_INFO, "SDCardSupport changed to: %s", value);
  } else if (strcmp(parname, "UseTLS") == 0) {
    syslog(LOG_INFO, "UseTLS changed to: %s", value);
  } else {
    syslog(LOG_WARNING, "Parameter %s is not recognized", name);

    // No known parameter was changed, do not restart.
    return;
  }

  // Stop the currently running process.
  if (!stop_dockerd()) {
    syslog(LOG_ERR,
           "Failed to stop dockerd process. Please restart the acap "
           "manually.");
    exit_code = -1;
    goto end;
  }

  // Start a new one.
  dockerd_process_pid = fork();
  if (dockerd_process_pid == 0) {
    if (!start_dockerd()) {
      syslog(LOG_ERR,
             "Failed to start dockerd process. Please restart the acap "
             "manually.");
      exit_code = -1;
      goto end;
    }
  }

  // Watch the child process
  g_child_watch_add(dockerd_process_pid, dockerd_process_exited_callback, NULL);

  if (!is_process_alive(dockerd_process_pid)) {
    // The child process died during adding of callback, tell loop to quit.
    goto end;
  }

  dockerd_started_correctly = true;
end:
  if (!dockerd_started_correctly) {
    // Tell the main process to quit its loop.
    syslog(LOG_INFO,
           "Parameter changed but dockerd did not start correctly, quitting.");
    g_main_loop_quit(loop);
  }
}

int
main(void)
{
  GError *error = NULL;
  AXParameter *ax_parameter = NULL;
  int exit_code = 0;

  openlog(NULL, LOG_PID, LOG_USER);
  syslog(LOG_INFO, "Started logging.");

  dockerd_process_pid = fork();

  if (dockerd_process_pid == -1) {
    syslog(LOG_ERR, "Fork failed.");

    exit_code = -1;
    goto end_main_thread;
  } else if (dockerd_process_pid == 0) {
    bool dockerd_started_correctly = start_dockerd();
    if (!dockerd_started_correctly) {
      syslog(LOG_ERR, "Starting dockerd failed with error %s", strerror(errno));
      exit_code = -1;
    } else {
      syslog(LOG_ERR, "Dockerd exited.");
    }

    // This is the child process. Skip the cleanup the main process does.
    goto end;
  } else {
    ax_parameter = ax_parameter_new("dockerdwrapper", &error);
    if (ax_parameter == NULL) {
      syslog(LOG_ERR, "Error when creating AXParameter: %s", error->message);
      exit_code = -1;
      goto end_main_thread;
    }

    gboolean geresult =
        ax_parameter_register_callback(ax_parameter,
                                       "root.dockerdwrapper.SDCardSupport",
                                       parameter_changed_callback,
                                       NULL,
                                       &error);

    if (geresult == FALSE) {
      syslog(LOG_ERR,
             "Could not register SDCardSupport callback. Error: %s",
             error->message);
      exit_code = -1;
      goto end_main_thread;
    }

    geresult = ax_parameter_register_callback(ax_parameter,
                                              "root.dockerdwrapper.UseTLS",
                                              parameter_changed_callback,
                                              NULL,
                                              &error);

    if (geresult == FALSE) {
      syslog(LOG_ERR,
             "Could not register UseTLS callback. Error: %s",
             error->message);
      exit_code = -1;
      goto end_main_thread;
    }

    /* Create the GLib event loop. */
    loop = g_main_loop_new(NULL, FALSE);
    loop = g_main_loop_ref(loop);

    if (!is_process_alive(dockerd_process_pid)) {
      // The process died during adding of callback, tell loop to quit.
      goto end_main_thread;
    }

    // Watch the child process.
    g_child_watch_add(
        dockerd_process_pid, dockerd_process_exited_callback, NULL);

    if (!is_process_alive(dockerd_process_pid)) {
      // The process died during adding of callback, tell loop to quit.
      g_main_loop_quit(loop);
    }

    // Start the main loop and setup signal handling.
    init_signals();

    /* Run the GLib event loop. */
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
  }

end_main_thread:
  if (stop_dockerd()) {
    syslog(LOG_INFO, "Shutting down. dockerd shut down successfully.");
  } else {
    syslog(LOG_WARNING, "Shutting down. Failed to shut down dockerd.");
  }

  if (ax_parameter != NULL) {
    ax_parameter_unregister_callback(ax_parameter,
                                     "root.dockerdwrapper.SDCardSupport");
    ax_parameter_unregister_callback(ax_parameter,
                                     "root.dockerdwrapper.UseTLS");
    ax_parameter_free(ax_parameter);
  }

end:
  g_clear_error(&error);
  return exit_code;
}
