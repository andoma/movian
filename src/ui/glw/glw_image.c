/*
 *  GL Widgets, GLW_IMAGE widget
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

typedef struct glw_image {
  glw_t w;

  float gi_alpha_self;

  float gi_angle;

  char *gi_pending_filename;
  glw_loadable_texture_t *gi_current;
  glw_loadable_texture_t *gi_pending;

  int16_t gi_border_left;
  int16_t gi_border_right;
  int16_t gi_border_top;
  int16_t gi_border_bottom;

  int16_t gi_padding_left;
  int16_t gi_padding_right;
  int16_t gi_padding_top;
  int16_t gi_padding_bottom;

  int16_t gi_margin_left;
  int16_t gi_margin_right;
  int16_t gi_margin_top;
  int16_t gi_margin_bottom;

  // gi_box_ is (gi_border_ + gi_padding_ + gi_margin_)
  int16_t gi_box_left;
  int16_t gi_box_right;
  int16_t gi_box_top;
  int16_t gi_box_bottom;
 
  int gi_bitmap_flags;

  uint8_t gi_mode;

#define GI_MODE_NORMAL           0
#define GI_MODE_BORDER_SCALING   1
#define GI_MODE_REPEATED_TEXTURE 2
#define GI_MODE_ALPHA_EDGES      3
#define GI_MODE_BORDER_ONLY_SCALING  4

  uint8_t gi_update;

  uint8_t gi_alpha_edge;

  uint8_t gi_was_valid;

  glw_renderer_t gi_gr;

  int16_t gi_last_width;
  int16_t gi_last_height;

  glw_rgb_t gi_color;
  glw_rgb_t gi_col_mul;
  glw_rgb_t gi_col_off;

  float gi_size_scale;

  float gi_saturation;

} glw_image_t;

static glw_class_t glw_image, glw_icon, glw_backdrop, glw_repeatedimage;

static uint8_t texcords[9][8] = {
    { 0, 1,   1, 1,   1, 0,  0, 0},  // Normal
    { 0, 1,   1, 1,   1, 0,  0, 0},  // Normal
    { 1, 1,   0, 1,   0, 0,  1, 0},  // Mirror X
    { 1, 0,   0, 0,   0, 1,  1, 1},  // 180 deg. rotate
    { 0, 0,   1, 0,   1, 1,  0, 1},  // Mirror Y
    { 0, 0,   0, 0,   0, 0,  0, 0},  // Transpose ???
    { 1, 1,   1, 0,   0, 0,  0, 1},  // Rot 90
    { 0, 0,   0, 0,   0, 0,  0, 0},  // Transverse ???
    { 0, 0,   0, 1,   1, 1,  1, 0},  // Rot 270
};






static void
glw_image_dtor(glw_t *w)
{
  glw_image_t *gi = (void *)w;

  free(gi->gi_pending_filename);

  if(gi->gi_current != NULL)
    glw_tex_deref(w->glw_root, gi->gi_current);

  if(gi->gi_pending != NULL)
    glw_tex_deref(w->glw_root, gi->gi_pending);

  glw_renderer_free(&gi->gi_gr);
}

/**
 *
 */
static void 
render_child_simple(glw_t *w, glw_rctx_t *rc)
{
  glw_rctx_t rc0 = *rc;
  glw_t *c;

  if((c = TAILQ_FIRST(&w->glw_childs)) != NULL) {
    rc0.rc_alpha = rc->rc_alpha * w->glw_alpha;
    glw_render0(c, &rc0);
  }
}

/**
 *
 */
static void 
render_child_autocentered(glw_image_t *gi, glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0;

  if((c = TAILQ_FIRST(&gi->w.glw_childs)) == NULL)
    return;

  rc0 = *rc;
  
  glw_reposition(&rc0, gi->gi_box_left, rc->rc_height - gi->gi_box_top,
		 rc->rc_width  - gi->gi_box_right, gi->gi_box_bottom);

  rc0.rc_alpha *= gi->w.glw_alpha;
  glw_render0(c, &rc0);
}


/**
 *
 */
static void
glw_scale_to_pixels(glw_rctx_t *rc, int w, int h)
{
  float xs = (float)w / rc->rc_width;
  float ys = (float)h / rc->rc_height;

  glw_Scalef(rc, xs, ys, 1.0f);
  rc->rc_width  = w;
  rc->rc_height = h;
}


/**
 *
 */
static void 
glw_image_render(glw_t *w, glw_rctx_t *rc)
{
  glw_image_t *gi = (void *)w;
  glw_loadable_texture_t *glt = gi->gi_current;
  float alpha_self;
  float blur = 1 - (rc->rc_blur * w->glw_blur);
  glw_rctx_t rc0;

  if(glt == NULL || glt->glt_state != GLT_STATE_VALID)
    return;

  if(!glw_is_tex_inited(&glt->glt_texture))
    alpha_self = 0;
  else
    alpha_self = rc->rc_alpha * w->glw_alpha * gi->gi_alpha_self;

  if(gi->gi_mode == GI_MODE_NORMAL || gi->gi_mode == GI_MODE_ALPHA_EDGES) {

    rc0 = *rc;

    glw_align_1(&rc0, w->glw_alignment);
      
    if(gi->gi_bitmap_flags & GLW_IMAGE_FIXED_SIZE)
      glw_scale_to_pixels(&rc0, glt->glt_xs, glt->glt_ys);
    else if(w->glw_class == &glw_image || w->glw_class == &glw_icon)
      glw_scale_to_aspect(&rc0, glt->glt_aspect);

    if(gi->gi_angle != 0)
      glw_Rotatef(&rc0, -gi->gi_angle, 0, 0, 1);

    glw_align_2(&rc0, w->glw_alignment);

    if(glw_is_focusable(w))
      glw_store_matrix(w, &rc0);

    if(alpha_self > 0.01f) {

      if(w->glw_flags2 & GLW2_SHADOW && !rc0.rc_inhibit_shadows) {
	float xd =  3.0f / rc0.rc_width;
	float yd = -3.0f / rc0.rc_height;

	glw_Translatef(&rc0, xd, yd, 0.0f);
	
	static const glw_rgb_t black = {0,0,0};

	glw_renderer_draw(&gi->gi_gr, w->glw_root, &rc0, &glt->glt_texture,
			  &black, NULL, alpha_self * 0.75f, blur);
	glw_Translatef(&rc0, -xd, -yd, 0.0f);
      }

      if(gi->gi_bitmap_flags & GLW_IMAGE_ADDITIVE)
	glw_blendmode(w->glw_root, GLW_BLEND_ADDITIVE);

      glw_renderer_draw(&gi->gi_gr, w->glw_root, &rc0, &glt->glt_texture,
			&gi->gi_col_mul, &gi->gi_col_off, alpha_self, blur);

      if(gi->gi_bitmap_flags & GLW_IMAGE_ADDITIVE)
	glw_blendmode(w->glw_root, GLW_BLEND_NORMAL);
    }

    render_child_simple(w, &rc0);

  } else {

    if(glw_is_focusable(w))
      glw_store_matrix(w, rc);

    if(alpha_self > 0.01f) {

      if(gi->gi_bitmap_flags & GLW_IMAGE_ADDITIVE)
	glw_blendmode(w->glw_root, GLW_BLEND_ADDITIVE);

      glw_renderer_draw(&gi->gi_gr, w->glw_root, rc, &glt->glt_texture,
			&gi->gi_col_mul, &gi->gi_col_off, alpha_self, blur);

      if(gi->gi_bitmap_flags & GLW_IMAGE_ADDITIVE)
	glw_blendmode(w->glw_root, GLW_BLEND_NORMAL);
    }

    render_child_autocentered(gi, rc);
  }
}

/**
 *
 */
static void
glw_image_layout_tesselated(glw_root_t *gr, glw_rctx_t *rc, glw_image_t *gi, 
			    glw_loadable_texture_t *glt)
{
  float tex[4][2];
  float vex[4][2];

  int x, y, i = 0;

  if(gr->gr_normalized_texture_coords) {
    tex[1][0] = 0.0f + (float)gi->gi_border_left  / glt->glt_xs;
    tex[2][0] = glt->glt_s - (float)gi->gi_border_right / glt->glt_xs;
    tex[0][0] = gi->gi_bitmap_flags & GLW_IMAGE_BORDER_LEFT ? 0.0f : tex[1][0];
    tex[3][0] = gi->gi_bitmap_flags & GLW_IMAGE_BORDER_RIGHT ? glt->glt_s : tex[2][0];

    tex[0][1] = 0.0f;
    tex[1][1] = 0.0f + (float)gi->gi_border_top    / glt->glt_ys;
    tex[2][1] = glt->glt_t - (float)gi->gi_border_bottom / glt->glt_ys;
    tex[3][1] = glt->glt_t;

  } else {

    tex[1][0] = gi->gi_border_left;
    tex[2][0] = glt->glt_xs - gi->gi_border_right;
    tex[0][0] = gi->gi_bitmap_flags & GLW_IMAGE_BORDER_LEFT  ? 0.0f : tex[1][0];
    tex[3][0] = gi->gi_bitmap_flags & GLW_IMAGE_BORDER_RIGHT ? glt->glt_xs : tex[2][0];

    tex[0][1] = 0.0f;
    tex[1][1] = gi->gi_border_top;
    tex[2][1] = glt->glt_ys - gi->gi_border_bottom;
    tex[3][1] = glt->glt_ys;
  }


  vex[0][0] = GLW_MIN(-1.0f + 2.0f * (gi->gi_margin_left)  / rc->rc_width, 0.0f);
  vex[1][0] = GLW_MIN(-1.0f + 2.0f * (gi->gi_border_left + gi->gi_margin_left)  / rc->rc_width, 0.0f);
  vex[2][0] = GLW_MAX( 1.0f - 2.0f * (gi->gi_border_right + gi->gi_margin_right) / rc->rc_width, 0.0f);
  vex[3][0] = GLW_MAX( 1.0f - 2.0f * (gi->gi_margin_right) / rc->rc_width, 0.0f);
    
  vex[0][1] = GLW_MAX( 1.0f - 2.0f * (gi->gi_margin_top)  / rc->rc_height, 0.0f);
  vex[1][1] = GLW_MAX( 1.0f - 2.0f * (gi->gi_border_top + gi->gi_margin_top)  / rc->rc_height, 0.0f);
  vex[2][1] = GLW_MIN(-1.0f + 2.0f * (gi->gi_border_bottom + gi->gi_margin_bottom) / rc->rc_height, 0.0f);
  vex[3][1] = GLW_MIN(-1.0f + 2.0f * (gi->gi_margin_bottom) / rc->rc_height, 0.0f);

  for(y = 0; y < 4; y++) {
    for(x = 0; x < 4; x++) {
      glw_renderer_vtx_pos(&gi->gi_gr, i, vex[x][0], vex[y][1], 0.0f);
      glw_renderer_vtx_st (&gi->gi_gr, i, tex[x][0], tex[y][1]);
      i++;
    }
  }
}

static const float alphaborder[4][4] = {
  {0,0,0,0},
  {0,1,1,0},
  {0,1,1,0},
  {0,0,0,0},
};

/**
 *
 */
static void
glw_image_layout_alpha_edges(glw_root_t *gr, glw_rctx_t *rc, glw_image_t *gi, 
			       glw_loadable_texture_t *glt)
{
  float tex[4][2];
  float vex[4][2];

  int x, y, i = 0;

  if(gr->gr_normalized_texture_coords) {
    tex[0][0] = 0.0f;
    tex[1][0] = 0.0f + (float)gi->gi_alpha_edge / glt->glt_xs;
    tex[2][0] = glt->glt_s - (float)gi->gi_alpha_edge / glt->glt_xs;
    tex[3][0] = glt->glt_s;

    tex[0][1] = 0.0f;
    tex[1][1] = 0.0f + (float)gi->gi_alpha_edge / glt->glt_ys;
    tex[2][1] = glt->glt_t - (float)gi->gi_alpha_edge / glt->glt_ys;
    tex[3][1] = glt->glt_t;
  } else {
    tex[0][0] = 0.0f;
    tex[1][0] = gi->gi_alpha_edge;
    tex[2][0] = glt->glt_xs - gi->gi_alpha_edge;
    tex[3][0] = glt->glt_xs;

    tex[0][1] = 0.0f;
    tex[1][1] = gi->gi_alpha_edge;
    tex[2][1] = glt->glt_ys - gi->gi_alpha_edge;
    tex[3][1] = glt->glt_ys;
  }

  vex[0][0] = -1.0f;
  vex[1][0] = GLW_MIN(-1.0f + 2.0f * gi->gi_alpha_edge / rc->rc_width, 0.0f);
  vex[2][0] = GLW_MAX( 1.0f - 2.0f * gi->gi_alpha_edge / rc->rc_width, 0.0f);
  vex[3][0] = 1.0f;
    
  vex[0][1] = 1.0f;
  vex[1][1] = GLW_MAX( 1.0f - 2.0f * gi->gi_alpha_edge / rc->rc_height, 0.0f);
  vex[2][1] = GLW_MIN(-1.0f + 2.0f * gi->gi_alpha_edge / rc->rc_height, 0.0f);
  vex[3][1] = -1.0f;

  for(y = 0; y < 4; y++) {
    for(x = 0; x < 4; x++) {
      glw_renderer_vtx_pos(&gi->gi_gr, i, vex[x][0], vex[y][1], 0.0f);
      glw_renderer_vtx_st (&gi->gi_gr, i, tex[x][0], tex[y][1]);
      glw_renderer_vtx_col(&gi->gi_gr, i, 1, 1, 1, alphaborder[x][y]);
      i++;
    }
  }
}


/**
 *
 */
static void
glw_image_layout_normal(glw_root_t *gr, glw_rctx_t *rc, glw_image_t *gi, 
			glw_loadable_texture_t *glt)
{
  float xs = gr->gr_normalized_texture_coords ? glt->glt_s : glt->glt_xs;
  float ys = gr->gr_normalized_texture_coords ? glt->glt_t : glt->glt_ys;

  uint8_t tex[8];
  int o = glt->glt_orientation < 9 ? glt->glt_orientation : 0;
  memcpy(tex, texcords[o], 8);


  glw_renderer_vtx_pos(&gi->gi_gr, 0, -1.0, -1.0, 0.0);
  glw_renderer_vtx_st (&gi->gi_gr, 0, 
		       tex[0] * xs , tex[1] * ys);

  glw_renderer_vtx_pos(&gi->gi_gr, 1,  1.0, -1.0, 0.0);
  glw_renderer_vtx_st (&gi->gi_gr, 1,
		       tex[2] * xs , tex[3] * ys);

  glw_renderer_vtx_pos(&gi->gi_gr, 2,  1.0,  1.0, 0.0);
  glw_renderer_vtx_st (&gi->gi_gr, 2,
		       tex[4] * xs , tex[5] * ys);

  glw_renderer_vtx_pos(&gi->gi_gr, 3, -1.0,  1.0, 0.0);
  glw_renderer_vtx_st (&gi->gi_gr, 3,
		       tex[6] * xs , tex[7] * ys);
}


/**
 *
 */
static void
glw_image_layout_repeated(glw_root_t *gr, glw_rctx_t *rc, glw_image_t *gi, 
			  glw_loadable_texture_t *glt)
{
  float xs = gr->gr_normalized_texture_coords ? glt->glt_s : glt->glt_xs;
  float ys = gr->gr_normalized_texture_coords ? glt->glt_t : glt->glt_ys;

  glw_renderer_vtx_pos(&gi->gi_gr, 0, -1.0, -1.0, 0.0);
  glw_renderer_vtx_st (&gi->gi_gr, 0, 0, ys);

  glw_renderer_vtx_pos(&gi->gi_gr, 1,  1.0, -1.0, 0.0);
  glw_renderer_vtx_st (&gi->gi_gr, 1, xs, ys);

  glw_renderer_vtx_pos(&gi->gi_gr, 2,  1.0,  1.0, 0.0);
  glw_renderer_vtx_st (&gi->gi_gr, 2, xs, 0);

  glw_renderer_vtx_pos(&gi->gi_gr, 3, -1.0,  1.0, 0.0);
  glw_renderer_vtx_st (&gi->gi_gr, 3, 0, 0);
}


/**
 *
 */
static void
glw_image_update_constraints(glw_image_t *gi)
{
  glw_loadable_texture_t *glt = gi->gi_current;
  glw_t *c;
  glw_root_t *gr = gi->w.glw_root;

  if(gi->gi_bitmap_flags & GLW_IMAGE_FIXED_SIZE) {

    glw_set_constraints(&gi->w, 
			glt->glt_xs,
			glt->glt_ys,
			0,
			GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y, 0);

  } else if(gi->w.glw_class == &glw_backdrop) {

    c = TAILQ_FIRST(&gi->w.glw_childs);

    if(c != NULL) {
      glw_set_constraints(&gi->w, 
			  c->glw_req_size_x +
			  gi->gi_box_left + gi->gi_box_right,
			  c->glw_req_size_y + 
			  gi->gi_box_top + gi->gi_box_bottom,
			  c->glw_req_weight,
			  c->glw_flags & GLW_CONSTRAINT_FLAGS, 0);

    } else if(glt != NULL) {
      glw_set_constraints(&gi->w, 
			  glt->glt_xs,
			  glt->glt_ys,
			  0, 0, 0);
    }

  } else if(gi->w.glw_class == &glw_icon) {

    float siz = gi->gi_size_scale * gr->gr_fontsize;

    glw_set_constraints(&gi->w, siz, siz, 0,
			GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y, 0);

  } else if(gi->w.glw_class == &glw_image && glt != NULL &&
	    gi->gi_bitmap_flags & GLW_IMAGE_SET_ASPECT) {
    float aspect = (float)glt->glt_xs / glt->glt_ys;
    glw_set_constraints(&gi->w, 0, 0, -aspect,
			GLW_CONSTRAINT_W, 0);
  }
}


/**
 *
 * 0--1--2--3 
 * | /| /|\ |
 * |/ |/ | \|
 * 4--5--6--7
 * | /| /| /|
 * |/ |/ |/ |
 * 8--9--a--b
 * |\ | /| /|
 * | \|/ |/ |
 * c--d--e--f
 */

static uint16_t borderobject[] = {
  4, 1, 0,
  4, 5, 1,
  5, 2, 1,
  5, 6, 2,
  6, 7, 2,
  2, 7, 3,
  8, 5, 4,
  8, 9, 5,
  9, 6, 5,
  9, 10, 6,
  10, 7, 6,
  10, 11, 7,
  12, 13, 8,
  8, 13, 9,
  13, 10, 9,
  13, 14, 10,
  14, 11, 10,
  14, 15, 11,
};

static uint16_t borderonlyobject[] = {
  4, 1, 0,
  4, 5, 1,
  5, 2, 1,
  5, 6, 2,
  6, 7, 2,
  2, 7, 3,
  8, 5, 4,
  8, 9, 5,
  //  9, 6, 5,
  //  9, 10, 6,
  10, 7, 6,
  10, 11, 7,
  12, 13, 8,
  8, 13, 9,
  13, 10, 9,
  13, 14, 10,
  14, 11, 10,
  14, 15, 11,
};


/**
 *
 */
static void 
glw_image_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_image_t *gi = (void *)w;
  glw_root_t *gr = w->glw_root;
  glw_loadable_texture_t *glt;
  glw_rctx_t rc0;
  glw_t *c;
  int hq = (w->glw_class == &glw_icon || w->glw_class == &glw_image);

  if(gi->gi_pending_filename != NULL) {
    // Request to load
    int xs = -1, ys = -1;
    int flags = 0;
    
    if(gi->gi_pending != NULL)
      glw_tex_deref(w->glw_root, gi->gi_pending);
    
    if(gi->gi_pending_filename[0] == 0) {
      gi->gi_pending = NULL;

      if(gi->gi_current != NULL) {
	glw_tex_deref(w->glw_root, gi->gi_current);
	gi->gi_current = NULL;
      }

      gi->gi_update = 1;

    } else {

    
      if(w->glw_class == &glw_repeatedimage)
	flags |= GLW_TEX_REPEAT;


      if(hq) {
	if(rc->rc_width < rc->rc_height) {
	  xs = rc->rc_width;
	} else {
	  ys = rc->rc_height;
	}
      }

      if(xs && ys) {

	gi->gi_pending = glw_tex_create(w->glw_root, gi->gi_pending_filename,
					flags, xs, ys);

	free(gi->gi_pending_filename);
	gi->gi_pending_filename = NULL;
      } else {
	gi->gi_pending = NULL;
      }
    }
  }

  if((glt = gi->gi_pending) != NULL) {
    glw_tex_layout(gr, glt);

    if(glt->glt_state == GLT_STATE_VALID || 
       glt->glt_state == GLT_STATE_ERROR) {
      // Pending texture completed, ok or error: transfer to current

      if(gi->gi_current != NULL)
	glw_tex_deref(w->glw_root, gi->gi_current);

      gi->gi_current = gi->gi_pending;
      gi->gi_pending = NULL;
      gi->gi_update = 1;
    }
  }

  if((glt = gi->gi_current) == NULL)
    return;

  glw_tex_layout(gr, glt);

  if(glt->glt_state == GLT_STATE_ERROR) {
    if(!gi->gi_was_valid) {
      glw_signal0(w, GLW_SIGNAL_READY, NULL);
      gi->gi_was_valid = 1;
    }
  } else if(glt->glt_state == GLT_STATE_VALID) {

    if(!gi->gi_was_valid) {
      glw_signal0(w, GLW_SIGNAL_READY, NULL);
      gi->gi_was_valid = 1;
    }

    if(gi->gi_update) {
      gi->gi_update = 0;

      glw_renderer_free(&gi->gi_gr);

      switch(gi->gi_mode) {
	
      case GI_MODE_NORMAL:
	glw_renderer_init_quad(&gi->gi_gr);
	glw_image_layout_normal(gr, rc, gi, glt);
	break;
      case GI_MODE_BORDER_SCALING:
	glw_renderer_init(&gi->gi_gr, 16, 18, borderobject);
	glw_image_layout_tesselated(gr, rc, gi, glt);
	break;
      case GI_MODE_REPEATED_TEXTURE:
	glw_renderer_init_quad(&gi->gi_gr);
	glw_image_layout_repeated(gr, rc, gi, glt);
	break;
      case GI_MODE_ALPHA_EDGES:
	glw_renderer_init(&gi->gi_gr, 16, 18, borderobject);
	glw_image_layout_alpha_edges(gr, rc, gi, glt);
	break;
      case GI_MODE_BORDER_ONLY_SCALING:
	glw_renderer_init(&gi->gi_gr, 16, 16, borderonlyobject);
	glw_image_layout_tesselated(gr, rc, gi, glt);
	break;
      default:
	abort();
      }


    } else if(gi->gi_last_width  != rc->rc_width ||
	      gi->gi_last_height != rc->rc_height) {

      gi->gi_last_width  = rc->rc_width;
      gi->gi_last_height = rc->rc_height;

      switch(gi->gi_mode) {
	
      case GI_MODE_NORMAL:
	break;
      case GI_MODE_BORDER_SCALING:
      case GI_MODE_BORDER_ONLY_SCALING:
	glw_image_layout_tesselated(gr, rc, gi, glt);
	break;
      case GI_MODE_REPEATED_TEXTURE:
	glw_image_layout_repeated(gr, rc, gi, glt);
	break;
      case GI_MODE_ALPHA_EDGES:
	glw_image_layout_alpha_edges(gr, rc, gi, glt);
	break;
      }

      if(hq && gi->gi_pending == NULL &&
	 gi->gi_pending_filename == NULL && rc->rc_width && rc->rc_height) {

	int xs = -1, ys = -1, rescale;
	
	if(rc->rc_width < rc->rc_height) {
	  rescale = abs(rc->rc_width - glt->glt_xs) > glt->glt_xs / 10;
	  xs = rc->rc_width;
	} else {
	  rescale = abs(rc->rc_height - glt->glt_ys) > glt->glt_ys / 10;
	  ys = rc->rc_height;
	}
	
	if(rescale) {
	  int flags = 0;
	  if(w->glw_class == &glw_repeatedimage)
	    flags |= GLW_TEX_REPEAT;

	  gi->gi_pending = glw_tex_create(w->glw_root, glt->glt_filename,
					  flags, xs, ys);
	}
      }
    }
  }

  glw_image_update_constraints(gi);

  if((c = TAILQ_FIRST(&w->glw_childs)) != NULL) {
    rc0 = *rc;
    
    rc0.rc_width  -= gi->gi_box_left + gi->gi_box_right;
    rc0.rc_height -= gi->gi_box_top  + gi->gi_box_bottom;

    if(rc0.rc_height >= 0 && rc0.rc_width >= 0)
      glw_layout0(c, &rc0);
  }
}


/*
 *
 */
static int
glw_image_callback(glw_t *w, void *opaque, glw_signal_t signal,
		    void *extra)
{
  glw_t *c;
  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_image_layout(w, extra);
    break;
  case GLW_SIGNAL_EVENT:
    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link)
      if(glw_signal0(c, GLW_SIGNAL_EVENT, extra))
	return 1;
    break;

  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
  case GLW_SIGNAL_CHILD_CREATED:
    glw_image_update_constraints((glw_image_t *)w);
    return 1;
  case GLW_SIGNAL_CHILD_DESTROYED:
    glw_set_constraints(w, 0, 0, 0, 0, 0);
    return 1;

  }
  return 0;
}

/**
 *
 */
static void
compute_colors(glw_image_t *gi)
{
  float iS = 1 - gi->gi_saturation;
  gi->gi_col_mul.r = gi->gi_color.r * iS;
  gi->gi_col_mul.g = gi->gi_color.g * iS;
  gi->gi_col_mul.b = gi->gi_color.b * iS;

  gi->gi_col_off.r = gi->gi_saturation;
  gi->gi_col_off.g = gi->gi_saturation;
  gi->gi_col_off.b = gi->gi_saturation;
}



/**
 *
 */
static void 
glw_image_ctor(glw_t *w)
{
  glw_image_t *gi = (void *)w;

  gi->gi_bitmap_flags = GLW_IMAGE_BORDER_LEFT | GLW_IMAGE_BORDER_RIGHT;

  gi->gi_alpha_self = 1;
  gi->gi_color.r = 1.0;
  gi->gi_color.g = 1.0;
  gi->gi_color.b = 1.0;
  gi->gi_size_scale = 1.0;

  compute_colors(gi);

  if(w->glw_class == &glw_repeatedimage)
    gi->gi_mode = GI_MODE_REPEATED_TEXTURE;
}

/**
 *
 */
static void
glw_image_set_rgb(glw_t *w, const float *rgb)
{
  glw_image_t *gi = (void *)w;
  gi->gi_color.r = rgb[0];
  gi->gi_color.g = rgb[1];
  gi->gi_color.b = rgb[2];
  compute_colors(gi);
}


/**
 *
 */
static void
update_box(glw_image_t *gi)
{
  gi->gi_box_left =
    gi->gi_margin_left + gi->gi_border_left + gi->gi_padding_left;
  gi->gi_box_top =
    gi->gi_margin_top + gi->gi_border_top + gi->gi_padding_top;
  gi->gi_box_right =
    gi->gi_margin_right + gi->gi_border_right + gi->gi_padding_right;
  gi->gi_box_bottom =
    gi->gi_margin_bottom + gi->gi_border_bottom + gi->gi_padding_bottom;
}

/**
 *
 */
static void
set_border(glw_t *w, const int16_t *v)
{
  glw_image_t *gi = (void *)w;

  if(gi->gi_mode != GI_MODE_BORDER_ONLY_SCALING)
    gi->gi_mode = GI_MODE_BORDER_SCALING;
  gi->gi_border_left   = v[0];
  gi->gi_border_top    = v[1];
  gi->gi_border_right  = v[2];
  gi->gi_border_bottom = v[3];
  update_box(gi);
  gi->gi_update = 1;
  glw_image_update_constraints(gi);
}


/**
 *
 */
static void
set_padding(glw_t *w, const int16_t *v)
{
  glw_image_t *gi = (void *)w;

  gi->gi_padding_left   = v[0];
  gi->gi_padding_top    = v[1];
  gi->gi_padding_right  = v[2];
  gi->gi_padding_bottom = v[3];
  update_box(gi);
  gi->gi_update = 1;
  glw_image_update_constraints(gi);
}


/**
 *
 */
static void
set_margin(glw_t *w, const int16_t *v)
{
  glw_image_t *gi = (void *)w;

  gi->gi_margin_left   = v[0];
  gi->gi_margin_top    = v[1];
  gi->gi_margin_right  = v[2];
  gi->gi_margin_bottom = v[3];
  update_box(gi);
  gi->gi_update = 1;
  glw_image_update_constraints(gi);
}


/**
 *
 */
static void
mod_image_flags(glw_t *w, int set, int clr)
{
  glw_image_t *gi = (void *)w;
  gi->gi_bitmap_flags = (gi->gi_bitmap_flags | set) & ~clr;
  gi->gi_update = 1;

  if(set & GLW_IMAGE_BORDER_ONLY)
    gi->gi_mode = GI_MODE_BORDER_ONLY_SCALING;
  if(clr & GLW_IMAGE_BORDER_ONLY)
    gi->gi_mode = GI_MODE_BORDER_SCALING;
}


/**
 *
 */
static void
set_source(glw_t *w, const char *filename)
{
  glw_image_t *gi = (glw_image_t *)w;
  
  const char *curname;

  if(gi->gi_pending_filename != NULL)
    curname = gi->gi_pending_filename;
  else if(gi->gi_pending != NULL) 
    curname = gi->gi_pending->glt_filename;
  else if(gi->gi_current != NULL) 
    curname = gi->gi_current->glt_filename;
  else 
    curname = NULL;
  
  if(curname != NULL && filename != NULL && !strcmp(filename, curname))
    return;
  
  if(gi->gi_pending_filename != NULL)
    free(gi->gi_pending_filename);
  
  gi->gi_pending_filename = filename ? strdup(filename) : strdup("");
}


/**
 *
 */
static void
set_alpha_self(glw_t *w, float f)
{
  glw_image_t *gi = (glw_image_t *)w;
  gi->gi_alpha_self = f;
}


/**
 * Only for icon class
 */
static void
set_size_scale(glw_t *w, float f)
{
  glw_image_t *gi = (glw_image_t *)w;
  gi->gi_size_scale = f;

  float siz = gi->gi_size_scale * w->glw_root->gr_fontsize;
  glw_set_constraints(w, siz, siz, 0, GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y, 0);
}

/**
 *
 */
static void 
glw_image_set(glw_t *w, va_list ap)
{
  glw_image_t *gi = (glw_image_t *)w;
  glw_attribute_t attrib;

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_ANGLE:
      gi->gi_angle = va_arg(ap, double);
      break;

    case GLW_ATTRIB_SATURATION:
      gi->gi_saturation = va_arg(ap, double);
      compute_colors(gi);
      break;
      
    case GLW_ATTRIB_ALPHA_EDGES:
      gi->gi_alpha_edge = va_arg(ap, int);
      gi->gi_mode = GI_MODE_ALPHA_EDGES;
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);

  if(w->glw_class == &glw_icon) {
  }
}


/**
 *
 */
static int
glw_image_ready(glw_t *w)
{
  glw_image_t *gi = (glw_image_t *)w;
  glw_loadable_texture_t *glt = gi->gi_current;
 
  return glt != NULL && (glt->glt_state == GLT_STATE_VALID || 
			 glt->glt_state == GLT_STATE_ERROR);
}


/**
 *
 */
static const char *
get_identity(glw_t *w)
{
  glw_image_t *gi = (glw_image_t *)w;
  glw_loadable_texture_t *glt = gi->gi_current;
  return glt ? glt->glt_filename : "unloaded";
}

/**
 *
 */
static glw_class_t glw_image = {
  .gc_name = "image",
  .gc_instance_size = sizeof(glw_image_t),
  .gc_render = glw_image_render,
  .gc_dtor = glw_image_dtor,
  .gc_ctor = glw_image_ctor,
  .gc_set = glw_image_set,
  .gc_signal_handler = glw_image_callback,
  .gc_default_alignment = LAYOUT_ALIGN_CENTER,
  .gc_ready = glw_image_ready,
  .gc_set_rgb = glw_image_set_rgb,
  .gc_set_padding = set_padding,
  .gc_mod_image_flags = mod_image_flags,
  .gc_set_source = set_source,
  .gc_set_alpha_self = set_alpha_self,
};

GLW_REGISTER_CLASS(glw_image);


/**
 *
 */
static glw_class_t glw_icon = {
  .gc_name = "icon",
  .gc_instance_size = sizeof(glw_image_t),
  .gc_render = glw_image_render,
  .gc_ctor = glw_image_ctor,
  .gc_dtor = glw_image_dtor,
  .gc_set = glw_image_set,
  .gc_signal_handler = glw_image_callback,
  .gc_default_alignment = LAYOUT_ALIGN_CENTER,
  .gc_set_rgb = glw_image_set_rgb,
  .gc_set_padding = set_padding,
  .gc_mod_image_flags = mod_image_flags,
  .gc_set_source = set_source,
  .gc_set_alpha_self = set_alpha_self,
  .gc_set_size_scale = set_size_scale,
};

GLW_REGISTER_CLASS(glw_icon);


/**
 *
 */
static glw_class_t glw_backdrop = {
  .gc_name = "backdrop",
  .gc_instance_size = sizeof(glw_image_t),
  .gc_render = glw_image_render,
  .gc_ctor = glw_image_ctor,
  .gc_dtor = glw_image_dtor,
  .gc_set = glw_image_set,
  .gc_signal_handler = glw_image_callback,
  .gc_default_alignment = LAYOUT_ALIGN_CENTER,
  .gc_set_rgb = glw_image_set_rgb,
  .gc_set_padding = set_padding,
  .gc_set_border = set_border,
  .gc_set_margin = set_margin,
  .gc_mod_image_flags = mod_image_flags,
  .gc_set_source = set_source,
  .gc_set_alpha_self = set_alpha_self,
  .gc_get_identity = get_identity,
};

GLW_REGISTER_CLASS(glw_backdrop);



/**
 *
 */
static glw_class_t glw_repeatedimage = {
  .gc_name = "repeatedimage",
  .gc_instance_size = sizeof(glw_image_t),
  .gc_render = glw_image_render,
  .gc_ctor = glw_image_ctor,
  .gc_dtor = glw_image_dtor,
  .gc_set = glw_image_set,
  .gc_signal_handler = glw_image_callback,
  .gc_default_alignment = LAYOUT_ALIGN_CENTER,
  .gc_set_rgb = glw_image_set_rgb,
  .gc_set_padding = set_padding,
  .gc_mod_image_flags = mod_image_flags,
  .gc_set_source = set_source,
  .gc_set_alpha_self = set_alpha_self,
};

GLW_REGISTER_CLASS(glw_repeatedimage);
