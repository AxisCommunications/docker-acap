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
#include <linux/limits.h>
#include <mntent.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

// Full path to the SD card
static const char *sd_card_path = "/var/spool/storage/SD_DISK";

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
 * @return True if successful, false if setup failed.
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
      bool sdcard_setup = setup_sdcard();
      if (!sdcard_setup) {
        syslog(LOG_ERR, "Failed to setup SD card.");
        goto end;
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
    }
  } else {
    if (use_sdcard) {
      bool sdcard_setup = setup_sdcard();
      if (!sdcard_setup) {
        syslog(LOG_ERR, "Failed to setup SD card.");
        goto end;
      }

      syslog(LOG_INFO, "Starting unsecured dockerd using SD card as storage.");
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
      }
    }
  }

  return_value = true;

end:
  free(use_sd_card_value);
  free(use_tls_value);

  return return_value;
}

int
main(void)
{
  int exit_code = 0;

  openlog(NULL, LOG_PID, LOG_USER);
  syslog(LOG_INFO, "Started logging.");

  pid_t pid = fork();

  if (pid == -1) {
    syslog(LOG_ERR, "Fork failed.");
    exit_code = -1;
  } else if (pid == 0) {
    bool dockerd_started_correctly = start_dockerd();
    if (!dockerd_started_correctly) {
      syslog(LOG_ERR, "Starting dockerd failed with error %s", strerror(errno));
      exit_code = -1;
    } else {
      syslog(LOG_ERR, "Dockerd exited.");
    }
  } else {
    waitpid(pid, NULL, 0);
    syslog(LOG_INFO, "dockerd exited.");
  }

  return exit_code;
}
