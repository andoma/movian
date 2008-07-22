/*
 *  Video menu
 *  Copyright (C) 2007-2008 Andreas Öman
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

#define _GNU_SOURCE
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <libgen.h>

#include <libavformat/avformat.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "layout/layout.h"
#include "video_decoder.h"
#include "video_menu.h"
#include "subtitles.h"

/**
 *
 */
static void
vdc_set_ilace(void *opaque, int value)
{
  vd_conf_t *vdc = opaque;
  vdc->gc_deilace_type = value;
}

/**
 * 
 */
static void
vdc_menu_ilace_opt(glw_t *w, vd_conf_t *vdc, 
		   const char *title, vd_deilace_type_t type)
{
  glw_selection_add_text_option(w, title, vdc_set_ilace, vdc, type,
				vdc->gc_deilace_type == type);
}

/**
 * Video menu
 */
void
video_menu_attach(glw_t *p, vd_conf_t *vdc)
{
  glw_t *w;
  if((w = glw_find_by_id(p, "video_deinterlacer", 0)) != NULL) {
    vdc_menu_ilace_opt(w, vdc, "None", VD_DEILACE_NONE);
    vdc_menu_ilace_opt(w, vdc, "Auto", VD_DEILACE_AUTO);
    vdc_menu_ilace_opt(w, vdc, "Half resolution", VD_DEILACE_HALF_RES);
    vdc_menu_ilace_opt(w, vdc, "YADIF half rate", VD_DEILACE_YADIF_FRAME);
    vdc_menu_ilace_opt(w, vdc, "YADIF full rate", VD_DEILACE_YADIF_FIELD);
  }

  if((w = glw_find_by_id(p, "video_delay", 0)) != NULL)
    glw_set(w,GLW_ATTRIB_INTPTR, &vdc->gc_avcomp, NULL);

  if((w = glw_find_by_id(p, "video_zoom", 0)) != NULL)
    glw_set(w, GLW_ATTRIB_INTPTR, &vdc->gc_zoom, NULL);


}
