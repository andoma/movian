/*
 *  Layout engine
 *  Copyright (C) 2008 Andreas Öman
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

#include <string.h>
#include <math.h>

#include <GL/glu.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include "layout.h"

static glw_t *layout_world;

static int fullscreen;

static int layout_world_input_event(inputevent_t *ie);

static int layout_world_callback(glw_t *w, void *opaque,
				 glw_signal_t signal, ...);

/**
 * Create world
 */
void
layout_world_create(void)
{
  layout_world = 
    glw_create(GLW_EXT,
	       GLW_ATTRIB_SIGNAL_HANDLER, layout_world_callback, NULL, 0,
	       NULL);

  inputhandler_register(0, layout_world_input_event);

}

/**
 * Draw a cube in -1,-1,-1 to 1,1,1 space
 */

static void
draw_cube(float alpha)
{
  return;

  if(alpha < 0.01)
    return;

  glLineWidth(2.0);
  glEnable(GL_BLEND);
  //  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    
  alpha *= 0.3;

  glColor4f(1, 1, 1, alpha);

  glBegin(GL_LINE_LOOP);
  glVertex3f(-1.0f, -1.0f,  0.0f);
  glVertex3f( 1.0f, -1.0f,  0.0f);
  glVertex3f( 1.0f,  1.0f,  0.0f);
  glVertex3f(-1.0f,  1.0f,  0.0f);
  glEnd();

  glBegin(GL_LINE_LOOP);
  glVertex3f(-1.0f, -1.0f,  1.0f);
  glVertex3f( 1.0f, -1.0f,  1.0f);
  glVertex3f( 1.0f,  1.0f,  1.0f);
  glVertex3f(-1.0f,  1.0f,  1.0f);
  glEnd();

  glColor4f(1, 1, 1, alpha);
  glBegin(GL_LINE_LOOP);
  glVertex3f(-1.0f, -1.0f, -1.0f);
  glVertex3f(-1.0f, -1.0f,  1.0f);
  glVertex3f(-1.0f,  1.0f,  1.0f);
  glVertex3f(-1.0f,  1.0f, -1.0f);
  glEnd();

  glColor4f(1, 1, 1, alpha);
  glBegin(GL_LINE_LOOP);
  glVertex3f(-1.0f, -1.0f, -1.0f);
  glVertex3f( 1.0f, -1.0f, -1.0f);
  glVertex3f( 1.0f,  1.0f, -1.0f);
  glVertex3f(-1.0f,  1.0f, -1.0f);
  glEnd();

  glColor4f(1, 1, 1, alpha);
  glBegin(GL_LINE_LOOP);
  glVertex3f( 1.0f, -1.0f,  1.0f);
  glVertex3f( 1.0f, -1.0f, -1.0f);
  glVertex3f( 1.0f,  1.0f, -1.0f);
  glVertex3f( 1.0f,  1.0f,  1.0f);
  glEnd();

  glLineWidth(1.0);
}

/**
 * Draw the world (call render function) 
 */
static void
draw_world(float ca, float rot, float alpha, float aspect)
{
  glw_rctx_t rc;

  const static GLdouble clip_bottom[4] = { 0.0, 1.0, 0.0, 1.0};

  memset(&rc, 0, sizeof(rc));
  rc.rc_aspect = aspect;
  rc.rc_zoom = 1.0f;
  rc.rc_alpha = alpha;


  glPushMatrix();
  glTranslatef(0, 1, 0);
  draw_cube(alpha);

  glClipPlane(GL_CLIP_PLANE5, clip_bottom);
  glEnable(GL_CLIP_PLANE5);

 
  glw_render(layout_world, &rc);
  glPopMatrix();

  glDisable(GL_CLIP_PLANE5);
}


#define CAMZ 3.4

static glw_vertex_anim_t cpos = 
 GLW_VERTEX_ANIM_SIN_INITIALIZER(0, 1.0, CAMZ);
static glw_vertex_anim_t ctgt = 
 GLW_VERTEX_ANIM_SIN_INITIALIZER(0, 1, 1);
static glw_vertex_anim_t fcol = 
 GLW_VERTEX_ANIM_SIN_INITIALIZER(0.09, 0.11, 0.2);
static glw_vertex_anim_t wextra =  /* x = mirror_alpha, y = aspect */
 GLW_VERTEX_ANIM_SIN_INITIALIZER(0.1, 1.0, -0.7);

/**
 * Render the world model
 */
void
layout_world_render(float aspect0)
{
  glw_rctx_t rc;
  //  appi_t *ai;
  static float ca; /* cube alpha */
  static float rot; /* cube rotation */
  int fs;
  float cz;
  //  static float topinfospace;
  float aspect;

  glw_vertex_t cpos_xyz;
  glw_vertex_t ctgt_xyz;
  glw_vertex_t fcol_xyz;
  glw_vertex_t wextra_xyz;

  /* Camera and "world" animation */

  cz = 4.0;
  fs = 0;

  if(!fullscreen) {
    glw_vertex_anim_set3f(&cpos,   0,    1.0,  CAMZ);
    glw_vertex_anim_set3f(&ctgt,   0,    1.0,  1.0);
    glw_vertex_anim_set3f(&fcol,   0.09, 0.11, 0.2);
    glw_vertex_anim_set3f(&wextra, 0.1,  1.0, 0);
  } else {
    glw_vertex_anim_set3f(&cpos,   0,    1.0,  CAMZ);
    glw_vertex_anim_set3f(&ctgt,   0,    1.0,  1.0);
    glw_vertex_anim_set3f(&fcol,   0.0,  0.0,  0.0);
    glw_vertex_anim_set3f(&wextra, 0.0,  1.0,  0.0);

  }

  glw_vertex_anim_fwd(&cpos,   0.02);
  glw_vertex_anim_fwd(&ctgt,   0.02);
  glw_vertex_anim_fwd(&fcol,   0.02);
  glw_vertex_anim_fwd(&wextra, 0.02);
  
  glw_vertex_anim_read(&wextra, &wextra_xyz);

  aspect = aspect0 / wextra_xyz.y;


  memset(&rc, 0, sizeof(rc));
  rc.rc_aspect = aspect;
  rc.rc_zoom = 1;

  glw_layout(layout_world, &rc);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(45, wextra_xyz.y, 1.0, 60.0);


  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glw_vertex_anim_read(&cpos, &cpos_xyz);
  glw_vertex_anim_read(&ctgt, &ctgt_xyz);
  gluLookAt(cpos_xyz.x, cpos_xyz.y, cpos_xyz.z,
	    ctgt_xyz.x, ctgt_xyz.y, ctgt_xyz.z,
	    0, 1, 0);

  /* Draw floor */
  
  glw_vertex_anim_read(&fcol, &fcol_xyz);

  if(fcol_xyz.x > 0.01 || fcol_xyz.y > 0.01 || fcol_xyz.z > 0.01) {
    glDisable(GL_BLEND);

    glBegin(GL_QUADS);

    glColor3f(fcol_xyz.x, fcol_xyz.y, fcol_xyz.z);

    glVertex3f(-10.0f, 0.0f, 10.0f);
    glVertex3f( 10.0f, 0.0f, 10.0f);
    glColor3f(0, 0, 0.0);
    glVertex3f( 20.0f, 0.0f, -10.0f);
    glVertex3f(-20.0f, 0.0f, -10.0f);
    glEnd();
  }

  draw_world(ca, rot, 1.0, rc.rc_aspect);


  if(wextra_xyz.x > 0.01) {
    /* invert model matrix along XZ plane for mirror effect */
    glScalef(1.0f, -1.0f, 1.0f);
    draw_world(ca, rot, wextra_xyz.x, rc.rc_aspect);
  }
}

/**
 *
 */
static int
layout_world_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  glw_t *c;
  glw_rctx_t *rc, rc0;
  float z = 0;
  float a = 1.0 - 0.9 * layout_switcher_alpha;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_RENDER:
    rc = va_arg(ap, void *);
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
      if(c->glw_parent_alpha > 0.01) {
	glPushMatrix();
	glTranslatef(0.0f, 0.0f, c->glw_pos.z);
	rc0 = *rc;
	rc0.rc_alpha *= c->glw_parent_alpha * a;
	glw_render(c, &rc0);
	glPopMatrix();
      }
    }
    break;

  case GLW_SIGNAL_LAYOUT:
    rc = va_arg(ap, void *);
    w->glw_selected = TAILQ_FIRST(&w->glw_childs);

    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
      c->glw_pos.z        = GLW_LP(16, c->glw_pos.z, z);

      if(c == w->glw_selected) {
	c->glw_parent_alpha = GLW_LP(16, c->glw_parent_alpha, 1.0f);
      } else {
	c->glw_parent_alpha = GLW_LP(8, c->glw_parent_alpha, 0);
      }
      
      glw_layout(c, rc);
      z -= 1.0f;
    }
    break;

  default:
    break;
  }

  va_end(ap);
  return 0;
}

/**
 *
 */
static int
layout_child_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  appi_t *ai = opaque;
  inputevent_t *ie;
  glw_signal_t sig = GLW_SIGNAL_NONE;

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_LAYOUT:
    if(w == w->glw_parent->glw_selected) {
      ai->ai_active = 1;
      fullscreen = ai->ai_req_fullscreen;
    } else {
      ai->ai_active = 0;
    }
    break;

  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);

    /* Transform some known keys into GLW signals */
    
    if(ie->type == INPUT_KEY) switch(ie->u.key) {
    case INPUT_KEY_UP:     sig = GLW_SIGNAL_UP;     break;
    case INPUT_KEY_DOWN:   sig = GLW_SIGNAL_DOWN;   break;
    case INPUT_KEY_LEFT:   sig = GLW_SIGNAL_LEFT;   break;
    case INPUT_KEY_RIGHT:  sig = GLW_SIGNAL_RIGHT;  break;
    case INPUT_KEY_ENTER:  sig = GLW_SIGNAL_ENTER;  break;
    case INPUT_KEY_SELECT: sig = GLW_SIGNAL_SELECT; break;
    default: break;
    }
    
    if(sig != GLW_SIGNAL_NONE &&
       glw_send_signal_to_focused(&ai->ai_gfs, sig, NULL))
      return 1;

    /* Nav signal not consumed, do it as input event */
    glw_send_signal_to_focused(&ai->ai_gfs, GLW_SIGNAL_INPUT_EVENT, ie);
    break;

  default:
    break;
  }

  va_end(ap);
  return 0;
}


/**
 * Show application instance (and optionally add it)
 */
void
layout_world_appi_show(appi_t *ai)
{
  glw_t *w = ai->ai_widget;

  if(w == TAILQ_FIRST(&layout_world->glw_childs))
    return;

  glw_focus_stack_activate(&ai->ai_gfs);

  w->glw_parent_alpha = 0.0f; /* fade in */
  w->glw_pos.z = 2.0f;  /* 2.0 so it appears that the app almost comes from 
			   "behind the user */

  glw_set(w,
	  GLW_ATTRIB_SIGNAL_HANDLER, layout_child_callback, ai, 1,
	  GLW_ATTRIB_PARENT_HEAD, layout_world,
	  NULL);
}

/**
 * Primary point for input event distribution
 */
static int
layout_world_input_event(inputevent_t *ie)
{
  glw_lock();
  if(layout_world->glw_selected != NULL)
    glw_send_signal(layout_world->glw_selected, GLW_SIGNAL_INPUT_EVENT, ie);
  glw_unlock();
  return 1;
}

