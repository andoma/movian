/*
 *  Showtime mediacenter
 *  Copyright (C) 2007-2013 Andreas Öman
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

#include "showtime.h"
#include "prop/prop.h"
#include "background.h"


typedef struct bg_helper {
  void (*set_image)(rstr_t *url, const char **vpaths, void *opaque);
  void (*set_alpha)(float alpha, void *opaque);
  void *opaque;
  prop_courier_t *pc;

  int fullwindow;
  int screensaver;

  rstr_t *skin_path;
  rstr_t *bg_url[3];
  float bg_alpha[2];
} bg_helper_t;


/**
 *
 */
static void
set_in_fullwindow(bg_helper_t *bgh, int v)
{
  bgh->fullwindow = v;
}


/**
 *
 */
static void
set_in_screensaver(bg_helper_t *bgh, int v)
{
  bgh->screensaver = v;
}


/**
 *
 */
static void
set_skin_path(bg_helper_t *bgh, rstr_t *r)
{
  rstr_set(&bgh->skin_path, r);
}


/**
 *
 */
static void
set_bg0(bg_helper_t *bgh, rstr_t *r)
{
  rstr_set(&bgh->bg_url[0], r);
}


/**
 *
 */
static void
set_bg1(bg_helper_t *bgh, rstr_t *r)
{
  rstr_set(&bgh->bg_url[1], r);
}


/**
 *
 */
static void
set_bg2(bg_helper_t *bgh, rstr_t *r)
{
  rstr_set(&bgh->bg_url[2], r);
}


/**
 *
 */
static void
set_alpha0(bg_helper_t *bgh, float a)
{
  bgh->bg_alpha[0] = a;
}


/**
 *
 */
static void
set_alpha1(bg_helper_t *bgh, float a)
{
  bgh->bg_alpha[1] = a;
}


/**
 *
 */
static void *
bgloader_thread(void *aux)
{
  bg_helper_t *bgh = aux;

  rstr_t *current_bg = NULL;
  float current_alpha = 0;

  while(1) {
    struct prop_notify_queue q;

    float alpha = 1.0f;
    int timo = alpha != current_alpha ? 50 : 0;

    prop_courier_wait(bgh->pc, &q, timo);
    prop_notify_dispatch(&q);

    rstr_t *bg;
    if(bgh->bg_url[0]) {
      bg = bgh->bg_url[0];

      if(bgh->bg_alpha[0])
	alpha = bgh->bg_alpha[0];
    } else if(bgh->bg_url[1]) {
      bg = bgh->bg_url[1];

      if(bgh->bg_alpha[1])
	alpha = bgh->bg_alpha[1];
    } else {
      bg = bgh->bg_url[2];
    }

    if(bgh->fullwindow || bgh->screensaver)
      alpha = 0;

    if(alpha != current_alpha) {
      if(current_alpha < alpha)
	current_alpha = MIN(alpha, current_alpha + 0.1);
      else if(current_alpha > alpha)
	current_alpha = MAX(alpha, current_alpha - 0.1);

      bgh->set_alpha(current_alpha, bgh->opaque);
    }

    if(!rstr_eq(current_bg, bg)) {
      rstr_set(&current_bg, bg);
      const char *v[3];

      v[0] = "skin";
      v[1] = rstr_get(bgh->skin_path);
      v[2] = NULL;

      bgh->set_image(bg, v, bgh->opaque);
    }

  }
  return NULL;
}

/**
 *
 */
void
background_init(prop_t *ui, prop_t *nav,
		void (*set_image)(rstr_t *url, const char **vpaths, 
				  void *opaque),
		void (*set_alpha)(float alpha, void *opaque),
		void *opaque)
{
  bg_helper_t *bgh = calloc(1, sizeof(bg_helper_t));
  bgh->pc = prop_courier_create_waitable();
  bgh->set_image = set_image;
  bgh->set_alpha = set_alpha;

  prop_subscribe(0,
		 PROP_TAG_NAME("ui","fullwindow"),
		 PROP_TAG_CALLBACK_INT, set_in_fullwindow, bgh,
		 PROP_TAG_ROOT, ui,
		 PROP_TAG_COURIER, bgh->pc,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("ui","screensaverActive"),
		 PROP_TAG_CALLBACK_INT, set_in_screensaver, bgh,
		 PROP_TAG_ROOT, ui,
		 PROP_TAG_COURIER, bgh->pc,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("ui","skin", "path"),
		 PROP_TAG_CALLBACK_RSTR, set_skin_path, bgh,
		 PROP_TAG_ROOT, ui,
		 PROP_TAG_COURIER, bgh->pc,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("ui","background"),
		 PROP_TAG_CALLBACK_RSTR, set_bg2, bgh,
		 PROP_TAG_ROOT, ui,
		 PROP_TAG_COURIER, bgh->pc,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("nav","currentpage","glw", "background"),
		 PROP_TAG_CALLBACK_RSTR, set_bg1, bgh,
		 PROP_TAG_ROOT, nav,
		 PROP_TAG_COURIER, bgh->pc,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("nav","currentpage", "model", "metadata",
			       "background"),
		 PROP_TAG_CALLBACK_RSTR, set_bg0, bgh,
		 PROP_TAG_ROOT, nav,
		 PROP_TAG_COURIER, bgh->pc,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("nav","currentpage","glw", "backgroundAlpha"),
		 PROP_TAG_CALLBACK_FLOAT, set_alpha1, bgh,
		 PROP_TAG_ROOT, nav,
		 PROP_TAG_COURIER, bgh->pc,
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_NAME("nav","currentpage", "model", "metadata",
			       "backgroundAlpha"),
		 PROP_TAG_CALLBACK_FLOAT, set_alpha0, bgh,
		 PROP_TAG_ROOT, nav,
		 PROP_TAG_COURIER, bgh->pc,
		 NULL);

  hts_thread_create_detached("bgloader", bgloader_thread, bgh, 
			     THREAD_PRIO_UI_WORKER_LOW);
}
