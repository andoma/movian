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

#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
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
  glw_t *m_plate;
  glw_t *m_array;

  glw_vertex_anim_t m_pos;
  glw_vertex_anim_t m_alpha;
  float m_alpha2;
} menu_t;

#define MENU_Z_MAX 3.0

/*
 * Callback for sub menus
 */
static int 
menu_submenu_item_event(glw_t *w, glw_signal_t signal, ...)
{
  menu_t *m = glw_get_opaque(w);
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
 * Callback for the GLW_ARRAY component
 */
static int 
menu_control(glw_t *w, glw_signal_t signal, ...)
{
  menu_t *m = glw_get_opaque(w);

  switch(signal) {
  case GLW_SIGNAL_DESTROY:
    free(m);
    return 1;

  default:
    return 0;
  }
}


/*
 *
 */
int
menu_input(appi_t *ai, inputevent_t *ie)
{
  glw_t *w, *c;
  menu_t *m = NULL, *mm;

  w = ai->ai_menu;
  if(w == NULL)
    return 0;

  w = glw_find_by_class(w, GLW_ARRAY);
  if(w == NULL)
    return 0;

  while(1) {
    c = w->glw_selected;
    if(c == NULL)
      return 0;

    if(c->glw_callback == menu_submenu_item_event) {
      /* This is a sub menu */
      mm = c->glw_opaque;

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
    c->glw_callback(c, GLW_SIGNAL_CLICK);

  c->glw_callback(c, GLW_SIGNAL_INPUT_EVENT, ie);
  return 0;
}


/*
 *
 */
glw_t *
menu_create_item(glw_t *p, const char *icon, const char *title,
		 glw_callback_t *cb, void *opaque, uint32_t u32, int first)
{
  glw_t *x;
  menu_t *m = glw_get_opaque(p);
  p = m->m_array;

  x = glw_create(GLW_CONTAINER_X,
		 first ? GLW_ATTRIB_PARENT_HEAD : GLW_ATTRIB_PARENT, p,
		 GLW_ATTRIB_CALLBACK, cb,
		 GLW_ATTRIB_OPAQUE, opaque,
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
static glw_t *
menu_create_menu(glw_t *p, const char *title)
{
  glw_t *w, *b, *y;
  menu_t *m = calloc(1, sizeof(menu_t));

  glw_vertex_anim_init(&m->m_pos, 0, 0, MENU_Z_MAX, GLW_VERTEX_ANIM_SIN_LERP);
  glw_vertex_anim_init(&m->m_alpha, 0, 0, 0, GLW_VERTEX_ANIM_SIN_LERP);

  b = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_FILENAME, "icon://plate-titled.png",
		 GLW_ATTRIB_OPAQUE, m,
		 GLW_ATTRIB_CALLBACK, menu_control,
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
		 GLW_ATTRIB_OPAQUE, m,
		 GLW_ATTRIB_X_SLICES, 1,
		 GLW_ATTRIB_Y_SLICES, 9,
		 NULL);

  if(p != NULL)
    p->glw_opaque = m;
  else
    m->m_expanded = 1;

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

  x = menu_create_item(p, icon, title, menu_submenu_item_event, NULL, 0,
		       first);
  menu_create_menu(x, title);
  return x;
}



/*
 *
 */

void
menu_init_app(appi_t *ai)
{
  glw_t *c;

  c = menu_create_menu(NULL, "Menu");
  settings_menu_create(c);
  ai->ai_menu = c;
}


/*
 *
 */
int 
menu_post_key_pop_and_hide(glw_t *w, glw_signal_t signal, ...)
{
  appi_t *ai = glw_get_opaque(w);
  menu_t *m = glw_get_opaque(w->glw_parent);

  switch(signal) {
  case GLW_SIGNAL_CLICK:
    input_keystrike(&ai->ai_ic, glw_get_u32(w));
    m->m_expanded = 0;
    ai->ai_menu_display = 0;
    return 1;
    
  default:
    return 0;
  }
}





/*
 *
 */
void
menu_layout(appi_t *ai)
{
  glw_t *c, *w;
  menu_t *m;
  glw_rctx_t rc;
  int d = 0;
  float af = 1.0f;
  LIST_HEAD(, menu) list;

  LIST_INIT(&list);

  memset(&rc, 0, sizeof(rc));
  rc.rc_aspect = MENU_ASPECT * (16.0f / 9.0f);


  w = ai->ai_menu;
  m = glw_get_opaque(w);
  m->m_alpha2 = GLW_LP(16, m->m_alpha2, !!ai->ai_menu_display);

  while(w != NULL) {
    m = glw_get_opaque(w);

    LIST_INSERT_HEAD(&list, m, m_link);
    if((c = glw_find_by_class(w, GLW_ARRAY)->glw_selected) == NULL)
      break;
    if(c->glw_callback != menu_submenu_item_event)
      break;
    m = glw_get_opaque(c);
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
menu_render(appi_t *ai, float alpha)
{
  glw_rctx_t rc;
  glw_t *c, *w = ai->ai_menu;
  glw_vertex_t xyz;
  menu_t *m;

  m = glw_get_opaque(w);

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
    m = glw_get_opaque(w);

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
    if(c->glw_callback != menu_submenu_item_event)
      break;
    
    m = glw_get_opaque(c);
    w = m->m_plate;
  }

  glPopMatrix();
}
