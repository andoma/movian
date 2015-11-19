#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
  char tmp[PATH_MAX];

  snprintf(tmp, sizeof(tmp), "%s", argv[0]);
  char *x = strrchr(tmp, '/');
  if(x == NULL)
    x = tmp;
  else
    x++;
  snprintf(x, sizeof(tmp) - (x - tmp), "%s", "Movian.bin");
  fprintf(stderr, "Trampoline launching: %s\n", tmp);
  execv(tmp, argv);
}
