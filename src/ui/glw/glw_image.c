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
#include "glw.h"
#include "glw_renderer.h"
#include "glw_texture.h"

typedef struct glw_image {
  glw_t w;

  float gi_alpha_self;

  float gi_angle;

  float gi_aspect;

  rstr_t *gi_pending_url;
  int gi_pending_url_flags;

  glw_loadable_texture_t *gi_current;
  glw_loadable_texture_t *gi_pending;

  int16_t gi_border[4];
  int16_t gi_padding[4];

  // gi_box_ is (gi_border_ + gi_padding_ )
  int16_t gi_box_left;
  int16_t gi_box_right;
  int16_t gi_box_top;
  int16_t gi_box_bottom;

  int gi_bitmap_flags;

  uint8_t gi_rescale_hold;

  uint8_t gi_mode;

#define GI_MODE_NORMAL           0
#define GI_MODE_BORDER_SCALING   1
#define GI_MODE_REPEATED_TEXTURE 2
#define GI_MODE_ALPHA_EDGES      3
#define GI_MODE_BORDER_ONLY_SCALING  4


  uint8_t gi_alpha_edge;

  uint8_t gi_widget_status;

  uint8_t gi_is_ready : 1;
  uint8_t gi_update : 1;
  uint8_t gi_need_reload : 1;
  uint8_t gi_loading_new_url : 1;
  uint8_t gi_recompile : 1;
  uint8_t gi_externalized : 1;
  uint8_t gi_saturated : 1;
  uint8_t gi_want_primary_color : 1;

  int16_t gi_fixed_size;
  int16_t gi_radius;
  int16_t gi_shadow;

  glw_renderer_t gi_gr;

  int16_t gi_last_width;
  int16_t gi_last_height;

  glw_rgb_t gi_color;
  glw_rgb_t gi_col_mul;
  glw_rgb_t gi_col_off;

  float gi_size_scale;

  float gi_saturation;

  float gi_autofade;

  float gi_child_aspect;

  LIST_ENTRY(glw_image) gi_link;

  rstr_t **gi_sources;

  int gi_switch_cnt;
  int gi_switch_tgt;

  float gi_max_intensity;

  glw_program_args_t gi_gpa;

  rstr_t *gi_fs;

} glw_image_t;

static glw_class_t glw_image, glw_icon, glw_backdrop, glw_repeatedimage,
  glw_frontdrop;

static int8_t tex_transform[9][4] = {
  { 1, 0, 0, 1},  // No transform
  { 1, 0, 0, 1},  // No transform
  { -1, 0,0, 1},  // Mirror X
  { -1, 0,0, -1}, // 180° rotate
  { 1, 0,0, -1},  // Mirror Y
  { 0,1,1,0},     // Transpose
  { 0,1,-1,0},    // 90° rotate
  { 0,1,1,0},     // Transverse ???
  { 0,-1,1,0},    // 270° rotate
};

static void update_box(glw_image_t *gi);
static void pick_source(glw_image_t *gi, int next);
static void compute_colors(glw_image_t *gi);

/**
 *
 */
static void
set_load_status(glw_image_t *gi, glw_widget_status_t status)
{
  if(gi->gi_widget_status == status)
    return;
  gi->gi_widget_status = status;
  glw_signal0(&gi->w, GLW_SIGNAL_STATUS_CHANGED, NULL);
}


/**
 *
 */
static void
sources_free(rstr_t **v)
{
  int i;
  if(v == NULL)
    return;
  for(i = 0; v[i] != NULL; i++)
    rstr_release(v[i]);
  free(v);
}



/**
 *
 */
static void
glw_image_dtor(glw_t *w)
{
  glw_image_t *gi = (glw_image_t *)w;

  sources_free(gi->gi_sources);

  rstr_release(gi->gi_pending_url);

  if(gi->gi_current != NULL)
    glw_tex_deref(w->glw_root, gi->gi_current);

  if(gi->gi_pending != NULL)
    glw_tex_deref(w->glw_root, gi->gi_pending);
  glw_renderer_free(&gi->gi_gr);
  rstr_release(gi->gi_fs);
  glw_destroy_program(w->glw_root, gi->gi_gpa.gpa_prog);
}


/**
 *
 */
static void
glw_image_set_fs(glw_t *w, rstr_t *fs)
{
  glw_image_t *gi = (glw_image_t *)w;
  rstr_set(&gi->gi_fs, fs);
  gi->gi_recompile = 1;

}

/**
 *
 */
static void
glw_icon_dtor(glw_t *w)
{
  glw_image_t *gi = (glw_image_t *)w;
  LIST_REMOVE(gi, gi_link);
  glw_image_dtor(w);
}


/**
 *
 */
static void
render_child_simple(glw_t *w, glw_rctx_t *rc)
{
  glw_rctx_t rc0 = *rc;
  glw_t *c = TAILQ_FIRST(&w->glw_childs);

  rc0.rc_alpha = rc->rc_alpha * w->glw_alpha;
  glw_render0(c, &rc0);
}

/**
 *
 */
static void
render_child_autocentered(glw_image_t *gi, const glw_rctx_t *rc)
{
  glw_t *c;
  glw_rctx_t rc0;

  c = TAILQ_FIRST(&gi->w.glw_childs);

  rc0 = *rc;

  glw_reposition(&rc0,
		 gi->gi_box_left,
		 rc->rc_height - gi->gi_box_top,
		 rc->rc_width  - gi->gi_box_right,
		 gi->gi_box_bottom);

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
glw_image_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_image_t *gi = (void *)w;

  if(gi->gi_externalized)
    return;

  const glw_loadable_texture_t *glt = gi->gi_current;
  float alpha_self;
  float blur = 1 - (rc->rc_sharpness * w->glw_sharpness);
  glw_rctx_t rc0;

  if(gi->gi_recompile) {
    glw_destroy_program(w->glw_root, gi->gi_gpa.gpa_prog);
    gi->gi_gpa.gpa_prog =
      glw_make_program(w->glw_root, NULL, rstr_get(gi->gi_fs));
    gi->gi_recompile = 0;
  }

  alpha_self = rc->rc_alpha * w->glw_alpha * gi->gi_alpha_self * gi->gi_autofade;


  const glw_rgb_t *rgb_off = gi->gi_saturated ? &gi->gi_col_off : NULL;

  if(gi->gi_mode == GI_MODE_NORMAL || gi->gi_mode == GI_MODE_ALPHA_EDGES) {

    if(glt == NULL || !glw_is_tex_inited(&glt->glt_texture))
      return;

    rc0 = *rc;

    glw_align_1(&rc0, w->glw_alignment);

    if(gi->gi_bitmap_flags & GLW_IMAGE_FIXED_SIZE) {
      glw_scale_to_pixels(&rc0, glt->glt_xs, glt->glt_ys);

    } else if(w->glw_class == &glw_image || w->glw_class == &glw_icon) {

      int extra_y_margin = 0;
      if(w->glw_class == &glw_icon) {

        int ys = gi->gi_fixed_size;
        if(ys == 0)
          ys = gi->gi_size_scale * w->glw_root->gr_current_size;
        if(ys > rc->rc_height)
          ys = rc->rc_height;

        extra_y_margin = MAX(rc->rc_height - ys, 0) / 2;
        glw_reposition(&rc0, 0,
                       rc0.rc_height - extra_y_margin,
                       rc0.rc_width,
                       extra_y_margin);
      }
      glw_scale_to_aspect(&rc0, glt->glt_aspect);
    }
    if(gi->gi_angle != 0)
      glw_Rotatef(&rc0, -gi->gi_angle, 0, 0, 1);

    glw_align_2(&rc0, w->glw_alignment);

    if(glw_is_focusable_or_clickable(w))
      glw_store_matrix(w, &rc0);

    if(w->glw_class == &glw_frontdrop && TAILQ_FIRST(&w->glw_childs) != NULL) {
      render_child_simple(w, &rc0);
      glw_zinc(&rc0);
    }

    if(alpha_self > GLW_ALPHA_EPSILON) {

      if(gi->gi_bitmap_flags & GLW_IMAGE_ADDITIVE)
	glw_blendmode(w->glw_root, GLW_BLEND_ADDITIVE);

      glw_renderer_draw(&gi->gi_gr, w->glw_root, &rc0,
			&glt->glt_texture, NULL,
			&gi->gi_col_mul, rgb_off, alpha_self, blur,
			gi->gi_gpa.gpa_prog ? &gi->gi_gpa : NULL);

      if(gi->gi_bitmap_flags & GLW_IMAGE_ADDITIVE)
	glw_blendmode(w->glw_root, GLW_BLEND_NORMAL);
    }

    if(w->glw_class != &glw_frontdrop && TAILQ_FIRST(&w->glw_childs) != NULL) {
      glw_zinc(&rc0);
      render_child_simple(w, &rc0);
    }

  } else {

    rc0 = *rc;

    if(glw_is_focusable_or_clickable(w))
      glw_store_matrix(w, &rc0);

    if(w->glw_class == &glw_frontdrop && TAILQ_FIRST(&w->glw_childs) != NULL) {
      render_child_autocentered(gi, &rc0);
      glw_zinc(&rc0);
    }

    if(glt && glw_is_tex_inited(&glt->glt_texture) &&
       alpha_self > GLW_ALPHA_EPSILON) {

      if(gi->gi_bitmap_flags & GLW_IMAGE_ADDITIVE)
	glw_blendmode(w->glw_root, GLW_BLEND_ADDITIVE);

      glw_renderer_draw(&gi->gi_gr, w->glw_root, &rc0,
			&glt->glt_texture, NULL,
			&gi->gi_col_mul, rgb_off, alpha_self, blur,
			gi->gi_gpa.gpa_prog ? &gi->gi_gpa : NULL);

      if(gi->gi_bitmap_flags & GLW_IMAGE_ADDITIVE)
	glw_blendmode(w->glw_root, GLW_BLEND_NORMAL);
    }
    if(w->glw_class != &glw_frontdrop && TAILQ_FIRST(&w->glw_childs) != NULL) {
      glw_zinc(&rc0);
      render_child_autocentered(gi, &rc0);
    }
  }
}

/**
 *
 */
static void
glw_image_layout_tesselated(glw_root_t *gr, const glw_rctx_t *rc,
                            glw_image_t *gi, glw_loadable_texture_t *glt)
{
  float tex[4][2];
  float vex[4][2];

  int x, y, i = 0;


  tex[1][0] = 0.0f + (float)gi->gi_border[0]  / glt->glt_xs;
  tex[2][0] = glt->glt_s - (float)gi->gi_border[2] / glt->glt_xs;
  tex[0][0] = gi->gi_bitmap_flags & GLW_IMAGE_BORDER_LEFT ? 0.0f : tex[1][0];
  tex[3][0] = gi->gi_bitmap_flags & GLW_IMAGE_BORDER_RIGHT ? glt->glt_s : tex[2][0];

  tex[0][1] = 0.0f;
  tex[1][1] = 0.0f + (float)gi->gi_border[1]    / glt->glt_ys;
  tex[2][1] = glt->glt_t - (float)gi->gi_border[3] / glt->glt_ys;
  tex[3][1] = glt->glt_t;


  vex[0][0] = -1.0;
  vex[1][0] = GLW_MIN(-1.0f + 2.0f * gi->gi_border[0] / rc->rc_width, 0.0f);
  vex[2][0] = GLW_MAX( 1.0f - 2.0f * gi->gi_border[2] / rc->rc_width, 0.0f);
  vex[3][0] = 1.0;

  vex[0][1] = -1.0f;
  vex[1][1] = GLW_MAX( 1.0f - 2.0f * gi->gi_border[1] / rc->rc_height, 0.0f);
  vex[2][1] = GLW_MIN(-1.0f + 2.0f * gi->gi_border[3] / rc->rc_height, 0.0f);
  vex[3][1] = 1.0f;

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



static void
settexcoord(glw_renderer_t *gr, int c, float s0, float t0,
	    const glw_root_t *root, const glw_loadable_texture_t *glt)
{
  const int8_t *m = glt->glt_orientation < 9 ?
    tex_transform[glt->glt_orientation] : tex_transform[0];

  s0 = s0 * 2 - 1;
  t0 = t0 * 2 - 1;

  float s = s0 * m[0] + t0 * m[1];
  float t = s0 * m[2] + t0 * m[3];

  s = (s + 1.0) * 0.5;
  t = (t + 1.0) * 0.5;

  glw_renderer_vtx_st(gr, c, s, t);
}


/**
 *
 */
static void
glw_image_layout_alpha_edges(glw_root_t *gr, const glw_rctx_t *rc,
			     glw_image_t *gi,
                             const glw_loadable_texture_t *glt)
{
  float tex[4][2];
  float vex[4][2];

  int x, y, i = 0;

  tex[0][0] = 0;
  tex[1][0] = 0.0f + (float)gi->gi_alpha_edge / glt->glt_xs;
  tex[2][0] = glt->glt_s - (float)gi->gi_alpha_edge / glt->glt_xs;
  tex[3][0] = glt->glt_s;

  tex[0][1] = 0;
  tex[1][1] = 0.0f + (float)gi->gi_alpha_edge / glt->glt_ys;
  tex[2][1] = glt->glt_t - (float)gi->gi_alpha_edge / glt->glt_ys;
  tex[3][1] = glt->glt_t;

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
      settexcoord(&gi->gi_gr, i, tex[x][0], tex[y][1], gr, glt);
      glw_renderer_vtx_col(&gi->gi_gr, i, 1, 1, 1, alphaborder[x][y]);
      i++;
    }
  }
}


/**
 *
 */
static void
glw_image_layout_normal(glw_root_t *gr, glw_image_t *gi,
			glw_loadable_texture_t *glt)
{
  int m = glt->glt_margin;

  float x1 = -1.0f + 2.0f * -m / (float)glt->glt_xs;
  float y1 = -1.0f + 2.0f * -m / (float)glt->glt_ys;
  float x2 =  1.0f + 2.0f *  m / (float)glt->glt_xs;
  float y2 =  1.0f + 2.0f *  m / (float)glt->glt_ys;

  glw_renderer_vtx_pos(&gi->gi_gr, 0, x1, y1, 0.0);
  settexcoord(&gi->gi_gr, 0, 0, 1, gr, glt);

  glw_renderer_vtx_pos(&gi->gi_gr, 1, x2, y1, 0.0);
  settexcoord(&gi->gi_gr, 1, 1, 1, gr, glt);

  glw_renderer_vtx_pos(&gi->gi_gr, 2, x2, y2, 0.0);
  settexcoord(&gi->gi_gr, 2, 1, 0, gr, glt);

  glw_renderer_vtx_pos(&gi->gi_gr, 3, x1, y2, 0.0);
  settexcoord(&gi->gi_gr, 3, 0, 0, gr, glt);
}


/**
 *
 */
static void
glw_image_layout_repeated(glw_root_t *gr, const glw_rctx_t *rc,
                          glw_image_t *gi, glw_loadable_texture_t *glt)
{
  float xs = glt->glt_s;
  float ys = glt->glt_t;

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

  if(gi->gi_bitmap_flags & GLW_IMAGE_FIXED_SIZE) {

    glw_set_constraints(&gi->w,
			glt->glt_xs,
			glt->glt_ys,
			0,
			GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y);

  } else if(gi->w.glw_class == &glw_backdrop ||
	    gi->w.glw_class == &glw_frontdrop) {

    c = TAILQ_FIRST(&gi->w.glw_childs);

    if(c != NULL) {
      glw_set_constraints(&gi->w,
			  glw_req_width(c) +
			  gi->gi_box_left + gi->gi_box_right,
			  glw_req_height(c) +
			  gi->gi_box_top + gi->gi_box_bottom,
			  c->glw_req_weight,
			  c->glw_flags & GLW_CONSTRAINT_FLAGS);

      if(gi->w.glw_flags2 & GLW2_DEBUG)
	printf("Req size: %d,%d\n", glw_req_width(c), glw_req_height(c));


    } else if(glt != NULL) {
      glw_set_constraints(&gi->w,
			  glt->glt_xs - glt->glt_margin * 2,
			  glt->glt_ys - glt->glt_margin * 2,
			  0, 0);
    }

  } else if(gi->w.glw_class == &glw_image && glt != NULL) {

    if(glt->glt_state == GLT_STATE_ERROR) {
      glw_clear_constraints(&gi->w);
    } else if(gi->gi_bitmap_flags & GLW_IMAGE_SET_ASPECT) {
      glw_set_constraints(&gi->w, 0, 0, -glt->glt_aspect, GLW_CONSTRAINT_W);
    } else if(gi->w.glw_flags & GLW_CONSTRAINT_CONF_X) {

      int ys = glw_req_width(&gi->w) / glt->glt_aspect;
      glw_set_constraints(&gi->w, 0, ys, 0, GLW_CONSTRAINT_Y);
    } else if(gi->w.glw_flags & GLW_CONSTRAINT_CONF_Y) {

      int xs = glw_req_height(&gi->w) * glt->glt_aspect;
      glw_set_constraints(&gi->w, xs, 0, 0, GLW_CONSTRAINT_X);
    }
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
static glw_loadable_texture_t *
glw_image_tex_load(glw_image_t *gi, rstr_t *url, int width, int height)
{
  int flags = gi->gi_bitmap_flags & GLW_IMAGE_TEX_OVERLAP;

  if(gi->w.glw_class == &glw_repeatedimage)
    flags |= GLW_TEX_REPEAT;

  if(unlikely(gi->gi_max_intensity < 1.0))
    flags |= GLW_TEX_INTENSITY_ANALYSIS;

  if(unlikely(gi->gi_want_primary_color))
    flags |= GLW_TEX_PRIMARY_COLOR_ANALYSIS;

  return glw_tex_create(gi->w.glw_root, url, flags, width, height,
                        gi->gi_radius, gi->gi_shadow, gi->gi_aspect,
                        gi->gi_pending_url_flags,
                        gi->w.glw_scope->gs_backend);
}



/**
 *
 */
static void
glw_image_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_image_t *gi = (void *)w;
  glw_root_t *gr = w->glw_root;
  glw_loadable_texture_t *glt;
  glw_rctx_t rc0;
  glw_t *c;
  int hq = (w->glw_class == &glw_icon || w->glw_class == &glw_image);
  hq = gi->gi_mode == GI_MODE_NORMAL || gi->gi_mode == GI_MODE_ALPHA_EDGES;
  gi->gi_switch_tgt = 180;

  if(gi->gi_sources && gi->gi_switch_tgt) {
    gi->gi_switch_cnt++;
    if(gi->gi_switch_cnt == gi->gi_switch_tgt) {
      pick_source(gi, 1);
      gi->gi_switch_cnt = 0;
    }
  }

  if(gr->gr_can_externalize &&
     rc->rc_width == gr->gr_width &&
     rc->rc_height == gr->gr_height &&
     w->glw_class == &glw_backdrop) {


    if(gr->gr_externalize_cnt < GLW_MAX_EXTERNALIZED) {
      gr->gr_externalized[gr->gr_externalize_cnt++] = w;
      gi->gi_externalized = 1;
      return;
    }
  }

  gi->gi_externalized = 0;

  if(gi->gi_pending_url != NULL) {
    // Request to load
    int xs = -1, ys = -1;
    gi->gi_loading_new_url = 1;

    if(gi->gi_pending != NULL) {
      glw_tex_deref(w->glw_root, gi->gi_pending);
      gi->gi_pending = NULL;
    }

    if(rstr_get(gi->gi_pending_url)[0] == 0) {
      // Empty string, unload all

      if(gi->gi_current != NULL) {
	glw_tex_deref(w->glw_root, gi->gi_current);
	gi->gi_current = NULL;
      }

      gi->gi_update = 1;

    } else {

      if(hq) {

        if(w->glw_class == &glw_icon) {

          ys = gi->gi_fixed_size;
          if(ys == 0)
            ys = gi->gi_size_scale * w->glw_root->gr_current_size;

          if(ys > rc->rc_height)
            ys = rc->rc_height;

        } else if(w->glw_class == &glw_image) {
	  if(rc->rc_width < rc->rc_height) {
	    xs = rc->rc_width;
	  } else {
	    ys = rc->rc_height;
	  }
	} else {
	  xs = rc->rc_width;
	  ys = rc->rc_height;
	}
      }

      if(xs && ys) {

	if(w->glw_flags2 & GLW2_DEBUG)
	  TRACE(TRACE_DEBUG, "IMG", "Loading texture: %s (%d %d)",
		rstr_get(gi->gi_pending_url),
		xs, ys);

        assert(gi->gi_pending == NULL);
	gi->gi_pending = glw_image_tex_load(gi, gi->gi_pending_url, xs, ys);

	rstr_release(gi->gi_pending_url);
	gi->gi_pending_url = NULL;
      }
    }
  }

  if((glt = gi->gi_pending) != NULL) {
    glw_tex_layout(gr, glt);

    if(gi->gi_current == NULL)
      set_load_status(gi, GLW_STATUS_LOADING);

    if(glw_is_tex_inited(&glt->glt_texture) ||
       glt->glt_state == GLT_STATE_ERROR) {
      // Pending texture completed, ok or error: transfer to current

      if(gi->gi_current != NULL)
	glw_tex_deref(w->glw_root, gi->gi_current);

      if(glt->glt_state == GLT_STATE_ERROR)
        set_load_status(gi, GLW_STATUS_ERROR);

      gi->gi_current = gi->gi_pending;
      gi->gi_pending = NULL;
      gi->gi_update = 1;
      gi->gi_loading_new_url = 0;
      compute_colors(gi);
    }
  }

  if((glt = gi->gi_current) == NULL) {
    return;
  }

  glw_lp(&gi->gi_autofade, w->glw_root, !gi->gi_loading_new_url, 0.25);

  glw_tex_layout(gr, glt);

  if(glt->glt_state == GLT_STATE_ERROR) {
    set_load_status(gi, GLW_STATUS_ERROR);
  } else if(glw_is_tex_inited(&glt->glt_texture)) {

    gr->gr_can_externalize = 0;

    set_load_status(gi, GLW_STATUS_LOADED);

    if(gi->gi_update) {
      gi->gi_update = 0;

      glw_renderer_free(&gi->gi_gr);

      switch(gi->gi_mode) {

      case GI_MODE_NORMAL:
	glw_renderer_init_quad(&gi->gi_gr);
	glw_image_layout_normal(gr, gi, glt);
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
      gi->gi_need_reload = hq;
    }

    if(gi->gi_need_reload && gi->gi_pending == NULL &&
       gi->gi_pending_url == NULL && rc->rc_width > 0 && rc->rc_height > 0) {

      int xs = -1, ys = -1, rescale;

      if(w->glw_class == &glw_image || w->glw_class == &glw_icon) {

	if(rc->rc_width < rc->rc_height) {
	  rescale = abs(rc->rc_width - glt->glt_xs - glt->glt_margin * 2);
	  xs = rc->rc_width;
	} else {
	  rescale = abs(rc->rc_height - glt->glt_ys - glt->glt_margin * 2);
	  ys = rc->rc_height;
	}
      } else {
	rescale = (rc->rc_width - glt->glt_xs) || (rc->rc_height - glt->glt_ys);
	xs = rc->rc_width;
	ys = rc->rc_height;
      }

      // Requesting aspect cause a lot of rounding errors
      // so to avoid ending up in infinite reload loops,
      // consider 1px off as nothing
      if(gi->gi_bitmap_flags & GLW_IMAGE_SET_ASPECT && rescale == 1)
	rescale = 0;

      if(rescale) {
        if(gi->gi_rescale_hold < 5) {
          gi->gi_rescale_hold++;
        } else {
          gi->gi_rescale_hold = 0;
          assert(gi->gi_pending == NULL);
          gi->gi_pending = glw_image_tex_load(gi, glt->glt_url, xs, ys);
          gi->gi_need_reload = 0;
        }
      } else {
        gi->gi_need_reload = 0;
      }
    }
  } else {
    set_load_status(gi, GLW_STATUS_LOADING);
  }

  glw_image_update_constraints(gi);

  if((c = TAILQ_FIRST(&w->glw_childs)) != NULL) {
    rc0 = *rc;

    if(gi->w.glw_flags2 & GLW2_DEBUG)
      printf("Output width = %d-%d=%d   Output height=%d-%d=%d\n",
	     rc0.rc_width, (gi->gi_box_left + gi->gi_box_right),
	     rc0.rc_width - (gi->gi_box_left + gi->gi_box_right),

	     rc0.rc_height, (gi->gi_box_top + gi->gi_box_bottom),
	     rc0.rc_height - (gi->gi_box_top + gi->gi_box_bottom));

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
  switch(signal) {
  default:
    break;

  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
  case GLW_SIGNAL_CHILD_CREATED:
    glw_image_update_constraints((glw_image_t *)w);
    return 1;
  case GLW_SIGNAL_CHILD_DESTROYED:
    glw_set_constraints(w, 0, 0, 0, 0);
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
  float scale = 1 - gi->gi_saturation;

  if(gi->gi_max_intensity < 1.0 && gi->gi_current != NULL &&
     gi->gi_current->glt_intensity > gi->gi_max_intensity) {
    scale *= gi->gi_max_intensity / gi->gi_current->glt_intensity;
  }

  gi->gi_col_mul.r = gi->gi_color.r * scale;
  gi->gi_col_mul.g = gi->gi_color.g * scale;
  gi->gi_col_mul.b = gi->gi_color.b * scale;

  gi->gi_col_off.r = gi->gi_saturation;
  gi->gi_col_off.g = gi->gi_saturation;
  gi->gi_col_off.b = gi->gi_saturation;

  gi->gi_saturated = gi->gi_saturation > 0;
}



/**
 *
 */
static void
glw_image_ctor(glw_t *w)
{
  glw_image_t *gi = (void *)w;

  gi->gi_bitmap_flags =
    GLW_TEX_CORNER_TOPLEFT |
    GLW_TEX_CORNER_TOPRIGHT |
    GLW_TEX_CORNER_BOTTOMLEFT |
    GLW_TEX_CORNER_BOTTOMRIGHT |
    GLW_IMAGE_BORDER_LEFT |
    GLW_IMAGE_BORDER_RIGHT;

  gi->gi_autofade = 1;
  gi->gi_alpha_self = 1;
  gi->gi_color.r = 1.0;
  gi->gi_color.g = 1.0;
  gi->gi_color.b = 1.0;
  gi->gi_size_scale = 1.0;
  gi->gi_max_intensity = 1.0f;

  compute_colors(gi);

  if(w->glw_class == &glw_repeatedimage)
    gi->gi_mode = GI_MODE_REPEATED_TEXTURE;
}

/**
 *
 */
static void
glw_icon_ctor(glw_t *w)
{
  glw_image_ctor(w);
  glw_image_t *gi = (glw_image_t *)w;
  glw_root_t *gr = w->glw_root;
  float siz = w->glw_root->gr_current_size;
  glw_set_constraints(w, siz, siz, 0, GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y);

  LIST_INSERT_HEAD(&gr->gr_icons, gi, gi_link);
}

/**
 *
 */
static int
glw_image_set_float3(glw_t *w, glw_attribute_t attrib, const float *rgb,
                     glw_style_t *gs)
{
  glw_image_t *gi = (void *)w;

  switch(attrib) {
  case GLW_ATTRIB_RGB:
    if(!glw_attrib_set_rgb(&gi->gi_color, rgb))
      return 0;
    compute_colors(gi);
    return 1;

  default:
    return -1;
  }
}


/**
 *
 */
static void
update_box(glw_image_t *gi)
{
  gi->gi_box_left   = gi->gi_border[0] + gi->gi_padding[0];
  gi->gi_box_top    = gi->gi_border[1] + gi->gi_padding[1];
  gi->gi_box_right  = gi->gi_border[2] + gi->gi_padding[2];
  gi->gi_box_bottom = gi->gi_border[3] + gi->gi_padding[3];

  if(gi->w.glw_flags2 & GLW2_DEBUG)
    printf("Box: %d,%d,%d,%d == %d,%d\n",
	   gi->gi_box_left,
	   gi->gi_box_top,
	   gi->gi_box_right,
	   gi->gi_box_bottom,
	   gi->gi_box_left + gi->gi_box_right,
	   gi->gi_box_top + gi->gi_box_bottom);
}

/**
 *
 */
static int
image_set_int16_4(glw_t *w, glw_attribute_t attrib, const int16_t *v,
                  glw_style_t *gs)
{
  glw_image_t *gi = (void *)w;

  switch(attrib) {
  case GLW_ATTRIB_BORDER:
    if(!glw_attrib_set_int16_4(gi->gi_border, v))
      return 0;
    if(gi->gi_mode != GI_MODE_BORDER_ONLY_SCALING)
      gi->gi_mode = GI_MODE_BORDER_SCALING;
    break;

  case GLW_ATTRIB_PADDING:
    if(!glw_attrib_set_int16_4(gi->gi_padding, v))
      return 0;
    break;

  default:
    return -1;
  }

  update_box(gi);
  gi->gi_update = 1;
  glw_image_update_constraints(gi);
  return 1;
}


/**
 *
 */
static void
mod_image_flags(glw_t *w, int set, int clr, glw_style_t *gs)
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
static rstr_t *
get_curname(glw_image_t *gi)
{
  rstr_t *curname;

  if(gi->gi_pending_url != NULL)
    curname = gi->gi_pending_url;
  else if(gi->gi_pending != NULL)
    curname = gi->gi_pending->glt_url;
  else if(gi->gi_current != NULL)
    curname = gi->gi_current->glt_url;
  else
    curname = NULL;
  return curname;
}


/**
 *
 */
static void
set_pending(glw_image_t *gi, rstr_t *filename, int flags)
{
  if(gi->gi_pending_url != NULL)
    rstr_release(gi->gi_pending_url);
  gi->gi_pending_url = filename ? rstr_dup(filename) : rstr_alloc("");
  gi->gi_pending_url_flags = flags;
}


/**
 *
 */
static void
mod_flags2(glw_t *w, int set, int clr)
{
  glw_image_t *gi = (void *)w;
  if((set | clr) & GLW2_SHADOW) {
    if(set & GLW2_SHADOW)
      gi->gi_shadow = 4;
    else
      gi->gi_shadow = 0;

    if(gi->gi_current || gi->gi_pending) {
      rstr_t *curname = get_curname(gi);
      if(curname != NULL)
        set_pending(gi, curname, gi->gi_pending_url_flags);
    }
  }
}


/**
 *
 */
static void
set_path(glw_image_t *gi, rstr_t *filename, int flags)
{
  const rstr_t *curname = get_curname(gi);

  if(curname != NULL && filename != NULL && !strcmp(rstr_get(filename),
						    rstr_get(curname)))
    return;

  set_pending(gi, filename, flags);
}


/**
 *
 */
static void
set_source(glw_t *w, rstr_t *filename, int flags, glw_style_t *gs)
{
  glw_image_t *gi = (glw_image_t *)w;
  set_path(gi, filename, flags);
}


/**
 *
 */
static void
pick_source(glw_image_t *gi, int next)
{
  const rstr_t *curname = get_curname(gi);
  if(curname == NULL) {
    set_pending(gi, gi->gi_sources[0], 0);
  } else {

    int i;
    int found = -1;
    for(i = 0; gi->gi_sources[i] != NULL && found == -1; i++) {
      if(!strcmp(rstr_get(gi->gi_sources[i]), rstr_get(curname)))
	found = i;
    }
    if(found == -1) {
      set_pending(gi, gi->gi_sources[0], 0);
    } else if(next) {
      if(gi->gi_sources[found+1] == NULL) {
	set_pending(gi, gi->gi_sources[0], 0);
      } else {
	set_pending(gi, gi->gi_sources[found+1], 0);
      }
    }
  }
}


/**
 *
 */
static void
set_sources(glw_t *w, rstr_t **filenames)
{
  glw_image_t *gi = (glw_image_t *)w;
  sources_free(gi->gi_sources);
  gi->gi_sources = filenames;

  pick_source(gi, 0);
}


/**
 *
 */
static int
glw_image_set_em(glw_t *w, glw_attribute_t attrib, float value)
{
  glw_image_t *gi = (glw_image_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_SIZE:
    if(w->glw_class != &glw_icon)
      return -1;

    if(gi->gi_size_scale == value)
      return 0;

    gi->gi_size_scale = value;

    float siz = gi->gi_size_scale * w->glw_root->gr_current_size;
    glw_set_constraints(w, siz, siz, 0, GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y);
    break;

  default:
    return -1;
  }
  return 1;
}


/**
 *
 */
static int
glw_image_set_float(glw_t *w, glw_attribute_t attrib, float value,
                    glw_style_t *gs)
{
  glw_image_t *gi = (glw_image_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_ANGLE:
    if(gi->gi_angle == value)
      return 0;
    gi->gi_angle = value;
    break;

  case GLW_ATTRIB_SATURATION:
    if(gi->gi_saturation == value)
      return 0;
    gi->gi_saturation = value;
    compute_colors(gi);
    break;

  case GLW_ATTRIB_ASPECT:
    if(gi->gi_aspect == value)
      return 0;
    gi->gi_aspect = value;
    gi->gi_update = 1;
    break;

  case GLW_ATTRIB_CHILD_ASPECT:
    if(gi->gi_child_aspect == value)
      return 0;
    gi->gi_child_aspect = value;
    break;

  case GLW_ATTRIB_ALPHA_SELF:
    if(gi->gi_alpha_self == value)
      return 0;

    gi->gi_alpha_self = value;
    break;

  case GLW_ATTRIB_SIZE_SCALE:
    return glw_image_set_em(w, GLW_ATTRIB_SIZE, value);

  default:
    return -1;
  }
  return 1;
}


/**
 *
 */
static int
glw_image_set_int(glw_t *w, glw_attribute_t attrib, int value,
                  glw_style_t *gs)
{
  glw_image_t *gi = (glw_image_t *)w;

  switch(attrib) {

  case GLW_ATTRIB_ALPHA_EDGES:
    if(gi->gi_alpha_edge == value)
      return 0;

    gi->gi_alpha_edge = value;
    gi->gi_mode = GI_MODE_ALPHA_EDGES;
    break;

  case GLW_ATTRIB_RADIUS:
    if(gi->gi_radius == value)
      return 0;

    gi->gi_radius = value;
    gi->gi_update = 1;
    break;

  case GLW_ATTRIB_SIZE:
    if(w->glw_class != &glw_icon)
      return -1;

    if(gi->gi_fixed_size == value)
      return 0;

    gi->gi_fixed_size = value;
    glw_set_constraints(w, value, value, 0,
                        GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y);
    break;

  default:
    return -1;
  }
  return 1;
}


/**
 *
 */
static glw_widget_status_t
glw_image_status(glw_t *w)
{
  glw_image_t *gi = (glw_image_t *)w;
  return gi->gi_widget_status;
}


/**
 *
 */
static const char *
get_identity(glw_t *w, char *tmp, size_t tmpsize)
{
  glw_image_t *gi = (glw_image_t *)w;
  glw_loadable_texture_t *glt = gi->gi_current;
  if(glt)
    return rstr_get(glt->glt_url);

  glt = gi->gi_pending;
  if(glt)
    return rstr_get(glt->glt_url);
  return rstr_get(gi->gi_pending_url);
}


void
glw_icon_flush(glw_root_t *gr)
{
  glw_image_t *gi;
  LIST_FOREACH(gi, &gr->gr_icons, gi_link) {
    if(gi->gi_fixed_size)
      continue;
    float siz = gi->gi_size_scale * gr->gr_current_size;

    glw_set_constraints(&gi->w, siz, siz, 0,
			GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y);
  }
}

/**
 *
 */
static int
glw_image_set_float_unresolved(glw_t *w, const char *a, float value,
                               glw_style_t *gs)
{
  glw_image_t *gi = (glw_image_t *)w;

  if(!strcmp(a, "maxIntensity")) {
    if(gi->gi_max_intensity == value)
        return 0;
    gi->gi_max_intensity = value;
    compute_colors(gi);
    return 1;
  }

  return GLW_SET_NOT_RESPONDING;
}



/**
 *
 */
int
glw_image_get_details(glw_t *w, char *path, size_t pathlen, float *alpha)
{
  char tmp[32];
  if(w->glw_class != &glw_backdrop)
    return -1;

  glw_image_t *gi = (glw_image_t *)w;
  const char *p = get_identity(w, tmp, sizeof(tmp));
  if(p == NULL)
    return -1;
  snprintf(path, pathlen, "%s", p);
  *alpha = w->glw_alpha * gi->gi_col_mul.g;
  return 0;
}


/**
 *
 */
static int
glw_image_primary_color(glw_t *w, float *rgb)
{
  glw_image_t *gi = (glw_image_t *)w;
  gi->gi_want_primary_color = 1;
  if(gi->gi_current != NULL) {
    memcpy(rgb, gi->gi_current->glt_primary_color, 3 * sizeof(float));
    return 1;
  }
  return 0;
}

/**
 *
 */
static glw_class_t glw_image = {
  .gc_name = "image",
  .gc_instance_size = sizeof(glw_image_t),
  .gc_layout = glw_image_layout,
  .gc_render = glw_image_render,
  .gc_dtor = glw_image_dtor,
  .gc_ctor = glw_image_ctor,
  .gc_set_float = glw_image_set_float,
  .gc_set_int = glw_image_set_int,
  .gc_signal_handler = glw_image_callback,
  .gc_default_alignment = LAYOUT_ALIGN_CENTER,
  .gc_status = glw_image_status,
  .gc_primary_color = glw_image_primary_color,
  .gc_set_float3 = glw_image_set_float3,
  .gc_mod_image_flags = mod_image_flags,
  .gc_set_source = set_source,
  .gc_set_sources = set_sources,
  .gc_get_identity = get_identity,
  .gc_set_fs = glw_image_set_fs,
  .gc_mod_flags2 = mod_flags2,
  .gc_set_int16_4 = image_set_int16_4,
  .gc_set_float_unresolved = glw_image_set_float_unresolved,
};

GLW_REGISTER_CLASS(glw_image);


/**
 *
 */
static glw_class_t glw_icon = {
  .gc_name = "icon",
  .gc_instance_size = sizeof(glw_image_t),
  .gc_layout = glw_image_layout,
  .gc_render = glw_image_render,
  .gc_ctor = glw_icon_ctor,
  .gc_dtor = glw_icon_dtor,
  .gc_set_float = glw_image_set_float,
  .gc_set_em = glw_image_set_em,
  .gc_set_int = glw_image_set_int,
  .gc_signal_handler = glw_image_callback,
  .gc_default_alignment = LAYOUT_ALIGN_CENTER,
  .gc_status = glw_image_status,
  .gc_primary_color = glw_image_primary_color,
  .gc_set_float3 = glw_image_set_float3,
  .gc_mod_image_flags = mod_image_flags,
  .gc_set_source = set_source,
  .gc_set_sources = set_sources,
  .gc_get_identity = get_identity,
  .gc_set_fs = glw_image_set_fs,
  .gc_mod_flags2 = mod_flags2,
  .gc_set_int16_4 = image_set_int16_4,
  .gc_set_float_unresolved = glw_image_set_float_unresolved,
};

GLW_REGISTER_CLASS(glw_icon);


/**
 *
 */
static glw_class_t glw_backdrop = {
  .gc_name = "backdrop",
  .gc_instance_size = sizeof(glw_image_t),
  .gc_layout = glw_image_layout,
  .gc_render = glw_image_render,
  .gc_ctor = glw_image_ctor,
  .gc_dtor = glw_image_dtor,
  .gc_set_float = glw_image_set_float,
  .gc_set_int = glw_image_set_int,
  .gc_signal_handler = glw_image_callback,
  .gc_default_alignment = LAYOUT_ALIGN_CENTER,
  .gc_status = glw_image_status,
  .gc_primary_color = glw_image_primary_color,
  .gc_set_float3 = glw_image_set_float3,
  .gc_set_int16_4 = image_set_int16_4,
  .gc_mod_image_flags = mod_image_flags,
  .gc_set_source = set_source,
  .gc_set_sources = set_sources,
  .gc_get_identity = get_identity,
  .gc_set_fs = glw_image_set_fs,
  .gc_mod_flags2 = mod_flags2,
  .gc_set_float_unresolved = glw_image_set_float_unresolved,
};

GLW_REGISTER_CLASS(glw_backdrop);



/**
 *
 */
static glw_class_t glw_frontdrop = {
  .gc_name = "frontdrop",
  .gc_instance_size = sizeof(glw_image_t),
  .gc_layout = glw_image_layout,
  .gc_render = glw_image_render,
  .gc_ctor = glw_image_ctor,
  .gc_dtor = glw_image_dtor,
  .gc_set_float = glw_image_set_float,
  .gc_set_int = glw_image_set_int,
  .gc_signal_handler = glw_image_callback,
  .gc_status = glw_image_status,
  .gc_primary_color = glw_image_primary_color,
  .gc_default_alignment = LAYOUT_ALIGN_CENTER,
  .gc_set_float3 = glw_image_set_float3,
  .gc_set_int16_4 = image_set_int16_4,
  .gc_mod_image_flags = mod_image_flags,
  .gc_set_source = set_source,
  .gc_set_sources = set_sources,
  .gc_get_identity = get_identity,
  .gc_set_fs = glw_image_set_fs,
  .gc_mod_flags2 = mod_flags2,
  .gc_set_float_unresolved = glw_image_set_float_unresolved,
};

GLW_REGISTER_CLASS(glw_frontdrop);



/**
 *
 */
static glw_class_t glw_repeatedimage = {
  .gc_name = "repeatedimage",
  .gc_instance_size = sizeof(glw_image_t),
  .gc_layout = glw_image_layout,
  .gc_render = glw_image_render,
  .gc_ctor = glw_image_ctor,
  .gc_dtor = glw_image_dtor,
  .gc_set_float = glw_image_set_float,
  .gc_set_int = glw_image_set_int,
  .gc_signal_handler = glw_image_callback,
  .gc_status = glw_image_status,
  .gc_primary_color = glw_image_primary_color,
  .gc_default_alignment = LAYOUT_ALIGN_CENTER,
  .gc_set_float3 = glw_image_set_float3,
  .gc_mod_image_flags = mod_image_flags,
  .gc_set_source = set_source,
  .gc_get_identity = get_identity,
  .gc_set_fs = glw_image_set_fs,
  .gc_mod_flags2 = mod_flags2,
  .gc_set_int16_4 = image_set_int16_4,
  .gc_set_float_unresolved = glw_image_set_float_unresolved,
};

GLW_REGISTER_CLASS(glw_repeatedimage);
