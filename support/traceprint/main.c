#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>



int
main(void)
{
  const int tty = isatty(1);

  struct sockaddr_in sin;
  int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  char buf[2000];
  char tmp[100];
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons(4000);

  struct timeval tv;

  if(bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
    perror("bind");
  }

  while(1) {
    struct sockaddr_in sin;
    socklen_t slen = sizeof(struct sockaddr_in);
    int len = recvfrom(fd, buf, 2000, 0, (struct sockaddr *)&sin, &slen);
    if(len < 1)
      break;

    int off = 0;
    const char *pfx = "";
    const char *postfix = "";

    if(buf[0] == '<' && isdigit(buf[1])) {
      int num = atoi(buf+1);
      int pri = num & 7;

      while(buf[off] != '>' && off < len)
        off++;

      off++;

      postfix = tty ? "\033[0m" : "";

      switch(pri) {
      case 0: pfx = "\033[31mEMERG      "; break;
      case 1: pfx = "\033[31mALERT      "; break;
      case 2: pfx = "\033[31mCRITICAL   "; break;
      case 3: pfx = "\033[31mERROR      "; break;
      case 4: pfx = "\033[33mWARNING    "; break;
      case 5: pfx = "\033[34mNOTICE     "; break;
      case 6: pfx = "\033[33mINFO       "; break;
      case 7: pfx = "DEBUG      "; break;
      }

      if(pfx[0] == '\033' && !tty)
        pfx += 5;
    }



    if(off == len)
      continue;

    const char *addr = inet_ntoa(sin.sin_addr);

    write(1, addr, strlen(addr));
    write(1, ": ", 2);

    gettimeofday(&tv, NULL);
    time_t now = tv.tv_sec;
    struct tm *tm = localtime(&now);

    snprintf(tmp, sizeof(tmp), "%02d:%02d:%02d.%03d: ",
             tm->tm_hour,
             tm->tm_min,
             tm->tm_sec,
             (int)tv.tv_usec / 1000);

    write(1, tmp, strlen(tmp));

    write(1, pfx, strlen(pfx));

    write(1, buf + off, len - off);
    if(buf[len - 1] != '\n')
      write(1, "\n", 1);

    write(1, postfix, strlen(postfix));

  }
  

  return 0;
}
