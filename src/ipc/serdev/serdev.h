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

#ifndef SERDEV_H__
#define SERDEV_H__

#include <unistd.h>
#include <termios.h>

typedef struct serdev {
  char *sd_path;
  int sd_fd;

  struct termios sd_tio;

  char sd_buf[512];

} serdev_t;

int serdev_writef(serdev_t *sd, const char *fmt, ...);

char *serdev_readline(serdev_t *sd, int timeout);

int serdev_set(serdev_t *sd, int baudrate);

int lgtv_probe(serdev_t *sd);

#endif // SERDEV_H__ 
