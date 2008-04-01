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
#include "input.h"
#include "layout/layout.h"
#include "video_decoder.h"
#include "video_menu.h"
#include "subtitles.h"
#include "layout/layout_forms.h"
#include "layout/layout_support.h"



/**
 * Video menu
 */
void
video_menu_attach(glw_t *m, glw_focus_stack_t *gfs, ic_t *ic, vd_conf_t *vdc)
{
  struct layout_form_entry_list lfelist;

  layout_form_entry_options_t deilace_options[] = {
    {"Automatic",          VD_DEILACE_AUTO},
    {"Disabled",           VD_DEILACE_NONE},
    {"Simple",             VD_DEILACE_HALF_RES},
    {"YADIF",              VD_DEILACE_YADIF_FIELD}
  };

  TAILQ_INIT(&lfelist);
 
  layout_form_fill_options(m, "deinterlacer_options",
			   deilace_options, 4);

  LFE_ADD_OPTION(&lfelist, "deinterlacer_options", &vdc->gc_deilace_type);
  LFE_ADD_INT(&lfelist, "avsync",    &vdc->gc_avcomp,"%dms", -2000, 2000, 50);
  LFE_ADD_INT(&lfelist, "videozoom", &vdc->gc_zoom,  "%d%%",   100, 1000, 10);

  layout_form_initialize(&lfelist, m, gfs, ic, 0);
}


/**
 * Video menu
 */
void
video_menu_add_tab(glw_t *m, glw_focus_stack_t *gfs, ic_t *ic, vd_conf_t *vdc,
		   const char *src)
{
  glw_t *t;
  char buf1[100], buf2[100];

  snprintf(buf1, sizeof(buf1), "%s-icon", src);
  snprintf(buf2, sizeof(buf2), "%s-tab",  src);

  t = layout_form_add_tab(m,
			  "menu",           buf1,
			  "menu_container", buf2);

  video_menu_attach(m, gfs, ic, vdc);
}
