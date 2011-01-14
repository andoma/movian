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
#include "glw_text_bitmap.h"
#include "glw_unicode.h"
#include "fileaccess/fileaccess.h"
#include "misc/string.h"
#include "event.h"



#define RS_P_START      0x7f000001
#define RS_P_NEWLINE    0x7f000002
#define RS_P_CENTER_ON  0x7f000003
#define RS_P_CENTER_OFF 0x7f000004

/**
 *
 */
typedef struct glw_text_bitmap_data {

  int gtbd_pixel_format;
  
  uint8_t *gtbd_data;

  int16_t gtbd_width;
  int16_t gtbd_height;

  int16_t gtbd_texture_width;
  int16_t gtbd_texture_height;

  int16_t gtbd_linesize;
  uint8_t gtbd_bpp;
  uint8_t gtbd_ellipsized;

  int *gtbd_cursor_pos;
  int16_t gtbd_lines;

} glw_text_bitmap_data_t;


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

  glw_text_bitmap_data_t gtb_data;

  enum {
    GTB_NEED_RERENDER,
    GTB_ON_QUEUE,
    GTB_RENDERING,
    GTB_VALID
  } gtb_status;

  uint8_t gtb_frozen;
  uint8_t gtb_pending_update;
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

  int *gtb_uc_buffer; /* unicode buffer */
  float gtb_cursor_alpha;

  int gtb_int;
  int gtb_int_step;
  int gtb_int_min;
  int gtb_int_max;

  float gtb_size_scale;
  float gtb_size_bias;

  glw_rgb_t gtb_color;

  prop_sub_t *gtb_sub;
  prop_t *gtb_p;

  int gtb_flags;


} glw_text_bitmap_t;

static glw_class_t glw_text, glw_label, glw_integer;



#define HORIZONTAL_ELLIPSIS_UNICODE 0x2026

static void gtb_notify(glw_text_bitmap_t *gtb);


static FT_Library glw_text_library;

TAILQ_HEAD(glyph_queue, glyph);
LIST_HEAD(glyph_list, glyph);

static struct glyph_queue allglyphs;

#define GLYPH_HASH_SIZE 512
#define GLYPH_HASH_MASK (GLYPH_HASH_SIZE-1)

static struct glyph_list glyph_hash[GLYPH_HASH_SIZE];

typedef struct glyph {
  FT_Face face;
  int uc;
  FT_UInt gi;
  int size;

  LIST_ENTRY(glyph) hash_link;
  TAILQ_ENTRY(glyph) lru_link;
  FT_Glyph glyph;
  int adv_x;

  FT_BBox bbox;

} glyph_t;

static int num_glyphs;


/**
 *
 */
static void
flush_glyph(void)
{
  glyph_t *g = TAILQ_FIRST(&allglyphs);
  assert(g != NULL);

  TAILQ_REMOVE(&allglyphs, g, lru_link);
  LIST_REMOVE(g, hash_link);
  FT_Done_Glyph(g->glyph);
  free(g);
  num_glyphs--;
}



/**
 *
 */
static glyph_t *
get_glyph(FT_Face face, int uc, int size)
{
  int err, hash = (uc ^ size) & GLYPH_HASH_MASK;
  glyph_t *g;

  LIST_FOREACH(g, &glyph_hash[hash], hash_link)
    if(g->uc == uc && g->size == size && g->face == face)
      break;

  if(g == NULL) {

    FT_UInt gi = FT_Get_Char_Index(face, uc);

    if((err = FT_Load_Glyph(face, gi, FT_LOAD_DEFAULT)) != 0)
      return NULL;
    
    g = calloc(1, sizeof(glyph_t));

    if((err = FT_Get_Glyph(face->glyph, &g->glyph)) != 0) {
      free(g);
      return NULL;
    }

    FT_Glyph_Get_CBox(g->glyph, FT_GLYPH_BBOX_GRIDFIT, &g->bbox);

    g->gi = gi;
    g->face = face;
    g->uc = uc;
    g->size = size;
    g->adv_x = face->glyph->advance.x;
    LIST_INSERT_HEAD(&glyph_hash[hash], g, hash_link);
    num_glyphs++;
  } else {
    TAILQ_REMOVE(&allglyphs, g, lru_link);
  }
  TAILQ_INSERT_TAIL(&allglyphs, g, lru_link);


  if(num_glyphs > 2048)
    flush_glyph();

  return g;
}


/**
 *
 */
static void
draw_glyph(glw_text_bitmap_data_t *gtbd, FT_Bitmap *bmp, uint8_t *dst, 
	   int left, int top, int index)
{
  const uint8_t *src = bmp->buffer;
  int x, y;
  int w, h;
  
  int x1, y1, x2, y2;

  x1 = GLW_MAX(0, left);
  x2 = GLW_MIN(left + bmp->width, gtbd->gtbd_width);
  y1 = GLW_MAX(0, top);
  y2 = GLW_MIN(top + bmp->rows, gtbd->gtbd_height);

  if(gtbd->gtbd_cursor_pos != NULL) {
    gtbd->gtbd_cursor_pos[index * 2 + 0] = x1;
    gtbd->gtbd_cursor_pos[index * 2 + 1] = x2;
  }

  w = GLW_MIN(x2 - x1, bmp->width);
  h = GLW_MIN(y2 - y1, bmp->rows);

  if(w < 0 || h < 0)
    return;

  dst += x1 * gtbd->gtbd_bpp + y1 * gtbd->gtbd_linesize;

  switch(gtbd->gtbd_bpp) {
  case 2:
    // Luma + Alpha channel
    for(y = 0; y < h; y++) {
      for(x = 0; x < w; x++) {
	dst[x*2 + 0] += src[x] ? 0xff : 0;
	dst[x*2 + 1] += src[x];
      }
      src += bmp->pitch;
      dst += gtbd->gtbd_linesize;
    }
    break;

  default:
    abort();
  }
}


/**
 *
 */
TAILQ_HEAD(line_queue, line);

typedef struct line {
  TAILQ_ENTRY(line) link;
  int start;
  int count;
  int width;
  int xspace;
  char center;

} line_t;


typedef struct pos {
  int kerning;
  int adv_x;
} pos_t;


/**
 *
 */
static int
gtb_make_tex(glw_root_t *gr, glw_text_bitmap_data_t *gtbd, FT_Face face, 
	     uint32_t *uc, int len, int flags, int docur, float scale,
	     float bias, int max_width, int debug, int maxlines,
	     int doellipsize)
{
  FT_Bool use_kerning = FT_HAS_KERNING( face );
  FT_UInt prev = 0;
  FT_BBox bbox;
  FT_Vector pen, delta;
  int err;
  int pen_x, pen_y;

  int c, i, j, d, e, height;
  glyph_t *g;
  int siz_x, start_x, start_y;
  int target_width, target_height;
  uint8_t *data;
  int origin_y;
  int pixelheight = gr->gr_fontsize * scale + bias;
  int pxheight = gr->gr_fontsize_px * scale + bias;
  int ellipsize_width;
  int lines = 0;
  line_t *li, *lix;
  struct line_queue lq;
  pos_t *pos;

  gtbd->gtbd_ellipsized = 0;

  max_width *= 64;

  if(pixelheight < 3)
    return -1;

  TAILQ_INIT(&lq);

  FT_Set_Pixel_Sizes(face, 0, pixelheight);

  /* Compute xsize of three dots, for ellipsize */
  g = get_glyph(face, HORIZONTAL_ELLIPSIS_UNICODE, pixelheight);
  ellipsize_width = g->adv_x;

  /* Compute position for each glyph */

  height = 64 * face->height * pixelheight / 2048;
  height = 64 * pixelheight;

  pen_x = 0;
  pen_y = 0;
  prev = 0;
  li = NULL;

  pos = malloc(sizeof(pos_t) * len);

  int out = 0;
  int center = 0;

  for(i = 0; i < len; i++) {

    if(li == NULL) {
      li = alloca(sizeof(line_t));
      li->start = -1;
      li->count = 0;
      li->xspace = 0;
      li->center = center;
      TAILQ_INSERT_TAIL(&lq, li, link);
      prev = 0;
      pen_x = 0;
    }

    switch(uc[i]) {
    case RS_P_START:
      if(i != 0)
	li = NULL;
      continue;

    case '\n':
    case RS_P_NEWLINE:
      li = NULL;
      continue;
      
    case RS_P_CENTER_ON:
      li->center = 1;
      center = 1;
      continue;

    case RS_P_CENTER_OFF:
      li->center = 1;
      center = 0;
      continue;

    default:
      break;
    }

    if(uc[i] > 0x7f000000)
      continue;

    uc[out] = uc[i];


    if(li->start == -1)
      li->start = out;

    if((g = get_glyph(face, uc[i], pixelheight)) == NULL)
      continue;

    if(use_kerning && g->gi && prev) {
      FT_Get_Kerning(face, prev, g->gi, FT_KERNING_DEFAULT, &delta); 
      pos[out].kerning = delta.x;
    } else {
      pos[out].kerning = 0;
    }
    pos[out].adv_x = g->adv_x;
    prev = g->gi;
    li->count++;
    out++;
  }
  
  bbox.xMin = 0;

  lines = 0;
  siz_x = 0;

  TAILQ_FOREACH(li, &lq, link) {

    int w = 0;

    for(j = 0; j < li->count; j++) {

      if(j == 0 
	 && (g = get_glyph(face, uc[li->start + j], pixelheight)) != NULL) {
	w += g->bbox.xMin;
	bbox.xMin = GLW_MIN(g->bbox.xMin, bbox.xMin);
      }

      if(j == li->count - 1
	 && (g = get_glyph(face, uc[li->start + j], pixelheight)) != NULL)
	w += g->bbox.xMax;


      int d = pos[li->start + j].adv_x + 
	(j > 0 ? pos[li->start + j].kerning : 0);

      
      if(lines < maxlines - 1 && w + d >= max_width && j < li->count - 1) {
	int k = j;
	int w2 = w;

	while(k > 0 && uc[li->start + k - 1] != ' ') {
	  k--;
	  w2 -= pos[li->start + k].adv_x + 
	    (k > 0 ? pos[li->start + k].kerning : 0);
	}

	if(k > 0) {
	  lix = alloca(sizeof(line_t));
	  lix->start = li->start + k;
	  lix->count = li->count - k;
	  lix->xspace = 0;

	  TAILQ_INSERT_AFTER(&lq, li, lix, link);

	  k--;
	  w2 -= pos[li->start + k].adv_x + 
	    (k > 0 ? pos[li->start + k].kerning : 0);

	  li->count = k;
	  w = w2;
	  break;
	}
      }

      if(lines == maxlines - 1 && doellipsize && 
	 w >= max_width - ellipsize_width) {

	while(j > 0 && uc[li->start + j - 1] == ' ') {
	  j--;
	  w -= pos[li->start + j].adv_x + 
	    (j > 0 ? pos[li->start + j].kerning : 0);
	}
	
	uc[li->start + j] = HORIZONTAL_ELLIPSIS_UNICODE;
	pos[li->start + j].kerning = 0;
	gtbd->gtbd_ellipsized = 1;

	w += ellipsize_width;
	li->count = j + 1;
	break;
      }
      if(j < li->count - 1)
	w += d;
    }
    
    li->width = w;
    siz_x = GLW_MAX(w, siz_x);
    lines++;
  }

  if(siz_x < 5) {
    free(pos);
    return -1;
  }

  target_width  = siz_x / 64 + 3;

  if(maxlines > 1) {
    TAILQ_FOREACH(li, &lq, link) {
      if(li->center)
	continue;

      int spaces = 0;
      int spill = siz_x - li->width;
      for(i = li->start; i < li->start + li->count; i++) {
	if(uc[i] == ' ')
	  spaces++;
      }
      if((float)spill / li->width < 0.2)
	li->xspace = spaces ? spill / spaces : 0;
    }
  }

  target_height =  lines * pxheight;
  gtbd->gtbd_lines = lines;

  bbox.yMin = 64 * face->descender * pixelheight / 2048;
  height = 64 * (target_height / lines);

  origin_y = ((64 * (lines - 1) * pxheight) - bbox.yMin) / 64;

  if(glw_can_tnpo2(gr)) {
    gtbd->gtbd_texture_width  = target_width;
    gtbd->gtbd_texture_height = target_height;
  } else {
    gtbd->gtbd_texture_width  = 1 << (av_log2(target_width)  + 1);
    gtbd->gtbd_texture_height = 1 << (av_log2(target_height) + 1);
  }

  start_x = -bbox.xMin;
  start_y = 0;

  /* Allocate drawing area */
  gtbd->gtbd_bpp = 2;
  gtbd->gtbd_linesize = gtbd->gtbd_texture_width * gtbd->gtbd_bpp;

  data = calloc(1, gtbd->gtbd_linesize * gtbd->gtbd_texture_height);
  gtbd->gtbd_width  = target_width;
  gtbd->gtbd_height = target_height;

#if 0
  if(debug) {
    for(i = 0; i < target_height / 2; i+=3)
      memset(data + i * linesize, 0xcc, linesize);
    for(;i < target_height; i++) {
      int x;
      for(x = 0; x < target_width; x+= 3)
	data[i * linesize + x] = 0xcc;
    }
  }
#endif

  if(docur) {
    gtbd->gtbd_cursor_pos = malloc(2 * (1 + len) * sizeof(int));
  } else {
    gtbd->gtbd_cursor_pos = NULL;
  }

  pen_y = 0;

  TAILQ_FOREACH(li, &lq, link) {
    pen_x = 0;
    
    if(li->center) {
      pen_x += (siz_x - li->width) / 2;
    }


    for(i = li->start; i < li->start + li->count; i++) {
      g = get_glyph(face, uc[i], pixelheight);
      if(g == NULL)
	continue;

      pen_x += pos[i].kerning;
      
      pen.x = start_x + pen_x;
      pen.y = start_y + pen_y;

      FT_BitmapGlyph bmp = (FT_BitmapGlyph)g->glyph;
      pen.x &= ~63;
      pen.y &= ~63;
      err = FT_Glyph_To_Bitmap((FT_Glyph*)&bmp, FT_RENDER_MODE_NORMAL, &pen, 0);
      if(err == 0) {
	draw_glyph(gtbd, &bmp->bitmap, data, bmp->left + 1,
		   target_height - 1 - origin_y - bmp->top, i);
	FT_Done_Glyph((FT_Glyph)bmp);
      }

      if(gtbd->gtbd_cursor_pos != NULL && uc[i] == ' ')
	gtbd->gtbd_cursor_pos[2 * i + 0] = pen_x / 64;

      pen_x += pos[i].adv_x;
      if(uc[i] == ' ')
	pen_x += li->xspace;

      if(gtbd->gtbd_cursor_pos != NULL && uc[i] == ' ')
	gtbd->gtbd_cursor_pos[2 * i + 1] = pen_x / 64;

    }
    pen_y -= height;
  }

  if(docur) {
    gtbd->gtbd_cursor_pos[2 * len] = gtbd->gtbd_cursor_pos[2 * len - 1];
    i = pxheight / 4;
    gtbd->gtbd_cursor_pos[2 * len + 1] = gtbd->gtbd_cursor_pos[2 * len] + i;

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

  gtbd->gtbd_data = data;

  switch(gtbd->gtbd_bpp) {
  case 2:
    gtbd->gtbd_pixel_format = GLW_TEXTURE_FORMAT_I8A8;
    break;
  default:
    abort();
  }

  free(pos);
  return 0;
}


/**
 *
 */
static void
glw_text_bitmap_layout(glw_t *w, glw_rctx_t *rc)
{
  glw_text_bitmap_t *gtb = (void *)w;
  glw_root_t *gr = w->glw_root;
  glw_text_bitmap_data_t *gtbd = &gtb->gtb_data;

  // Initialize renderers

  if(!glw_renderer_initialized(&gtb->gtb_text_renderer))
    glw_renderer_init_quad(&gtb->gtb_text_renderer);

  if(w->glw_class == &glw_text &&
     !glw_renderer_initialized(&gtb->gtb_cursor_renderer))
    glw_renderer_init_quad(&gtb->gtb_cursor_renderer);


  // Upload texture

  if(gtbd->gtbd_data != NULL && gtb->gtb_status == GTB_VALID) {
    glw_tex_upload(gr, &gtb->gtb_texture, gtbd->gtbd_data, 
		   gtbd->gtbd_pixel_format,
		   gtbd->gtbd_texture_width, gtbd->gtbd_texture_height ,0);

    free(gtbd->gtbd_data);
    gtbd->gtbd_data = NULL;
    gtb->gtb_need_layout = 1;
  }

  // Check if we need to repaint

  if(gtb->gtb_saved_width  != rc->rc_width || 
     gtb->gtb_saved_height != rc->rc_height) {

    if(gtb->gtb_status == GTB_VALID && gtb->gtb_flags & GTB_ELLIPSIZE) {
      
      if(gtbd->gtbd_ellipsized) {
	gtb->gtb_status = GTB_NEED_RERENDER;
      } else {
	if(rc->rc_width - gtb->gtb_padding_right - gtb->gtb_padding_left <
	   gtbd->gtbd_width)
	  gtb->gtb_status = GTB_NEED_RERENDER;
	
	if(rc->rc_height - gtb->gtb_padding_top - gtb->gtb_padding_bottom <
	   gtbd->gtbd_height)
	  gtb->gtb_status = GTB_NEED_RERENDER;
      }
    }

    gtb->gtb_saved_width  = rc->rc_width;
    gtb->gtb_saved_height = rc->rc_height;
    gtb->gtb_need_layout = 1;
  }


  if(gtb->gtb_status == GTB_VALID &&
     (gtb->gtb_need_layout || gtb->gtb_update_cursor)) {

    int left   =                 gtb->gtb_padding_left;
    int top    = rc->rc_height - gtb->gtb_padding_top;
    int right  = rc->rc_width  - gtb->gtb_padding_right;
    int bottom =                 gtb->gtb_padding_bottom;
    
    int text_width  = gtbd->gtbd_width;
    int text_height = gtbd->gtbd_height;
    
    float x1, y1, x2, y2;

    // Horizontal 
    if(text_width > right - left) {
      // Oversized, must cut
      text_width = right - left;
    } else { 
      switch(w->glw_alignment) {
      case GLW_ALIGN_CENTER:
      case GLW_ALIGN_BOTTOM:
      case GLW_ALIGN_TOP:
	left = (left + right - text_width) / 2;
	right = left + text_width;
	break;

      case GLW_ALIGN_LEFT:
      case GLW_ALIGN_TOP_LEFT:
      case GLW_ALIGN_BOTTOM_LEFT:
	right = left + gtbd->gtbd_width;
	break;

      case GLW_ALIGN_RIGHT:
      case GLW_ALIGN_TOP_RIGHT:
      case GLW_ALIGN_BOTTOM_RIGHT:
	left = right - gtbd->gtbd_width;
	break;
      }
    }

    // Vertical 
    if(text_height > top - bottom) {
      // Oversized, must cut
      text_height = top - bottom;
    } else { 
      switch(w->glw_alignment) {
      case GLW_ALIGN_CENTER:
      case GLW_ALIGN_LEFT:
      case GLW_ALIGN_RIGHT:
	bottom = (bottom + top - text_height) / 2;
	top = bottom + text_height;
	break;

      case GLW_ALIGN_TOP_LEFT:
      case GLW_ALIGN_TOP_RIGHT:
      case GLW_ALIGN_TOP:
	bottom = top - gtbd->gtbd_height;
	break;

      case GLW_ALIGN_BOTTOM:
      case GLW_ALIGN_BOTTOM_LEFT:
      case GLW_ALIGN_BOTTOM_RIGHT:
	top = bottom + gtbd->gtbd_height;
	break;
      }
    }

    x1 = -1.0f + 2.0f * left   / (float)rc->rc_width;
    y1 = -1.0f + 2.0f * bottom / (float)rc->rc_height;
    x2 = -1.0f + 2.0f * right  / (float)rc->rc_width;
    y2 = -1.0f + 2.0f * top    / (float)rc->rc_height;

    float s, t;

    if(gr->gr_normalized_texture_coords) {
      s = text_width  / (float)gtbd->gtbd_texture_width;
      t = text_height / (float)gtbd->gtbd_texture_height;
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

    if(w->glw_class == &glw_text) {
      gtb->gtb_update_cursor = 0;

      if(gtbd->gtbd_cursor_pos != NULL) {
	
	int i = gtb->gtb_edit_ptr;
	left  = gtbd->gtbd_cursor_pos[i*2  ];
	right = gtbd->gtbd_cursor_pos[i*2+1];

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
    }
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
  glw_text_bitmap_t *gtb = (void *)w;
  float alpha;

  if(glw_is_focusable(w))
    glw_store_matrix(w, rc);

  alpha = rc->rc_alpha * w->glw_alpha;

  if(alpha < 0.01f)
    return;

  if(w->glw_flags & GLW_DEBUG)
    glw_wirebox(w->glw_root, rc);

  if(glw_is_tex_inited(&gtb->gtb_texture) && gtb->gtb_data.gtbd_width > 0) {

    if(w->glw_flags & GLW_SHADOW && !rc->rc_inhibit_shadows) {
      float xd =  2.0f / rc->rc_width;
      float yd = -2.0f / rc->rc_height;
      glw_rctx_t rc0 = *rc;

      glw_Translatef(&rc0, xd, yd, 0.0);
      
      const static glw_rgb_t black = {0,0,0};
      
      glw_renderer_draw(&gtb->gtb_text_renderer, w->glw_root, &rc0, 
			&gtb->gtb_texture, &black, alpha * 0.75f, 0);
    }
    glw_renderer_draw(&gtb->gtb_text_renderer, w->glw_root, rc, 
		      &gtb->gtb_texture, &gtb->gtb_color, alpha, 0);
  }

  if(gtb->gtb_paint_cursor) {
    glw_root_t *gr = w->glw_root;
    float a = cos((gr->gr_frames & 2047) * (360.0f / 2048.0f)) * 0.5f + 0.5f;
    glw_renderer_draw(&gtb->gtb_cursor_renderer, w->glw_root, rc,
		      NULL, NULL, alpha * a, 0);
  }
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
  free(gtb->gtb_uc_buffer);

  free(gtb->gtb_data.gtbd_data);

  LIST_REMOVE(gtb, gtb_global_link);

  glw_tex_destroy(&gtb->gtb_texture);

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
  int lines = gtb->gtb_data.gtbd_lines ?: 1;
  int ys = gtb->gtb_padding_top + gtb->gtb_padding_bottom + 
    (gtb->gtb_size_bias + gr->gr_fontsize_px * gtb->gtb_size_scale)
    * lines;
  int xs = gtb->gtb_padding_left + gtb->gtb_padding_right +
    gtb->gtb_data.gtbd_width;

  int flags = GLW_CONSTRAINT_Y;

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
  glw_tex_destroy(&gtb->gtb_texture);
  if(gtb->gtb_status != GTB_ON_QUEUE)
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
static int
tag_to_code(char *s)
{
  const char *tag;
  int endtag = 0;

  while(*s == ' ')
    s++;
  if(*s == 0)
    return 0;

  tag = s;

  if(*tag == '/') {
    endtag = 1;
    tag++;
  }
    
  while(*s != ' ' && *s != '/' && *s != 0)
    s++;
  *s = 0;

  if(!endtag && !strcmp(tag, "p")) 
    return RS_P_START;

  if(!endtag && !strcmp(tag, "br")) 
    return RS_P_NEWLINE;

  if(!strcmp(tag, "center")) 
    return endtag ? RS_P_CENTER_OFF : RS_P_CENTER_ON;

  return 0;
}


/**
 *
 */
static void
parse_rich_str(glw_text_bitmap_t *gtb, const char *str)
{
  int x = 0, c, lines = 1, p = -1, d;
  int l = strlen(str);

  char *tmp = malloc(l);
  int lp;

  while((c = utf8_get(&str)) != 0) {
    if(c == '\r' || c == '\r')
      continue;

    if(c == '<') {
      lp = 0;
      while((d = utf8_get(&str)) != 0) {
	if(d == '>')
	  break;
	tmp[lp++] = d;
      }
      if(d == 0)
	break;
      tmp[lp] = 0;

      int c = tag_to_code(tmp);

      if(c)
	gtb->gtb_uc_buffer[x++] = c;
      continue;
    }


    if(c == '&') {
      lp = 0;
      while((d = utf8_get(&str)) != 0) {
	if(d == ';')
	  break;
	tmp[lp++] = d;
      }
      if(d == 0)
	break;
      tmp[lp] = 0;

      int c = html_entity_lookup(tmp);

      if(c != -1)
	gtb->gtb_uc_buffer[x++] = c;
      continue;
    }



    if(p != -1 && (d = glw_unicode_compose(p, c)) != -1) {
      gtb->gtb_uc_buffer[x-1] = d;
      p = -1;
    } else {
      gtb->gtb_uc_buffer[x++] = p = c;
    }
  }
  lines = lines;
  gtb->gtb_uc_len = x;
  free(tmp);
}


/**
 *
 */
static void
parse_str(glw_text_bitmap_t *gtb, const char *str)
{
  int x = 0, c, lines = 1, p = -1, d;

  while((c = utf8_get(&str)) != 0) {
    if(c == '\r')
      continue;
    if(c == '\n') 
      lines++;

    if(p != -1 && (d = glw_unicode_compose(p, c)) != -1) {
      gtb->gtb_uc_buffer[x-1] = d;
      p = -1;
    } else {
      gtb->gtb_uc_buffer[x++] = p = c;
    }
  }
  lines = lines;
  gtb->gtb_uc_len = x;
}


/**
 *
 */
static void
gtb_caption_has_changed(glw_text_bitmap_t *gtb)
{
  char buf[30];
  int l;
  const char *str;

  /* Convert UTF8 string to unicode int[] */

  if(gtb->w.glw_class == &glw_integer) {
    
    if(gtb->gtb_caption != NULL) {
      snprintf(buf, sizeof(buf), gtb->gtb_caption, gtb->gtb_int);
    } else {
      snprintf(buf, sizeof(buf), "%d", gtb->gtb_int);
    }
    str = buf;
    l = strlen(str);

  } else {

    l = gtb->gtb_caption ? strlen(gtb->gtb_caption) : 0;
    
    if(gtb->w.glw_class == &glw_text) /* Editable */
      l = GLW_MAX(l, 100);
    
    str = gtb->gtb_caption;
  }
  
  gtb->gtb_uc_size = l;
  gtb->gtb_uc_buffer = realloc(gtb->gtb_uc_buffer, l * sizeof(int));
  
  if(str != NULL) {

    switch(gtb->gtb_type) {
    case PROP_STR_UTF8:
      parse_str(gtb, str);
      break;

    case PROP_STR_RICH:
      parse_rich_str(gtb, str);
      break;

    default:
      abort();
    }
  } else {
    gtb->gtb_uc_len = 0;
  }

  if(gtb->w.glw_class == &glw_text) {
    gtb->gtb_edit_ptr = gtb->gtb_uc_len;
    gtb->gtb_update_cursor = 1;
  }
  
  if(gtb->gtb_status != GTB_ON_QUEUE)
    gtb->gtb_status = GTB_NEED_RERENDER;

  if(!(gtb->w.glw_flags & GLW_CONSTRAINT_Y)) // Only update if yet unset
    gtb_set_constraints(gtb->w.glw_root, gtb);
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

  case PROP_SET_RSTRING:
    caption = rstr_get(va_arg(ap, const rstr_t *));
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
static void 
glw_text_bitmap_ctor(glw_t *w)
{
  glw_text_bitmap_t *gtb = (void *)w;
  glw_root_t *gr = w->glw_root;

  w->glw_flags |= GLW_FOCUS_ON_CLICK | GLW_SHADOW | GLW_HIDDEN;
  gtb->gtb_edit_ptr = -1;
  gtb->gtb_int_step = 1;
  gtb->gtb_int_min = INT_MIN;
  gtb->gtb_int_max = INT_MAX;
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
set_padding(glw_t *w, const float *v)
{
  glw_text_bitmap_t *gtb = (void *)w;
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
gtb_update(glw_text_bitmap_t *gtb)
{
  if(gtb->gtb_frozen) {
    gtb->gtb_pending_update = 1;
  } else {
    gtb_caption_has_changed(gtb);
    gtb->gtb_pending_update = 0;
  }
}


/**
 *
 */
static void
mod_text_flags(glw_t *w, int set, int clr)
{
  glw_text_bitmap_t *gtb = (void *)w;
  gtb->gtb_flags = (gtb->gtb_flags | set) & ~clr;
  
  gtb_update(gtb);
}


/**
 *
 */
static void
set_caption(glw_t *w, const char *caption, int type)
{
  glw_text_bitmap_t *gtb = (void *)w;

  gtb_unbind(gtb);

  const int update = strcmp(caption ?: "", gtb->gtb_caption ?: "");

  free(gtb->gtb_caption);
  gtb->gtb_caption = caption != NULL ? strdup(caption) : NULL;
  gtb->gtb_type = type;
  assert(gtb->gtb_type == 0 || gtb->gtb_type == 1);
  if(caption == NULL)
    w->glw_flags |= GLW_HIDDEN;
  else
    w->glw_flags &= ~GLW_HIDDEN;
  
  if(update)
    gtb_update(gtb);
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
  w->glw_flags &= ~GLW_HIDDEN;
}

/**
 *
 */
static void 
glw_text_bitmap_set(glw_t *w, va_list ap)
{
  glw_text_bitmap_t *gtb = (void *)w;
  glw_attribute_t attrib;
  int update = 0;

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {
    case GLW_ATTRIB_VALUE:
      gtb->gtb_int = va_arg(ap, double);
      w->glw_flags &= ~GLW_HIDDEN;
      update = 1;
      break;

    case GLW_ATTRIB_FREEZE:
      if(va_arg(ap, int)) {
	gtb->gtb_frozen = 1;
      } else {
	if(gtb->gtb_pending_update)
	  update = 1;
	gtb->gtb_frozen = 0;
      }
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

    case GLW_ATTRIB_SIZE_SCALE:
      gtb->gtb_size_scale = va_arg(ap, double);
      if(!(gtb->w.glw_flags & GLW_CONSTRAINT_Y)) // Only update if yet unset
	gtb_set_constraints(gtb->w.glw_root, gtb);
      break;

    case GLW_ATTRIB_SIZE_BIAS:
      gtb->gtb_size_bias = va_arg(ap, double);
      if(!(gtb->w.glw_flags & GLW_CONSTRAINT_Y)) // Only update if yet unset
	gtb_set_constraints(gtb->w.glw_root, gtb);
      break;


   case GLW_ATTRIB_MAXLINES:
     gtb->gtb_maxlines = va_arg(ap, int);
     update = 1;
     break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);


  if(!update)
    return;

  if(gtb->gtb_frozen) {
    gtb->gtb_pending_update = 1;
  } else {
    gtb_caption_has_changed(gtb);
    gtb->gtb_pending_update = 0;
  }
}



/*
 *
 */
static void *
font_render_thread(void *aux)
{
  glw_root_t *gr = aux;
  glw_text_bitmap_t *gtb;
  uint32_t *uc, len, docur, i;
  glw_text_bitmap_data_t d;
  float scale, bias;
  int max_width, debug, doellipsize, maxlines;

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
    
    docur = gtb->gtb_edit_ptr >= 0;
    scale = gtb->gtb_size_scale;
    bias  = gtb->gtb_size_bias;
    max_width = gtb->gtb_saved_width;
    debug = gtb->w.glw_flags & GLW_DEBUG;
    doellipsize = gtb->gtb_flags & GTB_ELLIPSIZE;
    maxlines = gtb->gtb_maxlines;

    /* gtb (i.e the widget) may be destroyed directly after we unlock,
       so we can't access it after this point. We can hold a reference
       though. But it will only guarantee that the pointer stays valid */

    glw_ref(&gtb->w);
    glw_unlock(gr);

    if(uc == NULL || uc[0] == 0 || 
       gtb_make_tex(gr, &d, gr->gr_gtb_face, uc, len, 0, docur, scale, bias,
		    max_width, debug, maxlines, doellipsize)) {
      memset(&d, 0, sizeof(d));
    }

    free(uc);
    glw_lock(gr);

    if(gtb->w.glw_flags & GLW_DESTROYING) {
      /* widget got destroyed while we were away, throw away the results */
      glw_unref(&gtb->w);
      free(d.gtbd_data);
      free(d.gtbd_cursor_pos);
      continue;
    }

    glw_unref(&gtb->w);
    free(gtb->gtb_data.gtbd_data);
    free(gtb->gtb_data.gtbd_cursor_pos);

    memcpy(&gtb->gtb_data, &d, sizeof(glw_text_bitmap_data_t));

    if(gtb->gtb_status == GTB_RENDERING)
      gtb->gtb_status = GTB_VALID;

    gtb_set_constraints(gr, gtb);
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
  int i, c;

  if(w->glw_class != &glw_label &&
     w->glw_class != &glw_text &&
     w->glw_class != &glw_integer) {
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
glw_get_int(glw_t *w, int *result)
{
  glw_text_bitmap_t *gtb = (void *)w;

  if(w->glw_class != &glw_integer) 
    return -1;

  *result = gtb->gtb_int;
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
int
glw_text_bitmap_init(glw_root_t *gr)
{
  int error;
  const void *r;
  struct fa_stat fs;
  const char *font_variable = "theme://font.ttf";
  char errbuf[256];

  error = FT_Init_FreeType(&glw_text_library);
  if(error) {
    TRACE(TRACE_ERROR, "glw", "Freetype init error\n");
    return -1;
  }

  if((r = fa_quickload(font_variable, &fs, gr->gr_vpaths, 
		       errbuf, sizeof(errbuf))) == NULL) {
    TRACE(TRACE_ERROR, "glw", "Unable to load font: %s -- %s\n",
	  font_variable, errbuf);
    return -1;
  }

  TAILQ_INIT(&gr->gr_gtb_render_queue);
  TAILQ_INIT(&allglyphs);

  if(FT_New_Memory_Face(glw_text_library, r, fs.fs_size, 0, &gr->gr_gtb_face)) {
    TRACE(TRACE_ERROR, "glw", 
	  "Unable to create font face: %s\n", font_variable);
    return -1;
  }

  FT_Select_Charmap(gr->gr_gtb_face, FT_ENCODING_UNICODE);

  hts_cond_init(&gr->gr_gtb_render_cond, &gr->gr_mutex);

  glw_font_change_size(gr, 20);

  hts_thread_create_detached("GLW font renderer", font_render_thread, gr);
  return 0;
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
  gr->gr_fontsize_px = gr->gr_gtb_face->height * fontsize / 2048;
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
static glw_class_t glw_label = {
  .gc_name = "label",
  .gc_instance_size = sizeof(glw_text_bitmap_t),
  .gc_render = glw_text_bitmap_render,
  .gc_set = glw_text_bitmap_set,
  .gc_ctor = glw_text_bitmap_ctor,
  .gc_dtor = glw_text_bitmap_dtor,
  .gc_signal_handler = glw_text_bitmap_callback,
  .gc_get_text = glw_text_bitmap_get_text,
  .gc_default_alignment = GLW_ALIGN_LEFT,
  .gc_set_rgb = glw_text_bitmap_set_rgb,
  .gc_set_padding = set_padding,
  .gc_mod_text_flags = mod_text_flags,
  .gc_set_caption = set_caption,
  .gc_bind_to_property = bind_to_property,
};

GLW_REGISTER_CLASS(glw_label);


/**
 *
 */
static glw_class_t glw_text = {
  .gc_name = "text",
  .gc_instance_size = sizeof(glw_text_bitmap_t),
  .gc_render = glw_text_bitmap_render,
  .gc_set = glw_text_bitmap_set,
  .gc_ctor = glw_text_bitmap_ctor,
  .gc_dtor = glw_text_bitmap_dtor,
  .gc_signal_handler = glw_text_bitmap_callback,
  .gc_get_text = glw_text_bitmap_get_text,
  .gc_default_alignment = GLW_ALIGN_LEFT,
  .gc_set_rgb = glw_text_bitmap_set_rgb,
  .gc_set_padding = set_padding,
  .gc_mod_text_flags = mod_text_flags,
  .gc_set_caption = set_caption,
  .gc_bind_to_property = bind_to_property,
};

GLW_REGISTER_CLASS(glw_text);
