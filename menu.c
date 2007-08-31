/*
 *  User menues
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

#include <sys/time.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#include <ffmpeg/avstring.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "input.h"
#include "layout/layout.h"
#include "menu.h"
#include "settings.h"
#include "apps/dvdplayer/dvd.h"
#include "apps/playlist/playlist.h"
#include "gl/gl_video.h"

typedef struct menu {
  LIST_ENTRY(menu) m_link;
  int m_expanded;
  int m_istop;
  glw_t *m_plate;
  glw_t *m_array;
  glw_t *m_stack;

  glw_vertex_anim_t m_pos;
  glw_vertex_anim_t m_alpha;
  float m_alpha2;
} menu_t;

#define MENU_Z_MAX 3.0

#define menu_opaque(c) (glw_get_opaque(c, menu_submenu_item_event))

/*
 * Callback for sub menus
 */
static int 
menu_submenu_item_event(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  menu_t *m = opaque;
  inputevent_t *ie;
  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);
    if(ie->type == INPUT_KEY && ie->u.key == INPUT_KEY_ENTER)
      m->m_expanded = 1;
    return 1;
  case GLW_SIGNAL_DESTROY:
    glw_destroy(m->m_plate);
    return 1;

  default:
    return 0;
  }
}

/*
 * Callback for the GLW_BITMAP component
 */
static int 
menu_bitmap_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  menu_t *m = opaque;

  switch(signal) {
  case GLW_SIGNAL_DTOR:
    free(m);
    return 1;

  default:
    return 0;
  }
}

/*
 * Callback for the GLW_ARRAY component
 */
static int 
menu_array_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  return 0;
}

/*
 *
 */
void
menu_collapse(menu_t *m)
{
  if(m->m_istop)
    return;
  glw_vertex_anim_set3f(&m->m_pos,   0.0, 0.0, MENU_Z_MAX);
  glw_vertex_anim_set3f(&m->m_alpha, 0.0, 0.0, 0.0);
  glw_vertex_anim_fwd(&m->m_pos,   1.0);
  glw_vertex_anim_fwd(&m->m_alpha, 1.0);
}

/*
 *
 */
int
menu_input(glw_t *w, inputevent_t *ie)
{
  glw_t *c;
  menu_t *m = NULL, *mm;

  w = glw_find_by_class(w, GLW_ARRAY);
  if(w == NULL)
    return 0;

  while(1) {
    c = w->glw_selected;
    if(c == NULL)
      return 0;

    if((mm = menu_opaque(c)) != NULL) {
      /* This is a sub menu */
      if(mm->m_expanded) {
	/* It's expanded, continue down */
	m = mm;
	w = m->m_array;
	continue;
      }
    }
    break;
  }

  if(ie->type == INPUT_KEY) {
    switch(ie->u.key) {
    case INPUT_KEY_UP:
      glw_nav_signal(w, GLW_SIGNAL_UP);
      return 0;

    case INPUT_KEY_DOWN:
      glw_nav_signal(w, GLW_SIGNAL_DOWN);
      return 0;

    case INPUT_KEY_BACK:
      if(m != NULL)
	m->m_expanded = 0;
      else
	return 1; /* Close menu view */
    default:
      break;
    }
  }
  if(ie->type == INPUT_KEY && ie->u.key == INPUT_KEY_ENTER)
    glw_drop_signal(c, GLW_SIGNAL_CLICK, NULL);

  glw_drop_signal(c, GLW_SIGNAL_INPUT_EVENT, ie);
  return 0;
}


/*
 *
 */
static glw_t *
menu_create_item0(glw_t *p, const char *icon, const char *title,
		  glw_callback_t *cb, void *opaque, int pri,
		  uint32_t u32, int first)
{
  glw_t *x;
  menu_t *m;

  m = glw_get_opaque(p, menu_bitmap_callback);
  if(m == NULL)
    m = glw_get_opaque(p, menu_submenu_item_event);

  p = m->m_array;
  assert(p != NULL);

  x = glw_create(GLW_CONTAINER_X,
		 GLW_ATTRIB_ASPECT, 10.0f,
		 first ? GLW_ATTRIB_PARENT_HEAD : GLW_ATTRIB_PARENT, p,
		 GLW_ATTRIB_SIGNAL_HANDLER, cb, opaque, pri,
		 GLW_ATTRIB_U32, u32,
		 NULL);

  if(icon != NULL) {
    glw_create(GLW_BITMAP,
	       GLW_ATTRIB_FILENAME, icon,
	       GLW_ATTRIB_WEIGHT, 0.1,
	       GLW_ATTRIB_PARENT, x,
	       NULL);
  }

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_CAPTION, title,
	     GLW_ATTRIB_PARENT, x,
	     NULL);

  return x;
}


/*
 *
 */
glw_t *
menu_create_item(glw_t *p, const char *icon, const char *title,
		 glw_callback_t *cb, void *opaque, uint32_t u32, int first)
{
  return menu_create_item0(p, icon, title, cb, opaque, 0, u32, first);
}




/*
 *
 */
static glw_t *
menu_create_container0(glw_t *p, glw_callback_t *cb, void *opaque, int pri,
		       uint32_t u32, int first)
{
  glw_t *x;
  menu_t *m;

  m = glw_get_opaque(p, menu_bitmap_callback);
  if(m == NULL)
    m = glw_get_opaque(p, menu_submenu_item_event);

  p = m->m_array;
  assert(p != NULL);

  x = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_ASPECT, 10.0f,
		 first ? GLW_ATTRIB_PARENT_HEAD : GLW_ATTRIB_PARENT, p,
		 GLW_ATTRIB_SIGNAL_HANDLER, cb, opaque, pri,
		 GLW_ATTRIB_U32, u32,
		 NULL);
  return x;
}


/*
 *
 */
glw_t *
menu_create_container(glw_t *p, glw_callback_t *cb, void *opaque,
		      uint32_t u32, int first)
{
  return menu_create_container0(p, cb, opaque, 0, u32, first);
}


/*
 *
 */
static glw_t *
menu_create_menu(glw_t *p, const char *title)
{
  glw_t *w, *b, *y;
  menu_t *m = calloc(1, sizeof(menu_t));

  glw_vertex_anim_init(&m->m_pos, 0, 0, MENU_Z_MAX, GLW_VERTEX_ANIM_SIN_LERP);
  glw_vertex_anim_init(&m->m_alpha, 0, 0, 0, GLW_VERTEX_ANIM_SIN_LERP);

  b = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_FILENAME, "icon://plate-titled.png",
		 GLW_ATTRIB_SIGNAL_HANDLER, menu_bitmap_callback, m, 0,
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 GLW_ATTRIB_BORDER_WIDTH, 0.27,
		 NULL);

  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, b,
		 NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_CAPTION, title,
	     GLW_ATTRIB_WEIGHT, 0.1f,
	     NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_WEIGHT, 0.1f,
	     GLW_ATTRIB_PARENT, y,
	     NULL);

  w = glw_create(GLW_ARRAY,
		 GLW_ATTRIB_BORDER_WIDTH, 0.25f,
		 GLW_ATTRIB_PARENT, y,
		 GLW_ATTRIB_SIGNAL_HANDLER, menu_array_callback, m, 0,
		 GLW_ATTRIB_X_SLICES, 1,
		 GLW_ATTRIB_Y_SLICES, 9,
		 NULL);

  if(p != NULL)
    glw_set(p, 
	    GLW_ATTRIB_SIGNAL_HANDLER, menu_submenu_item_event, m, 0,
	    NULL);
  else {
    m->m_expanded = 1;
    m->m_istop = 1;
  }

  m->m_plate = b;
  m->m_array = w;
  return b;
}



/*
 *
 */
glw_t *
menu_create_submenu(glw_t *p, const char *icon, const char *title, int first)
{
  glw_t *x;

  char title2[40];

  snprintf(title2, sizeof(title2), "%s...", title);

  x = menu_create_item(p, icon, title2, menu_submenu_item_event, NULL, 0,
		       first);
  menu_create_menu(x, title);
  return x;
}

/*
 *
 */
glw_t *
menu_create_submenu_cb(glw_t *p, const char *icon, const char *title,
		       int first, glw_callback_t *cb, void *opaque)
{
  glw_t *x;

  x = menu_create_item0(p, icon, title, cb, opaque, 0, 0, first);
  menu_create_menu(x, title);
  return x;
}


/*
 *
 */

glw_t *
menu_push_top_menu(appi_t *ai, const char *title)
{
  char menutitle[32];
  glw_t *c;
  menu_t *m;

  av_strlcpy(menutitle, title, sizeof(menutitle));
  memcpy(menutitle + sizeof(menutitle) - 4, "...", 4);

  c = menu_create_menu(NULL, menutitle);
  m = glw_get_opaque(c, menu_bitmap_callback);

  settings_menu_create(c);

  if(ai != NULL) {
    glw_lock();
    m->m_stack = ai->ai_menu;
    ai->ai_menu = c;
    glw_unlock();
  }

  return c;
}


void
menu_pop_top_menu(appi_t *ai)
{
  glw_t *c = ai->ai_menu;
  menu_t *m;

  glw_lock();
  m = glw_get_opaque(c, menu_bitmap_callback);
  assert(m->m_stack != NULL);
  ai->ai_menu = m->m_stack;
  glw_destroy(c);
  glw_unlock();
}


/*
 *
 */

void
menu_init_app(appi_t *ai)
{
  menu_push_top_menu(ai, ai->ai_app->app_name);
}


/*
 *
 */
int 
menu_post_key_pop_and_hide(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  appi_t *ai = opaque;
  menu_t *m = glw_get_opaque(w->glw_parent, menu_array_callback);

  switch(signal) {
  case GLW_SIGNAL_CLICK:
    input_keystrike(&ai->ai_ic, glw_get_u32(w));
    menu_collapse(m);
    layout_menu_display = 0;
    return 1;
    
  default:
    return 0;
  }
}





/*
 *
 */
void
menu_layout(glw_t *w)
{
  glw_t *c;
  menu_t *m;
  glw_rctx_t rc;
  int d = 0;
  float af = 1.0f;
  LIST_HEAD(, menu) list;

  LIST_INIT(&list);

  memset(&rc, 0, sizeof(rc));
  rc.rc_aspect = MENU_ASPECT * (16.0f / 9.0f);

  m = glw_get_opaque(w, menu_bitmap_callback);

  m->m_alpha2 = GLW_LP(16, m->m_alpha2, layout_menu_display);

  while(w != NULL) {
    m = glw_get_opaque(w, menu_bitmap_callback);

    LIST_INSERT_HEAD(&list, m, m_link);
    if((c = glw_find_by_class(w, GLW_ARRAY)->glw_selected) == NULL)
      break;
    if((m = menu_opaque(c)) == NULL)
      break;
    w = m->m_plate;
  }

  LIST_FOREACH(m, &list, m_link) {
    w = m->m_plate;
    glw_vertex_anim_fwd(&m->m_pos,   0.02);
    glw_vertex_anim_fwd(&m->m_alpha, 0.02);

    if(m->m_expanded) {
      glw_vertex_anim_set3f(&m->m_pos,   0.0, 0.0, -d * 0.5);
      glw_vertex_anim_set3f(&m->m_alpha, af, 0.0, 0.0);
      d++;
      af *= 0.5;
    } else {
      glw_vertex_anim_set3f(&m->m_pos,   0.0, 0.0, MENU_Z_MAX);
      glw_vertex_anim_set3f(&m->m_alpha, 0.0, 0.0, 0.0);
    }


    glw_layout(w, &rc);
  }
}

/*
 *
 */
void
menu_render(glw_t *w, float alpha)
{
  glw_rctx_t rc;
  glw_t *c;
  glw_vertex_t xyz;
  menu_t *m;

  m = glw_get_opaque(w, menu_bitmap_callback);

  alpha *= m->m_alpha2;

  if(alpha < 0.01)
    return;

  glPushMatrix();
  glTranslatef(-1.8, 0.0, 0.0);
  glTranslatef(0, 1, 0);
  glRotatef(45, 0, 1, 0);
  glScalef(MENU_ASPECT, 1.0f, 1.0f);

  memset(&rc, 0, sizeof(rc));
  rc.rc_aspect = MENU_ASPECT * (16.0f / 9.0f);

  while(w != NULL) {
    m = glw_get_opaque(w, menu_bitmap_callback);

    glw_vertex_anim_read(&m->m_alpha, &xyz);
    rc.rc_alpha = alpha * xyz.x;
    if(rc.rc_alpha < 0.01)
      break;

    glPushMatrix();
    glw_vertex_anim_read(&m->m_pos, &xyz);
    glTranslatef(xyz.x, xyz.y, xyz.z);

    glw_render(w, &rc);
    glPopMatrix();

    if((c = glw_find_by_class(w, GLW_ARRAY)->glw_selected) == NULL)
      break;
    if((m = menu_opaque(c)) == NULL)
      break;

    w = m->m_plate;
  }

  glPopMatrix();
}
