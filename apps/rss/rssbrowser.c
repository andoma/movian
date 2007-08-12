/*
 *  RSS browser
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
#include <libglw/glw_slist.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include "showtime.h"
#include "input.h"
#include "play_file.h"
#include "rssbrowser.h"
#include "miw.h"
#include "rss.h"
#include "layout/layout.h"

typedef struct rssentry {
  TAILQ_ENTRY(rssentry) re_link;
  const char *re_url;
  pthread_t re_id;

  glw_t *re_widget;
  glw_t *re_title_widget1;
  glw_t *re_title_widget2;
  glw_t *re_image_container;
  glw_t *re_status_container;
  glw_t *re_sub;
  glw_t *re_item_list;

  struct rssbrowser *re_browser;

  int re_reloadtime;

} rssentry_t;

typedef struct rssbrowser {

  TAILQ_HEAD(, rssfeed) rb_feeds;
  glw_t *rb_list;
  int rb_zoomed;
  media_pipe_t rb_mp;

} rssbrowser_t;

static int 
rssfeed_entry_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  return 0;
}

/*
 *
 */

static void *
feed_thread(void *aux)
{
  rssentry_t *re = aux;
  rssfeed_t *rf;
  rss_channel_t *rch;
  rss_item_t *ri;
  glw_t *w, *c, *x;
  int has_desc;

  while(1) {
    
    glw_create(GLW_BITMAP, 
	       GLW_ATTRIB_PARENT, glw_create(GLW_ROTATOR,
					     GLW_ATTRIB_PARENT, 
					     re->re_status_container,
					     NULL),
	       GLW_ATTRIB_FILENAME, "icon://loading.png",
	       NULL);
    
    rf = rss_load(re->re_url);

    if(rf == NULL) {
      glw_create(GLW_BITMAP,
		 GLW_ATTRIB_FILENAME, "icon://error.png",
		 GLW_ATTRIB_PARENT, re->re_status_container,
		 NULL);
      sleep(10);
      continue;
    }

    glw_create(GLW_BITMAP,
	       GLW_ATTRIB_FILENAME, "icon://ok.png",
	       GLW_ATTRIB_PARENT, re->re_status_container,
	       NULL);
    
    rch = TAILQ_FIRST(&rf->channels);
    if(rch == NULL || re->re_item_list == NULL) {
      rss_free(rf);
      sleep(10);
      continue;
    }
    
    glw_set(re->re_title_widget1, 
	    GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	    GLW_ATTRIB_CAPTION, rch->title, 
	    NULL);

    glw_set(re->re_title_widget2,
	    GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	    GLW_ATTRIB_CAPTION, rch->title, 
	    NULL);
    
    has_desc = 0;

    w = TAILQ_FIRST(&re->re_item_list->glw_childs);

    TAILQ_FOREACH(ri, &rch->items, link) {

      if(ri->desc)
	has_desc = 1;


      if(w == NULL) {
	w = glw_create(GLW_XFADER,
		       GLW_ATTRIB_PARENT, re->re_item_list,
		       NULL);
      } else {
	c = glw_find_by_class(w, GLW_TEXT_BITMAP);
	if(!strcmp(c->glw_caption, ri->title)) {
	  w = TAILQ_NEXT(w, glw_parent_link);
	  continue;
	}
      }

      x = glw_create(GLW_BITMAP,
		     GLW_ATTRIB_PARENT, w,
		     GLW_ATTRIB_FILENAME, "icon://plate-wide.png",
		     GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		     NULL);

      x = glw_create(GLW_CONTAINER_X,
		     GLW_ATTRIB_PARENT, x,
		     GLW_ATTRIB_CAPTION, ri->media ? strdup(ri->media) : NULL,
		     NULL);


      if(ri->media) {
	glw_create(GLW_BITMAP,
		   GLW_ATTRIB_PARENT, x,
		   GLW_ATTRIB_FILENAME, "icon://video.png",
		   NULL);
      }

      glw_create(GLW_TEXT_BITMAP,
		 GLW_ATTRIB_PARENT, x,
		 GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
		 GLW_ATTRIB_WEIGHT, 10.0f,
		 GLW_ATTRIB_CAPTION, ri->title,
		 NULL);
      w = TAILQ_NEXT(w, glw_parent_link);
    }
  
    for(; w != NULL; w = c) {
      c = TAILQ_NEXT(w, glw_parent_link);
      glw_destroy(w);
    }

    rss_free(rf);

    glw_set(re->re_item_list, 
	    GLW_ATTRIB_SHOW_CHILDS, has_desc ? 7 : 11,
	    NULL);

    sleep(re->re_reloadtime);
  }
}


/*
 *
 */

static void
feed_add(rssbrowser_t *rb, const char *caption, const char *url,
	 int reloadtime)
{
  rssentry_t *re = calloc(1, sizeof(rssentry_t));
  glw_t *w, *x, *c, *y, *z, *v, *r;

  re->re_reloadtime = reloadtime ?: 60;

  re->re_url = strdup(url);

  v = glw_create(GLW_ZOOM_SELECTOR,
		 GLW_ATTRIB_SIGNAL_HANDLER, rssfeed_entry_callback, re, 0,
		 GLW_ATTRIB_PARENT, rb->rb_list,
		 NULL);

  c = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_PARENT, v,
		 GLW_ATTRIB_FILENAME, "icon://plate-titled.png",
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 NULL);

  re->re_widget = c;


  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, c,
		 NULL);

  z = glw_create(GLW_CONTAINER_Z,
		 GLW_ATTRIB_PARENT, y,
		 GLW_ATTRIB_WEIGHT, 0.3,
		 NULL);

  re->re_title_widget1 = glw_create(GLW_TEXT_BITMAP,
				    GLW_ATTRIB_PARENT, z,
				    GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
				    GLW_ATTRIB_CAPTION, caption,
				    NULL);

  x = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_PARENT, z,
		 NULL);
  

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_WEIGHT, 10.0f,
	     NULL);

  re->re_status_container = glw_create(GLW_XFADER,
				       GLW_ATTRIB_PARENT, x,
				       NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_WEIGHT, 0.1,
	     NULL);

  z = glw_create(GLW_CONTAINER_Z,
		 GLW_ATTRIB_PARENT, y,
		 GLW_ATTRIB_WEIGHT, 2.1,
		 GLW_ATTRIB_PARENT, y,
		 NULL);

  x = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_PARENT, z,
		 NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_WEIGHT, 0.5,
	     NULL);

  glw_create(GLW_BITMAP,
	     GLW_ATTRIB_FILENAME, "icon://rss.png",
	     GLW_ATTRIB_ALPHA, 0.25,
	     GLW_ATTRIB_PARENT, x,
	     NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, x,
	     GLW_ATTRIB_WEIGHT, 0.5,
	     NULL);

  w = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, z,
		 NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_WEIGHT, 4.0f,
	     GLW_ATTRIB_PARENT, w,
	     NULL);

  r = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_FILENAME, "icon://plate-wide.png",
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 NULL);

  re->re_title_widget2 = glw_create(GLW_TEXT_BITMAP,
				    GLW_ATTRIB_PARENT, r,
				    GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
				    GLW_ATTRIB_CAPTION, caption,
				    NULL);

  re->re_item_list = glw_create(GLW_ARRAY,
				GLW_ATTRIB_PARENT, v,
				GLW_ATTRIB_SIDEKICK, r,
				GLW_ATTRIB_X_SLICES, 1,
				GLW_ATTRIB_Y_SLICES, 11,
				NULL);

  pthread_create(&re->re_id, NULL, feed_thread, re);
}


/*
 *
 */
#if 0
static void
feed_item_clicked(appi_t *ai, rssbrowser_t *rb, glw_t *w)
{

  w = glw_find_by_class(w, GLW_CONTAINER_X);
  if(w == NULL)
    return;

  if(w->glw_caption == NULL)
    return;

  //  play_file(w->glw_caption, ai, &ai->ai_ic, NULL, NULL, NULL);
}
#endif

/*
 *
 */

static void
rssfeed_add(rssbrowser_t *rb, struct config_head *head)
{
  const char *url, *title, *reloadtime;

  if((url = config_get_str_sub(head, "url", NULL)) == NULL)
    return;

  title = config_get_str_sub(head, "title", NULL);

  reloadtime = config_get_str_sub(head, "reload", NULL);

  feed_add(rb, title, url, reloadtime ? atoi(reloadtime) : 0);
}
  


/*
 *
 */

static void
rssfeeds_configure(rssbrowser_t *rb)
{
  config_entry_t *ce;

  TAILQ_FOREACH(ce, &config_list, ce_link) {
    if(ce->ce_type == CFG_SUB && !strcasecmp("rssfeed", ce->ce_key)) {
      rssfeed_add(rb, &ce->ce_sub);
    }
  }
}



/*
 *
 */

static void *
rssbrowser_thread(void *aux)
{
  appi_t *ai = aux;
  rssbrowser_t *rb = calloc(1, sizeof(rssbrowser_t));
  inputevent_t ie;
  rssentry_t *re = NULL;

  TAILQ_INIT(&rb->rb_feeds);

  ai->ai_widget = rb->rb_list = 
    glw_create(GLW_ARRAY,
	       GLW_ATTRIB_SIGNAL_HANDLER, appi_widget_post_key, ai, 0,
	       GLW_ATTRIB_SIDEKICK, bar_title("Really Simple Syndication"),
	       NULL);

  rssfeeds_configure(rb);

  if(TAILQ_FIRST(&rb->rb_list->glw_childs) == NULL) {
    glw_destroy(ai->ai_widget);
    ai->ai_widget = 
      glw_create(GLW_TEXT_BITMAP,
		 GLW_ATTRIB_SIGNAL_HANDLER, appi_widget_post_key, ai, 0,
		 GLW_ATTRIB_CAPTION, "No feeds configured",
		 NULL);
    while(1) 
      sleep(1);
  }

  while(1) {

    input_getevent(&ai->ai_ic, 1, &ie, NULL);

    switch(ie.type) {

    default:
      break;
      
    case INPUT_KEY:
      if(ie.u.key == INPUT_KEY_CLOSE) {
	layout_hide(ai);
	break;
      }

      if(re) {

	switch(ie.u.key) {
	default:
	  break;

	case INPUT_KEY_UP:
	  glw_nav_signal(re->re_item_list, GLW_SIGNAL_UP);
	  break;

	case INPUT_KEY_DOWN:
	  glw_nav_signal(re->re_item_list, GLW_SIGNAL_DOWN);
	  break;

	case INPUT_KEY_LEFT:
	  glw_nav_signal(re->re_item_list, GLW_SIGNAL_LEFT);
	  break;

	case INPUT_KEY_RIGHT:
	  glw_nav_signal(re->re_item_list, GLW_SIGNAL_RIGHT);
	  break;


	case INPUT_KEY_ENTER:
	  break;
	
	case INPUT_KEY_BACK:
	  rb->rb_list->glw_flags &= ~GLW_ZOOMED; /* XXX locking */
	  re = NULL;
	  break;
	}

      } else {

	switch(ie.u.key) {
	default:
	  break;

	case INPUT_KEY_UP:
	  glw_nav_signal(rb->rb_list, GLW_SIGNAL_UP);
	  break;

	case INPUT_KEY_DOWN:
	  glw_nav_signal(rb->rb_list, GLW_SIGNAL_DOWN);
	  break;

	case INPUT_KEY_LEFT:
	  glw_nav_signal(rb->rb_list, GLW_SIGNAL_LEFT);
	  break;

	case INPUT_KEY_RIGHT:
	  glw_nav_signal(rb->rb_list, GLW_SIGNAL_RIGHT);
	  break;

	case INPUT_KEY_ENTER:
	  glw_nav_signal(rb->rb_list, GLW_SIGNAL_ENTER);
	  re = glw_get_opaque(rb->rb_list->glw_selected,
			      rssfeed_entry_callback);
	  break;
	
	case INPUT_KEY_BACK:
	  layout_hide(ai);
	  break;
	}
      }
    }
  }
  return NULL;
}


/*
 *
 */

void 
rssbrowser_spawn(appi_t *ai)
{
  pthread_create(&ai->ai_tid, NULL, rssbrowser_thread, ai);
}



app_t app_rss = {
  .app_name = "RSS browser",
  .app_icon = "icon://rss.png",
  .app_spawn = rssbrowser_spawn
};
