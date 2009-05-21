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

#include <libavutil/common.h>

#include "glw.h"
#include "glw_texture.h"
#include "glw_text_bitmap.h"
#include "glw_container.h"
#include "fileaccess/fa_rawloader.h"



static int glw_text_getutf8(const char **s);

static void gtb_notify(glw_text_bitmap_t *gtb);


static FT_Library glw_text_library;

typedef struct glyph {
  FT_Glyph glyph;
  FT_Vector pos;
} glyph_t;


static void
draw_glyph(glw_text_bitmap_data_t *gtbd, FT_Bitmap *bmp, uint8_t *dst, 
	   int left, int top, int index, int stride)
{
  uint8_t *src = bmp->buffer;
  int x, y;
  int w, h;
  
  int x1, y1, x2, y2;

  x1 = GLW_MAX(0, left);
  x2 = GLW_MIN(left + bmp->width, gtbd->gtbd_siz_x - 1);
  y1 = GLW_MAX(0, top);
  y2 = GLW_MIN(top + bmp->rows, gtbd->gtbd_siz_y - 1);

  if(gtbd->gtbd_cursor_pos != NULL) {
    gtbd->gtbd_cursor_pos[index * 2 + 0] = x1;
    gtbd->gtbd_cursor_pos[index * 2 + 1] = x2;
  }

  w = GLW_MIN(x2 - x1, bmp->width);
  h = GLW_MIN(y2 - y1, bmp->rows);

  if(w < 0 || h < 0)
    return;

  dst += x1 + y1 * stride;

  for(y = 0; y < h; y++) {
    for(x = 0; x < w; x++) {
      dst[x] += src[x];
    }
    src += bmp->pitch;
    dst += stride;
  }
}

static void
paint_shadow(uint8_t *dst, uint8_t *src, int w, int h)
{
  int x, y, s, v;

  for(x = 0; x < w; x++) {
    v = *src++;
    *dst++ = v;
    *dst++ = v;
  }

  for(y = 1; y < h; y++) {
    v = *src++;
    *dst++ = v;
    *dst++ = v;
    for(x = 1; x < w; x++) {
      v = src[0];
      s = src[-1-w];
      src++;
      *dst++ = v;
      *dst++ = GLW_MIN(s + v, 255);
    }
  }
}


static int
gtb_make_tex(glw_root_t *gr, glw_text_bitmap_data_t *gtbd, FT_Face face, 
	     const int *uc, const int len, int flags, int docur, float size)
{
  FT_GlyphSlot slot = face->glyph;
  FT_Bool use_kerning = FT_HAS_KERNING( face );
  FT_UInt gi, prev = 0;
  FT_BBox bbox, glyph_bbox;
  FT_Vector pen, delta;
  int err;
  int pen_x = 0, pen_y = 0;

  int c, i, d, e, h;
  glyph_t *g0, *g;
  int siz_x, siz_y, start_x, start_y;
  int target_width, target_height;
  uint8_t *data;
  int shadow = 1;
  int origin_y;
  int pixelheight = gr->gr_fontsize * size;

  g0 = alloca(sizeof(glyph_t) * len);

  FT_Set_Pixel_Sizes(face, 0, pixelheight);

  /* Compute position for each glyph */

  h = 64 * face->height * pixelheight / 2048;

  for(i = 0; i < len; i++) {

    if(uc[i] == '\n') {
      prev = 0;
      pen_x = 0;
      pen_y -= h;
      continue;
    }

    gi = FT_Get_Char_Index(face, uc[i]);

    if(use_kerning && gi && prev) {
      FT_Get_Kerning(face, prev, gi, FT_KERNING_DEFAULT, &delta); 
      pen_x += delta.x;
    }
    
    g = g0 + i;

    g->pos.x = pen_x;
    g->pos.y = pen_y;
    g->glyph = NULL;

    if((err = FT_Load_Glyph(face, gi, FT_LOAD_DEFAULT)) != 0)
      continue;

    if((err = FT_Get_Glyph(face->glyph, &g->glyph)) != 0)
      continue;

    pen_x += slot->advance.x;
    prev = gi;
  }


  /* Compute string bounding box */

  bbox.xMin = bbox.yMin = 32000;
  bbox.xMax = bbox.yMax = -32000;

  bbox.yMin = 62.2 * face->descender * pixelheight / 2048;
  bbox.yMax = 62.2 * face->ascender  * pixelheight / 2048;


  for(i = 0; i < len; i++) {
    if(uc[i] == '\n')
      continue;

    g = g0 + i;

    FT_Glyph_Get_CBox(g->glyph, FT_GLYPH_BBOX_UNSCALED, &glyph_bbox);
 
    glyph_bbox.xMin += g->pos.x;
    glyph_bbox.xMax += g->pos.x;
    glyph_bbox.yMin += g->pos.y;
    glyph_bbox.yMax += g->pos.y;

    bbox.xMin = GLW_MIN(glyph_bbox.xMin, bbox.xMin);
    bbox.xMax = GLW_MAX(glyph_bbox.xMax, bbox.xMax);
    bbox.yMin = GLW_MIN(glyph_bbox.yMin, bbox.yMin);
    bbox.yMax = GLW_MAX(glyph_bbox.yMax, bbox.yMax);
  }

  /* compute string dimensions in 62.2 cartesian pixels */

  siz_x = bbox.xMax - bbox.xMin;
  if(siz_x < 5)
    return -1;

  siz_y = bbox.yMax - bbox.yMin;

  target_width  = (siz_x / 62.2) + 3;
  target_height = (siz_y / 62.2) + 1;

  origin_y = -bbox.yMin / 62.2;

  if(glw_can_tnpo2(gr)) {
    gtbd->gtbd_linewidth = target_width;
  } else {
    gtbd->gtbd_linewidth = 1 << (av_log2(target_width) + 1);
  }

  gtbd->gtbd_texsize = (double)target_width / (double)gtbd->gtbd_linewidth;

  start_x = 0;
  start_y = 0;

  /* Allocate drawing area */

  data = calloc(1, gtbd->gtbd_linewidth * target_height);
  gtbd->gtbd_siz_x = target_width;
  gtbd->gtbd_siz_y = target_height;
  gtbd->gtbd_aspect = (float)target_width / (float)target_height;


  if(docur) {
    gtbd->gtbd_cursor_pos = malloc(2 * (1 + len) * sizeof(int));
    gtbd->gtbd_cursor_pos_size = len;
  } else {
    gtbd->gtbd_cursor_pos = NULL;
  }

  for(i = 0; i < len; i++) {
    if(uc[i] == '\n')
      continue;

    g = g0 + i;

    pen.x = start_x + g->pos.x;
    pen.y = start_y + g->pos.y;

    err = FT_Glyph_To_Bitmap(&g->glyph, FT_RENDER_MODE_NORMAL, &pen, 1);
    if(err == 0) {
      FT_BitmapGlyph bit = (FT_BitmapGlyph)g->glyph;
      
      draw_glyph(gtbd, &bit->bitmap, data, 
		 bit->left, target_height - 1 - origin_y - bit->top, 
		 i, gtbd->gtbd_linewidth);
    }
    FT_Done_Glyph(g->glyph); 
  }

  if(docur) {
    gtbd->gtbd_cursor_pos[2 * len] = gtbd->gtbd_cursor_pos[2 * (len - 1) + 1];

    if(gtbd->gtbd_cursor_pos[2 * len] == 0) {
      gtbd->gtbd_cursor_pos[2 * len] = target_width - 5;
      gtbd->gtbd_cursor_pos[2 * len + 1] = target_width;
    } else {
      i = target_width - gtbd->gtbd_cursor_pos[2 * len];
      
      if(i > 5)
	i = 5;
      gtbd->gtbd_cursor_pos[2 * len + 1] = gtbd->gtbd_cursor_pos[2 * len] + i;
    }
    gtbd->gtbd_cursor_scale = target_width;

    for(i = 0; i < len; i++) {
      if(gtbd->gtbd_cursor_pos[2 * i] == 0) {
	c = i;
	if(c == 0)
	  start_x = 0;
	else
	  start_x = gtbd->gtbd_cursor_pos[2 * c - 1];
	
	e = 1;
	while(1) {
	  i++;
	  if(i == len || gtbd->gtbd_cursor_pos[2 * i])
	    break;
	  e+= 2;
	}

	if(i == len || e == 0)
	  break;

	d = gtbd->gtbd_cursor_pos[2 * i] - start_x;
	d = d / e;
	e = start_x;
	for(;c < i; c++) {
	  gtbd->gtbd_cursor_pos[c*2 + 0] = e;
	  e += d;
	  gtbd->gtbd_cursor_pos[c*2 + 1] = e;
	  e += d;
	}

      }
    }
  }

  if(shadow) {

    gtbd->gtbd_data = calloc(1, 2 * gtbd->gtbd_linewidth * target_height);
    paint_shadow(gtbd->gtbd_data, data, 
		 gtbd->gtbd_linewidth, gtbd->gtbd_siz_y);
    free(data);
    gtbd->gtbd_pixel_format = GLW_TEXTURE_FORMAT_I8A8;

  } else {

    gtbd->gtbd_data = data;
    gtbd->gtbd_pixel_format = GLW_TEXTURE_FORMAT_I8;
  }
  return 0;
}



/*
 *
 */
static void
glw_text_bitmap_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_text_bitmap_t *gtb = (void *)w;
  glw_root_t *gr = w->glw_root;
  glw_text_bitmap_data_t *gtbd = &gtb->gtb_data;
  float x1, x2, n;
  int i;

  if(gtb->gtb_status == GTB_NEED_RERENDER) {
    TAILQ_INSERT_TAIL(&gr->gr_gtb_render_queue, gtb, gtb_workq_link);
    gtb->gtb_status = GTB_ON_QUEUE;
    hts_cond_signal(&gr->gr_gtb_render_cond);
    return;
  }

  if(!gtb->gtb_renderer_inited) {
    gtb->gtb_renderer_inited = 1;
    glw_render_init(&gtb->gtb_text_renderer,   4, GLW_RENDER_ATTRIBS_TEX);

    if(w->glw_class == GLW_TEXT)
      glw_render_init(&gtb->gtb_cursor_renderer, 4, GLW_RENDER_ATTRIBS_NONE);
  }

  if(gtbd->gtbd_data != NULL) {
    
    glw_render_set_pre(&gtb->gtb_text_renderer);

    glw_render_vtx_pos(&gtb->gtb_text_renderer, 0, -1.0, -1.0, 0.0);
    glw_render_vtx_st (&gtb->gtb_text_renderer, 0,  0.0,  1.0);

    glw_render_vtx_pos(&gtb->gtb_text_renderer, 1,  1.0, -1.0, 0.0);
    glw_render_vtx_st (&gtb->gtb_text_renderer, 1,  gtbd->gtbd_texsize,  1.0);

    glw_render_vtx_pos(&gtb->gtb_text_renderer, 2,  1.0,  1.0, 0.0);
    glw_render_vtx_st (&gtb->gtb_text_renderer, 2,  gtbd->gtbd_texsize,  0.0);

    glw_render_vtx_pos(&gtb->gtb_text_renderer, 3, -1.0,  1.0, 0.0);
    glw_render_vtx_st (&gtb->gtb_text_renderer, 3,  0.0,  0.0);

    glw_render_set_post(&gtb->gtb_text_renderer);

    glw_tex_upload(&gtb->gtb_texture, gtbd->gtbd_data, gtbd->gtbd_pixel_format,
		   gtbd->gtbd_linewidth, gtbd->gtbd_siz_y);

    free(gtbd->gtbd_data);
    gtbd->gtbd_data = NULL;

    gtb->gtb_siz_y  = gtbd->gtbd_siz_y;
    gtb->gtb_aspect = gtbd->gtbd_aspect;
  }


  if(w->glw_class != GLW_TEXT)
    return;

  /* Cursor handling */

  if(!glw_is_focused(w)) {
    gtb->gtb_paint_cursor = 0;
    return;
  }
  
  gtb->cursor_flash++;
  gtb->gtb_cursor_alpha = cos((float)gtb->cursor_flash / 10.0f) * 0.5 + 0.5;
 
  gtb->gtb_paint_cursor = 1;

  if(gtb->gtb_update_cursor) {

    gtb->gtb_update_cursor = 0;
    
    if(gtbd->gtbd_cursor_pos != NULL) {
      
      n = gtbd->gtbd_cursor_scale;
      
      i = gtb->gtb_edit_ptr;
      x1 = (float)gtbd->gtbd_cursor_pos[i*2  ] / n;
      x2 = (float)gtbd->gtbd_cursor_pos[i*2+1] / n;
      
      x1 = -1. + x1 * 2.;
      x2 = -1. + x2 * 2.;

    } else {
      
      x1 = 0.05;
      x2 = 0.5;
    }
    
    glw_render_set_pre(&gtb->gtb_cursor_renderer);
    glw_render_vtx_pos(&gtb->gtb_cursor_renderer, 0, x1, -0.9, 0.0);
    glw_render_vtx_pos(&gtb->gtb_cursor_renderer, 1, x2, -0.9, 0.0);
    glw_render_vtx_pos(&gtb->gtb_cursor_renderer, 2, x2,  0.9, 0.0);
    glw_render_vtx_pos(&gtb->gtb_cursor_renderer, 3, x1,  0.9, 0.0);
    
    glw_render_set_post(&gtb->gtb_cursor_renderer);
  }
}


/*
 *
 */
static void
glw_text_bitmap_render(glw_t *w, glw_rctx_t *rc)
{
  glw_text_bitmap_t *gtb = (void *)w;
  float alpha;
  glw_rctx_t rc0;

  alpha = rc->rc_alpha * w->glw_alpha;

  if(alpha < 0.01)
    return;
 
  rc0 = *rc;

  glw_PushMatrix(&rc0, rc);
  glw_align_1(&rc0, w->glw_alignment, GLW_ALIGN_LEFT);

  if(!glw_is_tex_inited(&gtb->gtb_texture) || gtb->gtb_aspect == 0) {

    glw_rescale(&rc0, 1.0);

    if(rc0.rc_size_y > gtb->gtb_siz_y) {
      float s = (float)gtb->gtb_siz_y / rc0.rc_size_y;
      glw_Scalef(&rc0, s, s, 1.0);
    }

    glw_Translatef(&rc0, 1.0, 0, 0);

    if(gtb->gtb_paint_cursor)
      glw_render(&gtb->gtb_cursor_renderer, &rc0,
		 GLW_RENDER_MODE_QUADS, GLW_RENDER_ATTRIBS_NONE,
		 NULL, 1, 1, 1, alpha * gtb->gtb_cursor_alpha);

    if(glw_is_focusable(w))
      glw_store_matrix(w, &rc0);

    glw_PopMatrix();
    return;
  }

  glw_rescale(&rc0, gtb->gtb_aspect);

  if(rc0.rc_size_y > gtb->gtb_siz_y) {
    float s = (float)gtb->gtb_siz_y / rc0.rc_size_y;
    glw_Scalef(&rc0, s, s, 1.0);
  }

  glw_align_2(&rc0, w->glw_alignment, GLW_ALIGN_LEFT);

  if(gtb->gtb_paint_cursor)
    glw_render(&gtb->gtb_cursor_renderer, &rc0,
	       GLW_RENDER_MODE_QUADS, GLW_RENDER_ATTRIBS_NONE,
	       NULL, 1, 1, 1, alpha * gtb->gtb_cursor_alpha);

  if(glw_is_focusable(w))
    glw_store_matrix(w, &rc0);

  glw_render(&gtb->gtb_text_renderer, &rc0, 
	     GLW_RENDER_MODE_QUADS, GLW_RENDER_ATTRIBS_TEX,
	     &gtb->gtb_texture,
	     gtb->gtb_color.r, gtb->gtb_color.g, gtb->gtb_color.b, alpha);

  glw_PopMatrix();
}


/*
 *
 */
static void
glw_text_bitmap_dtor(glw_t *w)
{
  glw_text_bitmap_t *gtb = (void *)w;
  glw_root_t *gr = w->glw_root;

  free(gtb->gtb_caption);

  free(gtb->gtb_data.gtbd_data);

  LIST_REMOVE(gtb, gtb_global_link);

  glw_tex_destroy(&gtb->gtb_texture);

  if(gtb->gtb_renderer_inited) {
    glw_render_free(&gtb->gtb_text_renderer);
    glw_render_free(&gtb->gtb_cursor_renderer);
  }
  if(gtb->gtb_status == GTB_ON_QUEUE)
    TAILQ_REMOVE(&gr->gr_gtb_render_queue, gtb, gtb_workq_link);
}


/**
 *
 */
static void
gtb_set_estimated_size(glw_root_t *gr, glw_text_bitmap_t *gtb)
{
  int ys = gr->gr_fontsize_px * gtb->gtb_lines * gtb->gtb_size;

  glw_set_constraints(&gtb->w, 0, ys, 0, 0, GLW_CONSTRAINT_Y, 0);
}


/**
 *
 */
static void
gtb_flush(glw_text_bitmap_t *gtb)
{
  glw_tex_destroy(&gtb->gtb_texture);
  gtb->gtb_status = GTB_NEED_RERENDER;
}


/**
 * Delete char from buf
 */
static int
del_char(glw_text_bitmap_t *gtb)
{
  int dlen = gtb->gtb_uc_len + 1; /* string length including trailing NUL */
  int i;
  int *buf = gtb->gtb_uc_buffer;

  if(gtb->gtb_edit_ptr == 0)
    return 0;

  dlen--;

  gtb->gtb_uc_len--;
  gtb->gtb_edit_ptr--;
  gtb->gtb_update_cursor = 1;

  for(i = gtb->gtb_edit_ptr; i != dlen; i++)
    buf[i] = buf[i + 1];


  return 1;
}



/**
 * Insert char in buf
 */
static int
insert_char(glw_text_bitmap_t *gtb, int ch)
{
  int dlen = gtb->gtb_uc_len + 1; /* string length including trailing NUL */
  int i;
  int *buf = gtb->gtb_uc_buffer;

  if(dlen == gtb->gtb_uc_size)
    return 0; /* Max length */
  
  dlen++;

  for(i = dlen; i != gtb->gtb_edit_ptr; i--)
    buf[i] = buf[i - 1];
  
  buf[i] = ch;
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



/*
 *
 */
static int
glw_text_bitmap_callback(glw_t *w, void *opaque, glw_signal_t signal,
			 void *extra)
{
  glw_text_bitmap_t *gtb = (void *)w;
  event_t *e;
  event_unicode_t *eu;

  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_DESTROY:
    gtb_unbind(gtb);
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_text_bitmap_layout(w, extra);
    break;
  case GLW_SIGNAL_RENDER:
    glw_text_bitmap_render(w, extra);
    break;
  case GLW_SIGNAL_DTOR:
    glw_text_bitmap_dtor(w);
    break;
  case GLW_SIGNAL_INACTIVE:
    gtb_flush(gtb);
    break;
  case GLW_SIGNAL_EVENT:
    if(w->glw_class == GLW_LABEL)
      return 0;

    e = extra;

    if(event_is_action(e, ACTION_BACKSPACE)) {

      if(!del_char(gtb)) 
	return 0;
      
      gtb_notify(gtb);
      return 1;
      
    } else if(event_is_type(e, EVENT_UNICODE)) {

      eu = extra;

      if(insert_char(gtb, eu->sym))
	gtb_notify(gtb);
      return 1;

    } else if(event_is_action(e, ACTION_LEFT)) {

      if(gtb->gtb_edit_ptr == 0)
	return 1;
      gtb->gtb_edit_ptr--;
      gtb->gtb_update_cursor = 1;
      return 1;

    } else if(event_is_action(e, ACTION_RIGHT)) {

      if(gtb->gtb_edit_ptr >= gtb->gtb_uc_len)
	return 1;
      gtb->gtb_edit_ptr++;
      gtb->gtb_update_cursor = 1;

    }
    return 0;
  }
  return 0;
}

/**
 *
 */
static void
gtb_caption_has_changed(glw_text_bitmap_t *gtb)
{
  char buf[30];
  int l, x, c, lines = 1;
  const char *str;

  /* Convert UTF8 string to unicode int[] */

  if(gtb->w.glw_class == GLW_INTEGER) {
    
    if(gtb->gtb_caption != NULL) {
      snprintf(buf, sizeof(buf), gtb->gtb_caption, gtb->gtb_int);
    } else {
      snprintf(buf, sizeof(buf), "%d", gtb->gtb_int);
    }
    str = buf;
    l = strlen(str);

  } else {

    l = gtb->gtb_caption ? strlen(gtb->gtb_caption) : 0;
    
    if(gtb->w.glw_class == GLW_TEXT) /* Editable */
      l = GLW_MAX(l, 100);
    
    str = gtb->gtb_caption;
  }
  
  gtb->gtb_uc_buffer = realloc(gtb->gtb_uc_buffer, l * sizeof(int));
  gtb->gtb_uc_size = l;
  x = 0;
  
  if(str != NULL) {
    while((c = glw_text_getutf8(&str)) != 0) {
      if(c == '\r')
	continue;
      if(c == '\n') 
	lines++;
      gtb->gtb_uc_buffer[x++] = c;
    }
  }
  gtb->gtb_lines = lines;

  gtb->gtb_uc_len = x;
  if(gtb->w.glw_class == GLW_TEXT) {
    gtb->gtb_edit_ptr = x;
    gtb->gtb_update_cursor = 1;
  }
  
  if(gtb->gtb_status != GTB_ON_QUEUE)
    gtb->gtb_status = GTB_NEED_RERENDER;

  if(gtb->w.glw_req_size_y == 0)
    gtb_set_estimated_size(gtb->w.glw_root, gtb);
}


/**
 *
 */
static void
prop_callback(void *opaque, prop_event_t event, ...)
{
  glw_text_bitmap_t *gtb = opaque;
  glw_root_t *gr;
  const char *caption;
  prop_t *p;

  if(gtb == NULL)
    return;

  gr = gtb->w.glw_root;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_SET_VOID:
    caption = NULL;
    p = va_arg(ap, prop_t *);
    break;

  case PROP_SET_STRING:
    caption = va_arg(ap, char *);
    p = va_arg(ap, prop_t *);
    break;

  default:
    return;
  }

  if(gtb->gtb_p != NULL)
    prop_ref_dec(gtb->gtb_p);

  gtb->gtb_p = p;
  if(p != NULL)
    prop_ref_inc(p);
  
  free(gtb->gtb_caption);
  gtb->gtb_caption = caption != NULL ? strdup(caption) : NULL;
  gtb_caption_has_changed(gtb);
}


/**
 *
 */
void 
glw_text_bitmap_ctor(glw_t *w, int init, va_list ap)
{
  glw_text_bitmap_t *gtb = (void *)w;
  glw_root_t *gr = w->glw_root;
  glw_attribute_t attrib;
  int update = 0;
  prop_t *p;
  const char **pname, *caption;

  glw_signal_handler_int(w, glw_text_bitmap_callback);

  if(init) {
    gtb->gtb_edit_ptr = -1;
    gtb->gtb_int_step = 1;
    gtb->gtb_int_min = INT_MIN;
    gtb->gtb_int_max = INT_MAX;
    gtb->gtb_size = 1.0;
    gtb->gtb_color.r = 1.0;
    gtb->gtb_color.g = 1.0;
    gtb->gtb_color.b = 1.0;
    gtb->gtb_siz_y = gr->gr_fontsize;

    update = 1;
    LIST_INSERT_HEAD(&gr->gr_gtbs, gtb, gtb_global_link);
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_VALUE:
      gtb->gtb_int = va_arg(ap, double);
      update = 1;
      break;

    case GLW_ATTRIB_CAPTION:
      caption = va_arg(ap, char *);

      gtb_unbind(gtb);

      update = strcmp(caption ?: "", gtb->gtb_caption ?: "");

      free(gtb->gtb_caption);
      gtb->gtb_caption = caption != NULL ? strdup(caption) : NULL;
      break;

    case GLW_ATTRIB_INT_STEP:
      gtb->gtb_int_step = va_arg(ap, double);
      break;

    case GLW_ATTRIB_INT_MIN:
      gtb->gtb_int_min = va_arg(ap, double);
      break;

    case GLW_ATTRIB_INT_MAX:
      gtb->gtb_int_max = va_arg(ap, double);
      break;

    case GLW_ATTRIB_SIZE:
      gtb->gtb_size = va_arg(ap, double);
      gtb_set_estimated_size(gr, gtb);
      break;

    case GLW_ATTRIB_RGB:
      gtb->gtb_color.r = va_arg(ap, double);
      gtb->gtb_color.g = va_arg(ap, double);
      gtb->gtb_color.b = va_arg(ap, double);
      break;

   case GLW_ATTRIB_BIND_TO_PROPERTY:
      p = va_arg(ap, prop_t *);
      pname = va_arg(ap, void *);

      gtb_unbind(gtb);

      gtb->gtb_sub = 
	prop_subscribe(PROP_SUB_DIRECT_UPDATE,
		       PROP_TAG_NAME_VECTOR, pname, 
		       PROP_TAG_CALLBACK, prop_callback, gtb, 
		       PROP_TAG_COURIER, w->glw_root->gr_courier,
		       PROP_TAG_NAMED_ROOT, p, "self",
		       PROP_TAG_ROOT, w->glw_root->gr_uii.uii_prop,
		       NULL);

      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);


  if(update)
    gtb_caption_has_changed(gtb);
}



/*
 *
 */
static void *
font_render_thread(void *aux)
{
  glw_root_t *gr = aux;
  glw_text_bitmap_t *gtb;
  int *uc, len, docur, i;
  glw_text_bitmap_data_t d;
  float size;

  glw_lock(gr);

  while(1) {
    
    while((gtb = TAILQ_FIRST(&gr->gr_gtb_render_queue)) == NULL)
      glw_cond_wait(gr, &gr->gr_gtb_render_cond);

    /* We are going to render unlocked so we cannot use gtb at all */

    len = gtb->gtb_uc_len;
    if(len > 0) {
      uc = malloc(len * sizeof(int));

      if(gtb->w.glw_flags & GLW_PASSWORD) {
	for(i = 0; i < len; i++)
	  uc[i] = '*';
      } else {
	memcpy(uc, gtb->gtb_uc_buffer, len * sizeof(int));
      }
    } else {
      uc = NULL;
    }

    gtb->w.glw_refcnt++;  /* just avoid glw_reaper from freeing us */

    TAILQ_REMOVE(&gr->gr_gtb_render_queue, gtb, gtb_workq_link);
    gtb->gtb_status = GTB_VALID;
    
    docur = gtb->gtb_edit_ptr >= 0;
    size = gtb->gtb_size;

    /* gtb (i.e the widget) may be destroyed directly after we unlock,
       so we can't access it after this point */
    glw_unlock(gr);

    if(uc == NULL || uc[0] == 0 || 
       gtb_make_tex(gr, &d, gr->gr_gtb_face, uc, len, 0, docur, size)) {
      d.gtbd_data = NULL;
      d.gtbd_siz_x = 0;
      d.gtbd_siz_y = 0;
      d.gtbd_cursor_pos = NULL;
    }

    free(uc);
    glw_lock(gr);

    if(gtb->w.glw_flags & GLW_DESTROYED) {
      /* widget got destroyed while we were away, throw away the results */
      glw_deref0(&gtb->w);
      free(d.gtbd_data);
      continue;
    }

    free(gtb->gtb_data.gtbd_data);
    free(gtb->gtb_data.gtbd_cursor_pos);
    memcpy(&gtb->gtb_data, &d, sizeof(glw_text_bitmap_data_t));

    if(d.gtbd_siz_y) {
      glw_set_constraints(&gtb->w,
			  d.gtbd_siz_x,
			  d.gtbd_siz_y,
			  0, 0, 
			  (gtb->w.glw_alignment == GLW_ALIGN_NONE ? 
			   GLW_CONSTRAINT_X : 0) | GLW_CONSTRAINT_Y, 0);

    } else {
      gtb->gtb_aspect = 0;
      gtb_set_estimated_size(gr, gtb);
    }
  }
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
    gtb_set_estimated_size(gr, gtb);
  }
}

/**
 *
 */
int
glw_get_text0(glw_t *w, char *buf, size_t buflen)
{
  glw_text_bitmap_t *gtb = (void *)w;
  char *q;
  int i, c;

  if(w->glw_class != GLW_LABEL &&
     w->glw_class != GLW_TEXT &&
     w->glw_class != GLW_INTEGER) {
    return -1;
  }

  q = buf;
  for(i = 0; i < gtb->gtb_uc_len; i++) {
    uint8_t tmp;
    c = gtb->gtb_uc_buffer[i];
    PUT_UTF8(c, tmp, if (q - buf < buflen - 1) *q++ = tmp;)
  }
  *q = 0;
  return 0;
}




/**
 *
 */
int
glw_get_int0(glw_t *w, int *result)
{
  glw_text_bitmap_t *gtb = (void *)w;

  if(w->glw_class != GLW_INTEGER) 
    return -1;

  *result = gtb->gtb_int;
  return 0;
}


/**
 *
 */
static int
glw_text_getutf8(const char **s)
{
  uint8_t c;
  int r;
  int l;

  c = **s;
  *s = *s + 1;

  switch(c) {
  case 0 ... 127:
    return c;

  case 192 ... 223:
    r = c & 0x1f;
    l = 1;
    break;

  case 224 ... 239:
    r = c & 0xf;
    l = 2;
    break;

  case 240 ... 247:
    r = c & 0x7;
    l = 3;
    break;

  case 248 ... 251:
    r = c & 0x3;
    l = 4;
    break;

  case 252 ... 253:
    r = c & 0x1;
    l = 5;
    break;
  default:
    return 0;
  }

  while(l-- > 0) {
    c = **s;
    *s = *s + 1;
    if(c == 0)
      return 0;
    r = r << 6 | (c & 0x3f);
  }
  return r;
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
    glw_get_text0(&gtb->w, buf, sizeof(buf));
    prop_set_string_ex(gtb->gtb_p, gtb->gtb_sub, buf);
  }
}

/**
 *
 */
int
glw_text_bitmap_init(glw_root_t *gr, int fontsize)
{
  int error;
  const void *r;
  size_t size;
  const char *font_variable = "theme://font.ttf";

  assert(fontsize > 10);

  error = FT_Init_FreeType(&glw_text_library);
  if(error) {
    TRACE(TRACE_ERROR, "glw", "Freetype init error\n");
    return -1;
  }

  if((r = fa_rawloader(font_variable, &size, gr->gr_theme)) == NULL) {
    TRACE(TRACE_ERROR, "glw", "Unable to load font: %s\n", font_variable);
    return -1;
  }

  TAILQ_INIT(&gr->gr_gtb_render_queue);

  if(FT_New_Memory_Face(glw_text_library, r, size, 0, &gr->gr_gtb_face)) {
    TRACE(TRACE_ERROR, "glw", 
	  "Unable to create font face: %s\n", font_variable);
    return -1;
  }

  gr->gr_fontsize = fontsize;
  gr->gr_fontsize_px = gr->gr_gtb_face->height * fontsize / 2048;

  FT_Select_Charmap(gr->gr_gtb_face, FT_ENCODING_UNICODE);

  hts_cond_init(&gr->gr_gtb_render_cond);

  hts_thread_create_detached(font_render_thread, gr);
  return 0;
}

/**
 *
 */
void
glw_font_change_size(glw_root_t *gr, int fontsize)
{
  assert(fontsize > 10);
  if(gr->gr_fontsize == fontsize)
    return;

  gr->gr_fontsize = fontsize;
  gr->gr_fontsize_px = gr->gr_gtb_face->height * fontsize / 2048;
  glw_text_flush(gr);
}


