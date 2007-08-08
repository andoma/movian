/*
 *  Functions for communicating with HTS tvheadend
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

#ifndef TVHEADEND_H
#define TVHEADEND_H

#include <netinet/in.h>
#include <arpa/inet.h>
#include <libglw/glw.h>
#include "showtime.h"
#include "hid/input.h"

typedef struct tvstatus {

  int tvs_status;    /* set to 1 if stuff is ok */
  char tvs_info[40];
  int tvs_snr;
  int tvs_ber;
  int tvs_uncorr;
  int tvs_cc_errors;
  int tvs_local_errors;
  char tvs_transport[40];
  char tvs_adapter[40];
} tvstatus_t;


typedef struct tvheadend {
  FILE *tvh_fp;
  struct sockaddr_in tvh_localaddr;
  pthread_mutex_t tvh_mutex;


  void (*tvh_data_callback)(struct tvheadend *tvh, uint8_t *buf,
			    unsigned int len);

  void (*tvh_status_callback)(struct tvheadend *tvh, int chindex,
			      tvstatus_t *tvs);

  struct sockaddr_in tvh_receptor;

  average_t tvh_byterate;

  int tvh_fd;
 
  /* report tag refresh via this input */

  ic_t *tvh_ic;

} tvheadend_t;

typedef struct tvprogramme {
  int tvp_index;
  char tvp_title[100];
  time_t tvp_start;
  time_t tvp_stop;
  char tvp_desc[1000];
  int tvp_reftag;
  char tvp_pvrstatus;
  char tvp_timetxt[20];
  char tvp_lentxt[20];
  char tvp_weekday[20];
  char tvp_filename[300];
  int tvp_channel;

} tvprogramme_t;

typedef struct tvchannel {
  char tvc_displayname[40];
  char tvc_icon[200];
  int tvc_reftag;
} tvchannel_t;

typedef struct tvhpkt {
  TAILQ_ENTRY(tvhpkt) tp_link;
  uint8_t *tp_buf;
  int tp_len;
} tvhpkt_t;



char *tvh_query(tvheadend_t *tvh, const char *fmt, ...);

void tvh_init(tvheadend_t *tvh, ic_t *input);


int tvh_int(char *r);

int tvh_get_channel(tvheadend_t *tvh, tvchannel_t *tvc, int channel);

int tvh_get_programme(tvheadend_t *tvh, tvprogramme_t *tvp, int channel, 
		      int prog);

glw_t *tvh_create_chicon(tvchannel_t *tvc, glw_t *parent, float weight);

void tvh_create_meta(tvheadend_t *tvh, glw_t *parent);

glw_t *tvh_create_pvrstatus(glw_t *parent, char pvrstatus, float weight);

int tvh_get_pvrlog(tvheadend_t *tvh, tvprogramme_t *tvp, int entry, 
		   int istag);



extern inline const char *
propcmp(const char *a, const char *b)
{
  return !strncmp(a, b, strlen(b)) &&
    a[strlen(b) + 0] == ' ' &&
    a[strlen(b) + 1] == '=' &&
    a[strlen(b) + 2] == ' '
    ? a + strlen(b) + 3 : NULL;
}

extern inline void
eolcpy(char *dst, const char *src, size_t len)
{
  while(len > 1 && *src != 0 && *src != '\n') {
    *dst++ = *src++;
    len--;
  }
  *dst = 0;
}

extern inline const char *
nextline(const char *s)
{
  s = strchr(s, '\n');
  if(s == NULL)
    return NULL;
  return s + 1;
}

#endif /* TVHEADEND_H */
