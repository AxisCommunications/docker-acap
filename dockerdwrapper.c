#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

int main(void)
{
  int exit_code = 0;

  openlog(NULL, LOG_PID, LOG_USER);
  syslog(LOG_INFO, "Started logging.");

  pid_t pid = fork();

  if (pid == -1)
  {
    syslog(LOG_ERR, "Fork failed.");
    exit_code = -1;
  }
  else if (pid == 0)
  {
    bool use_tls = system("parhandclient get root.dockerdwrapper.TLS | grep yes -q") == 0;
    int result;
    if (use_tls)
    {
      const char *ca_path = "/usr/local/packages/dockerdwrapper/ca.pem";
      const char *cert_path = "/usr/local/packages/dockerdwrapper/server-cert.pem";
      const char *key_path = "/usr/local/packages/dockerdwrapper/server-key.pem";

      bool ca_exists = access(ca_path, F_OK) == 0;
      bool cert_exists = access(cert_path, F_OK) == 0;
      bool key_exists = access(key_path, F_OK) == 0;

      if (!ca_exists)
      {
        syslog(LOG_ERR, "Cannot start using TLS, no CA certificate found at %s", ca_path);
      }
      if (!cert_exists)
      {
        syslog(LOG_ERR, "Cannot start using TLS, no server certificate found at %s", cert_path);
      }
      if (!key_exists)
      {
        syslog(LOG_ERR, "Cannot start using TLS, no server key found at %s", key_path);
      }

      if (!ca_exists || !cert_exists || !key_exists)
      {
        exit_code = -1;
        goto end;
      }

      syslog(LOG_INFO, "Starting dockerd in TLS mode.");
      result = execv("/usr/local/packages/dockerdwrapper/dockerd",
                     (char *[]){"dockerd", "-H", "tcp://0.0.0.0:2376", "--tlsverify",
                                "--tlscacert=/usr/local/packages/dockerdwrapper/ca.pem",
                                "--tlscert=/usr/local/packages/dockerdwrapper/server-cert.pem",
                                "--tlskey=/usr/local/packages/dockerdwrapper/server-key.pem",
                                (char *)NULL});
    }
    else if (!use_tls)
    {
      syslog(LOG_INFO, "Starting unsecured dockerd.");
      result = execv("/usr/local/packages/dockerdwrapper/dockerd",
                     (char *[]){"dockerd", "-H", "tcp://0.0.0.0:2375", (char *)NULL});
    }

    if (result == -1)
    {
      syslog(LOG_ERR, "Starting dockerd failed with error %s", strerror(errno));
      exit_code = -1;
    }
  }
  else
  {
    waitpid(pid, NULL, 0);
    syslog(LOG_INFO, "dockerd exited.");
  }

end:
  return exit_code;
}
