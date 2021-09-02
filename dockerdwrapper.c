#include <axsdk/axparameter.h>
#include <errno.h>
#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

// Pid of the running dockerd process.
static pid_t dockerd_process_pid = -1;

// Loop used by the main thread.
static GMainLoop *loop = NULL;

/**
 * @brief Signals handling
 *
 * @param signal_num Signal number.
 */
static void
handle_sigterm(__attribute__((unused)) int signal_num)
{
  g_main_loop_quit(loop);
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
  sa.sa_handler = handle_sigterm;
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
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
 * @brief Stop the currently running dockerd process.
 *
 * @return True if successful, false if setup failed.
 */
static bool
stop_dockerd(void)
{
  bool sigterm_successfully_sent = kill(dockerd_process_pid, SIGTERM) == 0;
  if (!sigterm_successfully_sent) {
    syslog(
        LOG_ERR, "Failed to send SIGTERM to child. Error: %s", strerror(errno));
    errno = 0;
  }

  bool killed = false;
  if (sigterm_successfully_sent) {
    // Give the process 10 seconds to shut down
    for (int i = 0; i < 10; i++) {
      int result = waitpid(dockerd_process_pid, NULL, 1);
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
      int result = wait(NULL);
      if (result != dockerd_process_pid) {
        syslog(LOG_ERR, "Failed to wait for process.");
      }
    }
  }

  remove("/var/run/docker.pid");
  dockerd_process_pid = -1;
  return killed;
}

/**
 * @brief Start a new dockerd process.
 *
 * @return True if successful, false if setup failed.
 */
static bool
start_dockerd(void)
{
  int result = 0;
  struct stat statbuf;
  bool use_sdcard =
      system("parhandclient get root.dockerdwrapper.SDCardSupport | grep yes "
             "-q") == 0;
  if (!stat("/usr/local/packages/dockerdwrapper/server-key.pem", &statbuf)) {
    if (use_sdcard) {
      bool sdcard_setup = setup_sdcard();
      if (!sdcard_setup) {
        syslog(LOG_ERR, "Failed to setup SD card.");
        return false;
      }
      syslog(LOG_INFO, "Starting dockerd in TLS mode.");
      result = execv(
          "/usr/local/packages/dockerdwrapper/dockerd",
          (char *[]){
              "dockerd",
              "-H",
              "tcp://0.0.0.0:2376",
              "-H",
              "unix:///var/run/docker.sock",
              "--tlsverify",
              "--data-root",
              "/var/spool/storage/SD_DISK/dockerd/data",
              "--exec-root",
              "/var/spool/storage/SD_DISK/dockerd/exec",
              "--tlscacert=/usr/local/packages/dockerdwrapper/ca.pem",
              "--tlscert=/usr/local/packages/dockerdwrapper/server-cert.pem",
              "--tlskey=/usr/local/packages/dockerdwrapper/server-key.pem",
              (char *)NULL});
      if (result == -1) {
        syslog(LOG_ERR,
               "Could not execv the dockerd process, error: %s",
               strerror(errno));
      }
    } else {
      syslog(LOG_INFO, "Starting dockerd in TLS mode.");
      result = execv(
          "/usr/local/packages/dockerdwrapper/dockerd",
          (char *[]){
              "dockerd",
              "-H",
              "tcp://0.0.0.0:2376",
              "-H",
              "unix:///var/run/docker.sock",
              "--tlsverify",
              "--data-root",
              "/var/spool/storage/SD_DISK/dockerd/data",
              "--exec-root",
              "/var/spool/storage/SD_DISK/dockerd/exec",
              "--tlscacert=/usr/local/packages/dockerdwrapper/ca.pem",
              "--tlscert=/usr/local/packages/dockerdwrapper/server-cert.pem",
              "--tlskey=/usr/local/packages/dockerdwrapper/server-key.pem",
              (char *)NULL});
      if (result == -1) {
        syslog(LOG_ERR,
               "Could not execv the dockerd process, error: %s",
               strerror(errno));
      }
    }
  } else {
    if (use_sdcard) {
      bool sdcard_setup = setup_sdcard();
      if (!sdcard_setup) {
        syslog(LOG_ERR, "Failed to setup SD card.");
        return false;
      }
      syslog(LOG_INFO, "Starting unsecured dockerd with sd card.");
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
      if (result == -1) {
        syslog(LOG_ERR,
               "Could not execv the dockerd process, error: %s",
               strerror(errno));
      }
    } else {
      syslog(LOG_INFO, "Starting unsecured dockerd without sd card.");
      result = execv("/usr/local/packages/dockerdwrapper/dockerd",
                     (char *[]){"dockerd",
                                "-H",
                                "unix:///var/run/docker.sock",
                                "-H",
                                "tcp://0.0.0.0:2375",
                                (char *)NULL});
      if (result == -1) {
        syslog(LOG_ERR,
               "Could not execv the dockerd process, error: %s",
               strerror(errno));
      }
    }
  }
  return true;
}

/**
 * @brief Callback function called when the SDCardSupport parameter
 * changes. Will restart the dockerd process with the new setting.
 *
 * @param name Name of the updated parameter.
 * @param value Value of the updated parameter.
 */
static void
SDCardSupport_changed_callback(const gchar *name,
                               const gchar *value,
                               __attribute__((unused)) gpointer data)
{
  if (strcasecmp(name, "root.dockerdwrapper.SDCardSupport") == 0) {
    syslog(LOG_WARNING, "SDCardSupport changed to: %s", value);
  } else {
    syslog(LOG_WARNING, "Parameter %s is not recognized", name);

    // SDCardSupport was not changed, do not restart.
    return;
  }

  // Stop the currently running process.
  if (!stop_dockerd()) {
    syslog(LOG_ERR,
           "Failed to stop dockerd process. Please restart the acap "
           "manually.");
    return;
  }

  // Start a new one.
  dockerd_process_pid = fork();
  if (dockerd_process_pid == 0) {
    if (!start_dockerd()) {
      syslog(LOG_ERR,
             "Failed to start dockerd process. Please restart the acap "
             "manually.");
      return;
    }
  }
}

int
main(void)
{
  GError *error = NULL;
  int exit_code = 0;
  AXParameter *parameter = NULL;
  int result;

  openlog(NULL, LOG_PID, LOG_USER);
  syslog(LOG_INFO, "Started logging.");

  dockerd_process_pid = fork();

  if (dockerd_process_pid == -1) {
    syslog(LOG_ERR, "Fork failed.");
    exit_code = -1;
  } else if (dockerd_process_pid == 0) {
    result = start_dockerd();
    if (result == -1) {
      syslog(LOG_ERR, "Starting dockerd failed with error %s", strerror(errno));
      exit_code = -1;
    } else {
      syslog(LOG_ERR, "Dockerd exited.");
    }

    // This is the child process. Do no cleanup, just exit.
    return 0;

  } else {
    parameter = ax_parameter_new("dockerdwrapper", &error);

    gboolean geresult =
        ax_parameter_register_callback(parameter,
                                       "root.dockerdwrapper.SDCardSupport",
                                       SDCardSupport_changed_callback,
                                       NULL,
                                       &error);

    if (geresult == FALSE || parameter == NULL) {
      syslog(LOG_ERR, "Could not register callback. Error: %s", error->message);
      goto end;
    }

    // Start the main loop and setup signal handling.
    loop = g_main_loop_new(NULL, FALSE);
    init_signals();
    g_main_loop_run(loop);

    // Unref the main loop when the main loop has been quit.
    g_main_loop_unref(loop);
  }

end:
  if (stop_dockerd()) {
    syslog(LOG_INFO, "Shutting down. dockerd shut down successfully.");
  } else {
    syslog(LOG_INFO, "Shutting down. Failed to shut down dockerd.");
  }

  g_error_free(error);
  ax_parameter_free(parameter);
  return exit_code;
}
