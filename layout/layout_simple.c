/*
 *  Simple layout (on mirror plane)
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

#include <string.h>
#include <math.h>

#include <GL/glu.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include "layout.h"
#include "menu.h"

#include "audio/audio_ui.h"

int layout_menu_display;
glw_t *root_menu;
glw_t *wroot;
glw_t *root_nav;

static int layout_allow_fullscreen;

static float miw_render(float aspect);

static int mirror_input_event(inputevent_t *ie);
static int menu_input_event(inputevent_t *ie);
static int hiprio_input_event(inputevent_t *ie);

static void layout_apps(float aspect);
static void layout_gadgets(float aspect);
static void render_gadgets(float alpha, float aspect, float displace);


static int 
layout_entry_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  return 0;
}

static appi_t *
layout_get_cur_app(void)
{
  glw_t *w = root_nav;

  if(!(w->glw_flags & GLW_ZOOMED))
    return NULL;

  w = w->glw_selected;  // the zoomer widget
  return glw_get_opaque(w, layout_entry_callback); 
}


static int 
layout_nav_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  int r = 0;
  inputevent_t *ie;
  
  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);
    switch(ie->type) {
    default:
      break;
      
    case INPUT_KEY:

      if(w->glw_flags & GLW_ZOOMED)
	return 0;

      switch(ie->u.key) {
      case INPUT_KEY_UP:
	r = glw_nav_signal(w, GLW_SIGNAL_UP);
	break;
      case INPUT_KEY_DOWN:
	r = glw_nav_signal(w, GLW_SIGNAL_DOWN);
	break;
      case INPUT_KEY_ENTER:
      case INPUT_KEY_RIGHT:
	r = glw_nav_signal(w, GLW_SIGNAL_ENTER);
	break;
      default:
	return 1;
      }
    }
  default:
    break;
  }
  va_end(ap);
  return r;
}




void
layout_std_create(void)
{
  glw_t *y;

  inputhandler_register(300, hiprio_input_event);
  inputhandler_register(200,   menu_input_event);
  inputhandler_register(100, mirror_input_event);

  root_menu = menu_push_top_menu(NULL, "Showtime");

  y = wroot = glw_create(GLW_CONTAINER_Y, 
			      NULL);
  
  root_nav = 
    glw_create(GLW_NAV,
	       GLW_ATTRIB_Y_SLICES, 15,
	       GLW_ATTRIB_PARENT, y,
	       GLW_ATTRIB_SIGNAL_HANDLER, layout_nav_callback, NULL, 0,
	       NULL);
}





void
layout_hide(appi_t *ai)
{
  glw_t *w = root_nav;
  w->glw_flags &= ~GLW_ZOOMED;
}



static void
draw_world(float ca, float rot, float alpha, float aspect,
	   float gadget_displace)
{
  glw_rctx_t rc;
  appi_t *ai;

  const static GLdouble clip_left[4]  =  {-1.0, 0.0, 0.0, 1.0};
  const static GLdouble clip_right[4] =  { 1.0, 0.0, 0.0, 1.0};
  const static GLdouble clip_bottom[4] = { 0.0, 1.0, 0.0, 1.0};
  const static GLdouble clip_top[4]    = { 0.0,-1.0, 0.0, 1.2};



  memset(&rc, 0, sizeof(rc));
  rc.rc_aspect = aspect;

  rc.rc_zoom = 1.0f;
  rc.rc_alpha = alpha;
  glPushMatrix();
  glTranslatef(0, 1, 0);
  glTranslatef(0, 0, 1);



  glClipPlane(GL_CLIP_PLANE0, clip_left);
  glClipPlane(GL_CLIP_PLANE1, clip_right);
  glClipPlane(GL_CLIP_PLANE2, clip_top);
  glClipPlane(GL_CLIP_PLANE3, clip_bottom);
  
  glEnable(GL_CLIP_PLANE0);
  glEnable(GL_CLIP_PLANE1);
  //    glEnable(GL_CLIP_PLANE2);
  glEnable(GL_CLIP_PLANE3);

  glw_render(wroot, &rc);
  glPopMatrix();

  glDisable(GL_CLIP_PLANE0);
  glDisable(GL_CLIP_PLANE1);
  glDisable(GL_CLIP_PLANE2);
  glDisable(GL_CLIP_PLANE3);

  ai = layout_get_cur_app();
  menu_render(ai ? ai->ai_menu : root_menu, alpha);

  render_gadgets(alpha, aspect, gadget_displace);

}

#define CAMZ 4.2

static glw_vertex_anim_t ctgt = 
 GLW_VERTEX_ANIM_SIN_INITIALIZER(0, 1, 1);
static glw_vertex_anim_t cpos = 
 GLW_VERTEX_ANIM_SIN_INITIALIZER(0, 2.5, CAMZ);
static glw_vertex_anim_t fcol = 
 GLW_VERTEX_ANIM_SIN_INITIALIZER(0.09, 0.11, 0.2);
static glw_vertex_anim_t wextra =  /* x = mirror_alpha, y = aspect */
 GLW_VERTEX_ANIM_SIN_INITIALIZER(0.1, 1.0, -0.7);

void
layout_std_draw(float aspect0)
{
  glw_rctx_t rc;
  appi_t *ai;
  static float ca; /* cube alpha */
  static float rot; /* cube rotation */
  int fs;
  float cz;
  static float topinfospace;
  float aspect;

  glw_vertex_t cpos_xyz;
  glw_vertex_t ctgt_xyz;
  glw_vertex_t fcol_xyz;
  glw_vertex_t wextra_xyz;


  /* Camera and "world" animation */

  ai = layout_get_cur_app();
  cz = 4.0;

  fs = 0;

  if(layout_menu_display) {
    /* displaying menu */
    glw_vertex_anim_set3f(&cpos,   0,    1.5,  CAMZ);
    glw_vertex_anim_set3f(&ctgt,   -0.5, 1.0, 1.0);
    glw_vertex_anim_set3f(&fcol,   0.09, 0.11, 0.2);
    glw_vertex_anim_set3f(&wextra, 0.1,  1.0,  -0.95);
  } else if(ca < 0.01 && ai != NULL && ai->ai_req_fullscreen) {
    /* fullscreen mode */
    glw_vertex_anim_set3f(&cpos,   0, 1.0, 3.4);
    glw_vertex_anim_set3f(&ctgt,   0, 1.0, 1.0);
    glw_vertex_anim_set3f(&fcol,   0, 0,   0);
    glw_vertex_anim_set3f(&wextra, 0, 1.0,   -0.95);
    
    if(glw_vertex_anim_read_i(&cpos) > 0.99)
      fs = 1;

  } else {
    /* normal */
    glw_vertex_anim_set3f(&cpos,   0,    1.0,  CAMZ);
    glw_vertex_anim_set3f(&ctgt,   0, 1.0, 1.0);
    glw_vertex_anim_set3f(&fcol,   0.09, 0.11, 0.2);
    glw_vertex_anim_set3f(&wextra, 0.1,  0.8,    -0.7);
  }

  layout_allow_fullscreen = fs;

  glw_vertex_anim_fwd(&cpos,   0.02);
  glw_vertex_anim_fwd(&ctgt,   0.02);
  glw_vertex_anim_fwd(&fcol,   0.02);
  glw_vertex_anim_fwd(&wextra, 0.02);
  
  glw_vertex_anim_read(&wextra, &wextra_xyz);

  aspect = aspect0 / wextra_xyz.y;

  glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

  memset(&rc, 0, sizeof(rc));
  rc.rc_aspect = aspect;
  rc.rc_zoom = 1;

  glw_layout(wroot, &rc);

  layout_apps(aspect);
  layout_gadgets(aspect);

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

  draw_world(ca, rot, 1.0, rc.rc_aspect, wextra_xyz.z);


  if(wextra_xyz.x > 0.01) {
    /* invert model matrix along XZ plane for mirror effect */
    glScalef(1.0f, -1.0f, 1.0f);
    draw_world(ca, rot, wextra_xyz.x, rc.rc_aspect, wextra_xyz.z);
  }

#if 0
  glAccum(GL_MULT, 0.8);
  glAccum(GL_ACCUM, 0.2);
  glAccum(GL_RETURN, 1.0f);
#endif

  /* reset projection-, model- matrix and render status widgets */

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluPerspective(45, 1.0, 1.0, 60.0);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  gluLookAt(0, 0, 2.40,
  	    0, 0, 0,
	    0, 1, 0);

  topinfospace = miw_render(aspect0);

  audio_layout();

  audio_render(1.0f);

}



static int
menu_input_event(inputevent_t *ie)
{
  appi_t *ai;

  ai = layout_get_cur_app();

  if(ie->type == INPUT_KEY && ie->u.key == INPUT_KEY_MENU) {
    layout_menu_display = !layout_menu_display;
    return 1;
  }

  if(layout_menu_display == 0)
    return 0;

  if(menu_input(ai ? ai->ai_menu : root_menu, ie))
    layout_menu_display = 0;

  return 1;
}



static int
hiprio_input_event(inputevent_t *ie)
{
  media_pipe_t *mp = primary_audio;

  if(mp == NULL)
    return 0;

  if(ie->type == INPUT_KEY && ie->u.key == INPUT_KEY_STOP) {
    input_postevent(&mp->mp_ai->ai_ic, ie);
    return 1;
  }
  return 0;
}



static int
mirror_input_event(inputevent_t *ie)
{
  switch(ie->type) {
  default:
    break;
      
  case INPUT_KEY:
    switch(ie->u.key) {
    case INPUT_KEY_TASKSWITCH:
      return 0;
    default:
      break;
    }
  }

  if(glw_drop_signal(root_nav, GLW_SIGNAL_INPUT_EVENT, ie))
    return 1;
  
  return 0;
}


static void
layout_apps(float aspect)
{
  appi_t *ai;
  glw_rctx_t rc;

  memset(&rc, 0, sizeof(rc));
  rc.rc_alpha = 1.0f;
  rc.rc_zoom = 1.0f;

  LIST_FOREACH(ai, &appis, ai_global_link) {
    rc.rc_aspect = aspect;
    ai->ai_got_fullscreen = ai->ai_req_fullscreen && layout_allow_fullscreen;
    menu_layout(ai->ai_menu);
  }
  menu_layout(root_menu);
}


static int 
appi_content_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  va_list ap;
  va_start(ap, signal);
  appi_t *ai = opaque;
  glw_t *c = ai->ai_widget;
  glw_rctx_t *rc;
  inputevent_t *ie;

  if(c != NULL) {

    switch(signal) {

    case GLW_SIGNAL_INPUT_EVENT:
      ie = va_arg(ap, void *);
      glw_drop_signal(c, GLW_SIGNAL_INPUT_EVENT, ie);
      break;

    case GLW_SIGNAL_RENDER:
      rc = va_arg(ap, void *);
      glw_render(c, rc);
      break;

    case GLW_SIGNAL_LAYOUT:
      rc = va_arg(ap, void *);
      glw_layout(c, rc);
      break;


    default:
      break;
    }
  }

  va_end(ap);
  return 0;
}




static int 
appi_preview_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  va_list ap;
  va_start(ap, signal);
  appi_t *ai = opaque;
  glw_t *c = ai->ai_preview;
  glw_rctx_t *rc;

  if(c != NULL) {

    switch(signal) {
    case GLW_SIGNAL_RENDER:
      rc = va_arg(ap, void *);
      glw_render(c, rc);
      break;

    case GLW_SIGNAL_LAYOUT:
      rc = va_arg(ap, void *);
      glw_layout(c, rc);
      break;

    default:
      break;
    }
  }

  va_end(ap);
  return 0;
}



void
layout_register_appi(appi_t *ai)
{
  glw_t *w, *z, *y, *x;

  w = glw_create(GLW_NAV_ENTRY,
		 GLW_ATTRIB_PARENT, root_nav,
		 GLW_ATTRIB_SIGNAL_HANDLER, layout_entry_callback, ai, 0,
		 NULL);

  /* List entry */
 
  z = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_PARENT, w,
		 GLW_ATTRIB_FILENAME, "icon://plate-wide.png",
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_PARENT, z,
	     GLW_ATTRIB_CAPTION, ai->ai_name,
	     NULL);

  /* Preview thing */

  y = glw_create(GLW_CONTAINER_Y,
		 NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, y,
	     NULL);

  x = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_PARENT, y,
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 GLW_ATTRIB_FILENAME, "icon://plate.png",
		 NULL);
		   

  z = glw_create(GLW_CONTAINER_Z,
		 GLW_ATTRIB_PARENT, x,
		 NULL);

  glw_create(GLW_BITMAP,
	     GLW_ATTRIB_PARENT, z,
	     GLW_ATTRIB_ALPHA, 0.4,
	     GLW_ATTRIB_FILENAME, ai->ai_icon,
	     NULL);

  /* Preview portal */
  glw_create(GLW_EXT,
	     GLW_ATTRIB_PARENT, z,
	     GLW_ATTRIB_SIGNAL_HANDLER, appi_preview_callback, ai, 0,
	     NULL);

  glw_create(GLW_DUMMY,
	     GLW_ATTRIB_PARENT, y,
	     NULL);

#if 0
  glw_set(w,
	  GLW_ATTRIB_PREVIEW, y,
	  NULL);
#endif
    
  /* Content portal */
  z = glw_create(GLW_EXT,
		 GLW_ATTRIB_SIGNAL_HANDLER, appi_content_callback, ai, 0,
		 NULL);

  glw_set(w,
	  GLW_ATTRIB_CONTENT, z,
	  NULL);
}





/*****************************************************************************
 *
 * Menu / status playfield
 *
 */

extern media_pipe_t *mixer_primary_audio;

static float
miw_render(float aspect)
{
  media_pipe_t *mp = primary_audio;
  static float a0, a1;
  float a;
  glw_rctx_t rc0;
  appi_t *ai = layout_get_cur_app();
  const float size = 0.05f;
  float space = 0;

  memset(&rc0, 0, sizeof(rc0));
  rc0.rc_alpha = 1.0f;
  rc0.rc_aspect = aspect;

  if(mp == NULL)
    return 0;

  rc0.rc_aspect *= 1 / size;
  rc0.rc_polyoffset -= 2;

  if(mp->mp_info_widget != NULL) {
    if(mp->mp_info_widget_auto_display > 0)
      mp->mp_info_widget_auto_display--;

    if(ai != NULL && ai->ai_got_fullscreen && layout_menu_display == 0 &&
       mp->mp_info_widget_auto_display == 0 && mp->mp_playstatus == MP_PLAY)
      a = 0;
    else
      a = 1.0;

    a0 = (a0 * 7.0f + a) / 8.0f;


    if(a0 > 0.01) {

      rc0.rc_alpha = a0;

      glPushMatrix();

      glTranslatef(0.0, 1 - size - size / 3, 0.0f);
      glScalef(1.0f, size, size);

      glScalef(0.97f, 0.97f, 0.97f);

      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glEnable(GL_BLEND);

      glBegin(GL_QUADS);
      glColor4f(0.0, 0.0, 0.0, 0.0);
      glVertex3f(-1.0f, -2.0f, 0.0f);
      glVertex3f( 1.0f, -2.0f, 0.0f);
      glColor4f(0.0, 0.0, 0.0, a1);
      glVertex3f( 1.0f,  1.0f, 0.0f);
      glVertex3f(-1.0f,  1.0f, 0.0f);
      glEnd();

      glDisable(GL_BLEND);

      glw_layout(mp->mp_info_widget, &rc0);
      glw_render(mp->mp_info_widget, &rc0);
      glPopMatrix();
      space++;
    }
  }

  if(mp->mp_info_extra_widget != NULL) {

    a = mp_show_extra_info;
    a1 = (a1 * 7.0f + a) / 8.0f;
    
    if(a1 > 0.01) {

      rc0.rc_alpha = a1;

      glPushMatrix();

      glTranslatef(0.0, -0.9f, 0.0f);
      glScalef(1.0f, 0.1f, 1.0f);

      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glEnable(GL_BLEND);

      glBegin(GL_QUADS);
      glColor4f(0.0, 0.0, 0.0, a0);
      glVertex3f(-1.0f, -1.0f, 0.0f);
      glVertex3f( 1.0f, -1.0f, 0.0f);
      glColor4f(0.0, 0.0, 0.0, 0.0);
      glVertex3f( 1.0f,  2.0f, 0.0f);
      glVertex3f(-1.0f,  2.0f, 0.0f);
      glEnd();

      glDisable(GL_BLEND);

      glw_render(mp->mp_info_extra_widget, &rc0);
      glPopMatrix();
      space++;
    }
  }
  return space;
}




/*
 *
 */

glw_t *
bar_title(const char *str)
{
  glw_t *r;

  r = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_FILENAME, "icon://plate-wide.png",
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, r,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	     GLW_ATTRIB_CAPTION, str,
	     NULL);

  return r;
}


/*
 *
 */

glw_t *gadget_clock;

const char *months[] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun", 
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static int
clock_date_update(glw_t *w, void *opaque, glw_signal_t sig, ...)
{
  char buf[30];
  struct tm tm;

  switch(sig) {
  default:
    break;
  case GLW_SIGNAL_PREPARE:
    localtime_r(&walltime, &tm);
    
    snprintf(buf, sizeof(buf), "%d %s", tm.tm_mday, months[tm.tm_mon]);
    glw_set(w, GLW_ATTRIB_CAPTION, buf, NULL);
    break;
  }
  return 0;
}



static int
clock_time_update(glw_t *w, void *opaque, glw_signal_t sig, ...)
{
  char buf[30];
  struct tm tm;

  switch(sig) {
  default:
    break;
  case GLW_SIGNAL_PREPARE:
    localtime_r(&walltime, &tm);
    
    snprintf(buf, sizeof(buf), "%d:%02d",
	     tm.tm_hour, tm.tm_min);
    glw_set(w, GLW_ATTRIB_CAPTION, buf, NULL);
    break;
  }
  return 0;
}







static void
layout_gadgets(float aspect)
{
  glw_rctx_t rc;
  glw_t *y;

  if(gadget_clock == NULL) {

    gadget_clock = glw_create(GLW_BITMAP,
			      GLW_ATTRIB_FILENAME, "icon://plate.png",
			      GLW_ATTRIB_FLAGS, GLW_NOASPECT,
			      NULL);
    
    y = glw_create(GLW_CONTAINER_Y,
		   GLW_ATTRIB_PARENT, gadget_clock,
		   NULL);

    glw_create(GLW_TEXT_BITMAP,
	       GLW_ATTRIB_PARENT, y,
	       GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	       GLW_ATTRIB_SIGNAL_HANDLER, clock_date_update, NULL, 0,
	       NULL);

    glw_create(GLW_TEXT_BITMAP,
	       GLW_ATTRIB_PARENT, y,
	       GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_CENTER,
	       GLW_ATTRIB_SIGNAL_HANDLER, clock_time_update, NULL, 0,
	       NULL);
  }
  memset(&rc, 0, sizeof(rc));
  rc.rc_aspect = aspect;

  glw_layout(gadget_clock, &rc);
}


static void
render_gadgets(float alpha, float aspect, float displace)
{
  glw_rctx_t rc;

  memset(&rc, 0, sizeof(rc));
  rc.rc_aspect = aspect;
  rc.rc_alpha = alpha * 0.5;

  glPushMatrix();
  glTranslatef(displace, 0.1, 1.7);
  glScalef(0.1, 0.1, 0.1);
  glw_render(gadget_clock, &rc);
  glPopMatrix();
}
