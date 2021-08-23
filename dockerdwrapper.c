#include <errno.h>
#include <glib.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

static int
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

  return res;
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
    int result = 0;
    struct stat statbuf;
    bool use_sdcard =
        system("parhandclient get root.dockerdwrapper.SDCardSupport | grep yes "
               "-q") == 0;
    if (!stat("/usr/local/packages/dockerdwrapper/server-key.pem", &statbuf)) {
      if (use_sdcard) {
        result = setup_sdcard();
        if (result != 0) {
          syslog(LOG_ERR, "Failed to setup SD card. Error code: %d", result);
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
      }
    } else {
      if (use_sdcard) {
        int result = setup_sdcard();
        if (result != 0) {
          syslog(LOG_ERR, "Failed to setup SD card. Error code: %d", result);
        }
        syslog(LOG_INFO, "Starting unsecured dockerd.");
        result = execv("/usr/local/packages/dockerdwrapper/dockerd",
                       (char *[]){"dockerd",
                                  "-H",
                                  "tcp://0.0.0.0:2375",
                                  "-H",
                                  "unix:///var/run/docker.sock",
                                  "--data-root",
                                  "/var/spool/storage/SD_DISK/dockerd/data",
                                  "--exec-root",
                                  "/var/spool/storage/SD_DISK/dockerd/exec",
                                  (char *)NULL});
      } else {
        result = execv("/usr/local/packages/dockerdwrapper/dockerd",
                       (char *[]){"dockerd",
                                  "-H",
                                  "unix:///var/run/docker.sock",
                                  "-H",
                                  "tcp://0.0.0.0:2375",
                                  (char *)NULL});
      }
    }
    if (result == -1) {
      syslog(LOG_ERR, "Starting dockerd failed with error %s", strerror(errno));
      exit_code = -1;
    }
  } else {
    waitpid(pid, NULL, 0);
    syslog(LOG_INFO, "dockerd exited.");
  }

end:
  return exit_code;
}
