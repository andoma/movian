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
#include "glw_navigation.h"

LIST_HEAD(glw_container_list, glw_container);

typedef struct glw_table {
  glw_t w;
  int gt_num_columns;
  int gt_width_sum;
  int16_t *gt_columns;
  struct glw_container_list gt_rows;
} glw_table_t;


typedef struct glw_container {
  glw_t w;

  glw_table_t *co_table;
  LIST_ENTRY(glw_container) co_table_link;
  int16_t *co_column_widths;

  int cflags;
  float weight_sum;

  int16_t width;
  int16_t spacing_width;
  int16_t padding_width;
  int16_t height;
  int16_t co_spacing;
  int16_t co_padding[4];
  int16_t co_biggest;
  uint16_t co_num_columns;
  char co_using_aspect;


} glw_container_t;


typedef struct glw_container_item {
  float pos;
  float scale;
  float fade;
  int16_t size;
  char inited;
} glw_container_item_t;



static glw_class_t glw_table;



/**
 *
 */
static void
table_recompute(glw_table_t *gt)
{
  glw_container_t *co;
  int columns = 0;
  int spacing_width = 0;

  LIST_FOREACH(co, &gt->gt_rows, co_table_link) {
    columns       = MAX(co->co_num_columns, columns);
    spacing_width = MAX(co->spacing_width, spacing_width);
  }

  if(columns != gt->gt_num_columns) {
    gt->gt_columns = realloc(gt->gt_columns, columns * sizeof(int16_t));
    gt->gt_num_columns = columns;
  }

  int width_sum = 0;
  for(int i = 0; i < columns; i++) {
    int16_t w = INT16_MIN;
    LIST_FOREACH(co, &gt->gt_rows, co_table_link) {
      if(i >= co->co_num_columns)
        continue;
      if(co->co_column_widths[i] >= 0)
        w = MAX(w, co->co_column_widths[i]);
    }
    gt->gt_columns[i] = w;
    if(w >= 0)
      width_sum += w;
  }
  gt->gt_width_sum = width_sum;
  glw_mod_constraints(&gt->w,
                      width_sum + spacing_width, 0, 0,
                      GLW_CONSTRAINT_X, GLW_CONSTRAINT_X);
}


/**
 *
 */
static int
glw_container_x_constraints(glw_container_t *co, glw_t *skip)
{
  glw_t *c;
  int height = 0;
  int padding_width = co->co_padding[0] + co->co_padding[2];
  int width = 0;
  float weight = 0;
  int cflags = 0, f;
  int elements = 0;
  int numfix = 0;
  glw_table_t *tab = co->co_table;

  co->co_biggest = 0;
  co->co_using_aspect = 0;

  if(co->w.glw_flags2 & GLW2_DEBUG)
    printf("Constraint round\n");

  if(unlikely(tab != NULL)) {

    int num_childs = 0;
    TAILQ_FOREACH(c, &co->w.glw_childs, glw_parent_link) {
      if(c->glw_flags & GLW_HIDDEN || c == skip)
        continue;
      num_childs++;
    }

    if(co->co_num_columns != num_childs) {
      co->co_column_widths =
        realloc(co->co_column_widths, sizeof(uint16_t) * num_childs);
      co->co_num_columns = num_childs;
    }
    memset(co->co_column_widths, 0, sizeof(uint16_t) * num_childs);
  }

  int i = -1;
  TAILQ_FOREACH(c, &co->w.glw_childs, glw_parent_link) {
    i++;
    if(c->glw_flags & GLW_HIDDEN || c == skip)
      continue;

    f = glw_filter_constraints(c);

    cflags |= f & (GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y);

    if(co->w.glw_flags2 & GLW2_DEBUG)
      printf("%c%c%c %-4d %-4d %f\n",
	     f & GLW_CONSTRAINT_X ? 'X' : ' ',
	     f & GLW_CONSTRAINT_Y ? 'Y' : ' ',
	     f & GLW_CONSTRAINT_W ? 'W' : ' ',
	     c->glw_req_size_x,
	     c->glw_req_size_y,
	     c->glw_req_weight);

    if(f & GLW_CONSTRAINT_Y)
      height = GLW_MAX(height, c->glw_req_size_y);

    if(unlikely(tab != NULL)) {
      if(f & GLW_CONSTRAINT_X) {
        co->co_column_widths[i] = c->glw_req_size_x;
      } else {
        co->co_column_widths[i] = INT16_MIN;
        weight += 1.0f;
      }

    } else {

      if(f & GLW_CONSTRAINT_X) {

        if(co->w.glw_flags2 & GLW2_HOMOGENOUS) {
          co->co_biggest = GLW_MAX(c->glw_req_size_x, co->co_biggest);
          numfix++;
        } else {
          width += c->glw_req_size_x;
        }
      } else if(f & GLW_CONSTRAINT_W) {

        if(c->glw_req_weight == 0)
          continue;
        if(c->glw_req_weight > 0)
          weight += c->glw_req_weight;
        else
          co->co_using_aspect = 1;

      } else {
        weight += 1.0f;
      }
    }
    elements++;
  }

  if(co->w.glw_flags2 & GLW2_HOMOGENOUS)
    width += numfix * co->co_biggest;

  int16_t spacing_width = elements > 0 ? (elements - 1) * co->co_spacing : 0;
  if(co->w.glw_flags2 & GLW2_DEBUG)
    printf("Total width: %d\n", width);

  if(co->weight_sum    != weight ||
     co->width         != width  ||
     co->padding_width != padding_width  ||
     co->spacing_width != spacing_width  ||
     co->cflags        != cflags) {

    co->weight_sum = weight;
    co->width = width;
    co->spacing_width = spacing_width;
    co->padding_width = padding_width;
    co->cflags = cflags;
    glw_need_refresh(co->w.glw_root, 0);
  }


  height += co->co_padding[3] + co->co_padding[1];
  if(unlikely(tab != NULL))
    table_recompute(tab);

  glw_set_constraints(&co->w, width + spacing_width + padding_width,
                      height, 0, cflags);
  return 1;
}


/**
 *
 */
static void
glw_container_x_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_container_t *co = (glw_container_t *)w;
  glw_t *c;
  int aspect_width = 0;          // Width used by aspect constraints
  float IW;
  int weightavail;  // Pixels available for weighted childs
  float pos;        // Current position
  float fixscale;   // Scaling to apply to fixed width requests
                    // Used if the available width < sum of requested width
  glw_rctx_t rc0;
  const glw_table_t *tab = co->co_table;

  rc0 = *rc;

  if(w->glw_flags2 & GLW2_DEBUG) {
    printf("%d x %d tablemode:%s\n",
           rc->rc_width, rc->rc_height, tab ? "yes" : "no");
  }

  rc0.rc_height = rc->rc_height - co->co_padding[1] - co->co_padding[3];

  if(co->co_using_aspect) {
    // If any of our childs wants a fixed aspect we need to compute
    // the total width those will consume

    TAILQ_FOREACH(c, &co->w.glw_childs, glw_parent_link) {
      int f = glw_filter_constraints(c);
      float w = (f & GLW_CONSTRAINT_W ? c->glw_req_weight : 1.0f);
      if(w < 0)
	aspect_width += rc0.rc_height * -w;
    }

  }

  if(w->glw_flags2 & GLW2_DEBUG) {
    printf("Widthsum: xconstraints:%d aspect:%d spacing:%d\n",
           co->width, aspect_width, co->spacing_width);
  }

  int space_pad = co->spacing_width + co->padding_width;
  int wsum = aspect_width + space_pad;
  
  if(tab != NULL)
    wsum += tab->gt_width_sum;
  else
    wsum += co->width;

  if(wsum > rc->rc_width) {
    // Requested pixel size > available width, must scale
    weightavail = 0;
    fixscale = (float)(rc->rc_width - aspect_width - space_pad) / co->width;
    pos = co->co_padding[0] * fixscale;
  } else {
    fixscale = 1;

    weightavail = rc->rc_width - wsum;
    // Pixels available for weighted childs

    pos = co->co_padding[0];

    if(co->weight_sum == 0) {

      if(co->w.glw_alignment == LAYOUT_ALIGN_CENTER) {
	pos = rc->rc_width / 2 - (wsum - co->padding_width) / 2;
      } else if(co->w.glw_alignment == LAYOUT_ALIGN_RIGHT) {
	pos = rc->rc_width - (wsum - co->padding_width);
      }
    }
  }

  int right, left = rintf(pos);

  IW = 1.0f / rc->rc_width;
  int i = -1;
  TAILQ_FOREACH(c, &co->w.glw_childs, glw_parent_link) {
    float cw;
    i++;
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    int f = glw_filter_constraints(c);

    if(unlikely(tab != NULL)) {
      if(tab->gt_columns[i] >= 0) {
        cw = tab->gt_columns[i];
      } else {
	cw = weightavail / co->weight_sum;
      }
    } else if(f & GLW_CONSTRAINT_X) {
      if(co->w.glw_flags2 & GLW2_HOMOGENOUS) {
	cw = co->co_biggest * fixscale;
      } else {
	cw = c->glw_req_size_x * fixscale;
      }

    } else {
      float w = (f & GLW_CONSTRAINT_W ? c->glw_req_weight : 1.0f);
      if(w == 0)
	continue;

      if(w > 0) {
	cw = weightavail * w / co->weight_sum;
      } else {
	cw = rc0.rc_height * -w;
      }
    }

    pos += cw;
    right = rintf(pos);
    
    rc0.rc_width = right - left;
    if(w->glw_flags2 & GLW2_DEBUG) {
      printf("    child get %d spacing:%d\n", rc0.rc_width, co->co_spacing);
    }

    glw_container_item_t *cd = glw_parent_data(c, glw_container_item_t);

    cd->pos = -1.0f + (right + left) * IW;
    cd->scale = rc0.rc_width * IW;
      
    cd->size = right - left;
    glw_layout0(c, &rc0);
    left = right + co->co_spacing;
    pos += co->co_spacing;
  }
}

/**
 *
 */
static int
glw_container_y_constraints(glw_container_t *co, glw_t *skip)
{
  glw_t *c;
  int width = 0;
  int height = co->co_padding[3] + co->co_padding[1];
  float weight = 0;
  int cflags = 0, f;
  int elements = 0;

  co->co_using_aspect = 0;

  if(co->w.glw_flags2 & GLW2_DEBUG)
    printf("Constraint round\n");

  TAILQ_FOREACH(c, &co->w.glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN || c == skip)
      continue;

    f = glw_filter_constraints(c);

    if(co->w.glw_flags2 & GLW2_DEBUG)
      printf("%c%c%c %d %d %f\n",
	     f & GLW_CONSTRAINT_X ? 'X' : ' ',
	     f & GLW_CONSTRAINT_Y ? 'Y' : ' ',
	     f & GLW_CONSTRAINT_W ? 'W' : ' ',
	     c->glw_req_size_x,
	     c->glw_req_size_y,
	     c->glw_req_weight);

    cflags |= f & (GLW_CONSTRAINT_X | GLW_CONSTRAINT_Y);

    if(f & GLW_CONSTRAINT_X)
      width = GLW_MAX(width, c->glw_req_size_x);

    if(f & GLW_CONSTRAINT_Y) {
      height += c->glw_req_size_y;
    } else if(f & GLW_CONSTRAINT_W) {
      if(c->glw_req_weight > 0)
	weight += c->glw_req_weight;
      else
	co->co_using_aspect = 1;
    } else {
      weight += 1.0f;
    }
    elements++;
  }

  if(elements > 0)
    height += (elements - 1) * co->co_spacing;

  if(co->height     != height ||
     co->weight_sum != weight ||
     co->cflags     != cflags) {

    co->height     = height;
    co->weight_sum = weight;
    co->cflags     = cflags;
    glw_need_refresh(co->w.glw_root, 0);
  }

  if(weight)
    cflags &= ~GLW_CONSTRAINT_Y;

  width += co->co_padding[0] + co->co_padding[2];
  glw_set_constraints(&co->w, width, height, 0, cflags);
  return 1;
}


static void
glw_container_y_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_container_t *co = (glw_container_t *)w;
  glw_t *c, *n;
  glw_rctx_t rc0 = *rc;
  int height = co->height;
  float IH;
  int weightavail;  // Pixels available for weighted childs
  float pos;        // Current position
  float fixscale;   // Scaling to apply to fixed height requests
                    // Used if the available height < sum of requested height


  rc0.rc_width = rc->rc_width - co->co_padding[0] - co->co_padding[2];

  if(co->co_using_aspect) {
    // If any of our childs wants a fixed aspect we need to compute
    // the total width those will consume
    TAILQ_FOREACH(c, &co->w.glw_childs, glw_parent_link) {
      int f = glw_filter_constraints(c);
      float w = (f & GLW_CONSTRAINT_W ? c->glw_req_weight : 1.0f);
      if(w < 0)
	height += rc0.rc_width / -w;
    }
  }

  if(height > rc->rc_height) {
    // Requested pixel size > available height, must scale
    weightavail = 0;
    fixscale = (float)rc->rc_height / height;
    pos = co->co_padding[1] * fixscale;
  } else {
    fixscale = 1;

    // Pixels available for weighted childs
    weightavail = rc->rc_height - height;

    pos = co->co_padding[1];

    if(co->weight_sum == 0) {

      if(co->w.glw_alignment == LAYOUT_ALIGN_CENTER) {
	pos = rc->rc_height / 2 - height / 2;
      } else if(co->w.glw_alignment == LAYOUT_ALIGN_BOTTOM) {
	pos = rc->rc_height - height;
      }
    }
  }

  int bottom, top = rintf(pos);
  IH = 1.0f / rc->rc_height;


  for(c = TAILQ_FIRST(&co->w.glw_childs); c != NULL; c = n) {
    n = TAILQ_NEXT(c, glw_parent_link);

    float cw = 0;

    glw_container_item_t *cd = glw_parent_data(c, glw_container_item_t);

    if(c->glw_flags & GLW_HIDDEN) {
      if(!(co->w.glw_flags2 & GLW2_AUTOFADE)) {
	cd->fade = 0;
	continue;
      }

      glw_lp(&cd->fade, co->w.glw_root, 0, 0.25);
      if(cd->fade < 0.01) {
	cd->inited = 0;
	continue;
      }
    }

    int f = glw_filter_constraints(c);

    if(f & GLW_CONSTRAINT_Y) {
      cw = fixscale * c->glw_req_size_y;
    } else {
      float w = (f & GLW_CONSTRAINT_W ? c->glw_req_weight : 1.0f);
      if(w > 0)
	cw = weightavail * w / co->weight_sum;
      else
        cw = rc0.rc_width / -w;
    }

    pos += cw;
    bottom = rintf(pos);
    rc0.rc_height = bottom - top;

    if(co->w.glw_flags2 & GLW2_AUTOFADE) {

      if(c->glw_flags & GLW_RETIRED) {

	glw_lp(&cd->fade, co->w.glw_root, 0, 0.25);
	if(cd->fade < 0.01) {
	  glw_destroy(c);
	  continue;
	}
	
      } else if(!(c->glw_flags & GLW_HIDDEN)) {
	glw_lp(&cd->fade, co->w.glw_root, 1, 0.25);
      }

      if(cd->inited) {
	glw_lp(&cd->pos, co->w.glw_root, (bottom + top) * IH, 0.25);
      } else {
	cd->pos = (bottom + top) * IH;
	if(!(c->glw_flags & GLW_HIDDEN))
	  cd->inited = 1;
      }
      cd->scale = rc0.rc_height * IH * cd->fade;
      cd->size = rc0.rc_height;

    } else {

      cd->fade = 1;
      cd->pos = (bottom + top) * IH;
      cd->scale = rc0.rc_height * IH;
      cd->size = rc0.rc_height;

    }

    glw_layout0(c, &rc0);
    top = bottom + co->co_spacing;
    pos += co->co_spacing;

  }
}




/**
 *
 */
static int
glw_container_z_constraints(glw_t *w, glw_t *skip)
{
  glw_t *c;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

    if(c->glw_flags & GLW_HIDDEN || c == skip)
      continue;

    if(glw_filter_constraints(c))
      break;
  }

  if(c == NULL) {

    TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

      if(c->glw_flags & GLW_HIDDEN || c == skip)
        continue;

      if(!(c->glw_class->gc_flags & GLW_UNCONSTRAINED))
        break;
    }
  }

  if(c != NULL) {
    if(w->glw_flags2 & GLW2_DEBUG)
      printf("Copy %c%c%c %d %d %f constraints from %s\n",
             c->glw_flags & GLW_CONSTRAINT_X ? 'X' : ' ',
             c->glw_flags & GLW_CONSTRAINT_Y ? 'Y' : ' ',
             c->glw_flags & GLW_CONSTRAINT_W ? 'W' : ' ',
             c->glw_req_size_x,
             c->glw_req_size_y,
             c->glw_req_weight,
             glw_get_path(c)
             );
    glw_copy_constraints(w, c);
  }
  else
    glw_clear_constraints(w);

  return 1;
}


/**
 *
 */
static void
glw_container_z_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;
    glw_layout0(c, rc);
  }
}


/**
 *
 */
static void
glw_container_y_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  float alpha = rc->rc_alpha * w->glw_alpha;
  float sharpness  = rc->rc_sharpness  * w->glw_sharpness;
  glw_container_t *co = (glw_container_t *)w;
  glw_rctx_t rc0, rc1;

  if(alpha < 0.01f)
    return;
  
  if(glw_is_focusable_or_clickable(w))
    glw_store_matrix(w, rc);

  if(co->co_padding[0] || co->co_padding[2]) {
    rc1 = *rc;
    glw_reposition(&rc1,
		   co->co_padding[0],
		   rc->rc_height,
		   rc->rc_width - co->co_padding[2],
		   0);
    rc = &rc1;
  }

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

    glw_container_item_t *cd = glw_parent_data(c, glw_container_item_t);

    if(cd->fade < 0.01)
      continue;

    rc0 = *rc;
    rc0.rc_alpha = alpha * cd->fade;
    rc0.rc_sharpness  = sharpness * cd->fade;

    rc0.rc_height = cd->size;

    glw_Translatef(&rc0, 0, 1.0 - cd->pos, 0);
    glw_Scalef(&rc0, 1.0, cd->scale, cd->scale);

    glw_render0(c, &rc0);
  }
}


/**
 *
 */
static void
glw_container_x_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  float alpha = rc->rc_alpha * w->glw_alpha;
  float sharpness = rc->rc_sharpness * w->glw_sharpness;
  glw_container_t *co = (glw_container_t *)w;
  glw_rctx_t rc0, rc1;

  if(alpha < 0.01f)
    return;

  if(glw_is_focusable_or_clickable(w))
    glw_store_matrix(w, rc);

  if(co->co_padding[1] || co->co_padding[3]) {
    rc1 = *rc;
    glw_reposition(&rc1,
		   0,
		   rc->rc_height - co->co_padding[1],
		   rc->rc_width,
		   co->co_padding[3]);
    rc = &rc1;
  }

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {

    if(c->glw_flags & GLW_HIDDEN)
      continue;

    rc0 = *rc;
    rc0.rc_alpha = alpha;
    rc0.rc_sharpness = sharpness;

    glw_container_item_t *cd = glw_parent_data(c, glw_container_item_t);

    rc0.rc_width = cd->size;

    glw_Translatef(&rc0, cd->pos, 0, 0);
    glw_Scalef(&rc0, cd->scale, 1.0, cd->scale);

    glw_render0(c, &rc0);
  }
}


/**
 *
 */
static void
glw_container_z_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_t *c;
  float alpha = rc->rc_alpha * w->glw_alpha;
  float sharpness  = rc->rc_sharpness  * w->glw_sharpness;
  int zmax = 0;
  glw_rctx_t rc0;

  if(alpha < 0.01f)
    return;

  if(glw_is_focusable_or_clickable(w))
    glw_store_matrix(w, rc);

  rc0 = *rc;
  rc0.rc_alpha = alpha;
  rc0.rc_sharpness = sharpness;
  rc0.rc_zmax = &zmax;

  TAILQ_FOREACH(c, &w->glw_childs, glw_parent_link) {
    if(c->glw_flags & GLW_HIDDEN)
      continue;

    rc0.rc_zindex = MAX(zmax, rc->rc_zindex);
    glw_render0(c, &rc0);
    glw_zinc(&rc0);
  }
  *rc->rc_zmax = MAX(*rc->rc_zmax, zmax);
}


static int
glw_container_x_callback(glw_t *w, void *opaque, glw_signal_t signal,
                         void *extra)
{
  glw_container_t *co = (glw_container_t *)w;
  switch(signal) {
  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
  case GLW_SIGNAL_CHILD_CREATED:
  case GLW_SIGNAL_CHILD_HIDDEN:
  case GLW_SIGNAL_CHILD_UNHIDDEN:
    return glw_container_x_constraints(co, NULL);
  case GLW_SIGNAL_CHILD_DESTROYED:
    return glw_container_x_constraints(co, extra);
  case GLW_SIGNAL_DESTROY:
    if(co->co_table != NULL)
      LIST_REMOVE(co, co_table_link);
    free(co->co_column_widths);
    return 0;
  default:
    return 0;
  }
}

static int
glw_container_y_callback(glw_t *w, void *opaque, glw_signal_t signal,
			  void *extra)
{
  switch(signal) {
  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
  case GLW_SIGNAL_CHILD_CREATED:
  case GLW_SIGNAL_CHILD_HIDDEN:
  case GLW_SIGNAL_CHILD_UNHIDDEN:
    return glw_container_y_constraints((glw_container_t *)w, NULL);
  case GLW_SIGNAL_CHILD_DESTROYED:
    return glw_container_y_constraints((glw_container_t *)w, extra);
  default:
    return 0;
  }
}

static int
glw_container_z_callback(glw_t *w, void *opaque, glw_signal_t signal,
			 void *extra)
{
  switch(signal) {
  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
  case GLW_SIGNAL_CHILD_CREATED:
    return glw_container_z_constraints(w, NULL);
  case GLW_SIGNAL_CHILD_DESTROYED:
    return glw_container_z_constraints(w, extra);
  default:
    return 0;
  }
}


/**
 *
 */
static int
glw_container_set_int(glw_t *w, glw_attribute_t attrib, int value,
                      glw_style_t *gs)
{
  glw_container_t *co = (glw_container_t *)w;

  switch(attrib) {

  case GLW_ATTRIB_SPACING:
    if(co->co_spacing == value)
      return 0;

    co->co_spacing = value;
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
container_set_int16_4(glw_t *w, glw_attribute_t attrib, const int16_t *v,
                      glw_style_t *gs)
{
  glw_container_t *co = (glw_container_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_PADDING:
    if(!glw_attrib_set_int16_4(co->co_padding, v))
      return 0;
    glw_signal0(w, GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED, NULL);
    return 1;
  default:
    return -1;
  }
}


/**
 *
 */
static void
retire_child(glw_t *w, glw_t *c)
{
  if(w->glw_flags2 & GLW2_AUTOFADE) {
    c->glw_flags |= GLW_RETIRED;
    glw_need_refresh(w->glw_root, 0);
    glw_suspend_subscriptions(c);
  } else {
    glw_destroy(c);
  }
}




/**
 *
 */
static glw_t *
glw_container_find_table(glw_t *w)
{
  for(; w != NULL; w = w->glw_parent)
    if(w->glw_class == &glw_table)
      return w;
  return NULL;
}


/**
 *
 */
static int
glw_container_set_int_unresolved(glw_t *w, const char *a, int value,
                                 glw_style_t *gs)
{
  glw_container_t *co = (glw_container_t *)w;

  if(!strcmp(a, "tableMode")) {
    if(!value == !co->co_table)
      return GLW_SET_NO_CHANGE;

    if(co->co_table != NULL)
      LIST_REMOVE(co, co_table_link);

    if(value) {
      co->co_table = (glw_table_t *)glw_container_find_table(w);
      if(co->co_table != NULL)
        LIST_INSERT_HEAD(&co->co_table->gt_rows, co, co_table_link);

    } else {
      co->co_table = NULL;
    }
    return GLW_SET_RERENDER_REQUIRED;
  }
  return GLW_SET_NOT_RESPONDING;
}


/**
 *
 */
static int
glw_table_callback(glw_t *w, void *opaque, glw_signal_t signal,
                   void *extra)
{
  glw_t *src;
  glw_table_t *gt = (glw_table_t *)w;
  switch(signal) {
  case GLW_SIGNAL_CHILD_CONSTRAINTS_CHANGED:
    src = extra;
    glw_mod_constraints(w,
                        0,
                        src->glw_req_size_y,
                        src->glw_req_weight,
                        (src->glw_flags & (GLW_CONSTRAINT_FLAGS)),
                        GLW_CONSTRAINT_Y | GLW_CONSTRAINT_W | GLW_CONSTRAINT_D);
    break;

  case GLW_SIGNAL_DESTROY:
    free(gt->gt_columns);
    break;

  default:
    break;
  }
  return 0;
}


/**
 *
 */
static glw_class_t glw_container_x = {
  .gc_name = "container_x",
  .gc_name2 = "hbox",
  .gc_instance_size = sizeof(glw_container_t),
  .gc_parent_data_size = sizeof(glw_container_item_t),
  .gc_flags = GLW_CAN_HIDE_CHILDS,
  .gc_set_int = glw_container_set_int,
  .gc_layout = glw_container_x_layout,
  .gc_render = glw_container_x_render,
  .gc_signal_handler = glw_container_x_callback,
  .gc_default_alignment = LAYOUT_ALIGN_LEFT,
  .gc_set_int16_4 = container_set_int16_4,
  .gc_bubble_event = glw_navigate_horizontal,
  .gc_set_int_unresolved = glw_container_set_int_unresolved,
};

static glw_class_t glw_container_y = {
  .gc_name = "container_y",
  .gc_name2 = "vbox",
  .gc_instance_size = sizeof(glw_container_t),
  .gc_parent_data_size = sizeof(glw_container_item_t),
  .gc_flags = GLW_CAN_HIDE_CHILDS,
  .gc_set_int = glw_container_set_int,
  .gc_layout = glw_container_y_layout,
  .gc_render = glw_container_y_render,
  .gc_signal_handler = glw_container_y_callback,
  .gc_default_alignment = LAYOUT_ALIGN_TOP,
  .gc_set_int16_4 = container_set_int16_4,
  .gc_retire_child = retire_child,
  .gc_bubble_event = glw_navigate_vertical,
};

static glw_class_t glw_container_z = {
  .gc_name = "container_z",
  .gc_name2 = "zbox",
  .gc_flags = GLW_CAN_HIDE_CHILDS,
  .gc_instance_size = sizeof(glw_t),
  .gc_layout = glw_container_z_layout,
  .gc_render = glw_container_z_render,
  .gc_signal_handler = glw_container_z_callback,
};

static glw_class_t glw_table = {
  .gc_name = "table",
  .gc_instance_size = sizeof(glw_table_t),
  .gc_layout = glw_container_z_layout,
  .gc_render = glw_container_z_render,
  .gc_signal_handler = glw_table_callback,
};

GLW_REGISTER_CLASS(glw_container_x);
GLW_REGISTER_CLASS(glw_container_y);
GLW_REGISTER_CLASS(glw_container_z);
GLW_REGISTER_CLASS(glw_table);
