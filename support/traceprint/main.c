#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>



int
main(void)
{
  struct sockaddr_in sin;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  char buf[2000];
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons(4000);

  if(bind(fd, (struct sockaddr *)&sin, sizeof(sin)))
    perror("bind");

  while(1) {
    int n = read(fd, buf, 2000);
    if(n < 1)
      break;

    write(1, buf, n);
    if(buf[n - 1] != '\n')
      write(1, "\n", 1);
  }
  

}
