#include <unistd.h>
#include <sys/wait.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>

int main(void)
{
  int exit_code = 0;

  openlog(NULL, LOG_PID, LOG_USER);
  syslog(LOG_INFO, "Started logging.");

  pid_t pid = fork();

  if (pid == -1) {
    syslog(LOG_ERR, "Fork failed.");
    exit_code = -1;
  }
  else if (pid == 0) {
    syslog(LOG_INFO, "Starting dockerd.");

    int result = execv("/usr/local/packages/run_dockerd/dockerd",
      (char*[]){"dockerd", "-H", "tcp://0.0.0.0:2375", (char *) NULL});

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

