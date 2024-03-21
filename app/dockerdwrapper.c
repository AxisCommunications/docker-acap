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

#include <arpa/inet.h>
#include <axsdk/ax_parameter.h>
#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <mntent.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>
#include "fastcgi_cert_manager.h"

#define syslog_v(...)                                                          \
  if (g_verbose) {                                                             \
    syslog(__VA_ARGS__);                                                       \
  }

static bool g_verbose = false;

/**
 * @brief Callback called when a well formed fcgi request is received.
 */
void callback_action(__attribute__((unused)) fcgi_handle handle,
                     int request_method,
                     char *cert_name,
                     char *file_path);

#define APP_NAME "dockerdwrapper"

/** @brief APP paths in a device */
#define APP_DIRECTORY "/usr/local/packages/" APP_NAME
#define APP_LOCALDATA "/usr/local/packages/" APP_NAME "/localdata"

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
#define SD_CARD_PATH "/var/spool/storage/SD_DISK"
static const char *sd_card_path = SD_CARD_PATH;

// All ax_parameters the acap has
static const char *ax_parameters[] = {"IPCSocket",
                                      "SDCardSupport",
                                      "UseTLS",
                                      "Verbose"};

static const char *tls_cert_path = APP_LOCALDATA;

typedef enum { PEM_CERT = 0, RSA_PRIVATE_KEY, NUM_CERT_TYPES } cert_types;

static const char *headers[NUM_CERT_TYPES] = {
    "-----BEGIN CERTIFICATE-----\n",
    "-----BEGIN RSA PRIVATE KEY-----\n"};

static const char *footers[NUM_CERT_TYPES] = {
    "-----END CERTIFICATE-----\n",
    "-----END RSA PRIVATE KEY-----\n"};

typedef struct {
  const char *name;
  int type;
} cert;

static cert tls_certs[] = {{"ca.pem", PEM_CERT},
                           {"server-cert.pem", PEM_CERT},
                           {"server-key.pem", RSA_PRIVATE_KEY}};

#define NUM_TLS_CERTS sizeof(tls_certs) / sizeof(cert)
#define CERT_FILE_MODE 0400
#define READ_WRITE_MODE 0600

typedef enum {
  STOPPING = -1,
  STARTED = 0,
  START_IN_PROGRESS,
  START_PENDING
} status;

static int g_status = START_IN_PROGRESS;

/**
 * @brief Checks if the certificate name is supported
 * and optionally updates the certificate type.
 *
 * @param cert_name the certificate to check
 * @param cert_type the type to be updated or NULL
 * @return true if valid, false otherwise.
 */
static bool
supported_cert(char *cert_name, int *cert_type)
{
  for (size_t i = 0; i < NUM_TLS_CERTS; ++i) {
    if (strcmp(cert_name, tls_certs[i].name) == 0) {
      if (cert_type)
        *cert_type = tls_certs[i].type; /* Update cert_type as well */
      return true;
    }
  }

  syslog(LOG_ERR,
         "The file_name is not supported. Supported names are \"%s\", \"%s\" "
         "and \"%s\".",
         tls_certs[0].name,
         tls_certs[1].name,
         tls_certs[2].name);
  return false;
}

/**
 * @brief Checks if the certificate appears valid according to type.
 *
 * @param file_path the certificate to check
 * @param cert_type the type to validate against
 * @return true if valid, false otherwise.
 */
static bool
valid_cert(char *file_path, int cert_type)
{
  char buffer[128];
  size_t toread;
  bool valid = false;

  FILE *fp = fopen(file_path, "r");
  if (fp == NULL) {
    syslog(LOG_ERR, "Could not fopen %s", file_path);
    return false;
  }

  /* Check header */
  toread = strlen(headers[cert_type]);
  if (fseek(fp, 0, SEEK_SET) != 0) {
    syslog(LOG_ERR,
           "Could not fseek(0, SEEK_SET) bytes, err: %s",
           strerror(errno));
    goto end;
  }
  if (fread(buffer, toread, 1, fp) != 1) {
    syslog(LOG_ERR,
           "Could not fread %d bytes, err: %s",
           (int)toread,
           strerror(errno));
    goto end;
  }
  if (strncmp(buffer, headers[cert_type], toread) != 0) {
    syslog(LOG_ERR, "Invalid header found");
    syslog_v(LOG_INFO,
             "Expected %.*s, found %.*s",
             (int)toread,
             headers[cert_type],
             (int)toread,
             buffer);
    goto end;
  }

  /* Check footer */
  toread = strlen(footers[cert_type]);
  if (fseek(fp, -toread, SEEK_END) != 0) {
    syslog(LOG_ERR,
           "Could not fseek(%d, SEEK_END) bytes, err: %s",
           (int)-toread,
           strerror(errno));
    goto end;
  }
  if (fread(buffer, toread, 1, fp) != 1) {
    syslog(LOG_ERR,
           "Could not fread %d bytes, err: %s",
           (int)toread,
           strerror(errno));
    goto end;
  }
  if (strncmp(buffer, footers[cert_type], toread) != 0) {
    syslog(LOG_ERR, "Invalid footer found");
    syslog_v(LOG_INFO,
             "Expected %.*s, found %.*s",
             (int)toread,
             footers[cert_type],
             (int)toread,
             buffer);
    goto end;
  }
  valid = true;

end:
  fclose(fp);
  return valid;
}

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
    dockerd_process_pid = -1;
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
  AXParameter *ax_parameter = ax_parameter_new("dockerdwrapper", &error);
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
  const char *data_root = SD_CARD_PATH "/dockerd/data";
  const char *exec_root = SD_CARD_PATH "/dockerd/exec";
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
 * @brief Gets and verifies the SDCardSupport selection
 *
 * @param use_sdcard_ret selection to be updated.
 * @return True if successful, false otherwise.
 */
static gboolean
get_and_verify_sd_card_selection(bool *use_sdcard_ret)
{
  gboolean return_value = false;
  bool use_sdcard = *use_sdcard_ret;
  char *use_sd_card_value = get_parameter_value("SDCardSupport");
  char *sd_file_system = NULL;

  if (use_sd_card_value != NULL) {
    bool use_sdcard = strcmp(use_sd_card_value, "yes") == 0;
    if (use_sdcard) {
      // Confirm that the SD card is usable
      sd_file_system = get_sd_filesystem();
      if (sd_file_system == NULL) {
        syslog(LOG_ERR,
               "Couldn't identify the file system of the SD card at %s",
               sd_card_path);
        /* TODO: Sleep and retry on SD not usable? */
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

      gchar card_path[100];
      g_stpcpy(card_path, sd_card_path);
      g_strlcat(card_path, "/dockerd", 100);

      if (access(card_path, F_OK) == 0 && access(card_path, W_OK) != 0) {
        syslog(
            LOG_ERR,
            "The application user does not have write permissions to the SD "
            "card directory at %s. Please change the directory permissions or "
            "remove the directory.",
            card_path);
        goto end;
      }

      if (!setup_sdcard()) {
        syslog(LOG_ERR, "Failed to setup SD card.");
        goto end;
      }
    }
    syslog(LOG_INFO, "SD card set to %d", use_sdcard);
    *use_sdcard_ret = use_sdcard;
    return_value = true;
  }

end:
  free(use_sd_card_value);
  free(sd_file_system);
  return return_value;
}

/**
 * @brief Gets and verifies the UseTLS selection
 *
 * @param use_tls_ret selection to be updated.
 * @return True if successful, false otherwise.
 */
static gboolean
get_and_verify_tls_selection(bool *use_tls_ret)
{
  gboolean return_value = false;
  bool use_tls = *use_tls_ret;
  char *ca_path = NULL;
  char *cert_path = NULL;
  char *key_path = NULL;

  char *use_tls_value = get_parameter_value("UseTLS");
  if (use_tls_value != NULL) {
    use_tls = (strcmp(use_tls_value, "yes") == 0);
    if (use_tls) {
      char *ca_path =
          g_strdup_printf("%s/%s", tls_cert_path, tls_certs[0].name);
      char *cert_path =
          g_strdup_printf("%s/%s", tls_cert_path, tls_certs[1].name);
      char *key_path =
          g_strdup_printf("%s/%s", tls_cert_path, tls_certs[2].name);

      bool ca_exists = access(ca_path, F_OK) == 0;
      bool cert_exists = access(cert_path, F_OK) == 0;
      bool key_exists = access(key_path, F_OK) == 0;

      if (!ca_exists || !cert_exists || !key_exists) {
        syslog(LOG_ERR,
               "One or more TLS certificates missing. Use cert_manager.cgi to "
               "upload valid certificates.");
      }

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

      bool ca_read_only = chmod(ca_path, CERT_FILE_MODE) == 0;
      bool cert_read_only = chmod(cert_path, CERT_FILE_MODE) == 0;
      bool key_read_only = chmod(key_path, CERT_FILE_MODE) == 0;

      if (!ca_read_only) {
        syslog(LOG_ERR,
               "Cannot start using TLS, CA certificate not read only: %s",
               ca_path);
      }
      if (!cert_read_only) {
        syslog(LOG_ERR,
               "Cannot start using TLS, server certificate not read only: %s",
               cert_path);
      }
      if (!key_read_only) {
        syslog(LOG_ERR,
               "Cannot start using TLS, server key not read only: %s",
               key_path);
      }

      if (!ca_read_only || !cert_read_only || !key_read_only) {
        goto end;
      }
    }
    syslog(LOG_INFO, "TLS set to %d", use_tls);
    *use_tls_ret = use_tls;
    return_value = true;
  }

end:
  free(use_tls_value);
  free(ca_path);
  free(cert_path);
  free(key_path);
  return return_value;
}

/**
 * @brief Gets and verifies the IPCSocket selection
 *
 * @param use_ipc_socket_ret selection to be updated.
 * @return True if successful, false otherwise.
 */
static gboolean
get_ipc_socket_selection(bool *use_ipc_socket_ret)
{
  gboolean return_value = false;
  bool use_ipc_socket = *use_ipc_socket_ret;
  char *use_ipc_socket_value = get_parameter_value("IPCSocket");
  if (use_ipc_socket_value != NULL) {
    use_ipc_socket = strcmp(use_ipc_socket_value, "yes") == 0;
    syslog(LOG_INFO, "IPC Socket set to %d", use_ipc_socket);
    *use_ipc_socket_ret = use_ipc_socket;
    return_value = true;
  }
  free(use_ipc_socket_value);
  return return_value;
}

/**
 * @brief Gets the Verbose selection
 *
 * @param use_verbose_ret selection to be updated.
 * @return True if successful, false otherwise.
 */
static gboolean
get_verbose_selection(bool *use_verbose_ret)
{
  gboolean return_value = false;
  bool use_verbose = *use_verbose_ret;
  char *use_verbose_value = get_parameter_value("Verbose");
  if (use_verbose_value != NULL) {
    use_verbose = strcmp(use_verbose_value, "yes") == 0;
    syslog(LOG_INFO, "Verbose set to %d", use_verbose);
    *use_verbose_ret = use_verbose;
    g_verbose = use_verbose;
    return_value = true;
  }
  free(use_verbose_value);
  return return_value;
}

/**
 * @brief Start a new dockerd process.
 *
 * @param use_sdcard start option.
 * @param use_tls start option.
 * @param use_ipc_socket start option.
 * @param use_verbose start option.
 * @return True if successful, false otherwise
 */
static bool
start_dockerd(bool use_sdcard,
              bool use_tls,
              bool use_ipc_socket,
              bool use_verbose)
{
  syslog_v(LOG_INFO, "start_dockerd: %d", g_status);

  syslog(LOG_INFO,
         "starting dockerd with settings: use_sdcard %d, use_tls %d, "
         "use_ipc_socket %d, use_verbose %d",
         use_sdcard,
         use_tls,
         use_ipc_socket,
         use_verbose);
  GError *error = NULL;

  bool return_value = false;
  bool result = false;

  gsize args_len = 1024;
  gsize msg_len = 128;
  gchar args[args_len];
  gchar msg[msg_len];
  guint args_offset = 0;
  gchar **args_split = NULL;

  // get host ip
  char host_buffer[256];
  char *IPbuffer;
  struct hostent *host_entry;
  gethostname(host_buffer, sizeof(host_buffer));
  host_entry = gethostbyname(host_buffer);
  IPbuffer = inet_ntoa(*((struct in_addr *)host_entry->h_addr_list[0]));

  // construct the rootlesskit command
  args_offset += g_snprintf(args + args_offset,
                            args_len - args_offset,
                            "%s %s %s %s %s %s %s %s %s",
                            "rootlesskit",
                            "--subid-source=static",
                            "--net=slirp4netns",
                            "--disable-host-loopback",
                            "--copy-up=/etc",
                            "--copy-up=/run",
                            "--propagation=rslave",
                            "--port-driver slirp4netns",
                            /* don't use same range as company proxy */
                            "--cidr=10.0.3.0/24");

  if (use_verbose) {
    args_offset += g_snprintf(
        args + args_offset, args_len - args_offset, " %s", "--debug");
  }

  const uint port = use_tls ? 2376 : 2375;
  args_offset += g_snprintf(args + args_offset,
                            args_len - args_offset,
                            " -p %s:%d:%d/tcp",
                            IPbuffer,
                            port,
                            port);

  // add dockerd arguments
  args_offset += g_snprintf(args + args_offset,
                            args_len - args_offset,
                            " %s %s %s",
                            "dockerd",
                            "--iptables=false",
                            "--config-file " APP_LOCALDATA "/daemon.json");

  if (!use_verbose) {
    args_offset += g_snprintf(
        args + args_offset, args_len - args_offset, " %s", "--log-level=warn");
  }

  g_strlcpy(msg, "Starting dockerd", msg_len);

  if (use_tls) {
    const char *ca_path = APP_LOCALDATA "/ca.pem";
    const char *cert_path = APP_LOCALDATA "/server-cert.pem";
    const char *key_path = APP_LOCALDATA "/server-key.pem";

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
    uid_t uid = getuid();
    uid_t gid = getgid();
    // The socket should reside in the user directory and have same group as
    // user
    args_offset += g_snprintf(args + args_offset,
                              args_len - args_offset,
                              " %s %d %s%d%s",
                              "--group",
                              gid,
                              "-H unix:///var/run/user/",
                              uid,
                              "/docker.sock");
    g_strlcat(msg, " with IPC socket.", msg_len);
  } else {
    g_strlcat(msg, " without IPC socket.", msg_len);
  }

  // Log startup information to syslog.
  syslog(LOG_INFO, "%s", msg);
  syslog(LOG_INFO, "%s", args); // TODO Remove this before release of rootless

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

  /* But being alive at this point DOESN'T mean we are up and stable. Sleep for
   * now..*/
  int post_watch_add_secs = 15;
  sleep(post_watch_add_secs);
  syslog_v(LOG_INFO,
           "start_dockerd: TODO: Alive but not up and stable? sleep(%d)",
           post_watch_add_secs);

  g_status = STARTED;
  return_value = true;

end:
  g_strfreev(args_split);
  g_clear_error(&error);
  return return_value;
}

/**
 * @brief Attempt to kill process and verify success with specified time.
 *
 * @param process_id pointer to the process id to kill.
 * @param sig the signal to attempt the kill with.
 * @param secs the maximum time to wait for verification.
 * @return exit_code. 0 if successful, -1 otherwise
 */
static int
kill_and_verify(int *process_id, uint sig, uint secs)
{
  int pid = *process_id;
  int exit_code;
  if ((exit_code = kill(pid, sig)) != 0) {
    syslog(LOG_INFO,
           "Failed to send %d to process %d. Error: %s",
           sig,
           pid,
           strerror(errno));
    errno = 0;
    return exit_code;
  }

  uint i = 0;
  while (i++ < secs) {
    sleep(1);
    if (*process_id == -1) { /* Set in process exited callback */
      syslog_v(LOG_INFO,
               "kill_and_verify: stopped(%d) pid %d after %d secs",
               sig,
               pid,
               i);
      return 0;
    }
  }

  syslog_v(LOG_INFO, "Failed to stop(%d) pid %d after %d secs", sig, pid, secs);
  return -1;
}

/**
 * @brief Stop the currently running dockerd process.
 *
 * @return exit_code. 0 if successful, -1 otherwise
 */
static int
stop_dockerd(void)
{
  if (dockerd_process_pid == -1) {
    /* Nothing to stop. */
    exit_code = 0;
    goto end;
  }

  /* Send SIGTERM to the process, wait up to 10 secs */
  if ((exit_code = kill_and_verify(&dockerd_process_pid, SIGTERM, 10)) == 0) {
    goto end;
  }
  syslog(LOG_WARNING, "Failed to send and verify SIGTERM to child");

  /* SIGTERM failed, try SIGKILL instead, wait up to 10 secs  */
  if ((exit_code = kill_and_verify(&dockerd_process_pid, SIGKILL, 10)) == 0) {
    goto end;
  }
  syslog_v(LOG_INFO, "Ignoring apparent failed SIGKILL to child");
  exit_code = 0;

end:
  if (g_status > STARTED) {
    /* Restart in progress and|or pending. Continue (quit the main loop).. */
    g_main_loop_quit(loop);
  }

  return exit_code;
}

/**
 * @brief Stop dockerd and quit the main loop (to effect a restart).
 *
 * @param quit. Quits main loop if true, otherwise just stops.
 */
static void
stop_and_quit_main_loop(bool quit)
{
  if (!is_process_alive(dockerd_process_pid)) {
    /* dockerd was not started. Just start (quit the main loop) */
    exit_code = 0;
    if (quit) {
      g_main_loop_quit(loop);
    }
  } else {
    /* Stop the current dockerd process before restarting */
    if ((exit_code = stop_dockerd()) != 0) {
      syslog(LOG_ERR, "Failed to stop dockerd process");
      if (quit) {
        g_main_loop_quit(loop);
      }
    }
  }
}

/**
 * @brief Start fcgi and dockerd. Called from outside the main loop.
 *
 * @return exit_code. 0 if successful, -1 otherwise
 */
static int
start(void)
{
  bool use_sdcard = false;
  bool use_tls = false;
  bool use_ipc_socket = false;
  bool use_verbose = false;

  if (g_status > STARTED) {
    /* Restarting. Sleep previously part of stop_dockerd.. */
    syslog_v(
        LOG_INFO,
        "TODO: Child processes cleaned up already? sleep(10) before starting");
    sleep(10);
  }

  if (!get_verbose_selection(&use_verbose)) {
    syslog(
        LOG_INFO,
        "Failed to get verbose selection. Uninstall and reinstall the acap?");
    exit_code = -1;
    goto end;
  }
  if (!get_ipc_socket_selection(&use_ipc_socket)) {
    syslog(LOG_INFO,
           "Failed to get ipc socket selection. Uninstall and reinstall the "
           "acap?");
    exit_code = -1;
    goto end;
  }
  if (!get_and_verify_tls_selection(&use_tls)) {
    syslog(LOG_INFO, "Failed to verify tls selection");
    goto fcgi; /* do not start dockerd */
  }
  if (!get_and_verify_sd_card_selection(&use_sdcard)) {
    syslog(LOG_INFO, "Failed to setup sd_card");
    goto fcgi; /* do not start dockerd */
  }

  if (!start_dockerd(use_sdcard, use_tls, use_ipc_socket, use_verbose)) {
    syslog(LOG_ERR, "Failed to start dockerd");
    goto fcgi; /* could not start dockerd */
  }

fcgi:
  /* Start fcgi to cert_manager*/
  if (fcgi_start(callback_action, use_verbose) != 0) {
    syslog(LOG_ERR, "Failed to init fcgi_start with callback method");
    exit_code = -1;
    goto end;
  }

end:
  /* Update status. Start again if START_PENDING and no errors */
  if ((exit_code == 0) && (g_status-- > STARTED)) {
    if (g_status > STARTED) {
      stop_and_quit_main_loop(/* No need to quit main loop */ false);
      return start();
    }
  } else {
    g_status = exit_code;
  }

  syslog_v(
      LOG_INFO, "start: -> g_status %d, exit_code %d", g_status, exit_code);
  return exit_code;
}

/**
 * @brief Restart fcgi and dockerd. Called from inside the main loop.
 */
static void
restart(void)
{
  /* Check and update status */
  if (g_status > STARTED) {
    g_status = START_PENDING;
    return;
  } else if (g_status < STARTED) {
    syslog(LOG_ERR, "Unable to restart, status %d", g_status);
    return;
  }
  g_status = START_IN_PROGRESS;

  /* Stop dockerd and quit the main loop to effect a restart */
  syslog_v(LOG_INFO, "restart: -> %d", g_status);
  stop_and_quit_main_loop(true);
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
  syslog_v(LOG_INFO, "dockerd_process_exited_callback called: %d", g_status);

  /* Sanity check of pid */
  if (pid != dockerd_process_pid) {
    syslog(LOG_WARNING,
           "TODO: Fix required? Expecting pid %d, found pid %d: ",
           dockerd_process_pid,
           pid);
    return;
  }

  if (status == 0) {
    /* Graceful exit. All good.. */
    exit_code = 0;
  } else if ((status == SIGKILL) || (status == SIGTERM)) {
    /* Likely here as a result of stop_dockerd().. */
    syslog_v(LOG_INFO,
             "stop_dockerd instigated %s exit",
             (status == SIGKILL) ? "SIGKILL" : "SIGTERM");
    exit_code = 0;
  } else if (!g_spawn_check_wait_status(status, &error)) {
    /* Something went wrong..*/
    syslog(LOG_ERR,
           "Dockerd process exited with status: %d, error: %s",
           status,
           error->message);
    g_clear_error(&error);
    exit_code = -1;
  } else {
    /* Not clear. Log as warning and continue..*/
    syslog(LOG_WARNING, "Dockerd process exited with status: %d", status);
  }

  g_spawn_close_pid(pid);

  // The lockfile might have been left behind if dockerd shut down in a bad
  // manner. Remove it manually.
  uid_t uid = getuid();
  char *pid_path = g_strdup_printf("/var/run/user/%d/docker.pid", uid);
  remove(pid_path);
  free(pid_path);

  /* Stop if exit was unexpected, otherwise continue */
  dockerd_process_pid = -1;
  if (exit_code != 0) {
    g_main_loop_quit(loop);
  }
}

/**
 * @brief Callback function called when any of the parameters
 * changes. Will restart processes with the new setting.
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

  bool unknown_parameter = true;
  for (size_t i = 0; i < sizeof(ax_parameters) / sizeof(ax_parameters[0]);
       ++i) {
    if (strcmp(parname, ax_parameters[i]) == 0) {
      syslog(LOG_INFO, "%s changed to: %s", ax_parameters[i], value);
      unknown_parameter = false;
    }
  }

  if (unknown_parameter) {
    syslog(LOG_WARNING, "Parameter %s is not recognized", name);
    /* No known parameter was changed, nothing to do */
    return;
  }

  /* Restart to pick up the parameter change */
  restart();
}

static AXParameter *
setup_axparameter(void)
{
  bool success = false;
  GError *error = NULL;
  AXParameter *ax_parameter = ax_parameter_new("dockerdwrapper", &error);
  if (ax_parameter == NULL) {
    syslog(LOG_ERR, "Error when creating AXParameter: %s", error->message);
    goto end;
  }

  for (size_t i = 0; i < sizeof(ax_parameters) / sizeof(ax_parameters[0]);
       ++i) {
    char *parameter_path =
        g_strdup_printf("%s.%s", "root.dockerdwrapper", ax_parameters[i]);
    gboolean geresult = ax_parameter_register_callback(
        ax_parameter, parameter_path, parameter_changed_callback, NULL, &error);
    free(parameter_path);

    if (geresult == FALSE) {
      syslog(LOG_ERR,
             "Could not register %s callback. Error: %s",
             ax_parameters[i],
             error->message);
      goto end;
    }
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

  // Get UID of the current user
  uid_t uid = getuid();

  char path[strlen(APP_DIRECTORY) + 256];
  sprintf(path,
          "/bin:/usr/bin:%s:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin",
          APP_DIRECTORY);

  char docker_host[256];
  sprintf(docker_host, "unix://run/user/%d/docker.sock", (int)uid);

  char xdg_runtime_dir[256];
  sprintf(xdg_runtime_dir, "/run/user/%d", (int)uid);

  // Set environment variables
  if (setenv("PATH", path, 1) != 0) {
    syslog(LOG_ERR, "Error setting environment PATH.");
    return -1;
  }

  if (setenv("HOME", APP_DIRECTORY, 1) != 0) {
    syslog(LOG_ERR, "Error setting environment APP_LOCATION.");
    return -1;
  }

  if (setenv("DOCKER_HOST", docker_host, 1) != 0) {
    syslog(LOG_ERR, "Error setting environment DOCKER_HOST.");
    return -1;
  }

  if (setenv("XDG_RUNTIME_DIR", xdg_runtime_dir, 1) != 0) {
    syslog(LOG_ERR, "Error setting environment XDG_RUNTIME_DIR.");
    return -1;
  }

  syslog(LOG_INFO, "PATH: %s", path);
  syslog(LOG_INFO, "HOME: %s", APP_DIRECTORY);
  syslog(LOG_INFO, "DOCKER_HOST: %s", docker_host);
  syslog(LOG_INFO, "XDG_RUNTIME_DIR: %s", xdg_runtime_dir);

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

main_loop:
  loop = g_main_loop_ref(loop);

  /* Start fcgi and dockerd */
  if ((exit_code = start()) != 0) {
    goto end;
  }

  /* Run the GLib event loop. */
  g_main_loop_run(loop);

  if (exit_code == 0) {
    /* Restart */
    goto main_loop;
  }
  g_main_loop_unref(loop);

end:
  /* Cleanup */
  g_status = STOPPING;
  if (!is_process_alive(dockerd_process_pid)) {
    if ((exit_code = stop_dockerd()) == 0) {
      syslog(LOG_INFO, "Shutting down. dockerd shut down successfully.");
    } else {
      syslog(LOG_WARNING, "Shutting down. Failed to shut down dockerd.");
    }
  }
  fcgi_stop();
  if (ax_parameter != NULL) {
    syslog_v(LOG_INFO, "Shutting down. unregistering ax_parameter callbacks.");
    for (size_t i = 0; i < sizeof(ax_parameters) / sizeof(ax_parameters[0]);
         ++i) {
      char *parameter_path =
          g_strdup_printf("%s.%s", "root.dockerdwrapper", ax_parameters[i]);
      ax_parameter_unregister_callback(ax_parameter, parameter_path);
      free(parameter_path);
    }
    ax_parameter_free(ax_parameter);
  }

  g_clear_error(&error);
  if (exit_code != 0) {
    syslog(LOG_ERR, "Please restart the acap manually.");
  }
  return exit_code;
}

void
callback_action(__attribute__((unused)) fcgi_handle handle,
                int request_method,
                char *cert_name,
                char *file_path)
{
  char *cert_file_with_path = NULL;

  /* Is cert supported? */
  int cert_type;
  if (!supported_cert(cert_name, &cert_type)) {
    goto end;
  }

  /* Action requested method */
  switch (request_method) {
    case POST:
      /* Is cert valid? */
      if (!valid_cert(file_path, cert_type)) {
        goto end;
      }

      /* If cert already exists make writeable */
      cert_file_with_path = g_strdup_printf("%s/%s", tls_cert_path, cert_name);
      if (g_file_test(cert_file_with_path, G_FILE_TEST_EXISTS)) {
        if (chmod(cert_file_with_path, READ_WRITE_MODE) != 0) {
          syslog(LOG_ERR,
                 "Failed to make %s writeable, err: %s",
                 cert_file_with_path,
                 strerror(errno));
          goto end;
        }
      }

      /* Copy cert, overwriting any existing, and restore mode */
      syslog(LOG_INFO, "Moving %s to %s", file_path, cert_file_with_path);
      GFile *source = g_file_new_for_path(file_path);
      GFile *destination = g_file_new_for_path(cert_file_with_path);
      GError *error = NULL;
      if (!g_file_copy(source,
                       destination,
                       G_FILE_COPY_OVERWRITE,
                       NULL,
                       NULL,
                       NULL,
                       &error)) {
        syslog(LOG_ERR,
               "Failed to copy %s to %s, err: %s",
               file_path,
               cert_file_with_path,
               error->message);
        g_error_free(error);
        goto post_end;
      }
      if (chmod(cert_file_with_path, CERT_FILE_MODE) != 0) {
        syslog(LOG_ERR,
               "Failed to make %s readonly, err: %s",
               cert_file_with_path,
               strerror(errno));
      }
    post_end:
      g_object_unref(source);
      g_object_unref(destination);
      break;

    case DELETE:
      /* Delete cert */
      cert_file_with_path = g_strdup_printf("%s/%s", tls_cert_path, cert_name);
      syslog(LOG_INFO, "Removing %s", cert_file_with_path);
      if (g_file_test(cert_file_with_path, G_FILE_TEST_EXISTS)) {
        if (g_remove(cert_file_with_path) != 0) {
          syslog(LOG_ERR,
                 "Failed to remove %s from %s.",
                 cert_name,
                 tls_cert_path);
          goto end;
        }
      }
      break;

    default:
      syslog(LOG_ERR, "Unsupported request method %i", request_method);
      goto end;
  }

  /* Restart to pick up the certificate change */
  restart();

end:
  /* Cleanup */
  free(cert_file_with_path);
  if (file_path && (/* Delete original cert */ g_remove(file_path) != 0)) {
    syslog(LOG_ERR, "Failed to remove %s, err: %s", file_path, strerror(errno));
  }
}
