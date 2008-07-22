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

#include <string.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/poll.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <stdarg.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <libhts/htstv.h>

#include "showtime.h"
#include "tv_headend.h"
#include "coms.h"
#include "miw.h"

static void tvh_connect(tvheadend_t *tvh);
static void tvh_disconnect(tvheadend_t *tvh);

static void *tvh_receptor(void *aux);


#define TVH_BUF_SIZE 5000

char *
tvh_query(tvheadend_t *tvh, const char *fmt, ...)
{
  char *buf = malloc(TVH_BUF_SIZE);
  int x, err = 0;
  char *r;
  va_list ap;

  pthread_mutex_lock(&tvh->tvh_mutex);

  va_start(ap, fmt);
  vsnprintf(buf, TVH_BUF_SIZE, fmt, ap);
  va_end(ap);

  if(tvh->tvh_fp == NULL) {
    pthread_mutex_unlock(&tvh->tvh_mutex);
    tvh_connect(tvh);
    pthread_mutex_lock(&tvh->tvh_mutex);
  }

  if(tvh->tvh_fp == NULL) {
    pthread_mutex_unlock(&tvh->tvh_mutex);
    return NULL;
  }

  fprintf(tvh->tvh_fp, "%s\n", buf);

  r = buf;
  x = TVH_BUF_SIZE;

  while(1) {
    if(fgets(r, x, tvh->tvh_fp) == NULL) {
      tvh_disconnect(tvh);
      err = 1;
      break;
    }

    if(!strncmp("eom error", r, 9)) {
      err = 1;
      *r = 0;
      break;
    }

    if(!strncmp("eom ok", r, 6)) {
      err = 0;
      *r = 0;
      break;
    }
    x -= strlen(r);
    r += strlen(r);
  }

  if(err) {
    free(buf);
    buf = NULL;
  }

  pthread_mutex_unlock(&tvh->tvh_mutex);
  return buf;
}


/*
 *
 */

int
tvh_int(char *r)
{
  int v;

  if(r == NULL)
    return 0;
  v = atoi(r);
  free(r);
  return v;
}


/*
 *
 */

static void
tve_fixup_times(tvevent_t *tve)
{
  struct tm tm1, tm2;
  int l;

  const char *wdays[7] = {
    "Söndag", "Måndag", "Tisdag", "Onsdag", "Torsdag", "Fredag", "Lördag"};

  if(tve->tve_start == 0) {
    tve->tve_weekday[0] = 0;
    tve->tve_timetxt[0] = 0;
    tve->tve_lentxt[0] = 0;
    return;
  }
  localtime_r(&tve->tve_start, &tm1);
  localtime_r(&tve->tve_stop, &tm2);

  strcpy(tve->tve_weekday, wdays[tm1.tm_wday % 7]);

  sprintf(tve->tve_timetxt, "%02d:%02d - %02d:%02d", 
	  tm1.tm_hour, tm1.tm_min, tm2.tm_hour, tm2.tm_min);

  l = (tve->tve_stop - tve->tve_start) / 60;
  sprintf(tve->tve_lentxt, "%d min", l);
}





static int
tvh_parse_event(tvevent_t *tve, void *r)
{
  const char *v, *x;

  if(r == NULL)
    return -1;

  memset(tve, 0, sizeof(tvevent_t));

  for(x = r; x != NULL; x = nextline(x)) {
    if((v = propcmp(x, "title")) != NULL) 
      eolcpy(tve->tve_title, v, sizeof(tve->tve_title));
    else if((v = propcmp(x, "start")) != NULL) 
      tve->tve_start = atoi(v);
    else if((v = propcmp(x, "stop")) != NULL) 
      tve->tve_stop = atoi(v);
    else if((v = propcmp(x, "desc")) != NULL) 
      eolcpy(tve->tve_desc, v, sizeof(tve->tve_desc));
    else if((v = propcmp(x, "tag")) != NULL) 
      tve->tve_event_tag = atoi(v);
    else if((v = propcmp(x, "next")) != NULL) 
      tve->tve_event_tag_next = atoi(v);
    else if((v = propcmp(x, "prev")) != NULL) 
      tve->tve_event_tag_prev = atoi(v);
    else if((v = propcmp(x, "pvrstatus")) != NULL) 
      tve->tve_pvrstatus = atoi(v);
  }
  tve_fixup_times(tve);
  free(r);
  return 0;
}

int
tvh_get_event_current(tvheadend_t *tvh, tvevent_t *tve, int chindex)
{
  return tvh_parse_event(tve, tvh_query(tvh, "event.info now %d", chindex));
}

int
tvh_get_event_by_time(tvheadend_t *tvh, tvevent_t *tve, 
		      int chindex, time_t when)
{
  return tvh_parse_event(tve, tvh_query(tvh, "event.info at %d %ld", 
					chindex, when));
}

int
tvh_get_event_by_tag(tvheadend_t *tvh, tvevent_t *tve, int tag)
{
  return tvh_parse_event(tve, tvh_query(tvh, "event.info tag %d", tag));
}



int
tvh_get_pvrlog(tvheadend_t *tvh, tvevent_t *tve, int val, int istag)
{
  void *r;
  const char *v, *x;

  if(istag)
    r = tvh_query(tvh, "pvr.gettag %d", val);
  else
    r = tvh_query(tvh, "pvr.getlog %d", val);

  if(r == NULL)
    return 1;

  memset(tve, 0, sizeof(tvevent_t));

  for(x = r; x != NULL; x = nextline(x)) {
    if((v = propcmp(x, "title")) != NULL) 
      eolcpy(tve->tve_title, v, sizeof(tve->tve_title));
    else if((v = propcmp(x, "start")) != NULL) 
      tve->tve_start = atoi(v);
    else if((v = propcmp(x, "stop")) != NULL) 
      tve->tve_stop = atoi(v);
    else if((v = propcmp(x, "desc")) != NULL) 
      eolcpy(tve->tve_desc, v, sizeof(tve->tve_desc));
    else if((v = propcmp(x, "pvr_tag")) != NULL) 
      tve->tve_pvr_tag = atoi(v);
    else if((v = propcmp(x, "event_tag")) != NULL) 
      tve->tve_event_tag = atoi(v);
    else if((v = propcmp(x, "pvrstatus")) != NULL) 
      tve->tve_pvrstatus = atoi(v);
    else if((v = propcmp(x, "filename")) != NULL) 
      eolcpy(tve->tve_filename, v, sizeof(tve->tve_filename));
    else if((v = propcmp(x, "channel")) != NULL) 
      tve->tve_channel = atoi(v);
  }
  tve_fixup_times(tve);
  free(r);
  return 0;
}



int
tvh_get_channel(tvheadend_t *tvh, tvchannel_t *tvc, int chindex)
{
  void *r;
  const char *v, *x;

  if((r = tvh_query(tvh, "channel.info %d", chindex)) == NULL)
    return 1;

  memset(tvc, 0, sizeof(tvchannel_t));
  for(x = r; x != NULL; x = nextline(x)) {
    if((v = propcmp(x, "displayname")) != NULL)
      eolcpy(tvc->tvc_displayname, v, sizeof(tvc->tvc_displayname));
    if((v = propcmp(x, "icon")) != NULL)
      eolcpy(tvc->tvc_icon, v, sizeof(tvc->tvc_icon));
    else if((v = propcmp(x, "tag")) != NULL) 
      tvc->tvc_tag = atoi(v);
  }
  free(r);
  return 0;
}





/*
 *
 *
 */

static void
tvh_connect(tvheadend_t *tvh)
{
  socklen_t slen = sizeof(struct sockaddr_in);
  struct sockaddr_in sin;
  int s;

  tvh->tvh_fp = open_fd_tcp(config_get_str("tvheadend", "127.0.0.1"),
			    9909, (struct sockaddr *)&tvh->tvh_localaddr, 
			    &slen);

  if(tvh->tvh_fp == NULL)
    return;

  s = socket(AF_INET, SOCK_DGRAM, 0);
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port = 0;
  
  if(bind(s, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
    close(s);
    fclose(tvh->tvh_fp);
    tvh->tvh_fp = NULL;
    return;
  }

  slen = sizeof(struct sockaddr_in);
  getsockname(s, (struct sockaddr *)&tvh->tvh_receptor, &slen);

  printf("Stream port %s %d\n",
		    inet_ntoa(tvh->tvh_localaddr.sin_addr),
		    ntohs(tvh->tvh_receptor.sin_port));

  tvh_int(tvh_query(tvh, "streamport %s %d", 
		    inet_ntoa(tvh->tvh_localaddr.sin_addr),
		    ntohs(tvh->tvh_receptor.sin_port)));
  tvh->tvh_fd = s;

  printf("streamport setup ok\n");
}

static void
tvh_disconnect(tvheadend_t *tvh)
{
  int s = tvh->tvh_fd;

  tvh->tvh_fd = -1;

  fclose(tvh->tvh_fp);
  close(s);
  tvh->tvh_fp = NULL;
}

void 
tvh_init(tvheadend_t *tvh, ic_t *input)
{
  pthread_t ptid;

  tvh->tvh_fd = -1;

  tvh->tvh_ic = input;
  tvh->tvh_fp = NULL;

  pthread_mutex_init(&tvh->tvh_mutex, NULL);
  pthread_create(&ptid, NULL, tvh_receptor, tvh);
  
}


/*****************************************************************************
 *
 * UDP interface
 *
 */

static void
tvh_update_status(tvheadend_t *tvh, int chindex, char *buf)
{
  const char *v, *x;

  tvstatus_t tvs;

  if(tvh->tvh_status_callback == NULL)
    return;

  tvs.tvs_status = 0;

  for(x = buf; x != NULL; x = nextline(x)) {
    
    if((v = propcmp(x, "info")) != NULL)
      eolcpy(tvs.tvs_info, v, sizeof(tvs.tvs_info));
    else if((v = propcmp(x, "uncorrected-blocks")) != NULL)
      tvs.tvs_uncorr = atoi(v);
    else if((v = propcmp(x, "cc-errors")) != NULL)
      tvs.tvs_cc_errors = atoi(v);
    else if((v = propcmp(x, "rate")) != NULL)
      tvs.tvs_rate = atoi(v);
    else if((v = propcmp(x, "status")) != NULL)
      tvs.tvs_status = atoi(v);
    else if((v = propcmp(x, "adapter")) != NULL)
      eolcpy(tvs.tvs_adapter, v, sizeof(tvs.tvs_adapter));
    else if((v = propcmp(x, "transport")) != NULL)
      eolcpy(tvs.tvs_transport, v, sizeof(tvs.tvs_transport));
  }

  tvh->tvh_status_callback(tvh, chindex, &tvs);
}


/*
 *
 */

static void *
tvh_receptor(void *aux)
{
  tvheadend_t *tvh = aux;
  int x;
  uint8_t *buf;
  inputevent_t ie;
  uint32_t v;

  while(1) {

    while(tvh->tvh_fd == -1)
      sleep(1);

    while(tvh->tvh_fd != -1) {
      buf = malloc(1500);

      x = recv(tvh->tvh_fd, buf, 1500, 0);

      average_update(&tvh->tvh_byterate, x);
    
      switch(buf[0]) {
      case HTSTV_EOS:
      case HTSTV_TRANSPORT_STREAM:
	if(tvh->tvh_data_callback != NULL) {
	  tvh->tvh_data_callback(tvh, buf, x);
	  continue;
	}
	break;

      case HTSTV_STATUS:
	buf[x] = 0;
	tvh_update_status(tvh, buf[1], (char *)buf + 2);
	break;

      case HTSTV_REFTAG:
	memcpy(&v, buf + 1, sizeof(uint32_t));
	ie.type = INPUT_SPECIAL;
	ie.u.u32 = ntohl(v);
	input_postevent(tvh->tvh_ic, &ie);
	break;
      }
      free(buf);
    }
  }
}









/*****************************************************************************
 *
 * Graphics
 *
 */




glw_t *
tvh_create_chicon(tvchannel_t *tvc, glw_t *parent, float weight)
{
  if(tvc->tvc_icon[0]) {
    return glw_create(GLW_BITMAP,
		      GLW_ATTRIB_FILENAME, tvc->tvc_icon,
		      GLW_ATTRIB_WEIGHT, weight,
		      GLW_ATTRIB_PARENT, parent,
		      GLW_ATTRIB_FLAGS, GLW_BORDER_BLEND,
		      GLW_ATTRIB_BORDER_WIDTH, 0.05,
		      NULL);
  } else {
    return glw_create(GLW_TEXT_BITMAP,
		      GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
		      GLW_ATTRIB_CAPTION, tvc->tvc_displayname,
		      GLW_ATTRIB_WEIGHT, weight,
		      GLW_ATTRIB_PARENT, parent,
		      NULL);
  }
}



glw_t *
tvh_create_pvrstatus(glw_t *parent, char pvrstatus, float weight)
{
  glw_t *w, *y;

  const char *txt = NULL;
  const char *icon = NULL;

  switch(pvrstatus) {
  case 'S':
    icon = "icon://clock.png";
    txt = "Scheduled";
    break;

  case 'd':
    icon = "icon://ok.png";
    txt = "Completed";
    break;

  case 'R':
    icon = "icon://rec.png";
    txt = "Recording...";
    break;

  case 'A':
    icon = "icon://error.png";
    txt = "Aborted";
    break;

  case 'E':
    icon = "icon://error.png";
    txt = "Error";
    break;

  case 'T':
    icon = "icon://error.png";
    txt = "No transponder";
    break;

  case 'O':
    icon = "icon://error.png";
    txt = "Overlaid recording";
    break;

  case 'F':
    icon = "icon://error.png";
    txt = "File error";
    break;

  case 'D':
    icon = "icon://error.png";
    txt = "No disk space";
    break;
  }

  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, parent,
		 GLW_ATTRIB_WEIGHT, weight, 
		 NULL);

  if(icon != NULL)
    glw_create(GLW_BITMAP,
	       GLW_ATTRIB_FILENAME, icon,
	       GLW_ATTRIB_WEIGHT, 3.0,
	       GLW_ATTRIB_PARENT, y,
	       NULL);

  if(txt != NULL)
    w = glw_create(GLW_TEXT_BITMAP,
		   GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
		   GLW_ATTRIB_CAPTION, txt,
		   GLW_ATTRIB_PARENT, y,
		   GLW_ATTRIB_WEIGHT, 1.0,
		   NULL);
  return y;
}
