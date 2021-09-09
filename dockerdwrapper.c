#include <axsdk/axparameter.h>
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

int
main(void)
{
  int exit_code = 0;
  GError *error = NULL;
  char *sd_card_support_value = NULL;

  openlog(NULL, LOG_PID, LOG_USER);
  syslog(LOG_INFO, "Started logging.");

  // Fetch SDCardSupport parameter value
  AXParameter *parameter = NULL;
  parameter = ax_parameter_new("dockerdwrapper", &error);
  if (!ax_parameter_get(
          parameter, "SDCardSupport", &sd_card_support_value, &error)) {
    exit_code = -1;
    goto end;
  }

  bool use_sdcard = strcmp(sd_card_support_value, "yes") == 0;
  if (use_sdcard) {
    // Confirm that the SD card is usable
    char *sd_file_system = get_sd_filesystem();
    if (sd_file_system == NULL) {
      syslog(LOG_ERR,
             "Couldn't identify the file system of the SD card at %s",
             sd_card_path);
    }

    if (strcmp(sd_file_system, "vfat") == 0) {
      syslog(LOG_ERR,
             "The SD card at %s uses file system %s "
             "which does not support Unix file permissions. Please reformat to "
             "a file system that support Unix file permissions, such as ext4 "
             "or xfs.",
             sd_card_path,
             sd_file_system);
      exit_code = -1;
      goto end;
    }
  }

  pid_t pid = fork();

  if (pid == -1) {
    syslog(LOG_ERR, "Fork failed.");
    exit_code = -1;
  } else if (pid == 0) {
    int result;
    struct stat statbuf;
    if (!stat("/usr/local/packages/dockerdwrapper/server-key.pem", &statbuf)) {
      syslog(LOG_INFO, "Starting dockerd in TLS mode.");
      result = execv(
          "/usr/local/packages/dockerdwrapper/dockerd",
          (char *[]){
              "dockerd",
              "-H",
              "tcp://0.0.0.0:2376",
              "--tlsverify",
              "--tlscacert=/usr/local/packages/dockerdwrapper/ca.pem",
              "--tlscert=/usr/local/packages/dockerdwrapper/server-cert.pem",
              "--tlskey=/usr/local/packages/dockerdwrapper/server-key.pem",
              (char *)NULL});
    } else {
      syslog(LOG_INFO, "Starting unsecured dockerd.");
      result = execv(
          "/usr/local/packages/dockerdwrapper/dockerd",
          (char *[]){"dockerd", "-H", "tcp://0.0.0.0:2375", (char *)NULL});
    }

    if (result == -1) {
      syslog(LOG_ERR, "Starting dockerd failed with error %s", strerror(errno));
      exit_code = -1;
      goto end;
    }
  } else {
    waitpid(pid, NULL, 0);
    syslog(LOG_INFO, "dockerd exited.");
  }

end:
  if (error) {
    syslog(LOG_INFO, "Failed %s", error->message);
    g_error_free(error);
  }
  ax_parameter_free(parameter);

  return exit_code;
}
