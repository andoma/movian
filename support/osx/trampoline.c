#include <syslog.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
  char tmp[PATH_MAX];
  char upgrade[PATH_MAX];

  snprintf(tmp, sizeof(tmp), "%s", argv[0]);
  char *x = strrchr(tmp, '/');
  if(x == NULL)
    x = tmp;
  else
    x++;
  snprintf(x, sizeof(tmp) - (x - tmp), "%s", "Movian.bin");

  const char *home = getenv("HOME");
  snprintf(upgrade, sizeof(upgrade), "%s/.hts/showtime/movian-upgrade.bin", home);
  setenv("UPGRADE_BINARY_PATH", upgrade, 1);

  openlog("Movian-Launcher", LOG_PID | LOG_NDELAY, LOG_SYSLOG);

  if(!(access(upgrade, X_OK))) {
    syslog(LOG_NOTICE, "Launching %s", upgrade);
    closelog();
    execv(upgrade, argv);
    openlog("Movian-Launcher", LOG_PID | LOG_NDELAY, LOG_SYSLOG);
    syslog(LOG_NOTICE, "Failed to launch upgrade -- %s\n", strerror(errno));
  }
  syslog(LOG_NOTICE, "Launching %s", tmp);
  closelog();
  execv(tmp, argv);
}
