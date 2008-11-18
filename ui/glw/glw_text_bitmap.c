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

#include "GL/gl.h"

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
#include "glw_text.h"
#include "glw_text_bitmap.h"
#include "glw_bitmap.h"
#include "glw_form.h"
#include "glw_container.h"

#include <libavutil/common.h>

#define BITMAP_HEIGHT 32

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
    for(x = 1; x < w; x++) {
      v = src[0];
      s = src[-1 - w];
      src++;
      *dst++ = v;
      *dst++ = GLW_MIN(s + v, 255);
    }
    v = *src++;
    *dst++ = v;
    *dst++ = v;
  }
}


static int
gtb_make_tex(glw_root_t *gr, glw_text_bitmap_data_t *gtbd, FT_Face face, 
	     const int *uc, const int len, int flags, int docur)
{
  FT_GlyphSlot slot = face->glyph;
  FT_Bool use_kerning = FT_HAS_KERNING( face );
  FT_UInt gi, prev = 0;
  FT_BBox bbox, glyph_bbox;
  FT_Vector pen, delta;
  int err;
  int pen_x = 0, pen_y = 0;

  int c, i, d, e;
  glyph_t *g0, *g;
  int siz_x, siz_y, start_x, start_y;
  int target_width, target_height;
  uint8_t *data;
  int shadow = 1;


  g0 = alloca(sizeof(glyph_t) * len);

  /* Compute position for each glyph */

  for(i = 0; i < len; i++) {
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

  for(i = 0; i < len; i++) {
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

  siz_y = (BITMAP_HEIGHT - 7) * 62;

  target_width  = 10 + (siz_x / 62);

  if(!(gr->gr_sysfeatures & GLW_SYSFEATURE_TNPO2)) {
    gtbd->gtbd_stride = 1 << (av_log2(target_width) + 1);
  } else {
    gtbd->gtbd_stride = target_width;
  }

  gtbd->gtbd_texsize = (double)target_width / (double)gtbd->gtbd_stride;

  target_height = BITMAP_HEIGHT;

  /* compute start pen position in 62.2 cartesian pixels */
  start_x = ((target_width  * 62 - siz_x) / 2 );
  start_y = ((target_height * 62 - siz_y) / 2 );

  /* Allocate drawing area */

  data = calloc(1, gtbd->gtbd_stride * target_height);
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
    g = g0 + i;

    pen.x = start_x + g->pos.x;
    pen.y = start_y + g->pos.y + 250;

    err = FT_Glyph_To_Bitmap(&g->glyph, FT_RENDER_MODE_NORMAL, &pen, 1);
    if(err == 0) {
      FT_BitmapGlyph bit = (FT_BitmapGlyph)g->glyph;
      
      draw_glyph(gtbd, &bit->bitmap, data, 
		 bit->left, target_height - bit->top, i, gtbd->gtbd_stride);
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

    gtbd->gtbd_data = calloc(1, 2 * gtbd->gtbd_stride * target_height);

    paint_shadow(gtbd->gtbd_data, data, gtbd->gtbd_stride, gtbd->gtbd_siz_y);

    free(data);

    gtbd->gtbd_format     = GL_LUMINANCE4_ALPHA4;
    gtbd->gtbd_ext_format = GL_LUMINANCE_ALPHA;
    gtbd->gtbd_ext_type   = GL_UNSIGNED_BYTE;

  } else {
    gtbd->gtbd_data       = data;
    gtbd->gtbd_format     = GL_ALPHA8;
    gtbd->gtbd_ext_format = GL_ALPHA;
    gtbd->gtbd_ext_type   = GL_UNSIGNED_BYTE;
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

  if(w->glw_alpha < 0.01)
    return;

  gtb->cursor_flash++;
  glw_form_alpha_update(w, rc);

  glw_set_active0(w);

  if(gtb->gtb_status == GTB_NEED_RERENDER) {
    TAILQ_INSERT_TAIL(&gr->gr_gtb_render_queue, gtb, gtb_workq_link);
    gtb->gtb_status = GTB_ON_QUEUE;
    hts_cond_signal(&gr->gr_gtb_render_cond);
    return;
  }
}


/*
 *
 */
static void
glw_text_bitmap_render(glw_t *w, glw_rctx_t *rc)
{
  glw_text_bitmap_t *gtb = (void *)w;
  glw_text_bitmap_data_t *gtbd = &gtb->gtb_data;
  float aspect, alpha, x1, x2, a;
  int i;

  alpha = rc->rc_alpha * w->glw_alpha;

  if(alpha < 0.01)
    return;


  if(gtbd->gtbd_data != NULL) {

    glActiveTextureARB(GL_TEXTURE0_ARB);

    if(gtb->gtb_texture == 0) {
      glGenTextures(1, &gtb->gtb_texture);
      glBindTexture(GL_TEXTURE_2D, gtb->gtb_texture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    
    } else {
      glBindTexture(GL_TEXTURE_2D, gtb->gtb_texture);
    }

    glTexImage2D(GL_TEXTURE_2D, 0, gtbd->gtbd_format, 
		 gtbd->gtbd_stride, gtbd->gtbd_siz_y,
		 0, gtbd->gtbd_ext_format, 
		 gtbd->gtbd_ext_type, gtbd->gtbd_data);

    free(gtbd->gtbd_data);
    gtbd->gtbd_data = NULL;
  }


  alpha *= glw_form_alpha_get(w);
  a = cos((float)gtb->cursor_flash / 10.0f) * 0.5 + 0.5;
 
  glPushMatrix();

  if(w->glw_aspect != 0) {
    aspect = w->glw_aspect;
    glw_rescale(rc->rc_aspect, aspect);
  } else {
    aspect = rc->rc_aspect;
  }

  switch(w->glw_alignment) {
  case GLW_ALIGN_CENTER:
    break;
  case GLW_ALIGN_DEFAULT:
  case GLW_ALIGN_LEFT:
    glTranslatef(-1.0, 0.0, 0.0f);
    break;
  case GLW_ALIGN_RIGHT:
    glTranslatef(1.0, 0.0, 0.0f);
    break;
  case GLW_ALIGN_BOTTOM:
    glTranslatef(0.0, -1.0, 0.0f);
    break;
  case GLW_ALIGN_TOP:
    glTranslatef(0.0, 1.0, 0.0f);
    break;
  }

  if(gtb->gtb_texture == 0 || gtbd->gtbd_siz_x == 0) {

    glw_rescale(aspect, 1.0);
    glTranslatef(1.0, 0, 0);
    if(rc->rc_focused && gtb->gtb_edit_ptr >= 0) {
      glColor4f(1,1,1, alpha * a);
      glBegin(GL_QUADS);
      glVertex3f( 0.05, -0.9f, 0.0f);
      glVertex3f( 0.5,  -0.9f, 0.0f);
      glVertex3f( 0.5,   0.9f, 0.0f);
      glVertex3f( 0.05,  0.9f, 0.0f);
      glEnd();
    }

    if(w->glw_flags & GLW_FOCUS_DRAW_CURSOR && rc->rc_focused)
      glw_form_cursor_set(rc);

    glPopMatrix();
    return;
  }

  glw_rescale(aspect, gtbd->gtbd_aspect);

  switch(w->glw_alignment) {
  case GLW_ALIGN_CENTER:
    break;
  case GLW_ALIGN_DEFAULT:
  case GLW_ALIGN_LEFT:
    glTranslatef(1.0f, 0.0f, 0.0f);
    break;
  case GLW_ALIGN_RIGHT:
    glTranslatef(-1.0f, 0.0f, 0.0f);
    break;
  case GLW_ALIGN_BOTTOM:
    glTranslatef(0.0, 1.0, 0.0f);
    break;
  case GLW_ALIGN_TOP:
    glTranslatef(0.0, -1.0, 0.0f);
    break;
  }

  if(rc->rc_focused &&
     gtb->gtb_edit_ptr >= 0 && gtbd->gtbd_cursor_scale > 0 &&
     gtb->gtb_edit_ptr <= gtbd->gtbd_cursor_pos_size) {
    i = gtb->gtb_edit_ptr;
    x1 = (float)gtbd->gtbd_cursor_pos[i*2  ] / (float)gtbd->gtbd_cursor_scale;
    x2 = (float)gtbd->gtbd_cursor_pos[i*2+1] / (float)gtbd->gtbd_cursor_scale;

    x1 = -1. + x1 * 2.;
    x2 = -1. + x2 * 2.;

    glDisable(GL_TEXTURE_2D);

    glColor4f(1,1,1, alpha);
    glBegin(GL_QUADS);
    
    glVertex3f(x1, -0.9f, 0.0f);
    glVertex3f(x2, -0.9f, 0.0f);
    glVertex3f(x2,  0.9f, 0.0f);
    glVertex3f(x1,  0.9f, 0.0f);
    glEnd();      
  }

  if(w->glw_flags & GLW_FOCUS_DRAW_CURSOR && rc->rc_focused)
    glw_form_cursor_set(rc);

  glActiveTextureARB(GL_TEXTURE0_ARB);

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, gtb->gtb_texture);

  glColor4f(1,1,1, alpha);

  glBegin(GL_QUADS);

  glTexCoord2f(0.0, 1.0);
  glVertex3f( -1.0, -1.0f, 0.0f);

  glTexCoord2f(gtbd->gtbd_texsize, 1.0);
  glVertex3f( 1.0, -1.0f, 0.0f);

  glTexCoord2f(gtbd->gtbd_texsize, 0.0);
  glVertex3f( 1.0, 1.0f, 0.0f);

  glTexCoord2f(0.0, 0.0);
  glVertex3f( -1.0, 1.0f, 0.0f);

  glEnd();

  glDisable(GL_TEXTURE_2D);
  glPopMatrix();
}


/*
 *
 */
static void
glw_text_bitmap_dtor(glw_t *w)
{
  glw_text_bitmap_t *gtb = (void *)w;
  glw_root_t *gr = w->glw_root;

  free(gtb->gtb_data.gtbd_data);

  LIST_REMOVE(gtb, gtb_global_link);

  if(gtb->gtb_texture != 0)
     glDeleteTextures(1, &gtb->gtb_texture);

  if(gtb->gtb_status == GTB_ON_QUEUE)
    TAILQ_REMOVE(&gr->gr_gtb_render_queue, gtb, gtb_workq_link);
}

/**
 *
 */
static void
gtb_flush(glw_text_bitmap_t *gtb)
{
  if(gtb->gtb_texture != 0) {
    glDeleteTextures(1, &gtb->gtb_texture);
    gtb->gtb_texture = 0;
    gtb->gtb_status = GTB_NEED_RERENDER;
  }
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
  return 1;
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
  int v;

  switch(signal) {
  default:
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
      return glw_navigate(w, extra);

    e = extra;

    if(w->glw_class == GLW_INTEGER) {

      int type = e->e_type;

      if(e->e_type == EVENT_UNICODE) {
	eu = extra;
	if(eu->sym == '+')
	  type = EVENT_INCR;
	if(eu->sym == '-')
	  type = EVENT_DECR;
      }

      switch(type) {
      default:
	break;
      case EVENT_INCR:
	if(glw_get_int0(w, &v) == 0) {
	  v = GLW_MIN(v + gtb->gtb_int_step, gtb->gtb_int_max);
	  glw_set_i(w, GLW_ATTRIB_INT, v, NULL);
	  glw_signal0(w, GLW_SIGNAL_CHANGED, NULL);
	}
	return 1;
	
      case EVENT_DECR:
	if(glw_get_int0(w, &v) == 0) {
	  v = GLW_MAX(v - gtb->gtb_int_step, gtb->gtb_int_min);
	  glw_set_i(w, GLW_ATTRIB_INT, v, NULL);
	  glw_signal0(w, GLW_SIGNAL_CHANGED, NULL);
	}
	return 1;
      }
      return 0;
    }

    switch(e->e_type) {
    default:
      break;

    case EVENT_BACKSPACE:
      if(!del_char(gtb)) 
	break;
      
      if(gtb->gtb_status != GTB_ON_QUEUE)
	gtb->gtb_status = GTB_NEED_RERENDER;
      return 1;

    case EVENT_UNICODE:
      eu = extra;

      if(insert_char(gtb, eu->sym)) {
	if(gtb->gtb_status != GTB_ON_QUEUE)
	  gtb->gtb_status = GTB_NEED_RERENDER;
      }
      return 1;

    case EVENT_LEFT:
      if(gtb->gtb_edit_ptr == 0)
	break;
      gtb->gtb_edit_ptr--;
      return 1;

    case EVENT_RIGHT:
      if(gtb->gtb_edit_ptr >= gtb->gtb_uc_len)
	break;
      gtb->gtb_edit_ptr++;
      return 1;

    }
    return glw_navigate(w, e);
  }
  return 0;
}


/**
 *
 */
static void
glw_text_bitmap_multiline(glw_text_bitmap_t *gtb, int l)
{
  glw_t *w = &gtb->w;
  char *buf = alloca(l + 1);
  memcpy(buf, w->glw_caption, l);
  buf[l] = 0;
  char *s1, *s2;

  s1 = buf;

  while((s2 = strchr(s1, '\n')) != NULL) {
    *s2 = 0;
    glw_create_i(w->glw_root,
		 GLW_LABEL,
		 GLW_ATTRIB_PARENT, w,
		 GLW_ATTRIB_CAPTION, s1,
		 GLW_ATTRIB_ALIGNMENT, w->glw_alignment,
		 NULL);
    s1 = s2 + 1;
  }

  glw_create_i(w->glw_root,
	       GLW_LABEL,
	       GLW_ATTRIB_PARENT, w,
	       GLW_ATTRIB_CAPTION, s1,
	       GLW_ATTRIB_ALIGNMENT, w->glw_alignment,
	       NULL);

}


/*
 *
 */
static int
glw_text_bitmap_multiline_callback(glw_t *w, void *opaque, glw_signal_t signal,
				   void *extra)
{
  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_LAYOUT:
    glw_container_xy_layout(w, extra);
    return 1;
  case GLW_SIGNAL_RENDER:
    glw_container_render(w, extra);
    return 1;
  }
  return 0;
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
  int l, update = 0, x, c;
  const char *str;
  char buf[30];
  glw_t *y;

  glw_signal_handler_int(w, glw_text_bitmap_callback);

  if(init) {
    gtb->gtb_edit_ptr = -1;
    gtb->gtb_int_step = 1;
    gtb->gtb_int_min = INT_MIN;
    gtb->gtb_int_max = INT_MAX;
    gtb->gtb_int_ptr = &gtb->gtb_int;
    update = 1;
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_INT:
      *gtb->gtb_int_ptr = va_arg(ap, int);
      update = 1;
      break;

    case GLW_ATTRIB_INTPTR:
      gtb->gtb_int_ptr = va_arg(ap, void *);
      if(gtb->gtb_int_ptr == NULL)
	gtb->gtb_int_ptr = &gtb->gtb_int;
      update = 1;
      break;

    case GLW_ATTRIB_CAPTION:
      (void)va_arg(ap, char *);
      update = 1;
      break;

    case GLW_ATTRIB_INT_STEP:
      gtb->gtb_int_step = va_arg(ap, int);
      break;

    case GLW_ATTRIB_INT_MIN:
      gtb->gtb_int_min = va_arg(ap, int);
      break;

    case GLW_ATTRIB_INT_MAX:
      gtb->gtb_int_max = va_arg(ap, int);
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);


  if(update) {

    /* Convert UTF8 string to unicode int[] */

    if(w->glw_class == GLW_INTEGER) {

      if(w->glw_caption != NULL) {
	snprintf(buf, sizeof(buf), w->glw_caption, *gtb->gtb_int_ptr);
      } else {
	snprintf(buf, sizeof(buf), "%d", *gtb->gtb_int_ptr);
      }
      str = buf;
      l = strlen(str);

    } else {

      while((y = TAILQ_FIRST(&w->glw_childs)) != NULL)
	glw_destroy0(y);

      l = w->glw_caption ? strlen(w->glw_caption) : 0;

      if(l && w->glw_class == GLW_LABEL && strchr(w->glw_caption, '\n')) {
	/* Label is multiline */
	glw_text_bitmap_multiline(gtb, l);
	glw_signal_handler_register(w, glw_text_bitmap_multiline_callback,
				    NULL, GLW_SIGNAL_PRI_INTERNAL - 1);
	return;
      }

      glw_signal_handler_unregister(w, glw_text_bitmap_multiline_callback,
				    NULL);

      l = w->glw_caption ? strlen(w->glw_caption) : 0;
      
      if(w->glw_class == GLW_TEXT) /* Editable */
	l = GLW_MAX(l, 100);

      str = w->glw_caption;
    }
      
    gtb->gtb_uc_buffer = realloc(gtb->gtb_uc_buffer, l * sizeof(int));
    gtb->gtb_uc_size = l;
    x = 0;

    if(str != NULL) 
      while((c = glw_text_getutf8(&str)) != 0)
	gtb->gtb_uc_buffer[x++] = c;

    gtb->gtb_uc_len   = x;
    if(w->glw_class == GLW_TEXT)
      gtb->gtb_edit_ptr = x;

    if(gtb->gtb_status != GTB_ON_QUEUE)
      gtb->gtb_status = GTB_NEED_RERENDER;
  }

  if(init) /* We do this here in case we break out for multiline editing */
    LIST_INSERT_HEAD(&gr->gr_gtbs, gtb, gtb_global_link);
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

  glw_lock();

  while(1) {
    
    while((gtb = TAILQ_FIRST(&gr->gr_gtb_render_queue)) == NULL)
      glw_cond_wait(&gr->gr_gtb_render_cond);

    /* We are going to render unlocked so we cannot use gtb at all */


    uc = malloc(gtb->gtb_uc_len * sizeof(int));
    len = gtb->gtb_uc_len;

    if(gtb->w.glw_flags & GLW_PASSWORD) {
      for(i = 0; i < len; i++)
	uc[i] = '*';
    } else {
      memcpy(uc, gtb->gtb_uc_buffer, len * sizeof(int));
    }
    gtb->w.glw_refcnt++;  /* just avoid glw_reaper from freeing us */

    TAILQ_REMOVE(&gr->gr_gtb_render_queue, gtb, gtb_workq_link);
    gtb->gtb_status = GTB_VALID;
    
    docur = gtb->gtb_edit_ptr >= 0;

    glw_unlock();

    if(uc == NULL || uc[0] == 0 || 
       gtb_make_tex(gr, &d, gr->gr_gtb_face, uc, len, 0, docur)) {
      d.gtbd_data = NULL;
      d.gtbd_siz_x = 0;
      d.gtbd_siz_y = 0;
      d.gtbd_cursor_pos = NULL;
    }

    free(uc);
    glw_lock();

    if(gtb->w.glw_flags & GLW_DESTROYED) {
      /* widget got destroyed while we were away, throw away the results */
      glw_deref0(&gtb->w);
      free(d.gtbd_data);
      continue;
    }

    free(gtb->gtb_data.gtbd_data);
    free(gtb->gtb_data.gtbd_cursor_pos);
    memcpy(&gtb->gtb_data, &d, sizeof(glw_text_bitmap_data_t));
  }
}

/*
 *
 */
int
glw_text_bitmap_init(glw_root_t *gr, const void *file_base, long file_size)
{
  hts_thread_t font_render_ptid;

  TAILQ_INIT(&gr->gr_gtb_render_queue);

  if(FT_New_Memory_Face(glw_text_library, file_base, file_size, 0, 
			&gr->gr_gtb_face))
    return -1;

  FT_Set_Pixel_Sizes(gr->gr_gtb_face, 0, BITMAP_HEIGHT);
  FT_Select_Charmap(gr->gr_gtb_face, FT_ENCODING_UNICODE);

  hts_cond_init(&gr->gr_gtb_render_cond);

  hts_thread_create(&font_render_ptid, font_render_thread, gr);
  return 0;
}

/**
 *
 */
void
glw_text_flush(glw_root_t *gr)
{
  glw_text_bitmap_t *gtb;
  LIST_FOREACH(gtb, &gr->gr_gtbs, gtb_global_link)
    gtb_flush(gtb);
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

  *result = *gtb->gtb_int_ptr;
  return 0;
}
