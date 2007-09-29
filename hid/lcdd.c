/*
 *  LCDd interface
 *  Copyright (C) 2007 Andreas Öman
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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include "lcdd.h"
#include "coms.h"


static void *
lcdd_thread(void *aux)
{
  FILE *fp;

  while(1) {
    fp = open_fd_tcp("127.0.0.1", 13666, NULL, NULL);
    if(fp == NULL) {
      sleep(1);
      continue;
    }

    fprintf(fp, "hello\n");
    fprintf(fp, "client_set -name showtime\n");
    fprintf(fp, "screen_add showtime\n");
    fprintf(fp, "widget_add showtime w1 string\n");
    fprintf(fp, "widget_add showtime w2 string\n");

    fprintf(fp, "widget_set showtime w1 1 1 \"Showtime\"\n");
    fflush(fp);

    while(1) {
      sleep(1);
    }
    fclose(fp);
  }
}



void
lcdd_init(void)
{
  static pthread_t ptid;
  pthread_create(&ptid, NULL, lcdd_thread, NULL);
}
