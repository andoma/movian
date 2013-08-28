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
#include "misc/str.h"
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
  rstr_t *gtb_font;
  prop_str_type_t gtb_type;

  glw_backend_texture_t gtb_texture;


  glw_renderer_t gtb_text_renderer;
  glw_renderer_t gtb_cursor_renderer;

  TAILQ_ENTRY(glw_text_bitmap) gtb_workq_link;
  LIST_ENTRY(glw_text_bitmap) gtb_global_link;

  pixmap_t *gtb_pixmap;

   enum {
     GTB_IDLE,
     GTB_QUEUED_FOR_DIMENSIONING,
     GTB_DIMENSIONING,
     GTB_NEED_RENDER,
     GTB_QUEUED_FOR_RENDERING,
     GTB_RENDERING,
     GTB_VALID
  } gtb_state;

  uint8_t gtb_frozen;
  uint8_t gtb_pending_updates;
#define GTB_UPDATE_REALIZE      2

  uint8_t gtb_paint_cursor;
  uint8_t gtb_update_cursor;
  uint8_t gtb_need_layout;
  uint8_t gtb_deferred_realize;
  uint8_t gtb_caption_dirty;

  int16_t gtb_edit_ptr;

  int16_t gtb_padding_left;
  int16_t gtb_padding_right;
  int16_t gtb_padding_top;
  int16_t gtb_padding_bottom;

  int16_t gtb_uc_len;
  int16_t gtb_uc_size;
  int16_t gtb_maxlines;
  int16_t gtb_default_size;
  int16_t gtb_min_size;

  uint32_t *gtb_uc_buffer; /* unicode buffer */
  float gtb_cursor_alpha;

  float gtb_size_scale;

  glw_rgb_t gtb_color;

  prop_sub_t *gtb_sub;
  prop_t *gtb_p;

  int gtb_flags;

  char *gtb_description;

} glw_text_bitmap_t;

static void gtb_notify(glw_text_bitmap_t *gtb);
static void gtb_realize(glw_text_bitmap_t *gtb);
static void gtb_caption_refresh(glw_text_bitmap_t *gtb);

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

  gr->gr_can_externalize = 0;

  // Initialize renderers

  if(!glw_renderer_initialized(&gtb->gtb_text_renderer))
    glw_renderer_init_quad(&gtb->gtb_text_renderer);

  if(w->glw_class == &glw_text &&
     !glw_renderer_initialized(&gtb->gtb_cursor_renderer))
    glw_renderer_init_quad(&gtb->gtb_cursor_renderer);


  // Upload texture

  if(pm != NULL && pm->pm_pixels != NULL) {
    glw_tex_upload(gr, &gtb->gtb_texture, pm, 0);

    free(pm->pm_pixels);
    pm->pm_pixels = NULL;
    gtb->gtb_need_layout = 1;
  }

  // Check if we need to repaint

  if((gtb->gtb_saved_width  != rc->rc_width || 
      gtb->gtb_saved_height != rc->rc_height)) {

    if(pm != NULL && gtb->gtb_state == GTB_VALID) {
      if(pm->pm_flags & PIXMAP_TEXT_WRAPPED)
	gtb->gtb_state = GTB_NEED_RENDER;

      if(rc->rc_width > gtb->gtb_saved_width && 
	 pm->pm_flags & PIXMAP_TEXT_TRUNCATED)
	gtb->gtb_state = GTB_NEED_RENDER;

      if(gtb->gtb_flags & GTB_ELLIPSIZE) {
	
	if(pm->pm_flags & PIXMAP_TEXT_TRUNCATED) {
	  gtb->gtb_state = GTB_NEED_RENDER;
	} else {

	  if(rc->rc_width - gtb->gtb_padding_right - gtb->gtb_padding_left <
	     pm->pm_width - pm->pm_margin * 2)
	    gtb->gtb_state = GTB_NEED_RENDER;

	  if(rc->rc_height - gtb->gtb_padding_top - gtb->gtb_padding_bottom <
	     pm->pm_height - pm->pm_margin * 2)
	    gtb->gtb_state = GTB_NEED_RENDER;
	}
      }
    }

    gtb->gtb_saved_width  = rc->rc_width;
    gtb->gtb_saved_height = rc->rc_height;
    gtb->gtb_need_layout = 1;

    if(gtb->w.glw_flags2 & GLW2_DEBUG)
      printf("   parent widget gives us :%d x %d\n", rc->rc_width, rc->rc_height);

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
    if(text_width > right - left || pm->pm_flags & PIXMAP_TEXT_TRUNCATED) {
      // Oversized, must cut
      text_width = right - left;

      if(!(gtb->gtb_flags & GTB_ELLIPSIZE)) {
	glw_renderer_vtx_col(&gtb->gtb_text_renderer, 0, 1,1,1,1+text_width/20);
	glw_renderer_vtx_col(&gtb->gtb_text_renderer, 1, 1,1,1,0);
	glw_renderer_vtx_col(&gtb->gtb_text_renderer, 2, 1,1,1,0);
	glw_renderer_vtx_col(&gtb->gtb_text_renderer, 3, 1,1,1,1+text_width/20);
      }

    } else { 

      glw_renderer_vtx_col(&gtb->gtb_text_renderer, 0, 1,1,1,1);
      glw_renderer_vtx_col(&gtb->gtb_text_renderer, 1, 1,1,1,1);
      glw_renderer_vtx_col(&gtb->gtb_text_renderer, 2, 1,1,1,1);
      glw_renderer_vtx_col(&gtb->gtb_text_renderer, 3, 1,1,1,1);

      switch(w->glw_alignment) {
      case LAYOUT_ALIGN_JUSTIFIED:
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
      case LAYOUT_ALIGN_JUSTIFIED:
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
     gtb->gtb_state == GTB_VALID) {

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

  gtb->gtb_paint_cursor = 
    gtb->gtb_flags & GTB_PERMANENT_CURSOR ||
    (w->glw_class == &glw_text && glw_is_focused(w));
  gtb->gtb_need_layout = 0;

  if(gtb->gtb_state == GTB_VALID && gtb->gtb_deferred_realize) {
    gtb->gtb_deferred_realize = 0;
    gtb_realize(gtb);
  }

  if(gtb->gtb_state != GTB_NEED_RENDER)
    return;

  TAILQ_INSERT_TAIL(&gr->gr_gtb_render_queue, gtb, gtb_workq_link);
  gtb->gtb_state = GTB_QUEUED_FOR_RENDERING;
  
  hts_cond_signal(&gr->gr_gtb_work_cond);
}


/**
 *
 */
static void
glw_text_bitmap_render(glw_t *w, const glw_rctx_t *rc)
{
  glw_text_bitmap_t *gtb = (glw_text_bitmap_t *)w;
  pixmap_t *pm = gtb->gtb_pixmap;
  float alpha;
  float blur = 1 - (rc->rc_sharpness * w->glw_sharpness);

  if(glw_is_focusable(w))
    glw_store_matrix(w, rc);

  alpha = rc->rc_alpha * w->glw_alpha;

  if(alpha < 0.01f)
    return;

  if(w->glw_flags2 & GLW2_DEBUG)
    glw_wirebox(w->glw_root, rc);

  if(glw_is_tex_inited(&gtb->gtb_texture) && pm != NULL) {
    glw_renderer_draw(&gtb->gtb_text_renderer, w->glw_root, rc, 
		      &gtb->gtb_texture,
		      &gtb->gtb_color, NULL, alpha, blur, NULL);
  }

  if(gtb->gtb_paint_cursor) {
    glw_root_t *gr = w->glw_root;
    float a = cos((gr->gr_frames & 2047) * (360.0f / 2048.0f)) * 0.5f + 0.5f;
    glw_renderer_draw(&gtb->gtb_cursor_renderer, w->glw_root, rc,
		      NULL, NULL, NULL, alpha * a, blur, NULL);
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

  free(gtb->gtb_description);
  free(gtb->gtb_caption);
  free(gtb->gtb_uc_buffer);
  rstr_release(gtb->gtb_font);

  if(gtb->gtb_pixmap != NULL)
    pixmap_release(gtb->gtb_pixmap);

  LIST_REMOVE(gtb, gtb_global_link);

  glw_tex_destroy(w->glw_root, &gtb->gtb_texture);

  glw_renderer_free(&gtb->gtb_text_renderer);
  glw_renderer_free(&gtb->gtb_cursor_renderer);

  switch(gtb->gtb_state) {
  case GTB_IDLE:
  case GTB_DIMENSIONING:
  case GTB_NEED_RENDER:
  case GTB_RENDERING:
  case GTB_VALID:
    break;

  case GTB_QUEUED_FOR_DIMENSIONING:
    TAILQ_REMOVE(&gr->gr_gtb_dim_queue, gtb, gtb_workq_link);
    break;

  case GTB_QUEUED_FOR_RENDERING:
    TAILQ_REMOVE(&gr->gr_gtb_render_queue, gtb, gtb_workq_link);
    break;
  }
}


/**
 *
 */
static void
gtb_set_constraints(glw_root_t *gr, glw_text_bitmap_t *gtb,
		    const pixmap_t *pm)
{
  int flags = GLW_CONSTRAINT_Y;

#if 0
  int lines = pm && pm->pm_lines ? pm->pm_lines : 1;
  int ys = gtb->gtb_padding_top + gtb->gtb_padding_bottom;
  int xs = gtb->gtb_padding_left + gtb->gtb_padding_right;

  int lh = (gtb->gtb_default_size ?: gr->gr_fontsize) * gtb->gtb_size_scale;
  int height = lh * lines;
  
  height = MAX(pm ? pm->pm_height - pm->pm_margin*2: 0, height);
  ys += height;

  if(pm != NULL)
    xs += pm->pm_width - pm->pm_margin*2;

  if(xs > 0 && gtb->gtb_maxlines == 1)
    flags |= GLW_CONSTRAINT_X;

#else


  int xs = pm->pm_width - pm->pm_margin*2 + gtb->gtb_padding_left + gtb->gtb_padding_right;
  int ys = pm->pm_height - pm->pm_margin*2 + gtb->gtb_padding_top + gtb->gtb_padding_bottom;

  if(gtb->gtb_maxlines == 1 && !(gtb->gtb_flags & GTB_ELLIPSIZE))
    flags |= GLW_CONSTRAINT_X;

#endif

  if(gtb->w.glw_flags2 & GLW2_DEBUG)
    printf("Constraints %c%c %d,%d\n",
	   flags & GLW_CONSTRAINT_X ? 'X' : ' ',
	   flags & GLW_CONSTRAINT_Y ? 'Y' : ' ',
	   xs, ys);

  glw_set_constraints(&gtb->w, xs, ys, 0, flags);
}


/**
 *
 */
static void
gtb_inactive(glw_text_bitmap_t *gtb)
{
  glw_tex_destroy(gtb->w.glw_root, &gtb->gtb_texture);

  // Make sure it is rerendered once we get back to life
  if(gtb->gtb_state == GTB_VALID)
    gtb->gtb_state = GTB_NEED_RENDER;
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
  gtb->gtb_caption_dirty = 1;
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
  gtb->gtb_caption_dirty = 1;
  return 1;
}


/**
 *
 */
static void
gtb_unbind(glw_text_bitmap_t *gtb)
{
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
    gtb_inactive(gtb);
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
	return 1;
      }
      return 0;

    } else if(event_is_action(e, ACTION_RIGHT)) {

      if(gtb->gtb_edit_ptr < gtb->gtb_uc_len) {
	gtb->gtb_edit_ptr++;
	gtb->gtb_update_cursor = 1;
	return 1;
      }
      return 0;

    } else if(event_is_action(e, ACTION_ACTIVATE)) {
      if(w->glw_root->gr_open_osk != NULL) {

	gtb_caption_refresh(gtb);
	w->glw_root->gr_open_osk(w->glw_root, gtb->gtb_description,
				 gtb->gtb_caption, w,
				 gtb->gtb_flags & GTB_PASSWORD);
	return 1;
      }
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
  glw_root_t *gr = gtb->w.glw_root;
  int direct = gtb->gtb_maxlines > 1;


#if 0


  switch(gtb->gtb_state) {
  case GTB_QUEUED_FOR_RENDERING:
    if(direct)
      return;
    TAILQ_REMOVE(&gr->gr_gtb_render_queue, gtb, gtb_workq_link);
    break;

  case GTB_QUEUED_FOR_DIMENSIONING:
    if(!direct)
      return;
    TAILQ_REMOVE(&gr->gr_gtb_dim_queue, gtb, gtb_workq_link);
    break;

  case GTB_IDLE:
  case GTB_DIMENSIONING:
  case GTB_NEED_RENDER:
  case GTB_RENDERING:
  case GTB_VALID:
    break;
  }
#endif

  if(gtb->gtb_state != GTB_IDLE && gtb->gtb_state != GTB_VALID) {
    gtb->gtb_deferred_realize = 1;
    return;
  }

  if(direct) {
    gtb->gtb_state = GTB_NEED_RENDER;
  } else {
    TAILQ_INSERT_TAIL(&gr->gr_gtb_dim_queue, gtb, gtb_workq_link);
    gtb->gtb_state = GTB_QUEUED_FOR_DIMENSIONING;
    hts_cond_signal(&gr->gr_gtb_work_cond);
  }
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

  gtb_caption_refresh(gtb);

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
				  NULL, 0, 0);
  gtb->gtb_caption_dirty = 0;
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
  gtb->gtb_caption_dirty = 1;

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

  w->glw_flags2 |= GLW2_FOCUS_ON_CLICK;
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
set_font(glw_t *w, rstr_t *font)
{
  glw_text_bitmap_t *gtb = (glw_text_bitmap_t *)w;
  rstr_set(&gtb->gtb_font, font);
  gtb_update_epilogue(gtb, GTB_UPDATE_REALIZE);
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
		   PROP_TAG_ROOT, w->glw_root->gr_prop_ui,
		   PROP_TAG_ROOT, w->glw_root->gr_prop_nav,
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

  if(!(gtb->w.glw_flags & GLW_CONSTRAINT_Y)) {
    int lh = (gtb->gtb_default_size ?: w->glw_root->gr_current_size) *
      gtb->gtb_size_scale;
    int ys = gtb->gtb_padding_top + gtb->gtb_padding_bottom + lh;
    glw_set_constraints(&gtb->w, 0, ys, 0, GLW_CONSTRAINT_Y);
  }

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
set_min_size(glw_t *w, int px)
{
  glw_text_bitmap_t *gtb = (void *)w;

  if(gtb->gtb_min_size == px)
    return;

  gtb->gtb_min_size = px;
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
static void
do_render(glw_text_bitmap_t *gtb, glw_root_t *gr, int no_output)
{
  int i;
  uint32_t *uc, len;
  pixmap_t *pm;
  int max_width, max_lines, flags, default_size, tr_align, min_size;
  float scale;
  rstr_t *font;

  if(gtb->w.glw_flags2 & GLW2_DEBUG) {
    printf("do_render(%p, %d): ", gtb, no_output);
    for(i = 0; i < gtb->gtb_uc_len; i++)
      printf("%C", gtb->gtb_uc_buffer[i]);
    printf("\n");
  }

  /* We are going to render unlocked so we cannot use gtb at all */

  len = gtb->gtb_uc_len;
  if(len > 0) {
    uc = malloc((len + 3) * sizeof(int));

    if(gtb->gtb_flags & GTB_PASSWORD) {
      int show_one = !!(gtb->gtb_flags & GTB_OSK_PASSWORD);
      for(i = 0; i < len - show_one; i++)
	uc[i] = '*';
      if(show_one && len)
	uc[len - 1] = gtb->gtb_uc_buffer[len - 1];
    } else {
      memcpy(uc, gtb->gtb_uc_buffer, len * sizeof(int));
    }
  } else {
    uc = NULL;
  }


  default_size = gtb->gtb_default_size ?: gr->gr_current_size;
  scale = gtb->gtb_size_scale;
  min_size = gtb->gtb_min_size;

  flags = 0;

  if(no_output) {
    max_width = gr->gr_width;
    flags |= TR_RENDER_NO_OUTPUT;
  } else {
    max_width =
      gtb->gtb_saved_width - gtb->gtb_padding_left - gtb->gtb_padding_right;
  }
  if(gtb->w.glw_flags2 & GLW2_DEBUG)
    printf("   max_width=%d\n", max_width);

  max_lines = gtb->gtb_maxlines;

  if(gtb->w.glw_flags2 & GLW2_DEBUG)
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

  if(gtb->gtb_font != NULL)
    font = rstr_dup(gtb->gtb_font);
  else
    font = rstr_dup(gr->gr_default_font);

  if(gtb->w.glw_flags2 & GLW2_DEBUG)
    printf("Font is %s\n", rstr_get(font));

  /* gtb (i.e the widget) may be destroyed directly after we unlock,
     so we can't access it after this point. We can hold a reference
     though. But it will only guarantee that the pointer stays valid */

  glw_ref(&gtb->w);
  glw_unlock(gr);

  if(uc != NULL && uc[0] != 0) {
    pm = text_render(uc, len, flags, default_size, scale,
		     tr_align, max_width, max_lines, rstr_get(font),
		     gr->gr_font_domain, min_size, gr->gr_vpaths);
  } else {
    pm = NULL;
  }
    
  rstr_release(font);
  free(uc);
  glw_lock(gr);
    

  if(gtb->w.glw_flags & GLW_DESTROYING) {
    /* widget got destroyed while we were away, throw away the results */
    glw_unref(&gtb->w);
    if(pm != NULL)
      pixmap_release(pm);
    return;
  }

  glw_unref(&gtb->w);

  if(gtb->w.glw_flags2 & GLW2_DEBUG && pm != NULL)
    printf("   pm = %d x %d (m=%d)\n", pm->pm_width, pm->pm_height, pm->pm_margin);

  if(gtb->gtb_state == GTB_RENDERING) {
    gtb->gtb_state = GTB_VALID;
    if(gtb->gtb_pixmap != NULL)
      pixmap_release(gtb->gtb_pixmap);
    gtb->gtb_pixmap = pm;
    if(pm != NULL && gtb->gtb_maxlines > 1) {
      gtb_set_constraints(gr, gtb, pm);
    }
  }

  if(gtb->gtb_state == GTB_DIMENSIONING) {
    gtb->gtb_state = GTB_NEED_RENDER;
    if(pm != NULL) {
      gtb_set_constraints(gr, gtb, pm);
      pixmap_release(pm);
    } else {
      int lh = (gtb->gtb_default_size ?: gr->gr_current_size) *
	gtb->gtb_size_scale;
      glw_set_constraints(&gtb->w, 0, lh, 0, GLW_CONSTRAINT_Y);
    }
  }
}



/**
 *
 */
static void *
font_render_thread(void *aux)
{
  glw_root_t *gr = aux;
  glw_text_bitmap_t *gtb;

  glw_lock(gr);

  while(gr->gr_font_thread_running) {
    
    if((gtb = TAILQ_FIRST(&gr->gr_gtb_dim_queue)) != NULL) {

      assert(gtb->gtb_state == GTB_QUEUED_FOR_DIMENSIONING);
      TAILQ_REMOVE(&gr->gr_gtb_dim_queue, gtb, gtb_workq_link);
      gtb->gtb_state = GTB_DIMENSIONING;
      do_render(gtb, gr, 1);
      continue;
    }


    if((gtb = TAILQ_FIRST(&gr->gr_gtb_render_queue)) != NULL) {

      assert(gtb->gtb_state == GTB_QUEUED_FOR_RENDERING);
      TAILQ_REMOVE(&gr->gr_gtb_render_queue, gtb, gtb_workq_link);
      gtb->gtb_state = GTB_RENDERING;
      do_render(gtb, gr, 0);
      continue;
    }
    glw_cond_wait(gr, &gr->gr_gtb_work_cond);
  }
  
  glw_unlock(gr);
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
    gtb_inactive(gtb);
    gtb_realize(gtb);
  }
}



/**
 *
 */
static void
gtb_notify(glw_text_bitmap_t *gtb)
{
  gtb_realize(gtb);

  if(gtb->gtb_p != NULL) {
    gtb_caption_refresh(gtb);
    prop_set_string_ex(gtb->gtb_p, gtb->gtb_sub, gtb->gtb_caption, 0);
  }
}


/**
 *
 */
static void
gtb_caption_refresh(glw_text_bitmap_t *gtb)
{
  int len = 0, i;
  char *s;

  if(gtb->gtb_caption_dirty == 0)
    return;

  for(i = 0; i < gtb->gtb_uc_len; i++)
    len += utf8_put(NULL, gtb->gtb_uc_buffer[i]);
  
  free(gtb->gtb_caption);
  gtb->gtb_caption = s = malloc(len + 1);
  for(i = 0; i < gtb->gtb_uc_len; i++)
    s += utf8_put(s, gtb->gtb_uc_buffer[i]);
  *s = 0;
  gtb->gtb_caption_dirty = 0;
}


/**
 *
 */
void
glw_text_bitmap_init(glw_root_t *gr)
{
  TAILQ_INIT(&gr->gr_gtb_dim_queue);
  TAILQ_INIT(&gr->gr_gtb_render_queue);

  hts_cond_init(&gr->gr_gtb_work_cond, &gr->gr_mutex);

  gr->gr_font_thread_running = 1;
  hts_thread_create_joinable("GLW font renderer", &gr->gr_font_thread,
			     font_render_thread, gr,
			     THREAD_PRIO_UI_WORKER_HIGH);
}


/**
 *
 */
void
glw_text_bitmap_fini(glw_root_t *gr)
{
  hts_mutex_lock(&gr->gr_mutex);
  gr->gr_font_thread_running = 0;
  hts_cond_signal(&gr->gr_gtb_work_cond);
  hts_mutex_unlock(&gr->gr_mutex);
  hts_thread_join(&gr->gr_font_thread);
  hts_cond_destroy(&gr->gr_gtb_work_cond);
}




/**
 *
 */
static const char *
glw_text_bitmap_get_text(glw_t *w)
{
  glw_text_bitmap_t *gtb = (glw_text_bitmap_t *)w;
  gtb_caption_refresh(gtb);
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
static void
update_text(glw_t *w, const char *str)
{
  glw_text_bitmap_t *gtb = (glw_text_bitmap_t *)w;
  if(gtb->gtb_p != NULL) {
    prop_set_string(gtb->gtb_p, str);
  } else {
    set_caption(w, str, 0);
  }
}



/**
 *
 */
static void
set_description(glw_t *w, const char *str)
{
  glw_text_bitmap_t *gtb = (glw_text_bitmap_t *)w;
  mystrset(&gtb->gtb_description, str);
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
  .gc_set_font = set_font,
  .gc_bind_to_property = bind_to_property,
  .gc_mod_flags2 = mod_flags2,
  .gc_freeze = freeze,
  .gc_thaw = thaw,
  .gc_set_size_scale = set_size_scale,
  .gc_set_default_size = set_default_size,
  .gc_set_min_size = set_min_size,
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
  .gc_set_font = set_font,
  .gc_bind_to_property = bind_to_property,
  .gc_freeze = freeze,
  .gc_thaw = thaw,
  .gc_set_size_scale = set_size_scale,
  .gc_set_default_size = set_default_size,
  .gc_set_min_size = set_min_size,
  .gc_set_max_lines = set_maxlines,
  .gc_update_text = update_text,
  .gc_set_desc = set_description,

};

GLW_REGISTER_CLASS(glw_text);
