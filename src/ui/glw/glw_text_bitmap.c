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
#include "image/image.h"
#include "fileaccess/fa_filepicker.h"
#include "ui/clipboard.h"

static glw_class_t glw_text;

/**
 *
 */
typedef struct glw_text_bitmap {
  struct glw w;

  TAILQ_ENTRY(glw_text_bitmap) gtb_workq_link;
  LIST_ENTRY(glw_text_bitmap) gtb_global_link;

  image_t *gtb_image;

  char *gtb_caption;
  rstr_t *gtb_font;
  prop_sub_t *gtb_sub;
  prop_t *gtb_p;
  char *gtb_description;

  prop_str_type_t gtb_caption_type;

  glw_backend_texture_t gtb_texture;

  glw_renderer_t gtb_text_renderer;
  glw_renderer_t gtb_cursor_renderer;
  glw_renderer_t gtb_background_renderer;


  uint32_t *gtb_uc_buffer; /* unicode buffer */
  float gtb_cursor_alpha;
  float gtb_background_alpha;
  float gtb_size_scale;
  glw_rgb_t gtb_color;
  glw_rgb_t gtb_background_color;

  int gtb_flags;


  enum {
    GTB_IDLE,
    GTB_QUEUED_FOR_DIMENSIONING,
    GTB_DIMENSIONING,
    GTB_NEED_RENDER,
    GTB_QUEUED_FOR_RENDERING,
    GTB_RENDERING,
    GTB_VALID
  } gtb_state;

  int16_t gtb_saved_width;
  int16_t gtb_saved_height;

  int16_t gtb_edit_ptr;

  int16_t gtb_padding[4];

  int16_t gtb_uc_len;
  int16_t gtb_uc_size;
  int16_t gtb_maxlines;
  int16_t gtb_default_size;
  int16_t gtb_max_width;

  int16_t gtb_margin;

  uint8_t gtb_pending_updates;
#define GTB_UPDATE_REALIZE      2

  uint8_t gtb_frozen : 1;
  uint8_t gtb_paint_cursor : 1;
  uint8_t gtb_update_cursor : 1;
  uint8_t gtb_need_layout : 1;
  uint8_t gtb_deferred_realize : 1;
  uint8_t gtb_caption_dirty : 1;

} glw_text_bitmap_t;

static void gtb_notify(glw_text_bitmap_t *gtb);
static void gtb_realize(glw_text_bitmap_t *gtb);
static void gtb_caption_refresh(glw_text_bitmap_t *gtb);

static glw_class_t glw_text, glw_label;


/**
 *
 */
static void
glw_text_bitmap_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_text_bitmap_t *gtb = (void *)w;
  glw_root_t *gr = w->glw_root;

  gr->gr_can_externalize = 0;

  // Initialize renderers

  if(unlikely(!glw_renderer_initialized(&gtb->gtb_text_renderer)))
    glw_renderer_init_quad(&gtb->gtb_text_renderer);

  if(w->glw_class == &glw_text &&
     unlikely(!glw_renderer_initialized(&gtb->gtb_cursor_renderer)))
    glw_renderer_init_quad(&gtb->gtb_cursor_renderer);

  if(gtb->gtb_background_alpha > GLW_ALPHA_EPSILON &&
     unlikely(!glw_renderer_initialized(&gtb->gtb_cursor_renderer))) {
    glw_renderer_init_quad(&gtb->gtb_background_renderer);
    glw_renderer_vtx_pos(&gtb->gtb_background_renderer, 0, -1, -1, 0);
    glw_renderer_vtx_pos(&gtb->gtb_background_renderer, 1,  1, -1, 0);
    glw_renderer_vtx_pos(&gtb->gtb_background_renderer, 2,  1,  1, 0);
    glw_renderer_vtx_pos(&gtb->gtb_background_renderer, 3, -1,  1, 0);
  }

  // Upload texture

  image_component_t *ic = image_find_component(gtb->gtb_image, IMAGE_PIXMAP);
  if(ic != NULL) {
    glw_tex_upload(gr, &gtb->gtb_texture, ic->pm, 0);
    gtb->gtb_margin = ic->pm->pm_margin;
    image_clear_component(ic);
    gtb->gtb_need_layout = 1;
  }

  const int tex_width  = glw_tex_width(&gtb->gtb_texture);
  const int tex_height = glw_tex_height(&gtb->gtb_texture);

  ic = image_find_component(gtb->gtb_image, IMAGE_TEXT_INFO);
  image_component_text_info_t *ti = ic ? &ic->text_info : NULL;

  // Check if we need to repaint

  if(gtb->gtb_saved_width  != rc->rc_width ||
     gtb->gtb_saved_height != rc->rc_height) {

    if(ti != NULL && gtb->gtb_state == GTB_VALID) {

      if(ti->ti_flags & IMAGE_TEXT_WRAPPED)
	gtb->gtb_state = GTB_NEED_RENDER;

      if(rc->rc_width > gtb->gtb_saved_width &&
	 ti->ti_flags & IMAGE_TEXT_TRUNCATED)
	gtb->gtb_state = GTB_NEED_RENDER;

      if(gtb->gtb_flags & GTB_ELLIPSIZE) {

	if(ti->ti_flags & IMAGE_TEXT_TRUNCATED) {
	  gtb->gtb_state = GTB_NEED_RENDER;
	} else {

	  if(rc->rc_width - gtb->gtb_padding[2] - gtb->gtb_padding[0] <
	     tex_width - gtb->gtb_margin * 2)
	    gtb->gtb_state = GTB_NEED_RENDER;

	  if(rc->rc_height - gtb->gtb_padding[1] - gtb->gtb_padding[3] <
	     tex_height - gtb->gtb_margin * 2)
	    gtb->gtb_state = GTB_NEED_RENDER;
	}
      }
    }

    gtb->gtb_saved_width  = rc->rc_width;
    gtb->gtb_saved_height = rc->rc_height;
    gtb->gtb_update_cursor = 1;
    gtb->gtb_need_layout = 1;

    if(gtb->w.glw_flags2 & GLW2_DEBUG)
      printf("  textbitmap: Parent widget gives us :%d x %d ti=%p\n",
             rc->rc_width, rc->rc_height, ti);

  }

  if(ti != NULL && gtb->gtb_need_layout) {

    const int margin = gtb->gtb_margin;

    int left   =                 gtb->gtb_padding[0] - margin;
    int top    = rc->rc_height - gtb->gtb_padding[1] + margin;
    int right  = rc->rc_width  - gtb->gtb_padding[2] + margin;
    int bottom =                 gtb->gtb_padding[3] - margin;

    int text_width  = tex_width;
    int text_height = tex_height;

    float x1, y1, x2, y2;

    if(gtb->w.glw_flags2 & GLW2_DEBUG)
      printf("  textbitmap: text_width:%d left:%d right:%d margin:%d\n",
             text_width, left, right, margin);

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
	bottom = top - tex_height;
	break;

      case LAYOUT_ALIGN_BOTTOM:
      case LAYOUT_ALIGN_BOTTOM_LEFT:
      case LAYOUT_ALIGN_BOTTOM_RIGHT:
	top = bottom + tex_height;
	break;
      }
    }

    y1 = -1.0f + 2.0f * bottom / (float)rc->rc_height;
    y2 = -1.0f + 2.0f * top    / (float)rc->rc_height;



    // Horizontal
    if(text_width > right - left || ti->ti_flags & IMAGE_TEXT_TRUNCATED) {

      // Oversized, must cut

      text_width = right - left;

      if(gtb->w.glw_flags2 & GLW2_DEBUG)
        printf("  textbitmap: Oversized, must cut. Width is now %d\n",
               text_width);
#if 0
      if(!(gtb->gtb_flags & GTB_ELLIPSIZE)) {
	glw_renderer_vtx_col(&gtb->gtb_text_renderer, 0, 1,1,1,1+text_width/20);
	glw_renderer_vtx_col(&gtb->gtb_text_renderer, 1, 1,1,1,0);
	glw_renderer_vtx_col(&gtb->gtb_text_renderer, 2, 1,1,1,0);
	glw_renderer_vtx_col(&gtb->gtb_text_renderer, 3, 1,1,1,1+text_width/20);
      }
#endif
    } else {

      glw_renderer_vtx_col_reset(&gtb->gtb_text_renderer);

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
	right = left + tex_width;
	break;

      case LAYOUT_ALIGN_RIGHT:
      case LAYOUT_ALIGN_TOP_RIGHT:
      case LAYOUT_ALIGN_BOTTOM_RIGHT:
	left = right - tex_width;
	break;
      }
    }


    x1 = -1.0f + 2.0f * left   / (float)rc->rc_width;
    x2 = -1.0f + 2.0f * right  / (float)rc->rc_width;


    const float s = text_width  / (float)tex_width;
    const float t = text_height / (float)tex_height;

    if(gtb->w.glw_flags2 & GLW2_DEBUG)
      printf("  s=%f t=%f\n", s, t);

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
    int left;
    float x1, y1, x2, y2;

    if(ti != NULL && ti->ti_charpos != NULL) {

      if(i < ti->ti_charposlen) {
	left  = ti->ti_charpos[i*2  ];
      } else {
	left  = ti->ti_charpos[2 * ti->ti_charposlen - 1];
      }

    } else {

      left = 0;
    }

    left  += gtb->gtb_padding[0];

    x1 = -1.0f + 2.0f * (left - 1)  / (float)rc->rc_width;
    x2 = -1.0f + 2.0f * (left    )  / (float)rc->rc_width;
    y1 = -1.0f + 2.0f * gtb->gtb_padding[3] / (float)rc->rc_height;
    y2 =  1.0f - 2.0f * gtb->gtb_padding[1] / (float)rc->rc_height;

    glw_renderer_vtx_pos(&gtb->gtb_cursor_renderer, 0, x1, y1, 0.0);
    glw_renderer_vtx_pos(&gtb->gtb_cursor_renderer, 1, x2, y1, 0.0);
    glw_renderer_vtx_pos(&gtb->gtb_cursor_renderer, 2, x2, y2, 0.0);
    glw_renderer_vtx_pos(&gtb->gtb_cursor_renderer, 3, x1, y2, 0.0);

    if(w->glw_flags2 & GLW2_DEBUG) {
      printf("Cursor updated %f %f %f %f  rect:%d,%d\n",
             x1, y1, x2, y2, rc->rc_width, rc->rc_height);
    }

    gtb->gtb_update_cursor = 0;
  }

  gtb->gtb_paint_cursor =
    gtb->gtb_flags & GTB_PERMANENT_CURSOR ||
    (w->glw_class == &glw_text && glw_is_focused(w));

  if(gtb->gtb_paint_cursor && rc->rc_alpha > GLW_ALPHA_EPSILON)
    glw_need_refresh(gr, 0);

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
  float alpha;
  float blur = 1 - (rc->rc_sharpness * w->glw_sharpness);
  glw_rctx_t rc0 = *rc;

  if(glw_is_focusable_or_clickable(w))
    glw_store_matrix(w, rc);

  alpha = rc->rc_alpha * w->glw_alpha;

  if(alpha < GLW_ALPHA_EPSILON)
    return;

  if(gtb->gtb_background_alpha > GLW_ALPHA_EPSILON) {
    glw_renderer_draw(&gtb->gtb_background_renderer, w->glw_root, &rc0,
		      NULL, NULL,
		      &gtb->gtb_background_color, NULL,
                      gtb->gtb_background_alpha * alpha, blur, NULL);
    glw_zinc(&rc0);
  }

  if(glw_is_tex_inited(&gtb->gtb_texture) && gtb->gtb_image != NULL) {
    glw_renderer_draw(&gtb->gtb_text_renderer, w->glw_root, &rc0,
		      &gtb->gtb_texture, NULL,
		      &gtb->gtb_color, NULL, alpha, blur, NULL);
  }

  if(gtb->gtb_paint_cursor) {
    glw_root_t *gr = w->glw_root;
    float a = cos((gr->gr_frames & 2047) * (360.0f / 2048.0f)) * 0.5f + 0.5f;

    glw_zinc(&rc0);

    glw_renderer_draw(&gtb->gtb_cursor_renderer, w->glw_root, &rc0,
		      NULL, NULL, NULL, NULL, alpha * a, blur, NULL);
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

  image_release(gtb->gtb_image);

  LIST_REMOVE(gtb, gtb_global_link);

  glw_tex_destroy(w->glw_root, &gtb->gtb_texture);

  glw_renderer_free(&gtb->gtb_text_renderer);
  glw_renderer_free(&gtb->gtb_cursor_renderer);
  glw_renderer_free(&gtb->gtb_background_renderer);

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
		    const image_t *im)
{
  int flags = GLW_CONSTRAINT_Y;

  const int xs = im->im_width - im->im_margin * 2 +
    gtb->gtb_padding[0] + gtb->gtb_padding[2];

  const int ys = im->im_height - im->im_margin * 2 +
    gtb->gtb_padding[1] + gtb->gtb_padding[3];

  if(gtb->gtb_maxlines == 1 && !(gtb->gtb_flags & GTB_ELLIPSIZE))
    flags |= GLW_CONSTRAINT_X;

  if(gtb->w.glw_flags2 & GLW2_DEBUG)
    printf("Textbitmap constraints %c%c %d,%d\n",
	   flags & GLW_CONSTRAINT_X ? 'X' : ' ',
	   flags & GLW_CONSTRAINT_Y ? 'Y' : ' ',
	   xs, ys);

  glw_set_constraints(&gtb->w, xs, ys, 0, flags);
}


static void
gtb_recompute_constraints_after_padding_change(glw_text_bitmap_t *gtb,
                                               const int16_t oldpad[4])
{
  glw_t *w = &gtb->w;
  int flags = w->glw_flags & (GLW_CONSTRAINT_Y | GLW_CONSTRAINT_X);
  if(flags == 0)
    return;

  const int xs = w->glw_req_size_x - oldpad[0] - oldpad[2] +
    gtb->gtb_padding[0] + gtb->gtb_padding[2];

  const int ys = w->glw_req_size_y - oldpad[1] - oldpad[3] +
    gtb->gtb_padding[1] + gtb->gtb_padding[3];

  if(gtb->w.glw_flags2 & GLW2_DEBUG)
    printf("Constraints %c%c %d,%d (padding changed)\n",
	   flags & GLW_CONSTRAINT_X ? 'X' : ' ',
	   flags & GLW_CONSTRAINT_Y ? 'Y' : ' ',
	   xs, ys);

  glw_set_constraints(w, xs, ys, 0, flags);
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
  gtb->gtb_sub = NULL;

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
  glw_text_bitmap_t *gtb = (glw_text_bitmap_t *)w;

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_DESTROY:
    gtb_unbind(gtb);
    break;
  case GLW_SIGNAL_INACTIVE:
    gtb_inactive(gtb);
    break;
  }
  return 0;
}


static void
insert_str(glw_text_bitmap_t *gtb, const char *str)
{
  int uc;
  while((uc = utf8_get(&str)) != 0) {
    insert_char(gtb, uc);
  }
  gtb_notify(gtb);
}


/**
 *
 */
static int
glw_text_bitmap_event(glw_t *w, event_t *e)
{
  glw_text_bitmap_t *gtb = (glw_text_bitmap_t *)w;

  if(event_is_action(e, ACTION_BS)) {

    del_char(gtb);
    gtb_notify(gtb);
    return 1;

  } else if(event_is_type(e, EVENT_UNICODE)) {

    event_int_t *eu = (event_int_t *)e;

    if(insert_char(gtb, eu->val))
      gtb_notify(gtb);
    return 1;

  } else if(event_is_type(e, EVENT_INSERT_STRING)) {
    event_payload_t *ep = (event_payload_t *)e;
    insert_str(gtb, ep->payload);
    return 1;

  } else if(event_is_action(e, ACTION_PASTE)) {

    rstr_t *str = clipboard_get();
    if(str != NULL)
      insert_str(gtb, rstr_get(str));
    rstr_release(str);
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

  } else if(event_is_action(e, ACTION_ACTIVATE)) {
    
    gtb_caption_refresh(gtb);

    if(gtb->gtb_flags & (GTB_FILE_REQUEST | GTB_DIR_REQUEST)) {

      if(gtb->gtb_p == NULL) {
        TRACE(TRACE_ERROR, "GLW",
              "File requests on unbound widgets is not supported");
      } else {

        int flags =
          (gtb->gtb_flags & GTB_FILE_REQUEST ? FILEPICKER_FILES : 0) |
          (gtb->gtb_flags & GTB_DIR_REQUEST  ? FILEPICKER_DIRECTORIES : 0);

        filepicker_pick_to_prop(gtb->gtb_description,
                                gtb->gtb_p, gtb->gtb_caption,
                                flags);
      }

    } else {

      if(event_is_action(e, ACTION_ACTIVATE) && e->e_flags & EVENT_MOUSE)
        return 1;

      w->glw_root->gr_open_osk(w->glw_root,
                               gtb->gtb_description,
                               gtb->gtb_caption, w,
                               gtb->gtb_flags & GTB_PASSWORD);
    }
    return 1;
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

  if(gtb->gtb_state != GTB_IDLE && gtb->gtb_state != GTB_VALID) {
    gtb->gtb_deferred_realize = 1;
    return;
  }

  if(direct) {
    gtb->gtb_state = GTB_NEED_RENDER;
    glw_need_refresh(gr, 0);
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
     type == gtb->gtb_caption_type)
    return;

  free(gtb->gtb_caption);
  gtb->gtb_caption = strdup(str ?: "");
  gtb->gtb_caption_type = type;

  assert(gtb->gtb_caption_type == 0 || gtb->gtb_caption_type == 1);

  if(gtb->w.glw_flags2 & GLW2_AUTOHIDE) {
    if(str == NULL || *str == 0)
      glw_hide(&gtb->w);
    else
      glw_unhide(&gtb->w);
  }

  if(gtb->gtb_caption_type == PROP_STR_RICH)
    flags |= TEXT_PARSE_HTML_TAGS | TEXT_PARSE_HTML_ENTITIES;

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
  prop_str_type_t type = 0;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_SET_VOID:
    caption = NULL;
    break;

  case PROP_SET_RSTRING:
    caption = rstr_get(va_arg(ap, const rstr_t *));
    type = va_arg(ap, prop_str_type_t);
    break;

  case PROP_VALUE_PROP:
    prop_ref_dec(gtb->gtb_p);
    gtb->gtb_p = prop_ref_inc(va_arg(ap, prop_t *));
    return;

  default:
    return;
  }


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
  gtb->gtb_edit_ptr = 0;
  gtb->gtb_size_scale = 1.0;
  gtb->gtb_color.r = 1.0;
  gtb->gtb_color.g = 1.0;
  gtb->gtb_color.b = 1.0;
  gtb->gtb_maxlines = 1;
  gtb->gtb_update_cursor = 1;
  LIST_INSERT_HEAD(&gr->gr_gtbs, gtb, gtb_global_link);
}


/**
 *
 */
static int
glw_text_bitmap_set_float3(glw_t *w, glw_attribute_t attrib, const float *rgb,
                           glw_style_t *origin)
{
  glw_text_bitmap_t *gtb = (glw_text_bitmap_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_RGB:
    return glw_attrib_set_rgb(&gtb->gtb_color, rgb);
  case GLW_ATTRIB_BACKGROUND_COLOR:
    return glw_attrib_set_rgb(&gtb->gtb_background_color, rgb);
  default:
    return -1;
  }
}


/**
 *
 */
static int
gtb_set_int16_4(glw_t *w, glw_attribute_t attrib, const int16_t *v,
                glw_style_t *origin)
{
  glw_text_bitmap_t *gtb = (glw_text_bitmap_t *)w;
  int16_t oldpad[4];
  switch(attrib) {
  case GLW_ATTRIB_PADDING:
    memcpy(oldpad, gtb->gtb_padding, 4 * sizeof(int16_t));
    if(!glw_attrib_set_int16_4(gtb->gtb_padding, v))
      return 0;

    gtb_recompute_constraints_after_padding_change(gtb, oldpad);
    gtb->gtb_need_layout = 1;
    return 1;
  default:
    return -1;
  }
}

/**
 *
 */
static void
mod_text_flags(glw_t *w, int set, int clr, glw_style_t *origin)
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
static int
gtb_set_rstr(glw_t *w, glw_attribute_t a, rstr_t *str, glw_style_t *origin)
{
  glw_text_bitmap_t *gtb = (glw_text_bitmap_t *)w;
  switch(a) {
  case GLW_ATTRIB_FONT:
    if(rstr_eq(gtb->gtb_font, str))
      return 0;

    rstr_set(&gtb->gtb_font, str);
    break;
  default:
    return -1;
  }

  gtb_update_epilogue(gtb, GTB_UPDATE_REALIZE);
  return 1;
}


/**
 *
 */
static void
bind_to_property(glw_t *w, glw_scope_t *scope, const char **pname)
{
  glw_text_bitmap_t *gtb = (void *)w;
  gtb_unbind(gtb);

  gtb->gtb_sub =
    prop_subscribe(PROP_SUB_DIRECT_UPDATE | PROP_SUB_SEND_VALUE_PROP,
		   PROP_TAG_NAME_VECTOR, pname,
		   PROP_TAG_CALLBACK, prop_callback, gtb,
		   PROP_TAG_COURIER, w->glw_root->gr_courier,
                   PROP_TAG_ROOT_VECTOR,
                   scope->gs_roots, scope->gs_num_roots,
		   PROP_TAG_ROOT, w->glw_root->gr_prop_ui,
		   PROP_TAG_NAMED_ROOT, w->glw_root->gr_prop_nav, "nav",
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
    int ys = gtb->gtb_padding[1] + gtb->gtb_padding[3] + lh;
    glw_set_constraints(&gtb->w, 0, ys, 0, GLW_CONSTRAINT_Y);
  }

  if(gtb->gtb_pending_updates & GTB_UPDATE_REALIZE)
    gtb_realize(gtb);

  gtb->gtb_pending_updates = 0;
}


/**
 *
 */
static int
gtb_set_em(glw_t *w, glw_attribute_t a, float v)
{
  glw_text_bitmap_t *gtb = (void *)w;

  switch(a) {

  case GLW_ATTRIB_SIZE:
    if(gtb->gtb_size_scale == v)
      return 0;

    gtb->gtb_size_scale = v;
    break;

  default:
    return -1;
  }
  gtb_update_epilogue(gtb, GTB_UPDATE_REALIZE);
  return 1;
}


/**
 *
 */
static int
gtb_set_float(glw_t *w, glw_attribute_t a, float v, glw_style_t *origin)
{
  glw_text_bitmap_t *gtb = (void *)w;

  switch(a) {

  case GLW_ATTRIB_SIZE_SCALE:
    return gtb_set_em(w, GLW_ATTRIB_SIZE, v);

  case GLW_ATTRIB_BACKGROUND_ALPHA:
    if(gtb->gtb_background_alpha == v)
      return 0;
    gtb->gtb_background_alpha = v;
    return 1;

  default:
    return -1;
  }
  gtb_update_epilogue(gtb, GTB_UPDATE_REALIZE);
  return 1;
}


/**
 *
 */
static int
gtb_set_int(glw_t *w, glw_attribute_t a, int v, glw_style_t *origin)
{
  glw_text_bitmap_t *gtb = (void *)w;

  switch(a) {
  case GLW_ATTRIB_SIZE:
    if(gtb->gtb_default_size == v)
      return 0;

    gtb->gtb_default_size = v;
    break;

  case GLW_ATTRIB_MAX_WIDTH:
    if(gtb->gtb_max_width == v)
      return 0;

    gtb->gtb_max_width = v;
    break;

  case GLW_ATTRIB_MAX_LINES:
    if(gtb->gtb_maxlines == v)
      return 0;

    gtb->gtb_maxlines = v;
    break;

  default:
    return -1;
  }
  gtb_update_epilogue(gtb, GTB_UPDATE_REALIZE);
  return 1;
}


/**
 *
 */
static void
do_render(glw_text_bitmap_t *gtb, glw_root_t *gr, int no_output)
{
  int i;
  uint32_t *uc, len;
  image_t *im;
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
  min_size = 0;

  flags = 0;

  if(no_output) {
    max_width = MIN(gtb->gtb_max_width, gr->gr_width);
    flags |= TR_RENDER_NO_OUTPUT;
  } else {
    max_width =
      MAX(gtb->gtb_max_width, gtb->gtb_saved_width) -
      gtb->gtb_padding[0] - gtb->gtb_padding[2];
  }
  if(gtb->w.glw_flags2 & GLW2_DEBUG)
    printf("  Max_width=%d\n", max_width);

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

  if(gtb->w.glw_class == &glw_text)
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
    printf("  Font is %s\n", rstr_get(font));

  /* gtb (i.e the widget) may be destroyed directly after we unlock,
     so we can't access it after this point. We can hold a reference
     though. But it will only guarantee that the pointer stays valid */

  glw_ref(&gtb->w);
  glw_unlock(gr);

  if(uc != NULL && uc[0] != 0) {
    im = text_render(uc, len, flags, default_size, scale,
		     tr_align, max_width, max_lines, rstr_get(font),
		     gr->gr_font_domain, min_size);
  } else {
    im = NULL;
  }

  rstr_release(font);
  free(uc);
  glw_lock(gr);


  if(gtb->w.glw_flags & GLW_DESTROYING) {
    /* widget got destroyed while we were away, throw away the results */
    glw_unref(&gtb->w);
    image_release(im);
    return;
  }

  glw_unref(&gtb->w);

  glw_need_refresh(gr, 0);

  if(gtb->w.glw_flags2 & GLW2_DEBUG && im != NULL)
    printf("  Returned image is %d x %d margin:%d\n",
           im->im_width, im->im_height, im->im_margin);

  if(gtb->gtb_state == GTB_RENDERING) {
    gtb->gtb_state = GTB_VALID;
    image_release(gtb->gtb_image);
    gtb->gtb_image = im;
    if(im != NULL && gtb->gtb_maxlines > 1) {
      gtb_set_constraints(gr, gtb, im);
    }
  }

  if(gtb->gtb_state == GTB_DIMENSIONING) {
    gtb->gtb_state = GTB_NEED_RENDER;
    if(im != NULL) {
      gtb_set_constraints(gr, gtb, im);
      image_release(im);
    } else {
      int lh = (gtb->gtb_default_size ?: gr->gr_current_size) *
	gtb->gtb_size_scale;
      lh += gtb->gtb_padding[1] + gtb->gtb_padding[3];
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

  if(!gtb->gtb_caption_dirty)
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
  .gc_layout = glw_text_bitmap_layout,
  .gc_render = glw_text_bitmap_render,
  .gc_ctor = glw_text_bitmap_ctor,
  .gc_dtor = glw_text_bitmap_dtor,
  .gc_signal_handler = glw_text_bitmap_callback,
  .gc_get_text = glw_text_bitmap_get_text,
  .gc_default_alignment = LAYOUT_ALIGN_LEFT,
  .gc_set_float3 = glw_text_bitmap_set_float3,
  .gc_set_int16_4 = gtb_set_int16_4,
  .gc_mod_text_flags = mod_text_flags,
  .gc_set_caption = set_caption,
  .gc_set_rstr = gtb_set_rstr,
  .gc_bind_to_property = bind_to_property,
  .gc_mod_flags2 = mod_flags2,
  .gc_freeze = freeze,
  .gc_thaw = thaw,
  .gc_set_float = gtb_set_float,
  .gc_set_em    = gtb_set_em,
  .gc_set_int   = gtb_set_int,
};

GLW_REGISTER_CLASS(glw_label);


/**
 *
 */
static glw_class_t glw_text = {
  .gc_name = "text",
  .gc_instance_size = sizeof(glw_text_bitmap_t),
  .gc_layout = glw_text_bitmap_layout,
  .gc_render = glw_text_bitmap_render,
  .gc_ctor = glw_text_bitmap_ctor,
  .gc_dtor = glw_text_bitmap_dtor,
  .gc_signal_handler = glw_text_bitmap_callback,
  .gc_get_text = glw_text_bitmap_get_text,
  .gc_default_alignment = LAYOUT_ALIGN_LEFT,
  .gc_set_float3 = glw_text_bitmap_set_float3,
  .gc_set_int16_4 = gtb_set_int16_4,
  .gc_mod_text_flags = mod_text_flags,
  .gc_set_caption = set_caption,
  .gc_set_rstr = gtb_set_rstr,
  .gc_bind_to_property = bind_to_property,
  .gc_freeze = freeze,
  .gc_thaw = thaw,

  .gc_set_float = gtb_set_float,
  .gc_set_em    = gtb_set_em,
  .gc_set_int   = gtb_set_int,

  .gc_update_text = update_text,
  .gc_set_desc = set_description,
  .gc_bubble_event = glw_text_bitmap_event,

};

GLW_REGISTER_CLASS(glw_text);
