/*
 *  Linux DVD drive support
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
#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <linux/cdrom.h>

#include "showtime.h"
#include "misc/callout.h"
#include "navigator.h"
#include "media.h"
#include "dvd.h"
#include "sd/sd.h"

typedef enum {
  DISC_NO_DRIVE,
  DISC_NO_DISC,
  DISC_ISOFS,
  DISC_AUDIO,
  DISC_UNKNOWN_TYPE,
} disc_status_t;

typedef struct disc_scanner {

  callout_t ds_timer;
  disc_status_t ds_status;
  char ds_title[40];

  char *ds_dev;

  prop_t *ds_prop;

} disc_scanner_t;


/**
 *
 */
static void
set_status(disc_scanner_t *ds, disc_status_t status, const char *title)
{
  char buf[64];
  char url[URL_MAX];
  if(ds->ds_status == status)
    return;

  ds->ds_status = status;

  if(ds->ds_prop != NULL) {
    prop_destroy(ds->ds_prop);
    ds->ds_prop = NULL;
  }

  switch(status) {
  case DISC_NO_DRIVE:
  case DISC_NO_DISC:
    break;

  case DISC_AUDIO:
    snprintf(buf, sizeof(buf), "Audio CD");
    snprintf(url, sizeof(url), "audiocd:%s", ds->ds_dev);
    ds->ds_prop = sd_add_service(ds->ds_dev, buf,  NULL, NULL, "dvd", url);
    break;

  case DISC_ISOFS:
    snprintf(buf, sizeof(buf), "DVD: %s", title);
    snprintf(url, sizeof(url), "dvd:%s", ds->ds_dev);

    ds->ds_prop = sd_add_service(ds->ds_dev, buf,  NULL, NULL, "dvd", url);
    break;

  case DISC_UNKNOWN_TYPE:
    snprintf(buf, sizeof(buf), "Unknown disc");
    ds->ds_prop = sd_add_service(ds->ds_dev, buf,  NULL, NULL, "dvd", NULL);
    break;
  }
}


/**
 *
 */
static void
check_disc_type(disc_scanner_t *ds, int fd)
{
  switch(ioctl(fd, CDROM_DISC_STATUS, NULL)) {
  case CDS_AUDIO:
  case CDS_MIXED:
    set_status(ds, DISC_AUDIO, NULL);
    return;
    
  case CDS_DATA_1:
    lseek(fd, 0x8000, SEEK_SET);
    char buf[2048];
    int r = read(fd, buf, 2048);
    
    if(r == 2048) {
      char *p = &buf[40];
      while(*p > 32 && p != &buf[72])
	p++;
      *p = 0;
      set_status(ds, DISC_ISOFS, buf + 40);
      return;
    }
    break;
  default:
    break;
  }

  set_status(ds, DISC_UNKNOWN_TYPE, NULL);
}



/**
 *
 */
static void 
dvdprobe(callout_t *co, void *aux)
{
  disc_scanner_t *ds = aux;
  int fd;

  callout_arm(&ds->ds_timer, dvdprobe, ds, 1);

  fd = open(ds->ds_dev, O_RDONLY | O_NONBLOCK);

  if(fd == -1) {
    set_status(ds, DISC_NO_DRIVE, NULL);
  } else {
    if(ioctl(fd, CDROM_DRIVE_STATUS, NULL) == CDS_DISC_OK) {
      if(ds->ds_prop == NULL)
	check_disc_type(ds, fd);
    } else {
      set_status(ds, DISC_NO_DISC, NULL);
    }
    close(fd);
  }
}

/**
 *
 */
static int
be_dvd_canhandle(const char *url)
{
  return !strncmp(url, "dvd:", strlen("dvd:"));
}


/**
 *
 */
static int
be_dvd_openpage(const char *url0, const char *type0, const char *parent,
		nav_page_t **npp, char *errbuf, size_t errlen)
{
  nav_page_t *np;
  prop_t *p;

  np = nav_page_create(url0, sizeof(nav_page_t), NULL, 0);

  p = np->np_prop_root;
  prop_set_string(prop_create(p, "type"), "video");
  *npp = np;
  return 0;
}

/**
 *
 */
static event_t *
be_dvd_play(const char *url, media_pipe_t *mp, char *errstr, size_t errlen)
{
  event_t *e;
  if(strncmp(url, "dvd:", strlen("dvd:"))) {
    snprintf(errstr, errlen, "dvd: Invalid URL");
    return NULL;
  }

  url += 4;
  e = dvd_play(url, mp, errstr, errlen, 0);

  if(e != NULL && event_is_action(e, ACTION_EJECT)) {

    int fd = open(url, O_RDONLY | O_NONBLOCK);
    if(fd != -1) {
      if(ioctl(fd, CDROMEJECT, NULL))
	TRACE(TRACE_ERROR, "DVD", "Eject of %s failed -- %s",
	      url, strerror(errno));
      close(fd);
    } else {
      TRACE(TRACE_ERROR, "DVD", "Unable to open %s for eject -- %s",
	    url, strerror(errno));
    }
  }
  return e;
}

/**
 *
 */
static int
be_dvd_init(void)
{
  disc_scanner_t *ds = calloc(1, sizeof(disc_scanner_t));

  ds->ds_dev = strdup("/dev/dvd");
  callout_arm(&ds->ds_timer, dvdprobe, ds, 0);
  return 0;
}


/**
 *
 */
nav_backend_t be_dvd = {
  .nb_canhandle = be_dvd_canhandle,
  .nb_open = be_dvd_openpage,
  .nb_play_video = be_dvd_play,
  .nb_init = be_dvd_init,
};
