/*
 *  Video output on GL surfaces
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

#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "showtime.h"
#include "video_decoder.h"
#include "menu.h"

/*
 * video decoder menues
 */
#if 0
static int 
vd_menu_pp(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  vd_conf_t *gc = opaque;
  glw_t *b;
  float v;

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    if((b = glw_find_by_class(w, GLW_BITMAP)) == NULL)
      return 0;
    
    v = w->glw_u32 == gc->gc_deilace_type ? 1 : 0;
    b->glw_alpha = (b->glw_alpha * 15 + v) / 16.0;
    return 0;

  case GLW_SIGNAL_CLICK:
    gc->gc_deilace_type = w->glw_u32;
    return 1;
    
  default:
    return 0;
  }
}



static int 
vd_menu_pp_field_parity(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  vd_conf_t *gc = opaque;
  glw_t *b;
  char txt[30];

  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    if((b = glw_find_by_class(w, GLW_TEXT_BITMAP)) == NULL)
      return 0;

    snprintf(txt, sizeof(txt), "Field parity: %s",
	     gc->gc_field_parity ? "Inverted" : "Normal");
    glw_set(b, GLW_ATTRIB_CAPTION, txt, NULL);
    return 0;

  case GLW_SIGNAL_CLICK:
    gc->gc_field_parity = !gc->gc_field_parity;
    return 1;
    
  default:
    return 0;
  }
}






static int 
vd_menu_avsync(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  inputevent_t *ie;
  vd_conf_t *gc = opaque;
  char buf[50];
  glw_t *b;

  va_list ap;
  va_start(ap, signal);


  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    if((b = glw_find_by_class(w, GLW_TEXT_BITMAP)) == NULL)
      return 0;
    snprintf(buf, sizeof(buf), "Video output delay: %dms", gc->gc_avcomp);

    glw_set(b, GLW_ATTRIB_CAPTION, buf, NULL);
    break;

  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);

    switch(ie->type) {
    case INPUT_KEY:
      switch(ie->u.key) {
      case INPUT_KEY_LEFT:
	gc->gc_avcomp -= 50;
	break;
      case INPUT_KEY_RIGHT:
	gc->gc_avcomp += 50;
	break;
      default:
	break;
      }
      break;
    default:
      break;
    }
    return 1;
    
  case GLW_SIGNAL_CLICK:
    return 1;

  default:
    return 0;
  }

  va_end(ap);
  return 0;
}







static int 
vd_menu_video_zoom(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  inputevent_t *ie;
  vd_conf_t *gc = opaque;
  char buf[50];
  glw_t *b;

  va_list ap;
  va_start(ap, signal);


  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    if((b = glw_find_by_class(w, GLW_TEXT_BITMAP)) == NULL)
      return 0;
    snprintf(buf, sizeof(buf), "Video zoom: %d%%", gc->gc_zoom);

    glw_set(b, GLW_ATTRIB_CAPTION, buf, NULL);
    break;

  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);

    switch(ie->type) {
    case INPUT_KEY:
      switch(ie->u.key) {
      case INPUT_KEY_LEFT:
	gc->gc_zoom -= 10;
	break;
      case INPUT_KEY_RIGHT:
	gc->gc_zoom += 10;
	break;
      default:
	break;
      }
      break;
    default:
      break;
    }
    return 1;
    
  case GLW_SIGNAL_CLICK:
    return 1;

  default:
    return 0;
  }

  va_end(ap);
  return 0;
}
#endif



glw_t *
vd_menu_setup(glw_t *p, vd_conf_t *gc)
{
#if 0
  glw_t *v, *s;
  
  v = menu_create_submenu(p, "icon://tv.png", "Video settings", 1);


  /*** Post processor */

  s = menu_create_submenu(v, "icon://tv.png", "Deinterlacer", 0);

  menu_create_item(s, "icon://menu-current.png", "No deinterlacing", 
		   vd_menu_pp, gc, VD_DEILACE_NONE, 0);

  menu_create_item(s, "icon://menu-current.png", "Automatic", 
		   vd_menu_pp, gc, VD_DEILACE_AUTO, 0);

  menu_create_item(s, "icon://menu-current.png", "Simple",
		   vd_menu_pp, gc, VD_DEILACE_HALF_RES, 0);

  menu_create_item(s, "icon://menu-current.png", "Yadif",
		   vd_menu_pp, gc, VD_DEILACE_YADIF_FRAME, 0);

  menu_create_item(s, "icon://menu-current.png", "Yadif 2x",
		   vd_menu_pp, gc, VD_DEILACE_YADIF_FIELD, 0);

  menu_create_item(s, "icon://menu-current.png", "Yadif NSI",
		   vd_menu_pp, gc, 
		   VD_DEILACE_YADIF_FRAME_NO_SPATIAL_ILACE, 0);
  menu_create_item(s, "icon://menu-current.png", "Yadif 2x NSI",
		   vd_menu_pp, gc, 
		   VD_DEILACE_YADIF_FIELD_NO_SPATIAL_ILACE, 0);


  menu_create_item(s, NULL, "Field Parity", 
		   vd_menu_pp_field_parity, gc, 0, 0);


 /*** AV sync */

  s = menu_create_submenu(v, "icon://audio.png", "A/V Sync", 0);

  menu_create_item(s, NULL, "", vd_menu_avsync, gc, 0, 0);


 /*** Video zoom */

  s = menu_create_submenu(v, "icon://zoom.png", "Video zoom", 0);

  menu_create_item(s, NULL, "", vd_menu_video_zoom, gc, 0, 0);

  return v;
#endif
  return NULL;
}


