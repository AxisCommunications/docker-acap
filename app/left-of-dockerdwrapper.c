#include <arpa/inet.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <glib-object.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "fastcgi_cert_manager.h"

static const char *tls_cert_path = APP_LOCALDATA;

typedef enum {
  PEM_CERT = 0,
  PRIVATE_KEY,
  RSA_PRIVATE_KEY,
  NUM_CERT_TYPES
} cert_types;

typedef struct {
  const char *header;
  const char *footer;
} cert_spec;

static const cert_spec cert_specs[NUM_CERT_TYPES] = {
    {"-----BEGIN CERTIFICATE-----\n", "-----END CERTIFICATE-----\n"},
    {"-----BEGIN PRIVATE KEY-----\n", "-----END PRIVATE KEY-----\n"},
    {"-----BEGIN RSA PRIVATE KEY-----\n", "-----END RSA PRIVATE KEY-----\n"}};

typedef struct {
  const char *name;
  int *type;
} cert;

static int ALL_CERTS[] = {PEM_CERT, -1};
static int ALL_KEYS[] = {PRIVATE_KEY, RSA_PRIVATE_KEY, -1};

static cert tls_certs[] = {{"ca.pem", ALL_CERTS},
                           {"server-cert.pem", ALL_CERTS},
                           {"server-key.pem", ALL_KEYS}};

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
 * @param cert_type pointer to the type(s) to be updated or NULL
 * @return true if valid, false otherwise.
 */
static bool
supported_cert(char *cert_name, int **cert_type)
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
 * @brief Checks if the file has the same header.
 *
 * @param fp FILE pointer for the file to check
 * @param header the type to validate against
 * @return true if found, false otherwise.
 */
static bool
find_header(FILE *fp, const char *header)
{
  char buffer[128];
  size_t toread;
  bool found = false;

  /* Check header */
  toread = strlen(header);
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
  if (strncmp(buffer, header, toread) != 0) {
    syslog_v(LOG_INFO,
             "Expecting %.*s, found %.*s",
             (int)toread,
             header,
             (int)toread,
             buffer);
    goto end;
  }
  found = true;

end:
  return found;
}

/**
 * @brief Checks if the file has the same footer.
 *
 * @param fp FILE pointer for the file to check
 * @param header the type to validate against
 * @return true if found, false otherwise.
 */
static bool
find_footer(FILE *fp, const char *footer)
{
  char buffer[128];
  size_t toread;
  bool found = false;

  /* Check footer */
  toread = strlen(footer);
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
  if (strncmp(buffer, footer, toread) != 0) {
    syslog_v(LOG_INFO,
             "Expecting %.*s, found %.*s",
             (int)toread,
             footer,
             (int)toread,
             buffer);
    goto end;
  }
  found = true;

end:
  return found;
}

/**
 * @brief Checks if the certificate appears valid according to type.
 *
 * @param file_path the certificate to check
 * @param cert_type pointer to the type(s) to validate against
 * @return true if valid, false otherwise.
 */
static bool
valid_cert(char *file_path, int *cert_type)
{
  bool valid = false;
  uint i;

  FILE *fp = fopen(file_path, "r");
  if (fp == NULL) {
    syslog(LOG_ERR, "Could not fopen %s", file_path);
    return false;
  }

  for (i = 0; (cert_type[i] >= 0) && (cert_type[i] < NUM_CERT_TYPES); i++) {
    /* Check for header */
    if (!find_header(fp, cert_specs[cert_type[i]].header)) {
      continue;
    }

    /* Check for corresponding footer */
    if (!find_footer(fp, cert_specs[cert_type[i]].footer)) {
      continue;
    }

    valid = true;
    goto end;
  }
  syslog(LOG_ERR, "No valid header & footer combination found");

end:
  fclose(fp);
  return valid;
}



static gboolean
get_and_verify_tls_selection(bool *use_tls_ret)
{
  ...

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

  const char *data_root =
      use_sdcard ? "/var/spool/storage/SD_DISK/dockerd/data" :
                   "/usr/local/packages/dockerdwrapper/localdata/data";
  args_offset += g_snprintf(
      args + args_offset, args_len - args_offset, " --data-root %s", data_root);

  if (use_sdcard) {
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
  }
  if (!get_ipc_socket_selection(&use_ipc_socket)) {
  }
  if (!get_and_verify_tls_selection(&use_tls)) {
  }
  if (!get_and_verify_sd_card_selection(&use_sdcard)) {
  }

  if (!start_dockerd(use_sdcard, use_tls, use_ipc_socket, use_verbose)) {
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
  ...

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

static void
parameter_changed_callback(const gchar *name,
                           const gchar *value,
                           __attribute__((unused)) gpointer data)
{
  ...
  /* Restart to pick up the parameter change */
  restart();
}

int
main(void)
{
  ...

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

  ...
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
    ...
  }
  fcgi_stop();
  ...
  return exit_code;
}


// MAX: callback for fcgi
void
callback_action(__attribute__((unused)) fcgi_handle handle,
                int request_method,
                char *cert_name,
                char *file_path)
{
  char *cert_file_with_path = NULL;

  /* Is cert supported? */
  int *cert_type;
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
