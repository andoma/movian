/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
#include <stdlib.h>
#include <string.h>

#include "glw.h"
#include "glw_texture.h"
#include "glw_renderer.h"

#define EDGE_SIZE 16.0
#define BLOOM_COUNT 3

typedef struct glw_bloom {
  glw_t w;

  glw_gf_ctrl_t b_flushctrl;

  float b_glow;

  int b_width;
  int b_height;
  glw_rtt_t b_rtt[BLOOM_COUNT];

  int b_render_initialized;
  glw_renderer_t b_render;

  int b_need_render;

} glw_bloom_t;

/**
 *
 */
static void
bloom_destroy_rtt(glw_root_t *gr, glw_bloom_t *b)
{
  int i;
  for(i = 0; i < BLOOM_COUNT; i++)
    glw_rtt_destroy(gr, &b->b_rtt[i]);
  b->b_width = 0;
  b->b_height = 0;
}


/**
 *
 */
static void
glw_bloom_dtor(glw_t *w)
{
  glw_bloom_t *b = (void *)w;

  glw_gf_unregister(&b->b_flushctrl);

  if(b->b_width || b->b_height)
    bloom_destroy_rtt(w->glw_root, b);

  glw_renderer_free(&b->b_render);
}


/**
 *
 */
static void 
glw_bloom_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_bloom_t *b = (void *)w;
  float a = rc->rc_alpha * w->glw_alpha;
  glw_rctx_t rc0;
  glw_t *c;

  rc0 = *rc;
  rc0.rc_alpha = a;
  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
    glw_render0(c, &rc0);

  if(b->b_glow < GLW_ALPHA_EPSILON)
    return;

  b->b_need_render = a > GLW_ALPHA_EPSILON;

  if(!b->b_need_render)
    return;
  
  if(glw_is_focusable_or_clickable(w))
    glw_store_matrix(w, rc);
  
  rc0 = *rc;

  glw_Scalef(&rc0, 
	     1.0 + EDGE_SIZE / rc->rc_width, 
	     1.0 + EDGE_SIZE / rc->rc_height, 
	     1.0);
#if 0
  glw_render(&b->b_render, w->glw_root, &rc0, 
	     GLW_RENDER_MODE_QUADS, GLW_RENDER_ATTRIBS_TEX,
	     &glw_rtt_texture(&b->b_rtt[0]), 1, 1, 1, a);
#endif

  a *= b->b_glow;

  glw_blendmode(w->glw_root, GLW_BLEND_ADDITIVE);
  glw_renderer_draw(&b->b_render, w->glw_root, &rc0, 
		    &glw_rtt_texture(&b->b_rtt[0]), NULL,
		    NULL, NULL, a * 0.50, 0, NULL);


  glw_renderer_draw(&b->b_render, w->glw_root, &rc0, 
		    &glw_rtt_texture(&b->b_rtt[1]), NULL,
		    NULL, NULL, a * 0.44, 0, NULL);


  glw_renderer_draw(&b->b_render, w->glw_root, &rc0, 
		    &glw_rtt_texture(&b->b_rtt[2]), NULL,
		    NULL, NULL, a * 0.33, 0, NULL);
 
  glw_blendmode(w->glw_root, GLW_BLEND_NORMAL);
}


/**
 *
 */
static void
glw_bloom_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_bloom_t *b = (void *)w;
  glw_root_t *gr = w->glw_root;
  glw_rctx_t rc0;
  glw_t *c;
  int x, y, i, sizx, sizy;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
    glw_layout0(c, rc);
  
  if(b->b_glow < GLW_ALPHA_EPSILON) {

    if(b->b_width || b->b_height)
      bloom_destroy_rtt(gr, b);
    return;
  }


  sizx = rc->rc_width + EDGE_SIZE;
  sizy = rc->rc_height + EDGE_SIZE;

  if(b->b_width != sizx || b->b_height != sizy) {
    if(b->b_width || b->b_height)
      bloom_destroy_rtt(gr, b);

    b->b_width  = sizx;
    b->b_height = sizy;

    if(b->b_width || b->b_height) {

      for(i = 0; i < BLOOM_COUNT; i++) {
	x = b->b_width  / (2 << i);
	y = b->b_height / (2 << i);
	glw_rtt_init(gr, &b->b_rtt[i], x, y, 1);
      }
    }
  }

  // Initialize output texture
  if(!b->b_render_initialized) {

    glw_renderer_init_quad(&b->b_render);

    glw_renderer_vtx_pos(&b->b_render, 0, -1.0, -1.0, 0.0);
    glw_renderer_vtx_st (&b->b_render, 0,  0.0,  0);

    glw_renderer_vtx_pos(&b->b_render, 1,  1.0, -1.0, 0.0);
    glw_renderer_vtx_st (&b->b_render, 1,  1.0,   0);

    glw_renderer_vtx_pos(&b->b_render, 2,  1.0,  1.0, 0.0);
    glw_renderer_vtx_st (&b->b_render, 2,  1.0,  1.0);

    glw_renderer_vtx_pos(&b->b_render, 3, -1.0,  1.0, 0.0);
    glw_renderer_vtx_st (&b->b_render, 3,  0.0,  1.0);
  }

  memset(&rc0, 0, sizeof(glw_rctx_t));
  rc0.rc_alpha  = 1;
  rc0.rc_width = b->b_width  - EDGE_SIZE;
  rc0.rc_height = b->b_height - EDGE_SIZE;
  rc0.rc_inhibit_shadows = 1;

  if(!b->b_need_render)
    return;

  for(i = 0; i < BLOOM_COUNT; i++) {

    glw_rtt_enter(gr, &b->b_rtt[i], &rc0);
    
    rc0.rc_width = b->b_width  - EDGE_SIZE;
    rc0.rc_height = b->b_height - EDGE_SIZE;

    glw_Scalef(&rc0, 
	       1.0 - EDGE_SIZE / b->b_width,
	       1.0 - EDGE_SIZE / b->b_height, 
	       1.0);
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      glw_render0(c, &rc0);
    glw_rtt_restore(gr, &b->b_rtt[i]);
  }
}

/**
 *
 */
static int
glw_bloom_callback(glw_t *w, void *opaque, glw_signal_t signal,
		    void *extra)
{
  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    glw_copy_constraints(w, extra);
    return 1;
  }
  return 0;
}


/**
 *
 */
static void
bflush(void *aux)
{
  glw_bloom_t *b = aux;

  if(b->b_width || b->b_height)
    bloom_destroy_rtt(b->w.glw_root, b);
}


/**
 *
 */
static void 
glw_bloom_ctor(glw_t *w)
{
  glw_bloom_t *b = (void *)w;
  b->b_flushctrl.opaque = b;
  b->b_flushctrl.flush = bflush;
  glw_gf_register(&b->b_flushctrl);

}

/**
 *
 */
static int
glw_bloom_set_float(glw_t *w, glw_attribute_t attrib, float value,
                    glw_style_t *gs)
{
  glw_bloom_t *b = (void *)w;

  switch(attrib) {
  case GLW_ATTRIB_VALUE:
    if(b->b_glow == value)
      return 0;

    b->b_glow = value;
    break;

  default:
    return -1;
  }
  return 1;
}


/**
 *
 */
static glw_class_t glw_bloom = {
  .gc_name = "bloom",
  .gc_instance_size = sizeof(glw_bloom_t),
  .gc_ctor = glw_bloom_ctor,
  .gc_set_float = glw_bloom_set_float,
  .gc_layout = glw_bloom_layout,
  .gc_render = glw_bloom_render,
  .gc_dtor = glw_bloom_dtor,
  .gc_signal_handler = glw_bloom_callback,
};

GLW_REGISTER_CLASS(glw_bloom);
