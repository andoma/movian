/*
 *  GL Widgets, Bitmap/texture based texts
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

#include <assert.h>
#include <inttypes.h>
#include <sys/time.h>
#include <time.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "glw.h"
#include "glw_texture.h"
#include "glw_renderer.h"
#include "glw_text_bitmap.h"
#include "misc/string.h"
#include "text/text.h"
#include "event.h"


/**
 *
 */
typedef struct glw_text_bitmap {
  struct glw w;

  int16_t gtb_saved_width;
  int16_t gtb_saved_height;

  char *gtb_caption;
  prop_str_type_t gtb_type;

  glw_backend_texture_t gtb_texture;


  glw_renderer_t gtb_text_renderer;
  glw_renderer_t gtb_cursor_renderer;

  TAILQ_ENTRY(glw_text_bitmap) gtb_workq_link;
  LIST_ENTRY(glw_text_bitmap) gtb_global_link;

  pixmap_t *gtb_pixmap;

  enum {
    GTB_NEED_RERENDER,
    GTB_ON_QUEUE,
    GTB_RENDERING,
    GTB_VALID
  } gtb_status;

  uint8_t gtb_frozen;
  uint8_t gtb_pending_updates;
#define GTB_UPDATE_REALIZE      2

  uint8_t gtb_paint_cursor;
  uint8_t gtb_update_cursor;
  uint8_t gtb_need_layout;

  int16_t gtb_edit_ptr;

  int16_t gtb_padding_left;
  int16_t gtb_padding_right;
  int16_t gtb_padding_top;
  int16_t gtb_padding_bottom;

  int16_t gtb_uc_len;
  int16_t gtb_uc_size;
  int16_t gtb_maxlines;
  int16_t gtb_default_size;

  uint32_t *gtb_uc_buffer; /* unicode buffer */
  float gtb_cursor_alpha;

  float gtb_size_scale;

  glw_rgb_t gtb_color;

  prop_sub_t *gtb_sub;
  prop_t *gtb_p;

  int gtb_flags;


} glw_text_bitmap_t;

static void gtb_notify(glw_text_bitmap_t *gtb);

static glw_class_t glw_text, glw_label;


/**
 *
 */
static void
glw_text_bitmap_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_text_bitmap_t *gtb = (void *)w;
  glw_root_t *gr = w->glw_root;
  pixmap_t *pm = gtb->gtb_pixmap;

  // Initialize renderers

  if(!glw_renderer_initialized(&gtb->gtb_text_renderer))
    glw_renderer_init_quad(&gtb->gtb_text_renderer);

  if(w->glw_class == &glw_text &&
     !glw_renderer_initialized(&gtb->gtb_cursor_renderer))
    glw_renderer_init_quad(&gtb->gtb_cursor_renderer);


  // Upload texture

  if(pm != NULL && pm->pm_pixels != NULL && gtb->gtb_status == GTB_VALID) {
    int fmt;

    fmt = pm->pm_type == PIXMAP_IA ? GLW_TEXTURE_FORMAT_I8A8 : GLW_TEXTURE_FORMAT_BGR32;

    glw_tex_upload(gr, &gtb->gtb_texture, pm->pm_pixels,
		   fmt, pm->pm_width, pm->pm_height, 0);

    free(pm->pm_pixels);
    pm->pm_pixels = NULL;
    gtb->gtb_need_layout = 1;
  }

  // Check if we need to repaint

  if((gtb->gtb_saved_width  != rc->rc_width || 
      gtb->gtb_saved_height != rc->rc_height)) {

    if(pm != NULL && gtb->gtb_status == GTB_VALID) {

      if(pm->pm_flags & PIXMAP_TEXT_WRAPPED)
	gtb->gtb_status = GTB_NEED_RERENDER;

      if(gtb->gtb_flags & GTB_ELLIPSIZE) {
	
	if(pm->pm_flags & PIXMAP_TEXT_TRUNCATED) {
	  gtb->gtb_status = GTB_NEED_RERENDER;
	} else {

	  if(rc->rc_width - gtb->gtb_padding_right - gtb->gtb_padding_left <
	     pm->pm_width - pm->pm_margin * 2)
	    gtb->gtb_status = GTB_NEED_RERENDER;

	  if(rc->rc_height - gtb->gtb_padding_top - gtb->gtb_padding_bottom <
	     pm->pm_height - pm->pm_margin * 2)
	    gtb->gtb_status = GTB_NEED_RERENDER;
	}
      }
    }

    gtb->gtb_saved_width  = rc->rc_width;
    gtb->gtb_saved_height = rc->rc_height;
    gtb->gtb_need_layout = 1;
  }

  if(pm != NULL && gtb->gtb_need_layout) {

    int left   =                 gtb->gtb_padding_left   - pm->pm_margin;
    int top    = rc->rc_height - gtb->gtb_padding_top    + pm->pm_margin;
    int right  = rc->rc_width  - gtb->gtb_padding_right  + pm->pm_margin;
    int bottom =                 gtb->gtb_padding_bottom - pm->pm_margin;
    
    int text_width  = pm->pm_width;
    int text_height = pm->pm_height;
    
    float x1, y1, x2, y2;

    // Horizontal 
    if(text_width > right - left) {
      // Oversized, must cut
      text_width = right - left;
    } else { 
      switch(w->glw_alignment) {
      case LAYOUT_ALIGN_CENTER:
      case LAYOUT_ALIGN_BOTTOM:
      case LAYOUT_ALIGN_TOP:
	left = (left + right - text_width) / 2;
	right = left + text_width;
	break;

      case LAYOUT_ALIGN_LEFT:
      case LAYOUT_ALIGN_TOP_LEFT:
      case LAYOUT_ALIGN_BOTTOM_LEFT:
	right = left + pm->pm_width;
	break;

      case LAYOUT_ALIGN_RIGHT:
      case LAYOUT_ALIGN_TOP_RIGHT:
      case LAYOUT_ALIGN_BOTTOM_RIGHT:
	left = right - pm->pm_width;
	break;
      }
    }

    // Vertical 
    if(text_height > top - bottom) {
      // Oversized, must cut
      text_height = top - bottom;
    } else { 
      switch(w->glw_alignment) {
      case LAYOUT_ALIGN_CENTER:
      case LAYOUT_ALIGN_LEFT:
      case LAYOUT_ALIGN_RIGHT:
	bottom = (bottom + top - text_height) / 2;
	top = bottom + text_height;
	break;

      case LAYOUT_ALIGN_TOP_LEFT:
      case LAYOUT_ALIGN_TOP_RIGHT:
      case LAYOUT_ALIGN_TOP:
	bottom = top - pm->pm_height;
	break;

      case LAYOUT_ALIGN_BOTTOM:
      case LAYOUT_ALIGN_BOTTOM_LEFT:
      case LAYOUT_ALIGN_BOTTOM_RIGHT:
	top = bottom + pm->pm_height;
	break;
      }
    }

    x1 = -1.0f + 2.0f * left   / (float)rc->rc_width;
    y1 = -1.0f + 2.0f * bottom / (float)rc->rc_height;
    x2 = -1.0f + 2.0f * right  / (float)rc->rc_width;
    y2 = -1.0f + 2.0f * top    / (float)rc->rc_height;

    float s, t;

    if(gr->gr_normalized_texture_coords) {
      s = text_width  / (float)pm->pm_width;
      t = text_height / (float)pm->pm_height;
    } else {
      s = text_width;
      t = text_height;
    }

    glw_renderer_vtx_pos(&gtb->gtb_text_renderer, 0, x1, y1, 0.0);
    glw_renderer_vtx_st (&gtb->gtb_text_renderer, 0, 0, t);

    glw_renderer_vtx_pos(&gtb->gtb_text_renderer, 1, x2, y1, 0.0);
    glw_renderer_vtx_st (&gtb->gtb_text_renderer, 1, s, t);

    glw_renderer_vtx_pos(&gtb->gtb_text_renderer, 2, x2, y2, 0.0);
    glw_renderer_vtx_st (&gtb->gtb_text_renderer, 2, s, 0);

    glw_renderer_vtx_pos(&gtb->gtb_text_renderer, 3, x1, y2, 0.0);
    glw_renderer_vtx_st (&gtb->gtb_text_renderer, 3, 0, 0);
  }
  
  if(w->glw_class == &glw_text && gtb->gtb_update_cursor && 
     gtb->gtb_status == GTB_VALID) {

    int i = gtb->gtb_edit_ptr;
    int left, right;
    float x1, y1, x2, y2;

    if(pm != NULL && pm->pm_charpos != NULL) {
	
      if(i < pm->pm_charposlen) {
	left  = pm->pm_charpos[i*2  ];
	right = pm->pm_charpos[i*2+1];
      } else {
	left  = pm->pm_charpos[2*pm->pm_charposlen - 1];
	right = left + 10;
      }

      left  += gtb->gtb_padding_left;
      right += gtb->gtb_padding_left;

    } else {

      left = 0;
      right = 10;
    }

    x1 = -1.0f + 2.0f * left   / (float)rc->rc_width;
    x2 = -1.0f + 2.0f * right  / (float)rc->rc_width;
    y1 = -0.9f;
    y2 =  0.9f;

    glw_renderer_vtx_pos(&gtb->gtb_cursor_renderer, 0, x1, y1, 0.0);
    glw_renderer_vtx_pos(&gtb->gtb_cursor_renderer, 1, x2, y1, 0.0);
    glw_renderer_vtx_pos(&gtb->gtb_cursor_renderer, 2, x2, y2, 0.0);
    glw_renderer_vtx_pos(&gtb->gtb_cursor_renderer, 3, x1, y2, 0.0);

    gtb->gtb_update_cursor = 0;
  }

  gtb->gtb_paint_cursor = w->glw_class == &glw_text && glw_is_focused(w);
  gtb->gtb_need_layout = 0;


  if(gtb->gtb_status != GTB_NEED_RERENDER)
    return;

  TAILQ_INSERT_TAIL(&gr->gr_gtb_render_queue, gtb, gtb_workq_link);
  gtb->gtb_status = GTB_ON_QUEUE;
  
  hts_cond_signal(&gr->gr_gtb_render_cond);
}


/**
 *
 */
static void
glw_text_bitmap_render(glw_t *w, glw_rctx_t *rc)
{
  glw_text_bitmap_t *gtb = (glw_text_bitmap_t *)w;
  pixmap_t *pm = gtb->gtb_pixmap;
  float alpha;
  float blur = 1 - (rc->rc_blur * w->glw_blur);

  if(glw_is_focusable(w))
    glw_store_matrix(w, rc);

  alpha = rc->rc_alpha * w->glw_alpha;

  if(alpha < 0.01f)
    return;

  if(w->glw_flags & GLW_DEBUG)
    glw_wirebox(w->glw_root, rc);

  if(glw_is_tex_inited(&gtb->gtb_texture) && pm != NULL) {
#if 0
    if(w->glw_flags & GLW_SHADOW && !rc->rc_inhibit_shadows) {
      float xd =  2.5f / rc->rc_width;
      float yd = -2.5f / rc->rc_height;
      glw_rctx_t rc0 = *rc;

      glw_Translatef(&rc0, xd, yd, 0.0);
      
      const static glw_rgb_t black = {0,0,0};
      
      glw_renderer_draw(&gtb->gtb_text_renderer, w->glw_root, &rc0, 
			&gtb->gtb_texture, &black, NULL, alpha,
			blur);
    }
#endif
    glw_renderer_draw(&gtb->gtb_text_renderer, w->glw_root, rc, 
		      &gtb->gtb_texture, &gtb->gtb_color, NULL, alpha,
		      blur);
  }

  if(gtb->gtb_paint_cursor) {
    glw_root_t *gr = w->glw_root;
    float a = cos((gr->gr_frames & 2047) * (360.0f / 2048.0f)) * 0.5f + 0.5f;
    glw_renderer_draw(&gtb->gtb_cursor_renderer, w->glw_root, rc,
		      NULL, NULL, NULL, alpha * a, blur);
  }
}


/**
 *
 */
static void
glw_text_bitmap_dtor(glw_t *w)
{
  glw_text_bitmap_t *gtb = (void *)w;
  glw_root_t *gr = w->glw_root;

  free(gtb->gtb_caption);
  free(gtb->gtb_uc_buffer);

  if(gtb->gtb_pixmap != NULL)
    pixmap_release(gtb->gtb_pixmap);

  LIST_REMOVE(gtb, gtb_global_link);

  glw_tex_destroy(w->glw_root, &gtb->gtb_texture);

  glw_renderer_free(&gtb->gtb_text_renderer);
  glw_renderer_free(&gtb->gtb_cursor_renderer);

  if(gtb->gtb_status == GTB_ON_QUEUE)
    TAILQ_REMOVE(&gr->gr_gtb_render_queue, gtb, gtb_workq_link);
}


/**
 *
 */
static void
gtb_set_constraints(glw_root_t *gr, glw_text_bitmap_t *gtb)
{
  const pixmap_t *pm = gtb->gtb_pixmap;
  int lines = pm && pm->pm_lines ? pm->pm_lines : 1;
  int flags = GLW_CONSTRAINT_Y;
  int ys = gtb->gtb_padding_top + gtb->gtb_padding_bottom;
  int xs = gtb->gtb_padding_left + gtb->gtb_padding_right;

  int lh = (gtb->gtb_default_size ?: gr->gr_fontsize) * gtb->gtb_size_scale;
  int height = lh * lines;
  
  height = MAX(pm ? pm->pm_height - pm->pm_margin*2: 0, height);
  ys += height;

  if(pm != NULL)
    xs += pm->pm_width - pm->pm_margin*2;

  if(!(gtb->gtb_flags & GTB_ELLIPSIZE) && gtb->gtb_maxlines == 1 && xs > 0)
    flags |= GLW_CONSTRAINT_X;

  if(0)
    printf("Constraints %c%c %d,%d\n",
	   flags & GLW_CONSTRAINT_X ? 'X' : ' ',
	   flags & GLW_CONSTRAINT_Y ? 'Y' : ' ',
	   xs, ys);

  glw_set_constraints(&gtb->w, xs, ys, 0, flags, 0);
}


/**
 *
 */
static void
gtb_flush(glw_text_bitmap_t *gtb)
{
  glw_tex_destroy(gtb->w.glw_root, &gtb->gtb_texture);
  if(gtb->gtb_status != GTB_ON_QUEUE)
    gtb->gtb_status = GTB_NEED_RERENDER;
}


/**
 * Delete char from buf
 */
static int
del_char(glw_text_bitmap_t *gtb)
{
  int i;

  if(gtb->gtb_edit_ptr == 0)
    return 0;

  gtb->gtb_uc_len--;
  gtb->gtb_edit_ptr--;
  gtb->gtb_update_cursor = 1;

  for(i = gtb->gtb_edit_ptr; i != gtb->gtb_uc_len; i++)
    gtb->gtb_uc_buffer[i] = gtb->gtb_uc_buffer[i + 1];

  return 1;
}



/**
 * Insert char in buf
 */
static int
insert_char(glw_text_bitmap_t *gtb, int ch)
{
  int i;

  if(gtb->gtb_uc_len == gtb->gtb_uc_size) {
    gtb->gtb_uc_size += 10;
    gtb->gtb_uc_buffer = realloc(gtb->gtb_uc_buffer, 
				 sizeof(int) * gtb->gtb_uc_size);
  }

  for(i = gtb->gtb_uc_len; i != gtb->gtb_edit_ptr; i--)
    gtb->gtb_uc_buffer[i] = gtb->gtb_uc_buffer[i - 1];
  
  gtb->gtb_uc_buffer[i] = ch;
  gtb->gtb_uc_len++;
  gtb->gtb_edit_ptr++;
  gtb->gtb_update_cursor = 1;
  return 1;
}


/**
 *
 */
static void
gtb_unbind(glw_text_bitmap_t *gtb)
{
  if(gtb->gtb_sub != NULL)
    prop_unsubscribe(gtb->gtb_sub);

  if(gtb->gtb_p != NULL) {
    prop_ref_dec(gtb->gtb_p);
    gtb->gtb_p = NULL;
  }
}



/**
 *
 */
static int
glw_text_bitmap_callback(glw_t *w, void *opaque, glw_signal_t signal,
			 void *extra)
{
  glw_text_bitmap_t *gtb = (void *)w;
  event_t *e;
  event_int_t *eu;

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_DESTROY:
    gtb_unbind(gtb);
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_text_bitmap_layout(w, extra);
    break;
  case GLW_SIGNAL_INACTIVE:
    gtb_flush(gtb);
    break;
  case GLW_SIGNAL_EVENT:
    if(w->glw_class == &glw_label)
      return 0;

    e = extra;

    if(event_is_action(e, ACTION_BS)) {

      del_char(gtb);
      gtb_notify(gtb);
      return 1;
      
    } else if(event_is_type(e, EVENT_UNICODE)) {

      eu = extra;

      if(insert_char(gtb, eu->val))
	gtb_notify(gtb);
      return 1;

    } else if(event_is_action(e, ACTION_LEFT)) {

      if(gtb->gtb_edit_ptr > 0) {
	gtb->gtb_edit_ptr--;
	gtb->gtb_update_cursor = 1;
      }
      return 1;

    } else if(event_is_action(e, ACTION_RIGHT)) {

      if(gtb->gtb_edit_ptr < gtb->gtb_uc_len) {
	gtb->gtb_edit_ptr++;
	gtb->gtb_update_cursor = 1;
      }
      return 1;
    }
    return 0;
  }
  return 0;
}


/**
 *
 */
static void
gtb_realize(glw_text_bitmap_t *gtb)
{
  if(gtb->gtb_status != GTB_ON_QUEUE)
    gtb->gtb_status = GTB_NEED_RERENDER;

  if(!(gtb->w.glw_flags & GLW_CONSTRAINT_Y)) // Only update if yet unset
    gtb_set_constraints(gtb->w.glw_root, gtb);
}



static void gtb_update_epilogue(glw_text_bitmap_t *gtb, int flags);

/**
 *
 */
static void
caption_set_internal(glw_text_bitmap_t *gtb, const char *str, int type)
{
  int len;
  int flags = 0;

  if(gtb->gtb_caption && !strcmp(str ?: "", gtb->gtb_caption) &&
     type == gtb->gtb_type)
    return;

  free(gtb->gtb_caption);
  gtb->gtb_caption = strdup(str ?: "");
  gtb->gtb_type = type;

  assert(gtb->gtb_type == 0 || gtb->gtb_type == 1);

  if(gtb->w.glw_flags2 & GLW2_AUTOHIDE) {
    if(str == NULL || *str == 0)
      glw_hide(&gtb->w);
    else
      glw_unhide(&gtb->w);
  }
  
  if(gtb->gtb_type == PROP_STR_RICH)
    flags |= TEXT_PARSE_TAGS | TEXT_PARSE_HTML_ENTETIES;

  free(gtb->gtb_uc_buffer);
  gtb->gtb_uc_buffer = text_parse(gtb->gtb_caption ?: "", &len, flags,
				  NULL, 0);
  gtb->gtb_uc_len = gtb->gtb_uc_size = len;

  if(gtb->w.glw_class == &glw_text) {
    gtb->gtb_edit_ptr = gtb->gtb_uc_len;
    gtb->gtb_update_cursor = 1;
  }
  
  gtb_update_epilogue(gtb, GTB_UPDATE_REALIZE);
}


/**
 *
 */
static void
gtb_update_epilogue(glw_text_bitmap_t *gtb, int flags)
{
  if(gtb->gtb_frozen) {
    gtb->gtb_pending_updates |= flags;
  } else {

    if(flags & GTB_UPDATE_REALIZE)
      gtb_realize(gtb);

    gtb->gtb_pending_updates = 0;
  }
}



/**
 *
 */
void
glw_gtb_set_caption_raw(glw_t *w, uint32_t *uc, int len)
{
  glw_text_bitmap_t *gtb = (glw_text_bitmap_t *)w;
  free(gtb->gtb_uc_buffer);

  gtb->gtb_uc_buffer = uc;
  gtb->gtb_uc_len = len;

  gtb_update_epilogue(gtb, GTB_UPDATE_REALIZE);
}


/**
 *
 */
static void
prop_callback(void *opaque, prop_event_t event, ...)
{
  glw_text_bitmap_t *gtb = opaque;
  const char *caption;
  prop_t *p;
  prop_str_type_t type = 0;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_SET_VOID:
    caption = NULL;
    p = va_arg(ap, prop_t *);
    break;

  case PROP_SET_RSTRING:
    caption = rstr_get(va_arg(ap, const rstr_t *));
    p = va_arg(ap, prop_t *);
    type = va_arg(ap, prop_str_type_t);
    break;

  default:
    return;
  }

  prop_ref_dec(gtb->gtb_p);
  gtb->gtb_p = prop_ref_inc(p);

  caption_set_internal(gtb, caption, type);
}


/**
 *
 */
static void 
glw_text_bitmap_ctor(glw_t *w)
{
  glw_text_bitmap_t *gtb = (void *)w;
  glw_root_t *gr = w->glw_root;

  w->glw_flags |= GLW_FOCUS_ON_CLICK;
  gtb->gtb_edit_ptr = -1;
  gtb->gtb_size_scale = 1.0;
  gtb->gtb_color.r = 1.0;
  gtb->gtb_color.g = 1.0;
  gtb->gtb_color.b = 1.0;
  gtb->gtb_maxlines = 1;
  LIST_INSERT_HEAD(&gr->gr_gtbs, gtb, gtb_global_link);
}


/**
 *
 */
static void 
glw_text_bitmap_set_rgb(glw_t *w, const float *rgb)
{
  glw_text_bitmap_t *gtb = (void *)w;

  gtb->gtb_color.r = rgb[0];
  gtb->gtb_color.g = rgb[1];
  gtb->gtb_color.b = rgb[2];
}


/**
 *
 */
static void
set_padding(glw_t *w, const int16_t *v)
{
  glw_text_bitmap_t *gtb = (void *)w;

  if(gtb->gtb_padding_left   == v[0] &&
     gtb->gtb_padding_top    == v[1] &&
     gtb->gtb_padding_right  == v[2] &&
     gtb->gtb_padding_bottom == v[3])
    return;

  gtb->gtb_padding_left   = v[0];
  gtb->gtb_padding_top    = v[1];
  gtb->gtb_padding_right  = v[2];
  gtb->gtb_padding_bottom = v[3];
  if(!(gtb->w.glw_flags & GLW_CONSTRAINT_Y)) // Only update if yet unset
    gtb_set_constraints(gtb->w.glw_root, gtb);
  gtb->gtb_need_layout = 1;
}

/**
 *
 */
static void
mod_text_flags(glw_t *w, int set, int clr)
{
  glw_text_bitmap_t *gtb = (glw_text_bitmap_t *)w;
  gtb->gtb_flags = (gtb->gtb_flags | set) & ~clr;
  
  gtb_update_epilogue(gtb, GTB_UPDATE_REALIZE);
}


/**
 *
 */
static void
set_caption(glw_t *w, const char *caption, int type)
{
  glw_text_bitmap_t *gtb = (void *)w;
  gtb_unbind(gtb);
  caption_set_internal(gtb, caption, type);
}


/**
 *
 */
static void 
bind_to_property(glw_t *w, prop_t *p, const char **pname,
		 prop_t *view, prop_t *args, prop_t *clone)
{
  glw_text_bitmap_t *gtb = (void *)w;
  gtb_unbind(gtb);

  gtb->gtb_sub = 
    prop_subscribe(PROP_SUB_DIRECT_UPDATE,
		   PROP_TAG_NAME_VECTOR, pname, 
		   PROP_TAG_CALLBACK, prop_callback, gtb, 
		   PROP_TAG_COURIER, w->glw_root->gr_courier,
		   PROP_TAG_NAMED_ROOT, p, "self",
		   PROP_TAG_NAMED_ROOT, view, "view",
		   PROP_TAG_NAMED_ROOT, args, "args",
		   PROP_TAG_NAMED_ROOT, clone, "clone",
		   PROP_TAG_ROOT, w->glw_root->gr_uii.uii_prop,
		   NULL);

  if(w->glw_flags2 & GLW2_AUTOHIDE)
    glw_unhide(w);
}


/**
 *
 */
static void
freeze(glw_t *w)
{
  glw_text_bitmap_t *gtb = (glw_text_bitmap_t *)w;
  gtb->gtb_frozen = 1;
}


/**
 *
 */
static void
thaw(glw_t *w)
{
  glw_text_bitmap_t *gtb = (glw_text_bitmap_t *)w;
  gtb->gtb_frozen = 0;

  if(gtb->gtb_pending_updates & GTB_UPDATE_REALIZE)
    gtb_realize(gtb);

  gtb->gtb_pending_updates = 0;
}


/**
 *
 */
static void
set_size_scale(glw_t *w, float v)
{
  glw_text_bitmap_t *gtb = (void *)w;

  if(gtb->gtb_size_scale == v)
    return;

  gtb->gtb_size_scale = v;
  gtb_update_epilogue(gtb, GTB_UPDATE_REALIZE);
}


/**
 *
 */
static void
set_default_size(glw_t *w, int px)
{
  glw_text_bitmap_t *gtb = (void *)w;

  if(gtb->gtb_default_size == px)
    return;

  gtb->gtb_default_size = px;
  gtb_update_epilogue(gtb, GTB_UPDATE_REALIZE);
}


/**
 *
 */
static void
set_maxlines(glw_t *w, int v)
{
  glw_text_bitmap_t *gtb = (void *)w;

  gtb->gtb_maxlines = v;
  gtb_update_epilogue(gtb, GTB_UPDATE_REALIZE);
}




/**
 *
 */
static void *
font_render_thread(void *aux)
{
  glw_root_t *gr = aux;
  glw_text_bitmap_t *gtb;
  uint32_t *uc, len, i;
  pixmap_t *pm;
  int max_width, max_lines, flags, default_size, tr_align;
  float scale;

  glw_lock(gr);

  while(1) {
    
    while((gtb = TAILQ_FIRST(&gr->gr_gtb_render_queue)) == NULL)
      glw_cond_wait(gr, &gr->gr_gtb_render_cond);

    /* We are going to render unlocked so we cannot use gtb at all */

    len = gtb->gtb_uc_len;
    if(len > 0) {
      uc = malloc((len + 3) * sizeof(int));

      if(gtb->gtb_flags & GTB_PASSWORD) {
	for(i = 0; i < len; i++)
	  uc[i] = '*';
      } else {
	memcpy(uc, gtb->gtb_uc_buffer, len * sizeof(int));
      }
    } else {
      uc = NULL;
    }

    assert(gtb->gtb_status == GTB_ON_QUEUE);
    TAILQ_REMOVE(&gr->gr_gtb_render_queue, gtb, gtb_workq_link);
    gtb->gtb_status = GTB_RENDERING;

    default_size = gtb->gtb_default_size ?: gr->gr_fontsize;
    scale = gtb->gtb_size_scale;

    max_width =
      gtb->gtb_saved_width - gtb->gtb_padding_left - gtb->gtb_padding_right;
    max_lines = gtb->gtb_maxlines;

    flags = 0;
    if(gtb->w.glw_flags & GLW_DEBUG)
      flags |= TR_RENDER_DEBUG;

    if(gtb->gtb_flags & GTB_ELLIPSIZE)
      flags |= TR_RENDER_ELLIPSIZE;

    if(gtb->gtb_flags & GTB_BOLD)
      flags |= TR_RENDER_BOLD;

    if(gtb->gtb_flags & GTB_ITALIC)
      flags |= TR_RENDER_ITALIC;

    if(gtb->gtb_flags & GTB_OUTLINE)
      flags |= TR_RENDER_OUTLINE;
    
    if(gtb->gtb_edit_ptr >= 0)
      flags |= TR_RENDER_CHARACTER_POS;

    tr_align = TR_ALIGN_JUSTIFIED;

    if(gtb->w.glw_flags2 & GLW2_SHADOW)
      flags |= TR_RENDER_SHADOW;

    switch(gtb->w.glw_alignment) {
    case LAYOUT_ALIGN_CENTER:
    case LAYOUT_ALIGN_BOTTOM:
    case LAYOUT_ALIGN_TOP:
      tr_align = TR_ALIGN_CENTER;
      break;

    case LAYOUT_ALIGN_LEFT:
    case LAYOUT_ALIGN_TOP_LEFT:
    case LAYOUT_ALIGN_BOTTOM_LEFT:
      tr_align = TR_ALIGN_LEFT;
      break;

    case LAYOUT_ALIGN_RIGHT:
    case LAYOUT_ALIGN_TOP_RIGHT:
    case LAYOUT_ALIGN_BOTTOM_RIGHT:
      tr_align = TR_ALIGN_RIGHT;
      break;
    }


    /* gtb (i.e the widget) may be destroyed directly after we unlock,
       so we can't access it after this point. We can hold a reference
       though. But it will only guarantee that the pointer stays valid */

    glw_ref(&gtb->w);
    glw_unlock(gr);

    if(uc != NULL && uc[0] != 0) {
      pm = text_render(uc, len, flags, default_size, scale,
		       tr_align, max_width, max_lines, NULL);
    } else {
      pm = NULL;
    }

    free(uc);
    glw_lock(gr);

    if(gtb->w.glw_flags & GLW_DESTROYING) {
      /* widget got destroyed while we were away, throw away the results */
      glw_unref(&gtb->w);
      if(pm != NULL)
	pixmap_release(pm);
      continue;
    }

    glw_unref(&gtb->w);

    if(gtb->gtb_pixmap != NULL)
      pixmap_release(gtb->gtb_pixmap);

    gtb->gtb_pixmap = pm;

    if(gtb->gtb_status == GTB_RENDERING)
      gtb->gtb_status = GTB_VALID;

    gtb_set_constraints(gr, gtb);
  }
  return NULL;
}

/**
 *
 */
void
glw_text_flush(glw_root_t *gr)
{
  glw_text_bitmap_t *gtb;
  LIST_FOREACH(gtb, &gr->gr_gtbs, gtb_global_link) {
    gtb_flush(gtb);
    gtb_set_constraints(gr, gtb);
  }
}

/**
 *
 */
int
glw_get_text(glw_t *w, char *buf, size_t buflen)
{
  glw_text_bitmap_t *gtb = (void *)w;
  char *q;
  int i;

  if(w->glw_class != &glw_label && w->glw_class != &glw_text) {
    return -1;
  }

  q = buf;
  for(i = 0; i < gtb->gtb_uc_len; i++)
    q += utf8_put(q, gtb->gtb_uc_buffer[i]);
  *q = 0;
  return 0;
}


/**
 *
 */
static void
gtb_notify(glw_text_bitmap_t *gtb)
{
  char buf[100];
  if(gtb->gtb_status != GTB_ON_QUEUE)
    gtb->gtb_status = GTB_NEED_RERENDER;

  if(gtb->gtb_p != NULL) {
    glw_get_text(&gtb->w, buf, sizeof(buf));
    prop_set_string_ex(gtb->gtb_p, gtb->gtb_sub, buf, 0);
  }
}


/**
 *
 */
void
glw_text_bitmap_init(glw_root_t *gr)
{
  TAILQ_INIT(&gr->gr_gtb_render_queue);

  hts_cond_init(&gr->gr_gtb_render_cond, &gr->gr_mutex);

  glw_font_change_size(gr, 20);

  hts_thread_create_detached("GLW font renderer", font_render_thread, gr,
			     THREAD_PRIO_NORMAL);
}




/**
 * Change font scaling
 */
void
glw_font_change_size(void *opaque, int fontsize)
{
  glw_root_t *gr = opaque;
  if(gr->gr_fontsize == fontsize || fontsize == 0)
    return;

  gr->gr_fontsize = fontsize;
  glw_text_flush(gr);
}


/**
 *
 */
static const char *
glw_text_bitmap_get_text(glw_t *w)
{
  glw_text_bitmap_t *gtb = (glw_text_bitmap_t *)w;
  return gtb->gtb_caption;
}


/**
 *
 */
static void 
mod_flags2(glw_t *w, int set, int clr)
{
  glw_text_bitmap_t *gtb = (glw_text_bitmap_t *)w;

  if(set & GLW2_AUTOHIDE && gtb->gtb_caption == NULL)
    glw_hide(w);

  if(clr & GLW2_AUTOHIDE)
    glw_unhide(w);


  if((set | clr) & GLW2_SHADOW)
    gtb_update_epilogue(gtb, GTB_UPDATE_REALIZE);
}


/**
 *
 */
static glw_class_t glw_label = {
  .gc_name = "label",
  .gc_instance_size = sizeof(glw_text_bitmap_t),
  .gc_render = glw_text_bitmap_render,
  .gc_ctor = glw_text_bitmap_ctor,
  .gc_dtor = glw_text_bitmap_dtor,
  .gc_signal_handler = glw_text_bitmap_callback,
  .gc_get_text = glw_text_bitmap_get_text,
  .gc_default_alignment = LAYOUT_ALIGN_LEFT,
  .gc_set_rgb = glw_text_bitmap_set_rgb,
  .gc_set_padding = set_padding,
  .gc_mod_text_flags = mod_text_flags,
  .gc_set_caption = set_caption,
  .gc_bind_to_property = bind_to_property,
  .gc_mod_flags2 = mod_flags2,
  .gc_freeze = freeze,
  .gc_thaw = thaw,
  .gc_set_size_scale = set_size_scale,
  .gc_set_default_size = set_default_size,
  .gc_set_max_lines = set_maxlines,
};

GLW_REGISTER_CLASS(glw_label);


/**
 *
 */
static glw_class_t glw_text = {
  .gc_name = "text",
  .gc_instance_size = sizeof(glw_text_bitmap_t),
  .gc_render = glw_text_bitmap_render,
  .gc_ctor = glw_text_bitmap_ctor,
  .gc_dtor = glw_text_bitmap_dtor,
  .gc_signal_handler = glw_text_bitmap_callback,
  .gc_get_text = glw_text_bitmap_get_text,
  .gc_default_alignment = LAYOUT_ALIGN_LEFT,
  .gc_set_rgb = glw_text_bitmap_set_rgb,
  .gc_set_padding = set_padding,
  .gc_mod_text_flags = mod_text_flags,
  .gc_set_caption = set_caption,
  .gc_bind_to_property = bind_to_property,
  .gc_freeze = freeze,
  .gc_thaw = thaw,
  .gc_set_size_scale = set_size_scale,
  .gc_set_default_size = set_default_size,
  .gc_set_max_lines = set_maxlines,
};

GLW_REGISTER_CLASS(glw_text);
