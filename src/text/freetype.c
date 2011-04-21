/*
 *  Text rendering with freetype
 *  Copyright (C) 2007 - 2011 Andreas Öman
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

#include <sys/param.h>
#include <assert.h>

#include "showtime.h"
#include "misc/queue.h"
#include "misc/pixmap.h"
#include "text.h"
#include "arch/arch.h"

#include "fileaccess/fileaccess.h"

#define HORIZONTAL_ELLIPSIS_UNICODE 0x2026




#include <ft2build.h>  
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_SYNTHESIS_H

#define ver(maj, min, pat) ((maj) * 100000 + (min) * 100 + (pat))

#define ftver ver(FREETYPE_MAJOR, FREETYPE_MINOR, FREETYPE_PATCH)

#if ftver >= ver(2,4,0)
#define HAVE_FACE_REFERENCE
#endif

static FT_Library text_library;
static hts_mutex_t text_mutex;


#define GLYPH_HASH_SIZE 128
#define GLYPH_HASH_MASK (GLYPH_HASH_SIZE-1)
TAILQ_HEAD(glyph_queue, glyph);
LIST_HEAD(glyph_list, glyph);
TAILQ_HEAD(face_queue, face);



//------------------------- Faces -----------------------

typedef struct face {
  TAILQ_ENTRY(face) link;

  FT_Face face;
  char *url;
 
  char *family;
  uint8_t style;

  int refcount;

} face_t;

static struct face_queue faces;


//------------------------- Glyph cache -----------------------

typedef struct glyph {
  int uc;
  int16_t size;
  uint8_t style;
  face_t *face;

  FT_UInt gi;

  LIST_ENTRY(glyph) hash_link;
  TAILQ_ENTRY(glyph) lru_link;
  FT_Glyph glyph;
  int adv_x;

  FT_BBox bbox;

} glyph_t;

static struct glyph_list glyph_hash[GLYPH_HASH_SIZE];
static struct glyph_queue allglyphs;
static int num_glyphs;


/**
 *
 */
static void
face_destroy(face_t *f)
{
  TRACE(TRACE_DEBUG, "Freetype", "Unloading %s", f->url);
  TAILQ_REMOVE(&faces, f, link);
  free(f->url);
  free(f->family);
  FT_Done_Face(f->face);
  free(f);
}


/**
 *
 */
static void
faces_purge(void)
{
  face_t *f, *n;
  for(f = TAILQ_FIRST(&faces); f != NULL; f = n) {
    n = TAILQ_NEXT(f, link);
    if(f->refcount == 0)
      face_destroy(f);
  }
}

/**
 *
 */
static unsigned long
face_read(FT_Stream stream, unsigned long offset, unsigned char *buffer,
	  unsigned long count)
{
  if(count == 0)
    return fa_seek(stream->descriptor.pointer, offset, SEEK_SET) < 0;
  return fa_read(stream->descriptor.pointer, buffer, count);
}


/**
 *
 */
static void
face_close(FT_Stream stream)
{
  fa_close(stream->descriptor.pointer);
}



/**
 *
 */
static void
face_finalizer(void *obj)
{
  FT_Face f = obj;
  free(f->generic.data);
}

/**
 *
 */
static face_t *
face_create(const char *path)
{
  char errbuf[256];
  FT_Open_Args oa = {0};
  FT_Error err;

  void *fh = fa_open(path, errbuf, sizeof(errbuf));
  if(fh == NULL) {
    TRACE(TRACE_ERROR, "glw", "Unable to load font: %s -- %s",
	  path, errbuf);
    return NULL;
  }

  face_t *face = calloc(1, sizeof(face_t));

  FT_Stream srec = calloc(1, sizeof(FT_StreamRec));
  srec->size = fa_fsize(fh);
  srec->descriptor.pointer = fh;
  srec->read = face_read;
  srec->close = face_close;

  oa.stream = srec;
  oa.flags = FT_OPEN_STREAM;
  
  if((err = FT_Open_Face(text_library, &oa, 0, &face->face)) != 0) {
    TRACE(TRACE_ERROR, "glw", "Unable to create font face: %s 0x%x", path, err);
    free(face);
    free(srec);
    return NULL;
  }

  TRACE(TRACE_DEBUG, "Freetype", "Loaded %s", path);

  face->face->generic.data = srec;
  face->face->generic.finalizer = face_finalizer;
  face->url = strdup(path);

  FT_Select_Charmap(face->face, FT_ENCODING_UNICODE);

  TAILQ_INSERT_TAIL(&faces, face, link);
  return face;
}


/**
 *
 */
static int
face_resovle(int uc, uint8_t style, const char *family,
	     char *urlbuf, size_t urllen, uint8_t *actualstylep)
{
#if ENABLE_LIBFONTCONFIG
  if(!fontconfig_resolve(uc, style, family, urlbuf, urllen, actualstylep))
    return 0;
#endif

#ifdef SHOWTIME_FONT_LIBERATION_URL
  snprintf(urlbuf, urllen,
	   SHOWTIME_FONT_LIBERATION_URL"/LiberationSans-Regular.ttf");
  *actualstylep = 0;
  return 0;
#endif
  return -1;
}

#ifdef HAVE_FACE_REFERENCE
/**
 *
 */
static face_t *
face_clone(face_t *src, const char *family)
{
  face_t *dst = calloc(1, sizeof(face_t));

  dst->face = src->face;
  FT_Reference_Face(src->face);
  dst->url = strdup(src->url);

  dst->family = strdup(family);
  dst->style = src->style;
  TAILQ_INSERT_TAIL(&faces, dst, link);
  return dst;
}
#endif


/**
 *
 */
static face_t *
face_find(int uc, uint8_t style, const char *family)
{
  face_t *f;
  char url[URL_MAX];
  uint8_t actualstyle;

  // Try already loaded faces
  TAILQ_FOREACH(f, &faces, link) {
    if((family == NULL || !strcmp(family, f->family ?: "" )) &&
       f->style == style && FT_Get_Char_Index(f->face, uc))
      return f;
  }

  if(face_resovle(uc, style, family, url, sizeof(url), &actualstyle))
    return NULL;

  TAILQ_FOREACH(f, &faces, link) {
    if(!strcmp(f->url, url)) {
      if(family == NULL || !strcmp(family, f->family ?: ""))
	return f;
#ifdef HAVE_FACE_REFERENCE
      return face_clone(f, family);
#endif
    }
  }

  if((f = face_create(url)) == NULL)
    return NULL;

  f->style = actualstyle;
  f->family = family ? strdup(family) : NULL;
  return f;
}


/**
 * family == NULL means don't care
 */
static glyph_t *
glyph_get(int uc, int size, uint8_t style, const char *family)
{
  int err, hash = (uc ^ size ^ style) & GLYPH_HASH_MASK;
  glyph_t *g;
  FT_GlyphSlot gs;

  LIST_FOREACH(g, &glyph_hash[hash], hash_link)
    if(g->uc == uc && g->size == size && g->style == style && 
       (family == NULL || !strcmp(family, g->face->family)))
      break;

  if(g == NULL) {
    face_t *f;
    FT_UInt gi = 0;

    f = face_find(uc, style, family);

    if(f == NULL)
      return NULL;

    gi = FT_Get_Char_Index(f->face, uc);

    FT_Size_RequestRec  req;
    req.type = FT_SIZE_REQUEST_TYPE_REAL_DIM;
    req.width = size << 6;
    req.height = size << 6;
    req.horiResolution = 0;
    req.vertResolution = 0;
    
    FT_Request_Size(f->face, &req);


    if((err = FT_Load_Glyph(f->face, gi, FT_LOAD_FORCE_AUTOHINT)) != 0)
      return NULL;
    
    gs = f->face->glyph;

    if(style & TR_STYLE_ITALIC && !(f->style & TR_STYLE_ITALIC))
      FT_GlyphSlot_Oblique(gs);

    if(style & TR_STYLE_BOLD && !(f->style & TR_STYLE_BOLD) && 
       gs->format == FT_GLYPH_FORMAT_OUTLINE) {
      int v = FT_MulFix(gs->face->units_per_EM,
			gs->face->size->metrics.y_scale) / 64;
      FT_Outline_Embolden(&gs->outline, v);
    }

    g = calloc(1, sizeof(glyph_t));

    if((err = FT_Get_Glyph(gs, &g->glyph)) != 0) {
      free(g);
      return NULL;
    }

    FT_Glyph_Get_CBox(g->glyph, FT_GLYPH_BBOX_GRIDFIT, &g->bbox);

    g->gi = gi;
    f->refcount++;
    g->face = f;
    g->uc = uc;
    g->style = style;
    g->size = size;
	 
    g->adv_x = gs->advance.x;
    LIST_INSERT_HEAD(&glyph_hash[hash], g, hash_link);
    num_glyphs++;
  } else {
    TAILQ_REMOVE(&allglyphs, g, lru_link);
  }
  TAILQ_INSERT_TAIL(&allglyphs, g, lru_link);

  return g;
}



/**
 *
 */
static void
glyph_flush(void)
{
  glyph_t *g = TAILQ_FIRST(&allglyphs);
  assert(g != NULL);

  g->face->refcount--;

  TAILQ_REMOVE(&allglyphs, g, lru_link);
  LIST_REMOVE(g, hash_link);
  FT_Done_Glyph(g->glyph);
  free(g);
  num_glyphs--;
}


/**
 *
 */
static void
draw_glyph(pixmap_t *pm, FT_Bitmap *bmp, int left, int top, int idx)
{
  const uint8_t *src = bmp->buffer;
  uint8_t *dst;
  int x, y;
  int w, h;
  
  int x1, y1, x2, y2;

  x1 = MAX(0, left);
  x2 = MIN(left + bmp->width, pm->pm_width);
  y1 = MAX(0, top);
  y2 = MIN(top + bmp->rows, pm->pm_height);

  if(pm->pm_charpos != NULL) {
    pm->pm_charpos[idx * 2 + 0] = x1;
    pm->pm_charpos[idx * 2 + 1] = x2;
  }

  w = MIN(x2 - x1, bmp->width);
  h = MIN(y2 - y1, bmp->rows);

  if(w < 0 || h < 0)
    return;

  dst = pm->pm_pixels;

  switch(pm->pm_pixfmt) {
  case PIX_FMT_Y400A:
    dst += x1 * 2 + y1 * pm->pm_linesize;

    // Luma + Alpha channel
    for(y = 0; y < h; y++) {
      for(x = 0; x < w; x++) {
	dst[x*2 + 0] += src[x] ? 0xff : 0;
	dst[x*2 + 1] += src[x];
      }
      src += bmp->pitch;
      dst += pm->pm_linesize;
    }
    break;

  default:
    return;
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


typedef struct item {
  glyph_t *g;
  int16_t kerning;
  int16_t adv_x;
  int code;
} item_t;


/**
 *
 */
static struct pixmap *
text_render0(const uint32_t *uc, const int len, int flags, int size,
	     int max_width, int max_lines, const char *family)
{
  pixmap_t *pm;
  FT_UInt prev = 0;
  FT_BBox bbox;
  FT_Vector pen, delta;
  int err;
  int pen_x, pen_y;

  int i, j, row_height;
  glyph_t *g;
  int siz_x, start_x, start_y;
  int target_width, target_height;
  int origin_y;
  int ellipsize_width;
  int lines = 0;
  line_t *li, *lix;
  struct line_queue lq;
  item_t *items;

  int pmflags = 0;
  uint8_t style;

  if(size < 3)
    return NULL;

  bbox.xMin = 0;
  bbox.yMin = 0;
  max_width *= 64;

  TAILQ_INIT(&lq);

  /* Compute xsize of three dots, for ellipsize */
  g = glyph_get(HORIZONTAL_ELLIPSIS_UNICODE, size, 0, family);
  if(g)
    ellipsize_width = g->adv_x;
  else {
    flags &= ~TR_RENDER_ELLIPSIZE;
    ellipsize_width = 0;
  }

  /* Compute position for each glyph */
  pen_x = 0;
  pen_y = 0;
  style = 0;
  prev = 0;
  li = NULL;

  items = malloc(sizeof(item_t) * len);

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
    case TR_CODE_START:
      if(i != 0)
	li = NULL;
      continue;

    case '\n':
    case TR_CODE_NEWLINE:
      li = NULL;
      continue;
      
    case TR_CODE_CENTER_ON:
      if(i != 0)
	li = NULL;
      else
	li->center = 1;
      center = 1;
      continue;

    case TR_CODE_CENTER_OFF:
      if(i != 0)
	li = NULL;
      else
	li->center = 0;
      center = 0;
      continue;

    case TR_CODE_ITALIC_ON:
      style |= TR_STYLE_ITALIC;
      break;

    case TR_CODE_ITALIC_OFF:
      style &= ~TR_STYLE_ITALIC;
      break;

    case TR_CODE_BOLD_ON:
      style |= TR_STYLE_BOLD;
      break;

    case TR_CODE_BOLD_OFF:
      style &= ~TR_STYLE_BOLD;
      break;

    default:
      break;
    }

    if(uc[i] > 0x7f000000)
      continue;

    if(li->start == -1)
      li->start = out;

    if((g = glyph_get(uc[i], size, style, family)) == NULL)
      continue;

    if(li == TAILQ_FIRST(&lq)) {
      FT_Face f = g->face->face;
      bbox.yMin = MIN(bbox.yMin, 64 * f->descender * size / f->units_per_EM);
    }

    if(FT_HAS_KERNING(g->face->face) && g->gi && prev) {
      FT_Get_Kerning(g->face->face, prev, g->gi, FT_KERNING_DEFAULT, &delta); 
      items[out].kerning = delta.x;
    } else {
      items[out].kerning = 0;
    }
    items[out].adv_x = g->adv_x;
    items[out].g = g;
    items[out].code = uc[i];

    prev = g->gi;
    li->count++;
    out++;
  }

  lines = 0;
  siz_x = 0;

  TAILQ_FOREACH(li, &lq, link) {

    int w = 0;

    for(j = 0; j < li->count; j++) {

      if(j == 0 && (g = items[li->start + j].g) != NULL) {
	w += g->bbox.xMin;
	bbox.xMin = MIN(g->bbox.xMin, bbox.xMin);
      }

      if(j == li->count - 1 && (g = items[li->start + j].g) != NULL)
	w += g->bbox.xMax;

      int d = items[li->start + j].adv_x + 
	(j > 0 ? items[li->start + j].kerning : 0);

      
      if(lines < max_lines - 1 && w + d >= max_width && j < li->count - 1) {
	int k = j;
	int w2 = w;

	while(k > 0 && items[li->start + k - 1].code != ' ') {
	  k--;
	  w2 -= items[li->start + k].adv_x + 
	    (k > 0 ? items[li->start + k].kerning : 0);
	}

	if(k > 0) {
	  lix = alloca(sizeof(line_t));
	  lix->start = li->start + k;
	  lix->count = li->count - k;
	  lix->xspace = 0;
	  lix->center = 0;

	  TAILQ_INSERT_AFTER(&lq, li, lix, link);

	  pmflags |= PIXMAP_TEXT_WRAPPED;
	  k--;
	  w2 -= items[li->start + k].adv_x + 
	    (k > 0 ? items[li->start + k].kerning : 0);

	  li->count = k;
	  w = w2;
	  break;
	}
      }

      if(lines == max_lines - 1 && (flags & TR_RENDER_ELLIPSIZE) &&
	 w >= max_width - ellipsize_width) {

	while(j > 0 && items[li->start + j - 1].code == ' ') {
	  j--;
	  w -= items[li->start + j].adv_x + 
	    (j > 0 ? items[li->start + j].kerning : 0);
	}
	
	items[li->start + j].g = glyph_get(HORIZONTAL_ELLIPSIS_UNICODE,
					   size, 0, family);
	items[li->start + j].kerning = 0;
	pmflags |= PIXMAP_TEXT_ELLIPSIZED;

	w += ellipsize_width;
	li->count = j + 1;
	break;
      }
      if(j < li->count - 1)
	w += d;
    }
    
    li->width = w;
    siz_x = MAX(w, siz_x);
    lines++;
  }

  if(siz_x < 5) {
    free(items);
    return NULL;
  }

  target_width  = siz_x / 64 + 3;

  if(max_lines > 1) {
    TAILQ_FOREACH(li, &lq, link) {
      if(li->center)
	continue;

      int spaces = 0;
      int spill = siz_x - li->width;
      for(i = li->start; i < li->start + li->count; i++) {
	if(items[i].code == ' ')
	  spaces++;
      }
      if((float)spill / li->width < 0.2)
	li->xspace = spaces ? spill / spaces : 0;
    }
  }

  target_height = lines * size;
  row_height = 64 * target_height / lines;

  origin_y = (64 * (lines - 1) * size) - bbox.yMin;

  start_x = -bbox.xMin;
  start_y = 0;

  // --- allocate and init pixmap

  pm = calloc(1, sizeof(pixmap_t));
  pm->pm_refcount = 1;
  pm->pm_lines = lines;
  pm->pm_flags = pmflags;
  pm->pm_codec = CODEC_ID_NONE;
  pm->pm_width = target_width;
  pm->pm_height = target_height;

  pm->pm_pixfmt = PIX_FMT_Y400A;
  pm->pm_linesize = target_width * 2;
  pm->pm_pixels = calloc(1, pm->pm_linesize * pm->pm_height);

  if(flags & TR_RENDER_DEBUG) {
    uint8_t *data = pm->pm_pixels;
    for(i = 0; i < target_height; i+=3)
      memset(data + i * pm->pm_linesize, 0xff, pm->pm_linesize);
  }

  if(flags & TR_RENDER_CHARACTER_POS) {
    pm->pm_charposlen = len;
    pm->pm_charpos = malloc(2 * pm->pm_charposlen * sizeof(int));
  }

  pen_y = 0;

  TAILQ_FOREACH(li, &lq, link) {
    pen_x = 0;
    
    if(li->center)
      pen_x += (siz_x - li->width) / 2;

    for(i = li->start; i < li->start + li->count; i++) {
      g = items[i].g;
      if(g == NULL)
	continue;

      pen_x += items[i].kerning;
      
      pen.x = start_x + pen_x + 31;
      pen.y = start_y + pen_y + origin_y + 31;

      pen.x &= ~63;
      pen.y &= ~63;

      FT_BitmapGlyph bmp = (FT_BitmapGlyph)g->glyph;
      err = FT_Glyph_To_Bitmap((FT_Glyph*)&bmp, FT_RENDER_MODE_NORMAL, &pen, 0);
      if(err == 0) {
	draw_glyph(pm, &bmp->bitmap, 
		   bmp->left,
		   target_height - bmp->top,
		   i);
	FT_Done_Glyph((FT_Glyph)bmp);
      }

      if(pm->pm_charpos != NULL && items[i].code == ' ')
	pm->pm_charpos[2 * i + 0] = pen_x / 64;

      pen_x += items[i].adv_x;
      if(items[i].code == ' ')
	pen_x += li->xspace;

      if(pm->pm_charpos != NULL && items[i].code == ' ')
	pm->pm_charpos[2 * i + 1] = pen_x / 64;
    }
    pen_y -= row_height;
  }
  free(items);
  return pm;
}


/**
 *
 */
struct pixmap *
text_render(const uint32_t *uc, const int len, int flags, int size,
	    int max_width, int max_lines, const char *family)
{
  struct pixmap *pm;

  hts_mutex_lock(&text_mutex);

  pm = text_render0(uc, len, flags, size, max_width, max_lines, family);
  while(num_glyphs > 512)
    glyph_flush();

  faces_purge();

  hts_mutex_unlock(&text_mutex);

  return pm;
}


/**
 *
 */
int
freetype_init(void)
{
  int error;

  error = FT_Init_FreeType(&text_library);
  if(error) {
    TRACE(TRACE_ERROR, "Freetype", "Freetype init error\n");
    return -1;
  }
  TAILQ_INIT(&faces);
  TAILQ_INIT(&allglyphs);
  hts_mutex_init(&text_mutex);
  arch_preload_fonts();
  return 0;
}


/**
 *
 */
void
freetype_load_font(const char *url)
{
  face_t *f;
  hts_mutex_lock(&text_mutex);

  f = face_create(url);
  if(f != NULL)
    f->refcount++;  // Make sure it never is unloaded

  hts_mutex_unlock(&text_mutex);
}
