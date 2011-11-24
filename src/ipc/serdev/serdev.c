/*
 *  Showtime mediacenter
 *  Copyright (C) 2010 Andreas Öman
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <glob.h>
#include <unistd.h>
#include <stdlib.h>

#include "config.h"
#include "arch/threads.h"
#include "ipc/ipc.h"
#include "serdev.h"


/**
 *
 */
int
serdev_set(serdev_t *sd, int baudrate)
{
  struct termios tio = {0};

  int cflags = CS8 | CLOCAL | CREAD;

  switch(baudrate) {
  case 9600:
    cflags |= B9600;
    break;
  default:
    return -1;
  }


  tio.c_cflag = cflags;
  tio.c_iflag = IGNPAR;
  tio.c_lflag = ICANON;

  if(tcflush(sd->sd_fd, TCIFLUSH)) {
    perror("tcflush");
    return -1;
  }

  if(tcsetattr(sd->sd_fd, TCSANOW, &tio)) {
    perror("tcsetattr");
    return -1;
  }
  return 0;
}


/**
 *
 */
int
serdev_writef(serdev_t *sd, const char *fmt, ...)
{
  char buf[512];
  int r;
  va_list ap;
  va_start(ap, fmt);
  
  vsnprintf(buf, sizeof(buf), fmt, ap);

  r = write(sd->sd_fd, buf, strlen(buf)) != strlen(buf);
  va_end(ap);
  return r;
}


/**
 *
 */
char *
serdev_readline(serdev_t *sd, int timeout)
{
  int n = 0, r;
  struct pollfd fds;

  while(n < sizeof(sd->sd_buf) - 1) {

    fds.fd = sd->sd_fd;
    fds.events = POLLIN;

    r = poll(&fds, 1, timeout);
    if(r < 1)
      return NULL;

    if(read(sd->sd_fd, &sd->sd_buf[n], 1) != 1)
      break;

    if(sd->sd_buf[n] == 0xd)
      continue;

    if(sd->sd_buf[n] == 0xa) {
      sd->sd_buf[n] = 0;
      return sd->sd_buf;
    }
    n++;
  }
  return NULL;
}


/**
 *
 */
static int
probe_chain(serdev_t *sd)
{
  if(!lgtv_probe(sd))
    return 0;

  return 1;
}


/**
 *
 */
static void *
serthread(void *aux)
{
  serdev_t *sd = aux;

  if(probe_chain(sd)) {
    close(sd->sd_fd);
    free(sd->sd_path);
    free(sd);
  }
  return NULL;
}


/**
 *
 */
static void
opendev(const char *dev)
{
  struct termios tio;
  int fd = open(dev, O_RDWR | O_NOCTTY);
 
  if(fd == -1)
    return;

  if(tcgetattr(fd, &tio)) {
    // Port not working
    close(fd);
    return;
  }

  serdev_t *sd = calloc(1, sizeof(serdev_t));
  sd->sd_fd = fd;
  sd->sd_path = strdup(dev);

  hts_thread_create_detached(sd->sd_path, serthread, sd,
			     THREAD_PRIO_NORMAL);
}




/**
 *
 */
static void
probepattern(const char *pat)
{
  int i;
  glob_t gl;

  if(glob(pat, GLOB_ERR, NULL, &gl))
    return;

  for(i = 0; i < gl.gl_pathc; i++)
    opendev(gl.gl_pathv[i]);

  globfree(&gl);

}


/**
 *
 */
void
serdev_start(void)
{
  probepattern("/dev/ttyS*");
  probepattern("/dev/ttyUSB*");
}
