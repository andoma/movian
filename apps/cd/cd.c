/*
 *  CD/DVD drive manager
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

#include <sys/ioctl.h>
#include <sys/stat.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <linux/cdrom.h>

#include <libglw/glw.h>

#include <fcntl.h>

#include "showtime.h"
#include "app.h"
#include "apps/dvdplayer/dvd.h"
#include "input.h"
#include "miw.h"



typedef struct cd_monitor {
  ic_t *ic;
  int run;
  int cdfd;

} cd_monitor_t;



static void *
cd_monitor_thread(void *aux)
{
  cd_monitor_t *cm = aux;
  int drivestatus;

  while(cm->run) {
    usleep(250000);
    drivestatus = ioctl(cm->cdfd, CDROM_DRIVE_STATUS, NULL);
    if(drivestatus != CDS_DISC_OK) {
      input_keystrike(cm->ic, INPUT_KEY_STOP);
      break;
    }
  }
  return NULL;
}


static void
cd_make_idle_widget(glw_t *p, const char *caption)
{
  glw_t *y;

  y = glw_create(GLW_CONTAINER_Y, 
		 GLW_ATTRIB_PARENT, p,
		 NULL);

  glw_create(GLW_DUMMY, 
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_WEIGHT, 9.0f,
	     NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_CAPTION, caption,
	     NULL);  
}





static void *
cd_start(void *aux)
{
  appi_t *ai = aux;
  int do_eject = 0;
  const char *devname;
  int r, fd;
  pthread_t ptid;
  pthread_attr_t attr;
  int drivestatus;
  int discstatus;
  char tmp[100];
  cd_monitor_t cm;

  ai->ai_no_input_events = 1;

  ai->ai_widget =
    glw_create(GLW_XFADER,
	       GLW_ATTRIB_SIGNAL_HANDLER, appi_widget_post_key, ai,
	       NULL);

  devname = config_get_str("dvd-device", "/dev/dvd");

  fd = open(devname, O_RDONLY | O_NONBLOCK);
  if(fd == -1) {
    sprintf(tmp, "No drive found at \"%s\"", devname);
    cd_make_idle_widget(ai->ai_widget, tmp);
    while(1) {
      sleep(1);
    }
  }

  cd_make_idle_widget(ai->ai_widget, "No disc");

  while(1) {

    usleep(500000);

    if(do_eject == 1)
      ioctl(fd, CDROMEJECT,  NULL);

    drivestatus = ioctl(fd, CDROM_DRIVE_STATUS, NULL);
    switch(drivestatus) {
    case CDS_NO_INFO:
      break;

    case CDS_DISC_OK:
      if(do_eject == 1)
	break;

      discstatus = ioctl(fd, CDROM_DISC_STATUS, NULL);
      switch(discstatus) {
      case CDS_AUDIO:
      case CDS_MIXED:
  	break;

      case CDS_DATA_1:
	ioctl(fd, CDROM_CLEAR_OPTIONS, CDO_LOCK);

	cm.run = 1;
	cm.cdfd = fd;
	cm.ic = &ai->ai_ic;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&ptid, &attr, cd_monitor_thread, &cm);

	ai->ai_no_input_events = 0;
	//	cd_set_app_icon(ai->ai_app, "icon://cd-dvd.png");
	r = dvd_main(ai, devname, 1, ai->ai_widget);
	cm.run = 0;
	ai->ai_no_input_events = 1;
	pthread_join(ptid, NULL);   /* wait for monitor thread */
	printf("monitor joined\n");
	input_flush_queue(&ai->ai_ic);

	cd_make_idle_widget(ai->ai_widget, "No disc");


	switch(r) {
	case INPUT_KEY_EJECT:
	  do_eject = 1;
	  break;
	}

	//	cd_set_app_icon(ai->ai_app, "icon://cd-unloaded.png");

	break;
      }

      if(do_eject == 1) {
	close(fd);

	fd = open(devname, O_RDONLY | O_NONBLOCK);
	if(fd == -1) {
	  perror("cannot open DVD");
	  exit(1);
	}
      }

      break;
      
    case CDS_TRAY_OPEN:
      do_eject = 0;
    case CDS_NO_DISC:
      break;

    case CDS_DRIVE_NOT_READY:
      break;
    }
  }
}


static void
cd_spawn(appi_t *ai)
{
  pthread_create(&ai->ai_tid, NULL, cd_start, ai);
}


/*
 *
 */

app_t app_cd = {
  .app_name = "CD/DVD Player",
  .app_icon = "icon://cd.png",
  .app_spawn = cd_spawn,
  .app_max_instances = 1,
  .app_auto_spawn = 1,
};
