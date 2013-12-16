/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <malloc.h>
#include <di/di.h>

#include "showtime.h"
#include "misc/callout.h"
#include "navigator.h"
#include "backend/backend.h"
#include "media.h"
#include "dvd.h"
#include "service.h"
#include "notifications.h"

typedef struct disc_scanner {

  callout_t ds_timer;

  int ds_disc_inserted;
  int ds_disc_ready;

  service_t *ds_service;

} disc_scanner_t;


/**
 *
 */
static void
check_disc_type(disc_scanner_t *ds)
{
  char *buf = memalign(32, 2048);
  char title[64];
  int r = DI_ReadDVD(buf, 1, 16);

  if(r) {
    notify_add(NOTIFY_ERROR, NULL, 5, "Unable to read disc, ejecting...");
    DI_Eject();

  } else {
    char *p = &buf[40];
    while(*p > 32 && p != &buf[72])
      p++;
    *p = 0;
    
    snprintf(title, sizeof(title), "DVD: %s", buf + 40);
    ds->ds_service = service_create(title, "dvd:/dev/di",
				    SVC_TYPE_VIDEO, NULL, 0);
    ds->ds_disc_ready = 1;
  }
  free(buf);
}



/**
 *
 */
static void 
dvdprobe(callout_t *co, void *aux)
{
  disc_scanner_t *ds = aux;

  callout_arm(&ds->ds_timer, dvdprobe, ds, 1);

  if(ds->ds_disc_inserted == 0) {

    DI_Mount();

    if(DI_GetStatus() == DVD_NO_DISC)
      return;

    TRACE(TRACE_INFO, "DVD", "DVD inserted");
    ds->ds_disc_inserted = 1;
    ds->ds_disc_ready = 0;

  } else {
    uint32_t s = DI_GetStatus();


    if(s == DVD_UNKNOWN) {
      ds->ds_disc_inserted = 0;
      notify_add(NOTIFY_ERROR, NULL, 5, "Unknown disc inserted, ejecting...");
      DI_Eject();
      return;
    }

    if(!(s & DVD_READY)) {
      TRACE(TRACE_DEBUG, "DVD", "Waiting for disc ready state = %x", s);
      return;
    }

    if(!ds->ds_disc_ready)
      check_disc_type(ds);

    if(DI_GetCoverRegister(&s) || !(s & DVD_COVER_DISC_INSERTED)) {
      ds->ds_disc_inserted = 0;
      TRACE(TRACE_INFO, "DVD", "DVD no longer present");
      
      if(ds->ds_service != NULL) {
	service_destroy(ds->ds_service);
	ds->ds_service = NULL;
      }
    }
  }
}

/**
 *
 */
static int
be_dvd_canhandle(struct backend *be, const char *url)
{
  return !strcmp(url, "dvd:/dev/di");
}


/**
 *
 */
static event_t *
be_dvd_play(struct backend *be, const char *url, media_pipe_t *mp,
	    int flags, int priority,
	    char *errstr, size_t errlen)
{
  event_t *e;
  int i;

  if(strcmp(url, "dvd:/dev/di")) {
    snprintf(errstr, errlen, "dvd: Invalid URL");
    return NULL;
  }

  for(i = 0; i < 20; i++) {
    if(DI_GetStatus() & DVD_READY)
      break;
    sleep(1);
  }

  e = dvd_play("/dev/di", mp, errstr, errlen, 0);

  if(e != NULL && event_is_action(e, ACTION_EJECT))
    DI_Eject();

  return e;
}

/**
 *
 */
static int
be_dvd_init(void)
{
  disc_scanner_t *ds = calloc(1, sizeof(disc_scanner_t));

  callout_arm(&ds->ds_timer, dvdprobe, ds, 0);
  return 0;
}


/**
 *
 */
static backend_t be_dvd = {
  .be_canhandle = be_dvd_canhandle,
  .be_open = backend_open_video,
  .be_play_video = be_dvd_play,
  .be_init = be_dvd_init,
};

BE_REGISTER(dvd);
