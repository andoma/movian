/*
 *  Private Video Recorder control
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

#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include <libglw/glw.h>
#include <libhts/htstv.h>

#include "showtime.h"
#include "input.h"
#include "tv_headend.h"
#include "app.h"
#include "menu.h"
#include "layout/layout.h"

typedef enum {
  PVR_SA_NONE,
  PVR_SA_2DNAV,
  PVR_SA_SCHED,
  PVR_SA_hm,

} pvr_subapp_t;

typedef struct pvrprog {
  LIST_ENTRY(pvrprog) pvp_link;
  TAILQ_ENTRY(pvrprog) pvp_linkq;
  
  tvprogramme_t *pvp_meta;
  int pvp_index;

  int pvp_keepme; /* temporary flag used for expunge */

  glw_t *pvp_widget;
  glw_t *pvp_metacontainer;


  glw_t *pvp_w_desc;
  glw_t *pvp_w_status_container;
  glw_t *pvp_w_status_xfader;

  glw_t *pvp_w_menu_container;
  glw_t *pvp_w_menu;

  float pvp_alpha;

  struct pvrchan *pvp_chan;

  enum {
    PVP_PROG,
    PVP_LOG

  } pvp_type;

} pvrprog_t;



typedef struct pvrchan {
  TAILQ_ENTRY(pvrchan) pvc_link;
  glw_t *pvc_widget;
  float pvc_ypos;
  float pvc_alpha;
  int pvc_id;

  LIST_HEAD(, pvrprog) pvc_programs;
  pvrprog_t *pvc_center;

  tvchannel_t pvc_tvc;
  struct pvr *pvc_pvr;

} pvrchan_t;



typedef struct pvr {
  tvheadend_t pvr_tvh;
  int pvr_num_chan;
  struct glw_head *pvr_tag_hash;

  TAILQ_HEAD(, pvrchan) pvr_channels;

  float pvr_xzoom;
  float pvr_yzoom;

  float pvr_y;

  float pvr_ty;

  time_t pvr_timeptr;
  double pvr_x;
  float pvr_tx;
  float pvr_tx0;

  pthread_mutex_t pvr_loader_lock;
  pthread_cond_t pvr_loader_cond;
  int pvr_loader_work;

  float pvr_autoscaler;

  pvrprog_t *pvr_selected;

  glw_t *pvr_bar;

  float pvr_zoomv;


  appi_t *pvr_ai;

  TAILQ_HEAD(, pvrprog) pvr_log;

  pvr_subapp_t pvr_cur_sa;

  struct glw_head pvr_rlist_1;
  struct glw_head pvr_rlist_2;
  struct glw_head pvr_rlist_3;

  glw_t *pvr_w_sched;

} pvr_t;

static void pvr_create_bar(pvr_t *pvr);

static int pvr_2dnav_callback(glw_t *w, glw_signal_t signal, ...);


/*
 *
 */
static int
pvr_menu_recording_cb(glw_t *w, glw_signal_t signal, ...)
{
  pvr_t *pvr = glw_get_opaque(w);
  int showme;

  switch(signal) {
  case GLW_SIGNAL_PRE_LAYOUT:
    showme = pvr->pvr_cur_sa == PVR_SA_2DNAV && pvr->pvr_selected;
    if(showme)
      w->glw_flags &= ~GLW_HIDDEN;
    else
      w->glw_flags |= GLW_HIDDEN;
    break;

  default:
    break;
  }
  return 0;
}

/*
 *
 */
static void
pvr_create_recording_submenu(pvr_t *pvr)
{
 glw_t *v;
 appi_t *ai = pvr->pvr_ai;

  v = menu_create_submenu_cb(ai->ai_menu, "icon://rec.png", "Record...", 1,
			     pvr_menu_recording_cb, pvr);
  
  menu_create_item(v, "icon://rec.png", "Record once",
		   menu_post_key_pop_and_hide, ai,
		   INPUT_KEY_RECORD_ONCE, 0);

  menu_create_item(v, "icon://rec.png", "Record daily",
		   menu_post_key_pop_and_hide, ai,
		   INPUT_KEY_RECORD_DAILY, 0);

  menu_create_item(v, "icon://rec.png", "Record weekly",
		   menu_post_key_pop_and_hide, ai,
		   INPUT_KEY_RECORD_WEEKLY, 0);

  menu_create_item(v, "icon://no.png", "Cancel recording",
		   menu_post_key_pop_and_hide, ai,
		   INPUT_KEY_RECORD_CANCEL, 0);
}

/*
 *
 */

static glw_color_t
pvp_plate_color_by_meta(tvprogramme_t *tvp)
{
  switch(tvp->tvp_pvrstatus) {
default:
    return GLW_COLOR_RED;

  case HTSTV_PVR_STATUS_SCHEDULED:
    return GLW_COLOR_GREEN;

  case HTSTV_PVR_STATUS_DONE:
    return GLW_COLOR_LIGHT_BLUE;

  case HTSTV_PVR_STATUS_NONE:
    return GLW_COLOR_WHITE;
  }
}


/*
 * PVR log
 */

static void
pvl_create_widget(pvr_t *pvr, pvrprog_t *pvp)
{
  glw_t *x, *c, *y;
  tvprogramme_t *tvp = pvp->pvp_meta;

  c = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_PARENT, pvp->pvp_widget,
		 GLW_ATTRIB_COLOR, pvp_plate_color_by_meta(pvp->pvp_meta),
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 GLW_ATTRIB_FILENAME, "icon://plate.png",
		 NULL);

  x = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_PARENT, c,
		 NULL);

  tvh_create_chicon(&pvp->pvp_chan->pvc_tvc, x, 1.0f);

  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, x,
		 GLW_ATTRIB_WEIGHT, 3.0f,
		 NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	     GLW_ATTRIB_CAPTION, tvp->tvp_title,
	     NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	     GLW_ATTRIB_CAPTION, tvp->tvp_timetxt,
	     NULL);

  tvh_create_pvrstatus(x, tvp->tvp_pvrstatus, 1.0f);

  pvp->pvp_widget = c;

}

static void
pvrprog_refresh(pvr_t *pvr, pvrprog_t *pvp)
{
  glw_t *p;
  if(tvh_get_pvrlog(&pvr->pvr_tvh, pvp->pvp_meta, 
		    pvp->pvp_meta->tvp_reftag, 1)) {
    return;
  }

  p = pvp->pvp_widget;

  pvl_create_widget(pvr, pvp);

  glw_destroy(p);
}



static void
pvrprog_update(pvr_t *pvr)
{
  int e = 0;
  pvrprog_t *pvp, *next;
  tvprogramme_t *tvp = NULL;
  pvrchan_t *pvc;


  pvp = TAILQ_FIRST(&pvr->pvr_log);
  
  while(1) {
    if(tvp == NULL)
      tvp = calloc(1, sizeof(tvprogramme_t));
    
    if(tvh_get_pvrlog(&pvr->pvr_tvh, tvp, e, 0))
      break;

    glw_lock();
    
    if(pvp == NULL) {
      pvp = calloc(1, sizeof(pvrprog_t));
      pvp->pvp_type = PVP_LOG;
      pvp->pvp_widget = glw_create(GLW_XFADER,
				   GLW_ATTRIB_PARENT, pvr->pvr_w_sched,
				   NULL);
      
      TAILQ_INSERT_TAIL(&pvr->pvr_log, pvp, pvp_linkq);
    } else {
      free(pvp->pvp_meta);
    }

    TAILQ_FOREACH(pvc, &pvr->pvr_channels, pvc_link)
      if(pvc->pvc_id == tvp->tvp_channel)
	break;
    
    pvp->pvp_chan = pvc;
    pvp->pvp_meta = tvp;
    pvp->pvp_index = tvp->tvp_index;
    tvp = NULL;

    pvl_create_widget(pvr, pvp);
    
    glw_unlock();

    pvp = TAILQ_NEXT(pvp, pvp_linkq);
    e++;
  }
  
  free(tvp);
  
  for(; pvp ; pvp = next) {
    next = TAILQ_NEXT(pvp, pvp_linkq);
    glw_lock();
    if(pvp->pvp_widget)
      glw_destroy(pvp->pvp_widget);
    TAILQ_REMOVE(&pvr->pvr_log, pvp, pvp_linkq);
    glw_unlock();

    free(pvp);
  }
}









/*
 *
 */

static void
pvr_load_chans(pvr_t *pvr)
{
  int i;
  tvheadend_t *tvh = &pvr->pvr_tvh;
  pvrchan_t *pvc;

  for(i = 0; i < pvr->pvr_num_chan; i++) {
    pvc = calloc(1, sizeof(pvrchan_t));
    pvc->pvc_pvr = pvr;

    tvh_get_channel(tvh, &pvc->pvc_tvc, i);

    pvc->pvc_widget = tvh_create_chicon(&pvc->pvc_tvc, NULL, 1.0f);
    pvc->pvc_ypos = i;
    pvc->pvc_id = i;
    TAILQ_INSERT_TAIL(&pvr->pvr_channels, pvc, pvc_link);
    LIST_INIT(&pvc->pvc_programs);
  }
}



/*
 *
 */

static glw_t *
pvp_create_widget(pvrprog_t *pvp)
{
  tvprogramme_t *tvp = pvp->pvp_meta;
  glw_t *r, *c;

  r = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_COLOR, pvp_plate_color_by_meta(pvp->pvp_meta),
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 GLW_ATTRIB_FILENAME, "icon://plate.png",
		 NULL);

  c = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, r, 
		 NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, c,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	     GLW_ATTRIB_CAPTION, tvp->tvp_title,
	     NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, c,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	     GLW_ATTRIB_CAPTION, tvp->tvp_timetxt,
	     NULL);

  return r;
}


/*
 *
 */

static void
pvp_update_front_status(pvrprog_t *pvp)
{
  glw_set(pvp->pvp_widget, 
	  GLW_ATTRIB_COLOR, pvp_plate_color_by_meta(pvp->pvp_meta),
	  NULL);
}





/*
 *
 */

static int
pvp_sort(pvrprog_t *a, pvrprog_t *b)
{
  return a->pvp_index - b->pvp_index;
}


static pvrprog_t *
pvp_load(tvheadend_t *tvh, pvr_t *pvr, pvrchan_t *pvc, int index)
{
  pvrprog_t *pvp;
  int r;

  LIST_FOREACH(pvp, &pvc->pvc_programs, pvp_link)
    if(pvp->pvp_index == index)
      return pvp;
  
  pvp = calloc(1, sizeof(pvrprog_t));
  pvp->pvp_meta = malloc(sizeof(tvprogramme_t));
  pvp->pvp_index = index;
  pvp->pvp_chan = pvc;
  pvp->pvp_type = PVP_PROG;

  r = tvh_get_programme(tvh, pvp->pvp_meta, pvc->pvc_id, pvp->pvp_index);

  if(r) {
    free(pvp->pvp_meta);
    free(pvp);
    return NULL;
  }

  pvp->pvp_widget = pvp_create_widget(pvp);
  
  glw_lock();
  LIST_INSERT_SORTED(&pvc->pvc_programs, pvp, pvp_link, pvp_sort);
  glw_unlock();

  return pvp;
}


static pvrprog_t *
pvp_load_by_time(tvheadend_t *tvh, pvr_t *pvr, pvrchan_t *pvc, time_t t)
{
  int p;
  pvrprog_t *pvp;

  LIST_FOREACH(pvp, &pvc->pvc_programs, pvp_link)
    if(pvp->pvp_meta->tvp_start <= t && pvp->pvp_meta->tvp_stop > t)
      return pvp;

  p = tvh_int(tvh_query(tvh, "programme.bytime %d %d",
			pvc->pvc_id, pvr->pvr_timeptr));

  return pvp_load(tvh, pvr, pvc, p);
}

static void
pvp_destroy(pvrprog_t *pvp)
{
  glw_lock();

  if(pvp->pvp_w_desc)
    glw_destroy(pvp->pvp_w_desc);

  if(pvp->pvp_w_status_container)
    glw_destroy(pvp->pvp_w_status_container);

  if(pvp->pvp_w_menu_container)
    glw_destroy(pvp->pvp_w_menu_container);

  glw_destroy(pvp->pvp_widget);
  LIST_REMOVE(pvp, pvp_link);
  free(pvp->pvp_meta);
  free(pvp);
  glw_unlock();
}


/*
 *
 */

#define PVR_TIME_MARGINS 7200

static void *
pvr_update_thread(void *aux)
{
  pvr_t *pvr = aux;
  tvheadend_t *tvh = &pvr->pvr_tvh;
  pvrchan_t *pvc;
  pvrprog_t *center, *pvp, *nxt;
  int i;

  pthread_mutex_lock(&pvr->pvr_loader_lock);

  while(1) {

    TAILQ_FOREACH(pvc, &pvr->pvr_channels, pvc_link) {
    
      center = pvp_load_by_time(tvh, pvr, pvc, pvr->pvr_timeptr);

      if(center == NULL)
	continue;

      pvc->pvc_center = center;

      center->pvp_keepme = 1;

      for(i = center->pvp_index - 1; ; i--) {
	pvp = pvp_load(tvh, pvr, pvc, i);
	if(pvp == NULL)
	  break;
	pvp->pvp_keepme = 1;
	if(pvp->pvp_meta->tvp_stop < pvr->pvr_timeptr - PVR_TIME_MARGINS)
	  break;
      }

      for(i = center->pvp_index + 1; ; i++) {
	pvp = pvp_load(tvh, pvr, pvc, i);
	if(pvp == NULL)
	  break;
	pvp->pvp_keepme = 1;
	if(pvp->pvp_meta->tvp_start > pvr->pvr_timeptr + PVR_TIME_MARGINS)
	  break;
      }
      
      for(pvp = LIST_FIRST(&pvc->pvc_programs) ; pvp != NULL; pvp = nxt) {
	nxt = LIST_NEXT(pvp, pvp_link);
	if(pvp->pvp_keepme) {
	  pvp->pvp_keepme = 0;
	} else {
	  pvp_destroy(pvp);
	}
      }
    }
    while(pvr->pvr_loader_work == 0)
      pthread_cond_wait(&pvr->pvr_loader_cond, &pvr->pvr_loader_lock);

    pvr->pvr_loader_work = 0;
  }
}

static void
pvr_tag_refresh(pvr_t *pvr, int tag)
{
  tvheadend_t *tvh = &pvr->pvr_tvh;
  pvrchan_t *pvc;
  pvrprog_t *pvp;

  pthread_mutex_lock(&pvr->pvr_loader_lock);

  if(tag == -1) {

    pvrprog_update(pvr);

  } else {

    TAILQ_FOREACH(pvp, &pvr->pvr_log, pvp_linkq) {
      if(pvp->pvp_meta->tvp_reftag == tag)
	pvrprog_refresh(pvr, pvp);

      if(pvp->pvp_w_status_xfader != NULL)
	tvh_create_pvrstatus(pvp->pvp_w_status_xfader, 
			     pvp->pvp_meta->tvp_pvrstatus, 1.0f);
    }

    TAILQ_FOREACH(pvc, &pvr->pvr_channels, pvc_link) {
      LIST_FOREACH(pvp, &pvc->pvc_programs, pvp_link) {
	if(pvp->pvp_meta->tvp_reftag == tag) {
	  tvh_get_programme(tvh, pvp->pvp_meta, pvc->pvc_id, pvp->pvp_index);

	  if(pvp->pvp_w_status_xfader != NULL)
	    tvh_create_pvrstatus(pvp->pvp_w_status_xfader, 
				 pvp->pvp_meta->tvp_pvrstatus, 1.0f);

	  pvp_update_front_status(pvp);
	}
      }
    }
  }
  pthread_mutex_unlock(&pvr->pvr_loader_lock);
}


/*
 *
 */
static void
pvr_rec_cmd(pvr_t *pvr, pvrprog_t *pvp, const char *cmd)
{
  if(pvp == NULL)
    return;

  tvh_int(tvh_query(&pvr->pvr_tvh, "programme.record %d %d %s",
		    pvp->pvp_chan->pvc_id, pvp->pvp_index, cmd));
}

/*
 *
 */
static int
pvr_sa_2dnav_keystrike(pvr_t *pvr, inputevent_t *ie)
{
  appi_t *ai = pvr->pvr_ai;

  switch(ie->u.key) {
  default:
    break;

  case INPUT_KEY_BACK:
    pvr->pvr_ai->ai_widget->glw_flags &= ~GLW_ZOOMED; /* XXX: fix locking */
    pvr->pvr_cur_sa = 0;
    break;
    
  case INPUT_KEY_ENTER:
    ai->ai_menu_display = !ai->ai_menu_display;
    break;

  case INPUT_KEY_UP:
    if(!has_analogue_pad) {
      pvr->pvr_ty -= 0.2;
      return 1;
    }

    break;

  case INPUT_KEY_DOWN:
    if(!has_analogue_pad) {
      pvr->pvr_ty += 0.2;
      return 1;
    }

    break;

  case INPUT_KEY_RIGHT:
    if(!has_analogue_pad) {
      pvr->pvr_timeptr += 150;
      return 1;
    }

    break;

  case INPUT_KEY_LEFT:
    if(!has_analogue_pad) {
      pvr->pvr_timeptr -= 150;
      return 1;
    }


  case INPUT_KEY_NEXT:
    pvr->pvr_timeptr += 86400;
    return 1;
  case INPUT_KEY_PREV:
  case INPUT_KEY_RESTART_TRACK:
    pvr->pvr_timeptr -= 86400;
    return 1;

  case INPUT_KEY_SEEK_FORWARD:
    pvr->pvr_timeptr += 7200;
    return 1;

  case INPUT_KEY_SEEK_BACKWARD:
    pvr->pvr_timeptr -= 7200;
    return 1;


  case INPUT_KEY_RECORD_ONCE:
    pvr_rec_cmd(pvr, pvr->pvr_selected, "once");
    return 1;

  case INPUT_KEY_RECORD_DAILY:
    pvr_rec_cmd(pvr, pvr->pvr_selected, "daily");
    return 1;

  case INPUT_KEY_RECORD_WEEKLY:
    pvr_rec_cmd(pvr, pvr->pvr_selected, "weekly");
    return 1;

  case INPUT_KEY_RECORD_CANCEL:
    pvr_rec_cmd(pvr, pvr->pvr_selected, "cancel");
    return 1;

  case INPUT_KEY_RECORD_TOGGLE:
    pvr_rec_cmd(pvr, pvr->pvr_selected, "toggle");
    return 1;
  }
  return 0;
}

/*
 *
 */

static void
pvr_sa_2dnav_ie(pvr_t *pvr, inputevent_t *ie)
{
  int reload = 0;

  switch(ie->type) {

  default:
    break;

  case INPUT_PAD:
    pvr->pvr_ty += ie->u.xy.y / 50.0;
    pvr->pvr_timeptr += ie->u.xy.x * 60;

    if(ie->u.xy.x != 0)
      reload = 1;
    break;

  case INPUT_KEY:
    reload = pvr_sa_2dnav_keystrike(pvr, ie);
    break;
  }

  if(reload) {
    pthread_mutex_lock(&pvr->pvr_loader_lock);
    pvr->pvr_loader_work = 1;
    pthread_cond_signal(&pvr->pvr_loader_cond);
    pthread_mutex_unlock(&pvr->pvr_loader_lock);
  }
}

/*
 *
 */

static void
pvr_sa_sched_ie(pvr_t *pvr, inputevent_t *ie)
{
}

/*
 *
 */

static void
pvr_sa_none_ie(pvr_t *pvr, inputevent_t *ie)
{
  glw_t *w = pvr->pvr_ai->ai_widget;

  switch(ie->type) {

  default:
    break;

  case INPUT_KEY:

    switch(ie->u.key) {

    case INPUT_KEY_BACK:
      layout_hide(pvr->pvr_ai);
      break;

    case INPUT_KEY_UP:
      glw_nav_signal(w, GLW_SIGNAL_UP);
      break;

    case INPUT_KEY_DOWN:
      glw_nav_signal(w, GLW_SIGNAL_DOWN);
      break;

    case INPUT_KEY_LEFT:
      glw_nav_signal(w, GLW_SIGNAL_LEFT);
      break;

    case INPUT_KEY_RIGHT:
      glw_nav_signal(w, GLW_SIGNAL_RIGHT);
      break;

    case INPUT_KEY_ENTER:
      pvr->pvr_cur_sa = w->glw_selected->glw_u32;
      glw_nav_signal(w, GLW_SIGNAL_ENTER);
      break;

    default:
      break;
    }
  }
}

/*
 *
 */

static glw_t *
add_sub_app(glw_t *p, const char *title, const char *icon, pvr_subapp_t said)
{
  glw_t *w, *x, *z, *y;

  w = glw_create(GLW_ZOOM_SELECTOR,
		 GLW_ATTRIB_U32, said,
		 GLW_ATTRIB_PARENT, p,
		 NULL);
 
  z = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_PARENT, w,
		 GLW_ATTRIB_FILENAME, "icon://plate-titled.png",
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 NULL);
  
  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, z,
		 NULL);
  
  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_WEIGHT, 0.15,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_CAPTION, title,
	     GLW_ATTRIB_PARENT, y,
	     NULL);
    

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_WEIGHT, 0.1,
	     NULL);

  x = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_WEIGHT, 0.9,
		 GLW_ATTRIB_PARENT, y,
		 NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_WEIGHT, 0.2,
	     NULL);

  glw_create(GLW_BITMAP,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_FILENAME, icon,
	     NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_WEIGHT, 0.2,
	     NULL);

  return w;
}





/*
 *
 */

static void *
pvr_thread(void *aux)
{
  appi_t *ai = aux;
  pvr_t *pvr = calloc(1, sizeof(pvr_t));
  tvheadend_t *tvh = &pvr->pvr_tvh;
  pthread_t ptid;
  inputevent_t ie;
  glw_t *w;

  pthread_mutex_init(&pvr->pvr_loader_lock, NULL);
  pthread_cond_init(&pvr->pvr_loader_cond, NULL);

  //  mm->mm_pvr = pvr;

  time(&pvr->pvr_timeptr);
  
  pvr->pvr_timeptr = (pvr->pvr_timeptr / 60) * 60;
  pvr->pvr_x = pvr->pvr_timeptr;
  pvr->pvr_autoscaler = 3600;

  TAILQ_INIT(&pvr->pvr_channels);
  TAILQ_INIT(&pvr->pvr_log);
  pvr->pvr_tag_hash = glw_taghash_create();
  pvr->pvr_yzoom = 5.0f;
  pvr->pvr_xzoom = 5.0f;
  pvr->pvr_ai = ai;


  ai->ai_widget = 
    glw_create(GLW_ARRAY,
	       GLW_ATTRIB_OPAQUE, ai,
	       GLW_ATTRIB_CALLBACK, appi_widget_post_key,
	       GLW_ATTRIB_SIDEKICK, bar_title("Video Recorder"),
	       NULL);

  w = add_sub_app(ai->ai_widget, "Program guide", "icon://epg.png", 
		  PVR_SA_2DNAV);


  glw_create(GLW_EXT,
	     GLW_ATTRIB_PARENT, w,
	     GLW_ATTRIB_CALLBACK, pvr_2dnav_callback,
	     GLW_ATTRIB_OPAQUE, pvr,
	     NULL);

  w = add_sub_app(ai->ai_widget, "Scheduled Recordings", "icon://clock.png",
		  PVR_SA_SCHED);

  pvr->pvr_w_sched = 
    glw_create(GLW_ARRAY,
	       GLW_ATTRIB_PARENT, w,
	       GLW_ATTRIB_SIDEKICK, bar_title("Scheduled recordings"),
	       NULL);

  pvr_create_bar(pvr);

  pvr_create_recording_submenu(pvr);

  tvh_init(tvh, &ai->ai_ic);

  while(1) {

    pvr->pvr_num_chan = tvh_int(tvh_query(tvh, "channels.total"));
    pvr_load_chans(pvr);
    pvrprog_update(pvr);

    pthread_create(&ptid, NULL, pvr_update_thread, pvr);

    while(1) {
      input_getevent(&ai->ai_ic, 1, &ie, NULL);

      if(ie.type == INPUT_SPECIAL) {
	pvr_tag_refresh(pvr, ie.u.u32);
	continue;
      }

      switch(pvr->pvr_cur_sa) {
      case PVR_SA_NONE:
	pvr_sa_none_ie(pvr, &ie);
	break;

      case PVR_SA_2DNAV:
	pvr_sa_2dnav_ie(pvr, &ie);
	break;

      case PVR_SA_SCHED:
	pvr_sa_sched_ie(pvr, &ie);
	break;

	
      default:
	break;

      }

    }
  }
  return NULL;
}


/*
 *
 */
static void
render_list(struct glw_head *head, glw_rctx_t *rc)
{
  glw_rctx_t rc0;
  glw_t *w;

  LIST_FOREACH(w, head, glw_tmp_link) {
    glPushMatrix();
    glTranslatef(w->glw_pos.x, w->glw_pos.y, w->glw_pos.z);
    glScalef(w->glw_scale.x, w->glw_scale.y, w->glw_scale.z);
    glRotatef(w->glw_extra, 1.0f, 0.0f, 0.0f);
    rc0 = w->glw_rctx;
    rc0.rc_alpha *= rc->rc_alpha;
    glw_render(w, &rc0);
    glPopMatrix();
  }
}



/*
 *
 */
static void 
pvr_2dnav_render(pvr_t *pvr, glw_rctx_t *rc)
{

  GLdouble clip_top[4] = {0.0, -1.0, 0.0, 1.0};
  GLdouble clip_left[4] = {1.0, 0.0, 0.0, 0.75};
  GLdouble clip_right[4] = {-1.0, 0.0, 0.0, 1.0};

  //  glClipPlane(GL_CLIP_PLANE0, clip_top);
  //  glEnable(GL_CLIP_PLANE0);
  

  render_list(&pvr->pvr_rlist_1, rc);

  if(pvr->pvr_y > 1.6) {
    clip_top[3] = 0.833;
    glEnable(GL_CLIP_PLANE0);
    glClipPlane(GL_CLIP_PLANE0, clip_top);
  }

  render_list(&pvr->pvr_rlist_2, rc);

  glClipPlane(GL_CLIP_PLANE1, clip_left);
  glClipPlane(GL_CLIP_PLANE2, clip_right);
  glEnable(GL_CLIP_PLANE1);
  //  glEnable(GL_CLIP_PLANE2);
  
  render_list(&pvr->pvr_rlist_3, rc);

  glDisable(GL_CLIP_PLANE0);
  glDisable(GL_CLIP_PLANE1);
  glDisable(GL_CLIP_PLANE2);

}



/*
 *
 */
static void 
pvr_2dnav_layout(pvr_t *pvr, glw_rctx_t *rc)
{
  appi_t *ai = pvr->pvr_ai;
  pvrchan_t *pvc;
  pvrprog_t *pvp;
  float t, a0, a1, x1, x2, c, s, y;
  int timeptr;
  pvrprog_t *sel = NULL;
  float angle;
  float a2;
  float zo, za;
  glw_t *w;


  if(ai->ai_menu_display && pvr->pvr_zoomv < 1.0)
    pvr->pvr_zoomv += 0.02;
  else if(!ai->ai_menu_display && pvr->pvr_zoomv > 0.0)
    pvr->pvr_zoomv -= 0.02;
    
  zo = GLW_S(GLW_MIN(pvr->pvr_zoomv * 2, 1.0));
  a2 = GLW_LERP(zo, 0.2, 0.0);

  glDisable(GL_DEPTH_TEST);

  LIST_INIT(&pvr->pvr_rlist_1);
  LIST_INIT(&pvr->pvr_rlist_2);
  LIST_INIT(&pvr->pvr_rlist_3);

  if(pvr->pvr_ty < -1.0)
    pvr->pvr_ty = -1.0;

  y = pvr->pvr_y = (pvr->pvr_y * 7.0 + pvr->pvr_ty) / 8.0;
  pvr->pvr_x = (pvr->pvr_x * 7.0 + (double)pvr->pvr_timeptr) / 8.0f;
  pvr->pvr_tx = (pvr->pvr_tx * 7.0 + pvr->pvr_tx0) / 8.0f;

  if(y > 1.6)
    y = 1.6;

  w = pvr->pvr_bar;
  
  w->glw_pos.x = 0.0;
  w->glw_pos.y = 2 * y / 5 + 0.25;
  w->glw_pos.z = 0.0f;

  w->glw_scale.x = 1.0f;
  w->glw_scale.y = 0.05f;
  w->glw_scale.z = 0.05f;


  w->glw_rctx = *rc;
  w->glw_rctx.rc_aspect = rc->rc_aspect * w->glw_scale.x / w->glw_scale.y;

  w->glw_rctx.rc_alpha = (1 - zo);

  glw_layout(w, &w->glw_rctx);
  LIST_INSERT_HEAD(&pvr->pvr_rlist_1, w, glw_tmp_link);
  w->glw_flags |= GLW_TMP_LINKED;


  timeptr = pvr->pvr_x;

  TAILQ_FOREACH(pvc, &pvr->pvr_channels, pvc_link) {
    t = pvc->pvc_ypos - pvr->pvr_y;

    y = 2 * -t / 5;
    if(y < -1.2 || y > 1.2)
      continue;

    if(fabs(t) < 0.5) {
      a0 = 1.0f;
      za = GLW_S(GLW_MAX(pvr->pvr_zoomv * 2, 1.0) - 1.0f);
    } else {
      za = 0.0f;
      a0 = a2;
    }

    pvc->pvc_alpha = (pvc->pvc_alpha * 7 + a0) / 8;

    w = pvc->pvc_widget;

    w->glw_rctx = *rc;
    w->glw_rctx.rc_alpha = pvc->pvc_alpha;

    w->glw_pos.x = -0.88;
    w->glw_pos.y = GLW_LERP(za, 2 * -t / 5, 0.0);
    w->glw_pos.z = 0;

    w->glw_scale.x = 0.1;
    w->glw_scale.y = 0.2;
    w->glw_scale.z = 0.2;
    
    w->glw_extra = 0;

    w->glw_rctx.rc_aspect = 
      rc->rc_aspect * w->glw_scale.x / w->glw_scale.y;

    glw_layout(w, &w->glw_rctx);
    LIST_INSERT_HEAD(&pvr->pvr_rlist_2, w, glw_tmp_link);
    w->glw_flags |= GLW_TMP_LINKED;

    LIST_FOREACH(pvp, &pvc->pvc_programs, pvp_link) {
      x1 = (pvp->pvp_meta->tvp_start - timeptr) / pvr->pvr_autoscaler;
      x2 = (pvp->pvp_meta->tvp_stop -  timeptr) / pvr->pvr_autoscaler;
  

      s = x2 - x1;
      c = x1 + s / 2;

      if(x1 > 2 || x2 < -2)
	continue;

      if(pvp->pvp_meta->tvp_start <= pvr->pvr_timeptr &&
	 pvp->pvp_meta->tvp_stop > pvr->pvr_timeptr &&
	 fabs(t) < 0.5) {
	a1 = 1.0f;

	sel = pvp;
	za = zo;
      } else {
	za  = 0.0f;
	a1 = a2; //0.2f;
      }

 
      pvp->pvp_alpha = (pvp->pvp_alpha * 7 + a1) / 8;

      if(pvp->pvp_alpha < 0.01)
	continue;

      angle = 0 * 180.0f;

      w = pvp->pvp_widget;

      w->glw_rctx = *rc;
      w->glw_rctx.rc_alpha = pvp->pvp_alpha;
      
      w->glw_pos.x = GLW_LERP(za, 0.8 * c,   0.0);
      w->glw_pos.y = GLW_LERP(za, 0.4 * -t,  0.0);
      w->glw_pos.z = 0;

      w->glw_scale.x = GLW_LERP(za, 0.4 * s, 0.65);
      w->glw_scale.y = 0.19f;
      w->glw_scale.z = GLW_MIN(w->glw_scale.x, w->glw_scale.y);

      w->glw_rctx.rc_aspect = rc->rc_aspect * w->glw_scale.x / w->glw_scale.y;
      
      w->glw_extra = angle > 90 ? 180 - angle : angle;

      glw_layout(w, &w->glw_rctx);
      LIST_INSERT_HEAD(&pvr->pvr_rlist_3, w, glw_tmp_link);
      w->glw_flags |= GLW_TMP_LINKED;
    }
  }

  if(!ai->ai_menu_display)
    pvr->pvr_selected = sel;

  if((pvp = pvr->pvr_selected) != NULL) {
    
    zo = GLW_S(GLW_MAX(pvr->pvr_zoomv * 2, 1.0) - 1.0f);

    w = pvp->pvp_w_desc;
    if(w != NULL && zo > 0.0) {

      w->glw_pos.x = 0;
      w->glw_pos.y = GLW_LERP(zo, -1, -2.0/3.0);
      w->glw_pos.z = 0;

      w->glw_scale.x = 1.0f;
      w->glw_scale.y = 1.0/3.0;
      w->glw_scale.z = 1.0f;

      w->glw_rctx = *rc;
      w->glw_rctx.rc_alpha = zo;
      w->glw_rctx.rc_aspect = rc->rc_aspect * w->glw_scale.x / w->glw_scale.y;
      
      glw_layout(w, &w->glw_rctx);
      LIST_INSERT_HEAD(&pvr->pvr_rlist_1, w, glw_tmp_link);
      w->glw_flags |= GLW_TMP_LINKED;
    }

    w = pvp->pvp_w_status_container;
    if(w != NULL && zo > 0.0) {

      w->glw_pos.x = 0;
      w->glw_pos.y = GLW_LERP(zo, 1.0f, 2.0/3.0);
      w->glw_pos.z = 0;

      w->glw_scale.x = 1.0/3.0;
      w->glw_scale.y = 1.0/3.0;
      w->glw_scale.z = 1.0f;

      w->glw_rctx = *rc;
      w->glw_rctx.rc_alpha = zo;
      w->glw_rctx.rc_aspect = rc->rc_aspect * w->glw_scale.x / w->glw_scale.y;
      
      glw_layout(w, &w->glw_rctx);
      LIST_INSERT_HEAD(&pvr->pvr_rlist_1, w, glw_tmp_link);
      w->glw_flags |= GLW_TMP_LINKED;
    }


    w = pvp->pvp_w_menu_container;
    if(w != NULL && zo > 0.0) {

      w->glw_pos.x = 0.8;
      w->glw_pos.y = GLW_LERP(zo, 1.0f, 2.0/3.0);
      w->glw_pos.z = 0;

      w->glw_scale.x = 0.19f;
      w->glw_scale.y = 1.0/3.0;
      w->glw_scale.z = 1.0f;

      w->glw_rctx = *rc;
      w->glw_rctx.rc_alpha = zo;
      w->glw_rctx.rc_aspect = rc->rc_aspect * w->glw_scale.x / w->glw_scale.y;
      
      glw_layout(w, &w->glw_rctx);
      LIST_INSERT_HEAD(&pvr->pvr_rlist_1, w, glw_tmp_link);
      w->glw_flags |= GLW_TMP_LINKED;
    }
  }
}





static int
pvr_2dnav_callback(glw_t *w, glw_signal_t signal, ...)
{
  pvr_t *pvr = glw_get_opaque(w);
  inputevent_t *ie;
  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_EXT_RENDER:
    pvr_2dnav_render(pvr, va_arg(ap, void *));
    return 0;

  case GLW_SIGNAL_EXT_LAYOUT:
    pvr_2dnav_layout(pvr, va_arg(ap, void *));
    return 0;

  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);
    input_postevent(&pvr->pvr_ai->ai_ic, ie);
    return 1;

  default:
    return 0;
  }
  va_end(ap);
  return 0;
}


/*
 *
 */


const char *pvr_wdays[7] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", 
  "Thursday", "Friday", "Saturday"};


static int
pvr_curdate_callback(glw_t *w, glw_signal_t signal, ...)
{
  pvr_t *pvr = glw_get_opaque(w);
  struct tm *tm, tm0;
  char tmp[30];

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_PRE_LAYOUT:
    tm = localtime_r(&pvr->pvr_timeptr, &tm0);

    snprintf(tmp, sizeof(tmp),
	     "%s %d/%d", pvr_wdays[tm->tm_wday % 7], 
	     tm->tm_mday, tm->tm_mon + 1);

    glw_set(w, GLW_ATTRIB_CAPTION, tmp, NULL);
    break;

  default:
    break;
  }
  va_end(ap);
  return 0;
}


/*
 *
 */

static void 
pvr_create_bar(pvr_t *pvr)
{
  glw_t *x;
  
  pvr->pvr_bar = glw_create(GLW_BITMAP,
			    GLW_ATTRIB_FILENAME, "icon://plate-wide.png",
			    GLW_ATTRIB_FLAGS, GLW_NOASPECT,
			    NULL);

  x = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_PARENT, pvr->pvr_bar,
		 NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_CALLBACK, pvr_curdate_callback,
	     GLW_ATTRIB_OPAQUE, pvr,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_PARENT, x,
	     NULL);

}


/*
 *
 */
void 
pvr_spawn(appi_t *ai)
{
  pthread_create(&ai->ai_tid, NULL, pvr_thread, ai);
}

app_t app_pvr = {
  .app_name = "Video Recorder",
  .app_icon = "icon://pvr.png",
  .app_spawn = pvr_spawn
};
