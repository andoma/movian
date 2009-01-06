/*
 *  GL Widgets
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

#include <stdlib.h>
#include <string.h>

#include "glw.h"
#include "glw_fx_texrot.h"

/**
 *
 */
static void
glw_fx_texrot_dtor(glw_t *w)
{
  glw_fx_texrot_t *fx = (void *)w;

  glw_gf_unregister(&fx->fx_flushctrl);

  if(fx->fx_tex != NULL)
    glw_tex_deref(w->glw_root, fx->fx_tex);

  if(fx->fx_texsize != 0) {
    glDeleteTextures(1, &fx->fx_fbtex);
    glDeleteFramebuffersEXT(1, &fx->fx_fb);
  }
}

/**
 *
 */
static void 
glw_fx_texrot_render(glw_t *w, glw_rctx_t *rc)
{
  glw_fx_texrot_t *fx = (void *)w;
  glw_texture_t *gt = fx->fx_tex;
  float a = rc->rc_alpha * w->glw_alpha;

  if(gt != NULL && gt->gt_state == GT_STATE_VALID && a > 0.01) {

    glColor4f(1, 1, 1, a);

    glBindTexture(GL_TEXTURE_2D, fx->fx_fbtex);

    glBegin(GL_QUADS);

    glTexCoord2f(0.0, 1.0);
    glVertex3f( -1.0, -1.0f, 0.0f);

    glTexCoord2f(1.0, 1.0);
    glVertex3f( 1.0, -1.0f, 0.0f);

    glTexCoord2f(1.0, 0.0);
    glVertex3f( 1.0, 1.0f, 0.0f);

    glTexCoord2f(0.0, 0.0);
    glVertex3f( -1.0, 1.0f, 0.0f);
    
    glEnd();
  }
}





/**
 *
 */
static void
glw_fx_texrot_init(glw_fx_texrot_t *fx)
{
  int i;

  for(i = 0; i < FX_NPLATES; i++) {
    fx->fx_plates[i].angle = drand48() * 360;
    fx->fx_plates[i].inc = drand48() * 0.1;


    fx->fx_plates[i].coldrive = drand48() * M_PI * 2;
    fx->fx_plates[i].colinc = drand48() * 0.01;

    fx->fx_plates[i].x = (drand48() - 0.5) * 0.5;
    fx->fx_plates[i].y = (drand48() - 0.5) * 0.5;

    fx->fx_plates[i].texalpha = drand48() * 2 * M_PI;
    fx->fx_plates[i].texbeta = drand48() * 2 * M_PI;

    fx->fx_plates[i].tex1 = drand48() * 0.001;
    fx->fx_plates[i].tex2 = drand48() * 0.002;
  }
}

/**
 *
 */
static void
glw_fx_texrot_render_internal(glw_fx_texrot_t *fx, glw_texture_t *gt)
{
  int i;

  glBlendFunc(GL_SRC_ALPHA, GL_ONE);

  glBindTexture(GL_TEXTURE_2D, gt->gt_texture);

  glColor4f(1.0, 1.0, 1.0, 0.15);

  glScalef(2.5, 2.5, 1.0);

  for(i = 0; i < FX_NPLATES; i++) {

    glPushMatrix();

    fx->fx_plates[i].coldrive += fx->fx_plates[i].colinc;

    glTranslatef(fx->fx_plates[i].x, fx->fx_plates[i].y, 0);

    glRotatef(fx->fx_plates[i].angle, 0, 0.0, 1);
    fx->fx_plates[i].angle += fx->fx_plates[i].inc;

    glBegin(GL_QUADS);

    glTexCoord2f(0, 0);
    glVertex3f( -1.0f, -1.0f, 0.0f);

    glTexCoord2f(0, 1);
    glVertex3f( 1.0f, -1.0f, 0.0f);

    glTexCoord2f(1, 1);
    glVertex3f( 1.0f, 1.0f, 0.0f);

    glTexCoord2f(1, 0);
    glVertex3f( -1.0f, 1.0f, 0.0f);
    glEnd();

    glPopMatrix();
  }
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}







/**
 *
 */
static void
glw_fx_texrot_buildtex(glw_fx_texrot_t *fx, glw_texture_t *gt)
{
  int viewport[4];
 
  if(fx->fx_texsize == 0) {
    
    fx->fx_texsize = 512;

    glGenTextures(1, &fx->fx_fbtex);
    
    glBindTexture(GL_TEXTURE_2D, fx->fx_fbtex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, fx->fx_texsize, fx->fx_texsize, 0,
		 GL_RGB, GL_UNSIGNED_BYTE, NULL);



    glGenFramebuffersEXT(1, &fx->fx_fb);
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fx->fx_fb);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT,
			      GL_COLOR_ATTACHMENT0_EXT,
			      GL_TEXTURE_2D, fx->fx_fbtex, 0);


  }

  /* Save viewport */
  glGetIntegerv(GL_VIEWPORT, viewport);


  glBindTexture(GL_TEXTURE_2D, 0);
  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT,  fx->fx_fb);
  
  glViewport(0, 0, fx->fx_texsize, fx->fx_texsize);

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  gluPerspective(45, 1.0, 1.0, 60.0);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  gluLookAt(0, 0, 2.4,
	    0, 0, 0,
	    0, 1, 0);

  glClear(GL_COLOR_BUFFER_BIT);

  glw_fx_texrot_render_internal(fx, gt);

  glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);

  


  /* Restore viewport */
  glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}




/**
 *
 */
static void 
glw_fx_texrot_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_fx_texrot_t *fx = (void *)w;
  glw_texture_t *gt = fx->fx_tex;

  if(gt != NULL) {
    glw_tex_layout(w->glw_root, gt);
    glw_fx_texrot_buildtex(fx, gt);
  }
}

/**
 *
 */
static int
glw_fx_texrot_callback(glw_t *w, void *opaque, glw_signal_t signal,
		    void *extra)
{
  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_fx_texrot_layout(w, extra);
    break;
  case GLW_SIGNAL_RENDER:
    glw_fx_texrot_render(w, extra);
    break;
  case GLW_SIGNAL_DTOR:
    glw_fx_texrot_dtor(w);
    break;
  }
  return 0;
}


/**
 *
 */
static void
fxflush(void *aux)
{
  glw_fx_texrot_t *fx = aux;

  if(fx->fx_texsize != 0) {
    glDeleteTextures(1, &fx->fx_fbtex);
    glDeleteFramebuffersEXT(1, &fx->fx_fb);
    fx->fx_texsize = 0;
  }
}


/*
 *
 */
void 
glw_fx_texrot_ctor(glw_t *w, int init, va_list ap)
{
  glw_fx_texrot_t *fx = (void *)w;
  glw_attribute_t attrib;
  const char *filename = NULL;

  if(init) {
    glw_signal_handler_int(w, glw_fx_texrot_callback);
    glw_fx_texrot_init(fx);

    /* Flush due to opengl shutdown */
    fx->fx_flushctrl.opaque = fx;
    fx->fx_flushctrl.flush = fxflush;
    glw_gf_register(&fx->fx_flushctrl);
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_SOURCE:
      filename = va_arg(ap, char *);
      break;
    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);


  if(filename == NULL)
    return;

  if(fx->fx_tex != NULL)
    glw_tex_deref(w->glw_root, fx->fx_tex);

  fx->fx_tex = glw_tex_create(w->glw_root, filename);
}
