#include <axsdk/axparameter.h>
#include <errno.h>
#include <glib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

// Loop run on the main process.
static GMainLoop *loop = NULL;

// Exit code.
static int exit_code = 0;

// Pid of the running dockerd process.
static pid_t dockerd_process_pid = -1;

// Source for the callback.
static GSource *dockerd_process_callback_source = NULL;

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
 * @brief Callback called when the dockerd process exits.
 */
static void
dockerd_process_exited_callback(__attribute__((unused)) GPid pid,
                                gint status,
                                __attribute__((unused)) gpointer user_data)
{
  GError *error = NULL;
  if (g_spawn_check_exit_status(status, &error)) {
    syslog(LOG_ERR, "Exited successfully");
  } else {
    syslog(LOG_ERR, "Dockerd process exited with error: %d", status);
    g_error_free(error);
  }

  wait(NULL);

  g_main_loop_quit(loop);
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
  // Remove the callback source, so we don't get a callback.
  g_source_destroy(dockerd_process_callback_source);
  dockerd_process_callback_source = NULL;

  bool killed = false;
  int status;
  pid_t return_pid = waitpid(dockerd_process_pid, &status, WNOHANG);
  if (return_pid == -1) {
    // Does the process exists at all?
    if (0 != kill(dockerd_process_pid, 0)) {
      // No, it doesn't. Flag it as killed.
      killed = true;
      goto end;
    }
    syslog(LOG_ERR, "Failed to check process status.");
    goto end;
  } else if (return_pid == dockerd_process_pid) {
    // Child is exited, no need to kill it.
    killed = true;
    goto end;
  }

  // The process is alive and running, let's kill it.
  bool sigterm_successfully_sent = kill(dockerd_process_pid, SIGTERM) == 0;
  if (!sigterm_successfully_sent) {
    syslog(
        LOG_ERR, "Failed to send SIGTERM to child. Error: %s", strerror(errno));
    errno = 0;
  }

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

end:
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
  bool use_sdcard =
      system("parhandclient get root.dockerdwrapper.SDCardSupport | grep yes "
             "-q") == 0;
  bool use_tls =
      system("parhandclient get root.dockerdwrapper.UseTLS | grep yes -q") == 0;
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
      return false;
    }
    if (use_sdcard) {
      bool sdcard_setup = setup_sdcard();
      if (!sdcard_setup) {
        syslog(LOG_ERR, "Failed to setup SD card.");
        return false;
      }
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
      if (result == -1) {
        syslog(LOG_ERR,
               "Could not execv the dockerd process, error: %s",
               strerror(errno));
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

  dockerd_started_correctly = true;
  if (!is_process_alive(dockerd_process_pid)) {
    // The process has already died, do not add callback.
    dockerd_started_correctly = false;
    goto end;
  }

  // Add callback for when the process exits.
  dockerd_process_callback_source =
      g_child_watch_source_new(dockerd_process_pid);
  g_source_set_callback(dockerd_process_callback_source,
                        G_SOURCE_FUNC(dockerd_process_exited_callback),
                        NULL,
                        NULL);
  g_source_attach(dockerd_process_callback_source,
                  g_main_loop_get_context(loop));

  if (!is_process_alive(dockerd_process_pid)) {
    // The process died during adding of callback, tell loop to quit.
    dockerd_started_correctly = false;
    goto end;
  }

end:
  if (!dockerd_started_correctly) {
    // Tell the main process to quit its loop.
    g_main_loop_quit(loop);
  }
}

int
main(void)
{
  GError *error = NULL;
  AXParameter *parameter = NULL;
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

    // This is the child process. Do no cleanup, just exit.
    goto end;
  } else {
    parameter = ax_parameter_new("dockerdwrapper", &error);
    if (parameter == NULL) {
      syslog(LOG_ERR, "Error when creating paramter: %s", error->message);
      exit_code = -1;
      goto end_main_thread;
    }

    gboolean geresult =
        ax_parameter_register_callback(parameter,
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

    geresult = ax_parameter_register_callback(parameter,
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

    /* Run the GLib event loop. */
    loop = g_main_loop_new(NULL, FALSE);
    loop = g_main_loop_ref(loop);

    if (!is_process_alive(dockerd_process_pid)) {
      // The process is already dead, do not add callback.
      goto end_main_thread;
    }

    // Add callback for when the process exits
    dockerd_process_callback_source =
        g_child_watch_source_new(dockerd_process_pid);
    g_source_set_callback(dockerd_process_callback_source,
                          G_SOURCE_FUNC(dockerd_process_exited_callback),
                          NULL,
                          NULL);
    g_source_attach(dockerd_process_callback_source,
                    g_main_loop_get_context(loop));

    if (!is_process_alive(dockerd_process_pid)) {
      // The process died during adding of callback, tell loop to quit.
      g_main_loop_quit(loop);
    }

    g_main_loop_run(loop);
    g_main_loop_unref(loop);
  }

end_main_thread:
  if (stop_dockerd()) {
    syslog(LOG_INFO, "Shutting down. dockerd shut down successfully.");
  } else {
    syslog(LOG_WARNING, "Shutting down. Failed to shut down dockerd.");
  }

  ax_parameter_unregister_callback(parameter,
                                   "root.dockerdwrapper.SDCardSupport");
  ax_parameter_unregister_callback(parameter, "root.dockerdwrapper.UseTLS");

  if (parameter != NULL) {
    ax_parameter_free(parameter);
  }

end:
  g_source_unref(dockerd_process_callback_source);
  g_error_free(error);
  return exit_code;
}
