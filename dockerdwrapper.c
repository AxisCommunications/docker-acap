#include <linux/magic.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <syslog.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
  int exit_code = 0;

  openlog(NULL, LOG_PID, LOG_USER);
  syslog(LOG_INFO, "Started logging.");

  bool use_sdcard = system("parhandclient get root.dockerdwrapper.SDCardSupport | grep yes -q") == 0;
  if (use_sdcard) {
    struct statfs sd_disk_stat;
    int stat_result = statfs("/var/spool/storage/SD_DISK", &sd_disk_stat);
    if (stat_result != 0) {
      syslog(LOG_ERR, "Cannot use SD_CARD, no storage exists at /var/spool/storage/SD_DISK");
      return -1;
    }
    if (sd_disk_stat.f_type != EXT4_SUPER_MAGIC) {
      syslog(LOG_ERR, "The sd card at /var/spool/storage/SD_DISK uses another file system than ext4. Only ext4 is supported when running dockerdwrapper on the sd card.");
      return -1;
    }
  }

  pid_t pid = fork();

  if (pid == -1) {
    syslog(LOG_ERR, "Fork failed.");
    exit_code = -1;
  }
  else if (pid == 0) {
    int result;
    struct stat statbuf;
    if (!stat("/usr/local/packages/dockerdwrapper/server-key.pem", &statbuf)) {
        syslog(LOG_INFO, "Starting dockerd in TLS mode.");
        result = execv("/usr/local/packages/dockerdwrapper/dockerd",
          (char*[]){"dockerd", "-H", "tcp://0.0.0.0:2376", "--tlsverify",
                    "--tlscacert=/usr/local/packages/dockerdwrapper/ca.pem",
                    "--tlscert=/usr/local/packages/dockerdwrapper/server-cert.pem",
                    "--tlskey=/usr/local/packages/dockerdwrapper/server-key.pem",
                    (char *) NULL});
    } else {
        syslog(LOG_INFO, "Starting unsecured dockerd.");
        result = execv("/usr/local/packages/dockerdwrapper/dockerd",
          (char*[]){"dockerd", "-H", "tcp://0.0.0.0:2375", (char *) NULL});
    }

    if (result == -1) {
      syslog(LOG_ERR, "Starting dockerd failed with error %s", strerror(errno));
      exit_code = -1;
    }
  }
  else {
    waitpid(pid, NULL, 0);
    syslog(LOG_INFO, "dockerd exited.");
  }

  return exit_code;
}
