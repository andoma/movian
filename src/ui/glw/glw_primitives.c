/*
 *  GL Widgets, GLW_CONTAINER -widgets
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

#include "glw.h"
#include "glw_renderer.h"
#include "glw_texture.h"

typedef struct glw_quad {
  glw_t w;
  
  glw_rgb_t color;
  glw_renderer_t r;
} glw_quad_t;




static void
glw_quad_render(glw_t *w, glw_rctx_t *rc)
{
  glw_quad_t *q = (void *)w;

  if(!glw_renderer_initialized(&q->r)) {
    glw_renderer_init_quad(&q->r);
    glw_renderer_vtx_pos(&q->r, 0, -1, -1, 0);
    glw_renderer_vtx_pos(&q->r, 1,  1, -1, 0);
    glw_renderer_vtx_pos(&q->r, 2,  1,  1, 0);
    glw_renderer_vtx_pos(&q->r, 3, -1,  1, 0);
  }

  glw_renderer_draw(&q->r, w->glw_root, rc, NULL, &q->color, NULL, 
		    rc->rc_alpha * w->glw_alpha, 0);
}


/**
 *
 */
static int
glw_quad_callback(glw_t *w, void *opaque, glw_signal_t signal,
		  void *extra)
{
  return 0;
}


/**
 *
 */
static void 
glw_quad_init(glw_t *w)
{
  glw_quad_t *q = (void *)w;
  q->color.r = 1.0;
  q->color.g = 1.0;
  q->color.b = 1.0;
}


/**
 *
 */
static void 
glw_quad_set_rgb(glw_t *w, const float *rgb)
{
  glw_quad_t *q = (void *)w;
  q->color.r = rgb[0];
  q->color.g = rgb[1];
  q->color.b = rgb[2];
}


static glw_class_t glw_quad = {
  .gc_name = "quad",
  .gc_instance_size = sizeof(glw_quad_t),
  .gc_ctor = glw_quad_init,
  .gc_render = glw_quad_render,
  .gc_signal_handler = glw_quad_callback,
  .gc_set_rgb = glw_quad_set_rgb,
};



GLW_REGISTER_CLASS(glw_quad);














typedef struct glw_raster {
  glw_t w;
  
  glw_rgb_t color;
  glw_renderer_t r;
  glw_backend_texture_t tex;
} glw_raster_t;



static const uint8_t rastermap[] = {
  255,255,255, 255,255,255, 255,255,255, 255,255,255,
  255,255,255,0,0,0, 0,0,0, 0,0,0,
  255,255,255,0,0,0, 0,0,0, 0,0,0,
  255,255,255,0,0,0, 0,0,0, 0,0,0
};

#define RASTER_TILE_SIZE 4 

static void
glw_raster_render(glw_t *w, glw_rctx_t *rc)
{
  glw_raster_t *q = (void *)w;

#if 0
  printf("RASTER W=%d H=%d\n", rc->rc_width, rc->rc_height);
  int i;
  for(i = 0; i < 16; i++) {
    printf("%f%c", rc->rc_be.gbr_mtx[i], "\t\t\t\n"[i&3]);
  }
#endif

  if(!glw_is_tex_inited(&q->tex))
    glw_tex_upload(w->glw_root, &q->tex, rastermap, GLW_TEXTURE_FORMAT_RGB,
		   RASTER_TILE_SIZE, RASTER_TILE_SIZE,  GLW_TEX_REPEAT);


  if(!glw_renderer_initialized(&q->r))
    glw_renderer_init_quad(&q->r);

  glw_renderer_vtx_pos(&q->r, 0, -1, -1, 0);
  glw_renderer_vtx_st (&q->r, 0, 0, rc->rc_height / (float)RASTER_TILE_SIZE);

  glw_renderer_vtx_pos(&q->r, 1,  1, -1, 0);
  glw_renderer_vtx_st (&q->r, 1, rc->rc_width / (float)RASTER_TILE_SIZE, rc->rc_height / (float)RASTER_TILE_SIZE);

  glw_renderer_vtx_pos(&q->r, 2,  1,  1, 0);
  glw_renderer_vtx_st (&q->r, 2, rc->rc_width / (float)RASTER_TILE_SIZE, 0);

  glw_renderer_vtx_pos(&q->r, 3, -1,  1, 0);
  glw_renderer_vtx_st (&q->r, 3, 0, 0);

  glw_renderer_draw(&q->r, w->glw_root, rc, &q->tex,
		    &q->color, NULL, rc->rc_alpha * w->glw_alpha, 0);
}


/**
 *
 */ 
static void 
glw_raster_ctor(glw_t *w)
{
  glw_raster_t *q = (void *)w;
  q->color.r = 1.0;
  q->color.g = 1.0;
  q->color.b = 1.0;
}


/**
 *
 */
static void 
glw_raster_set_rgb(glw_t *w, const float *rgb)
{
  glw_raster_t *q = (void *)w;
  q->color.r = rgb[0];
  q->color.g = rgb[1];
  q->color.b = rgb[2];
}


/**
 *
 */
static glw_class_t glw_raster = {
  .gc_name = "raster",
  .gc_instance_size = sizeof(glw_raster_t),
  .gc_render = glw_raster_render,
  .gc_ctor = glw_raster_ctor,
  .gc_set_rgb = glw_raster_set_rgb,
};



GLW_REGISTER_CLASS(glw_raster);
