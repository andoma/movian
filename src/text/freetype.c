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
#include FT_STROKER_H

#define ver(maj, min, pat) ((maj) * 100000 + (min) * 100 + (pat))

#define ftver ver(FREETYPE_MAJOR, FREETYPE_MINOR, FREETYPE_PATCH)

static FT_Library text_library;
static hts_mutex_t text_mutex;


#define GLYPH_HASH_SIZE 128
#define GLYPH_HASH_MASK (GLYPH_HASH_SIZE-1)
TAILQ_HEAD(glyph_queue, glyph);
LIST_HEAD(glyph_list, glyph);
TAILQ_HEAD(face_queue, face);
LIST_HEAD(family_list, family);

//----------------- Family name <-> id map --------------

typedef struct family {
  LIST_ENTRY(family) link;
  char *name;
  int id;
} family_t;

static int family_id_tally;
static struct family_list families;

//------------------------- Faces -----------------------

typedef struct face {
  TAILQ_ENTRY(face) link;

  FT_Face face;
  char *url;
 
  int *family_id_vec;
  uint8_t style;
  uint8_t persistent;

  struct glyph_list glyphs;

} face_t;

static struct face_queue faces;


//------------------------- Glyph cache -----------------------

typedef struct glyph {
  int uc;
  int16_t size;
  uint8_t style;

  face_t *face;
  LIST_ENTRY(glyph) face_link;

  FT_UInt gi;

  LIST_ENTRY(glyph) hash_link;
  TAILQ_ENTRY(glyph) lru_link;
  FT_Glyph orig_glyph;
  FT_Glyph bmp;
  FT_Glyph outline;
  int adv_x;

  FT_BBox bbox;

} glyph_t;

static struct glyph_list glyph_hash[GLYPH_HASH_SIZE];
static struct glyph_queue allglyphs;
static int num_glyphs;


/**
 *
 */
static int
family_get(const char *name)
{
  family_t *f;

  if(name == NULL)
    return 0;

  char *n2 = mystrdupa(name), *e;
  e = strrchr(n2, ' ');
  if(e != NULL) {
    e++;
    if(!strcasecmp(e, "thin") ||
       !strcasecmp(e, "light") ||
       !strcasecmp(e, "bold") ||
       !strcasecmp(e, "heavy"))
      e[-1] = 0;
  }

  LIST_FOREACH(f, &families, link)
    if(!strcasecmp(n2, f->name))
      break;
  
  if(f == NULL) {
    f = malloc(sizeof(family_t));
    f->id = ++family_id_tally;
    f->name = strdup(n2);
  } else {
    LIST_REMOVE(f, link);
  }
  LIST_INSERT_HEAD(&families, f, link);
  return f->id;
}


/**
 *
 */
__attribute__((unused)) static const char *
family_get_by_id(int id)
{
  family_t *f;

  LIST_FOREACH(f, &families, link)
    if(f->id == id)
      return f->name;
  return NULL;
}




/**
 *
 */
static void
glyph_destroy(glyph_t *g)
{
  LIST_REMOVE(g, face_link);
  TAILQ_REMOVE(&allglyphs, g, lru_link);
  LIST_REMOVE(g, hash_link);
  FT_Done_Glyph(g->orig_glyph);
  if(g->bmp)
    FT_Done_Glyph(g->bmp);
  if(g->outline)
    FT_Done_Glyph(g->outline);
  free(g);
  num_glyphs--;
}


/**
 *
 */
static void
face_destroy(face_t *f)
{
  glyph_t *g;
  while((g = LIST_FIRST(&f->glyphs)) != NULL)
    glyph_destroy(g);

  TRACE(TRACE_DEBUG, "Freetype", "Unloading '%s' [%s] originally from %s",
	f->face->family_name, f->face->style_name, f->url ?: "memory");
  TAILQ_REMOVE(&faces, f, link);
  free(f->url);
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

    if(LIST_FIRST(&f->glyphs) == NULL && f->persistent == 0)
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
remove_face_alias(int family_id)
{
  int i;
  face_t *f;
  TAILQ_FOREACH(f, &faces, link) {
    for(i = 1; f->family_id_vec[i] != 0; i++) {
      if(f->family_id_vec[i] == family_id) {
	while(1) {
	  f->family_id_vec[i] = f->family_id_vec[i+1];
	  if(f->family_id_vec[i] == 0)
	    break;
	  i++;
	}
      }
    }
  }
}


/**
 *
 */
static face_t *
face_create_epilogue(face_t *face, const char *source)
{
  const char *family = face->face->family_name;
  const char *style = face->face->style_name;
  TRACE(TRACE_DEBUG, "Freetype", "Loaded '%s' [%s] from %s",
	family, style, source);

  if(style != NULL) {
    char *f = mystrdupa(style), *tmp = NULL;
    const char *tok;
    while((tok = strtok_r(f, " ", &tmp)) != NULL) {
      f = NULL;
      if(!strcasecmp(tok, "bold"))
	face->style = TR_STYLE_BOLD;
      if(!strcasecmp(tok, "italic"))
	face->style = TR_STYLE_ITALIC;
    }
  }

  face->family_id_vec = calloc(2, sizeof(int));
  face->family_id_vec[0] = family_get(family);

  FT_Select_Charmap(face->face, FT_ENCODING_UNICODE);

  remove_face_alias(family_get(family));

  TAILQ_INSERT_TAIL(&faces, face, link);
  return face;
}


/**
 *
 */
static face_t *
face_create_from_uri(const char *path)
{
  char errbuf[256];
  FT_Open_Args oa = {0};
  FT_Error err;
  size_t s;

  fa_handle_t *fh = fa_open(path, errbuf, sizeof(errbuf));
  if(fh == NULL) {
    TRACE(TRACE_ERROR, "Freetype", "Unable to load font: %s -- %s",
	  path, errbuf);
    return NULL;
  }

  s = fa_fsize(fh);
  if(s < 0) {
    TRACE(TRACE_ERROR, "Freetype",
	  "Unable to load font: %s -- Not a seekable file",
	  path);
    fa_close(fh);
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
    TRACE(TRACE_ERROR, "Freetype",
	  "Unable to create font face: %s 0x%x", path, err);
    free(face);
    free(srec);
    return NULL;
  }
  face->url = strdup(path);

  return face_create_epilogue(face, path);
}



/**
 *
 */
static face_t *
face_create_from_memory(const void *ptr, size_t len)
{
  face_t *face = calloc(1, sizeof(face_t));

  if(FT_New_Memory_Face(text_library, ptr, len, 0, &face->face)) {
    free(face);
    return NULL;
  }
  return face_create_epilogue(face, "memory");
}



/**
 *
 */
static int
face_resolve(int uc, uint8_t style, int family_id,
	     char *urlbuf, size_t urllen)
{
#if ENABLE_LIBFONTCONFIG
  if(!fontconfig_resolve(uc, style, family_get_by_id(family_id),
			 urlbuf, urllen))
    return 0;
#endif

#ifdef SHOWTIME_FONT_LIBERATION_URL
  snprintf(urlbuf, urllen,
	   SHOWTIME_FONT_LIBERATION_URL"/LiberationSans-Regular.ttf");
  return 0;
#endif
  return -1;
}


/**
 *
 */
static int
face_is_family(face_t *f, int family_id)
{
  int i = 0;
  while(f->family_id_vec[i] != 0)
    if(f->family_id_vec[i++] == family_id)
      return 1;
  return 0;
}



/**
 *
 */
static void
face_set_family(face_t *f, int family_id)
{
  int len = 0;

  if(f->family_id_vec != NULL)
    while(f->family_id_vec[len] != 0)
      len++;
#if 0
  printf("Font %s alias to %s\n",
	 f->family_id_vec ? family_get_by_id(f->family_id_vec[0]) : "<yet unnamed>",
	 family_get_by_id(family_id));
#endif

  f->family_id_vec = realloc(f->family_id_vec, sizeof(int) * (len + 2));
  f->family_id_vec[len] = family_id;
  f->family_id_vec[len+1] = 0;
}


/**
 *
 */
static face_t *
face_find(int uc, uint8_t style, int family_id)
{
  face_t *f;
  char url[URL_MAX];

  // Try already loaded faces
  TAILQ_FOREACH(f, &faces, link)
    if(face_is_family(f, family_id) &&
       f->style == style &&
       FT_Get_Char_Index(f->face, uc))
      return f;

  TAILQ_FOREACH(f, &faces, link)
    if(face_is_family(f, family_id) &&
       f->style == 0 &&
       FT_Get_Char_Index(f->face, uc))
      return f;

  if(!face_resolve(uc, style, family_id, url, sizeof(url))) {
    TAILQ_FOREACH(f, &faces, link)
      if(f->url != NULL && !strcmp(f->url, url))
	break;


    if(f != NULL) {
      // Same family, just return
      if(face_is_family(f, family_id))
	return f;
    
      face_set_family(f, family_id);
      return f;
    }

    f = face_create_from_uri(url);
  }

  if(f == NULL) {
    TAILQ_FOREACH(f, &faces, link)
      if(f->style == style && FT_Get_Char_Index(f->face, uc))
	break;
  }
  if(f == NULL) {
    TAILQ_FOREACH(f, &faces, link)
      if(f->style == 0 && FT_Get_Char_Index(f->face, uc))
	break;
  }

  if(f != NULL)
    face_set_family(f, family_id);

  return f;
}


/**
 *
 */
static glyph_t *
glyph_get(int uc, int size, uint8_t style, int family_id)
{
  int err, hash = (uc ^ size ^ style) & GLYPH_HASH_MASK;
  glyph_t *g;
  FT_GlyphSlot gs;

  LIST_FOREACH(g, &glyph_hash[hash], hash_link)
    if(g->uc == uc &&
       g->size == size &&
       g->style == style && 
       face_is_family(g->face, family_id))
      break;

  if(g == NULL) {
    face_t *f;
    FT_UInt gi = 0;

    f = face_find(uc, style, family_id);

    if(f == NULL) {
      f = face_find(uc, 0, family_id);
      if(f == NULL)
	return NULL;
    }

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

    if((err = FT_Get_Glyph(gs, &g->orig_glyph)) != 0) {
      free(g);
      return NULL;
    }

    FT_Glyph_Get_CBox(g->orig_glyph, FT_GLYPH_BBOX_GRIDFIT, &g->bbox);

    g->gi = gi;
    LIST_INSERT_HEAD(&f->glyphs, g, face_link);
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
glyph_flush_one(void)
{
  glyph_t *g = TAILQ_FIRST(&allglyphs);
  assert(g != NULL);
  glyph_destroy(g);
}


/**
 *
 */
static void
draw_glyph(pixmap_t *pm, int left, int top, FT_Bitmap *bmp, int color)
{
  pixmap_t src;
  src.pm_type = PIXMAP_I;
  src.pm_pixels = bmp->buffer;
  src.pm_width = bmp->width;
  src.pm_height = bmp->rows;
  src.pm_linesize = bmp->width;
  pixmap_composite(pm, &src, left, top, color);
}


static int
is_not_gray(uint32_t rgb)
{
  uint8_t r = rgb;
  uint8_t g = rgb >> 8;
  uint8_t b = rgb >> 16;
  return (r != g) || (g != b);
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
  char alignment;
  int height;
  int descender;
  int shadow;
  int outline;
  int default_height;
  uint32_t color;
  enum {
    LINE_TYPE_TEXT = 0,
    LINE_TYPE_HR,

  } type;
} line_t;


typedef struct item {
  glyph_t *g;
  int code;
  uint32_t color;
  uint32_t shadow_color;
  uint32_t outline_color;
  int16_t kerning;
  int16_t adv_x;
  uint16_t outline;
  uint16_t shadow;
} item_t;


static const float legacy_size_mult[16] = {
  0,
  0.5,
  0.75,
  1.0,
  1.25,
  1.5,
  2.0,
  3.0
};


/**
 *
 */
static struct pixmap *
text_render0(const uint32_t *uc, const int len,
	     int flags, int default_size, float scale,
	     int global_alignment, int max_width, int max_lines,
	     const char *family)
{
  const int default_family_id = family_get(family ?: "Arial");
  int family_id = default_family_id;
  pixmap_t *pm;
  FT_UInt prev = 0;
  FT_BBox bbox;
  FT_Vector pen, delta;
  FT_Stroker stroker = NULL;
  int pen_x, pen_y;

  int i, j;
  glyph_t *g = NULL;
  int siz_x, start_x, start_y;
  int lines = 0;
  line_t *li, *lix;
  struct line_queue lq;
  item_t *items;

  int pmflags = 0;
  uint8_t style;
  int color_output = 0;

  int current_size = default_size * scale;
  uint32_t current_color = 0xffffff;
  uint32_t current_alpha = 0xff000000;

  int current_outline = 0;
  uint32_t current_outline_color = 0x00;
  uint32_t current_outline_alpha = 0xff000000;

  int current_shadow = 0;
  uint32_t current_shadow_color = 0x00;
  uint32_t current_shadow_alpha = 0xff000000;

  if(current_size < 3 || scale < 0.001)
    return NULL;

  bbox.xMin = 0;
  bbox.yMin = 0;
  max_width *= 64;

  TAILQ_INIT(&lq);


  /* Compute position for each glyph */
  pen_x = 0;
  pen_y = 0;
  style = 0;

  if(flags & TR_RENDER_BOLD)
    style |= TR_STYLE_BOLD;

  if(flags & TR_RENDER_ITALIC)
    style |= TR_STYLE_ITALIC;

  if(flags & TR_RENDER_SHADOW)
    current_shadow = -1;

  if(flags & TR_RENDER_OUTLINE)
    current_outline = 64;

  prev = 0;
  li = NULL;

  items = malloc(sizeof(item_t) * len);

  int out = 0;
  int alignment = global_alignment;

  for(i = 0; i < len; i++) {

    if(li == NULL) {
      li = alloca(sizeof(line_t));
      li->default_height = current_size;
      li->type = LINE_TYPE_TEXT;
      li->start = -1;
      li->count = 0;
      li->xspace = 0;
      li->alignment = alignment;
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

    case TR_CODE_HR:
      li = alloca(sizeof(line_t));
      li->default_height = current_size;
      li->type = LINE_TYPE_HR;
      li->start = -1;
      li->count = 0;
      li->xspace = 0;
      li->alignment = 0;
      li->height = 4;
      li->color = current_color | current_alpha;
      TAILQ_INSERT_TAIL(&lq, li, link);
      li = NULL;
      continue;
      
    case TR_CODE_CENTER_ON:
      if(i != 0)
	li = NULL;
      else
	li->alignment = TR_ALIGN_CENTER;
      alignment = TR_ALIGN_CENTER;
      continue;

    case TR_CODE_CENTER_OFF:
      alignment = global_alignment;
      if(i != 0)
	li = NULL;
      else
	li->alignment = global_alignment;
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

    case TR_CODE_FONT_RESET:
      current_size = default_size * scale;
      current_color = 0xffffff;
      current_alpha = 0xff000000;
      family_id = default_family_id;
      break;

    case TR_CODE_SIZE_PX ... TR_CODE_SIZE_PX + 0xffff:
      current_size = (uc[i] & 0xffff) * scale;
      break;

    case TR_CODE_COLOR ... TR_CODE_COLOR + 0xffffff:
      current_color = uc[i] & 0xffffff; // BGR host order
      color_output |= is_not_gray(current_color);
      break;

    case TR_CODE_SHADOW_COLOR ... TR_CODE_SHADOW_COLOR + 0xffffff:
      current_shadow_color = uc[i] & 0xffffff; // BGR host order
      color_output |= is_not_gray(current_shadow_color);
      break;

    case TR_CODE_OUTLINE_COLOR ... TR_CODE_OUTLINE_COLOR + 0xffffff:
      current_outline_color = uc[i] & 0xffffff; // BGR host order
      color_output |= is_not_gray(current_outline_color);
      break;

    case  TR_CODE_FONT_FAMILY ...  TR_CODE_FONT_FAMILY + 0xffffff:
      family_id = uc[i] & 0xffffff;
      break;

    case TR_CODE_ALPHA ... TR_CODE_ALPHA + 0xff:
      current_alpha = (uc[i] & 0xff) << 24;
      break;

    case TR_CODE_SHADOW_ALPHA ... TR_CODE_SHADOW_ALPHA + 0xff:
      current_shadow_alpha = (uc[i] & 0xff) << 24;
      break;

    case TR_CODE_OUTLINE_ALPHA ... TR_CODE_OUTLINE_ALPHA + 0xff:
      current_outline_alpha = (uc[i] & 0xff) << 24;
      break;

    case TR_CODE_SHADOW ... TR_CODE_SHADOW + 0xffff:
      current_shadow = (uc[i] & 0xffff) * scale;
      break;

    case TR_CODE_OUTLINE ... TR_CODE_OUTLINE + 0xffff:
      current_outline = 64 * (uc[i] & 0xffff) * scale;
      break;

    case TR_CODE_FONT_SIZE + 1 ... TR_CODE_FONT_SIZE + 7:
      current_size = legacy_size_mult[uc[i] & 0xf] * default_size * scale;
      break;

    default:
      break;
    }

    if(uc[i] >= 0x70000000)
      continue;

    if(li->start == -1)
      li->start = out;

    if((g = glyph_get(uc[i], current_size, style, family_id)) == NULL)
      continue;

    if(FT_HAS_KERNING(g->face->face) && g->gi && prev) {
      FT_Get_Kerning(g->face->face, prev, g->gi, FT_KERNING_DEFAULT, &delta); 
      items[out].kerning = delta.x;
    } else {
      items[out].kerning = 0;
    }
    items[out].adv_x = g->adv_x;
    items[out].g = g;
    items[out].code = uc[i];
    items[out].color = current_color | current_alpha;

    items[out].outline = current_outline;
    items[out].outline_color = current_outline_color | current_outline_alpha;

    if(current_shadow == -1)
      items[out].shadow = 1 + current_shadow / 20;
    else
      items[out].shadow = current_shadow;

    items[out].shadow_color = current_shadow_color | current_shadow_alpha;

    prev = g->gi;
    li->count++;
    out++;
  }

  lines = 0;
  siz_x = 0;

  TAILQ_FOREACH(li, &lq, link) {

    int w = 0;

    if(li->type == LINE_TYPE_HR)
      continue;

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
	  lix->default_height = li->default_height;
	  lix->type = LINE_TYPE_TEXT;
	  lix->start = li->start + k;
	  lix->count = li->count - k;
	  lix->xspace = 0;
	  lix->alignment = global_alignment;
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


      if(lines == max_lines - 1 && (flags & TR_RENDER_ELLIPSIZE) && g != NULL) {
	glyph_t *eg = glyph_get(HORIZONTAL_ELLIPSIS_UNICODE, g->size, 0,
				g->face->family_id_vec[0]);
	if(eg != NULL) {
	  int ellipsize_width = g->adv_x;
	  if(w >= max_width - ellipsize_width) {

	    while(j > 0 && items[li->start + j - 1].code == ' ') {
	      j--;
	      w -= items[li->start + j].adv_x + 
		(j > 0 ? items[li->start + j].kerning : 0);
	    }
	    
	    items[li->start + j].g = eg;
	    items[li->start + j].kerning = 0;
	    pmflags |= PIXMAP_TEXT_ELLIPSIZED;
	    
	    w += ellipsize_width;
	    li->count = j + 1;
	    break;
	  }
	}
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

  int target_width  = siz_x / 64 + 3; /// +3 ???
  int target_height = 0;
  int margin = 0;

  TAILQ_FOREACH(li, &lq, link) {

    int height = 0;
    int descender = 0;
    int shadow = 0;
    int outline = 0;
    int topspill = 0;

    switch(li->type) {

    case LINE_TYPE_TEXT:

      for(i = li->start; i < li->start + li->count; i++) {
	glyph_t *g = items[i].g;
	FT_Face f = g->face->face;
	height = MAX(g->size, height);
	descender = MIN(descender,
			64 * f->descender * g->size / f->units_per_EM);
	shadow = MAX(items[i].shadow, shadow);
	outline = MAX(items[i].outline, outline);

	topspill = MAX(topspill, g->bbox.yMax - height * 64 - descender);
      }

      li->height = height ?: li->default_height;
      li->descender = descender;
      li->shadow = shadow;
      li->outline = outline;

      if(li == TAILQ_FIRST(&lq))
	margin = MAX(margin, 2 * li->outline + topspill);

      if(li == TAILQ_LAST(&lq, line_queue))
	margin = MAX(margin, MAX(li->shadow*64, li->outline*2));

      if(max_lines > 1 && li->alignment == TR_ALIGN_JUSTIFIED) {
	int spaces = 0;
	int spill = siz_x - li->width;
	for(i = li->start; i < li->start + li->count; i++) {
	  if(items[i].code == ' ')
	    spaces++;
	}
	if((float)spill / li->width < 0.2)
	  li->xspace = spaces ? spill / spaces : 0;
      }
      break;

    case LINE_TYPE_HR:
      break;
    }
    
    target_height += li->height;
  }

  int origin_y = target_height * 64;
  start_x = -bbox.xMin;
  start_y = 0;

  margin = (margin + 63) / 64;

  // --- allocate and init pixmap

  pm = pixmap_create(target_width + margin*2, target_height + margin*2,
		     color_output ? PIXMAP_BGR32 : PIXMAP_IA, 1);

  pm->pm_lines = lines;
  pm->pm_flags = pmflags;
  pm->pm_margin = margin;
  
  if(flags & TR_RENDER_DEBUG) {
    uint8_t *data = pm->pm_pixels;
    for(i = 0; i < pm->pm_height; i+=3)
      memset(data + i * pm->pm_linesize, 0xc0, pm->pm_linesize);

    int y;
    int l = color_output ? 4 : 2;
    for(i = 0; i < pm->pm_width; i+=3)
      for(y = 0; y < pm->pm_height; y++)
	memset(data + y * pm->pm_linesize + i * l, 0xc0, l);
  }

  if(flags & TR_RENDER_CHARACTER_POS) {
    pm->pm_charposlen = len;
    pm->pm_charpos = malloc(2 * pm->pm_charposlen * sizeof(int));
  }

  pen_y = 0;

  TAILQ_FOREACH(li, &lq, link) {

    pen_y -= li->height * 64;

    if(li->type == LINE_TYPE_HR) {
      int ypos = 0;
      ypos = target_height - (pen_y + li->height * 64);
			      
      ypos = ypos >> 6;
      ypos = MIN(target_height, MAX(0, ypos));

      
      switch(pm->pm_type) {
      case PIXMAP_BGR32: 
	{
	  uint32_t *yptr = (uint32_t *)(pm->pm_pixels + ypos * pm->pm_linesize);
	  int i;
	  for(i = 0; i < pm->pm_width; i++)
	    *yptr++ = li->color;

	  yptr = (uint32_t *)(pm->pm_pixels + (ypos + 1) * pm->pm_linesize);
	  uint32_t color;

	  uint8_t r = li->color;
	  uint8_t g = li->color >> 8;
	  uint8_t b = li->color >> 16;
	  uint8_t a = li->color >> 24;

	  color = (a << 24) | ((b >> 1) << 16) |
	    ((g >> 1) << 8) | (r >> 1);
	  
	  for(i = 0; i < pm->pm_width; i++)
	    *yptr++ = color;
	}
	break;
	
      case PIXMAP_IA:
	{
	  uint8_t *yptr = pm->pm_pixels + ypos * pm->pm_linesize;
	  int i;
	  uint8_t r = li->color;
	  uint8_t a = li->color >> 24;
	  for(i = 0; i < pm->pm_width; i++) {
	    *yptr++ = li->color;
	    *yptr++ = li->color >> 24;
	  }

	  yptr = pm->pm_pixels + (ypos + 1) * pm->pm_linesize;

	  r = li->color >> 1;
	  
	  for(i = 0; i < pm->pm_width; i++) {
	    *yptr++ = r;
	    *yptr++ = a;
	  }
	}
	break;
	
      default:
	break;
      }
      continue;
    }

    pen_x = 0;
    
    switch(li->alignment) {
    case TR_ALIGN_LEFT:
    case TR_ALIGN_JUSTIFIED:
      break;
    case TR_ALIGN_CENTER:
      pen_x += (siz_x - li->width) / 2;
      break;
    case TR_ALIGN_RIGHT:
      pen_x += li->width - siz_x;
      break;
    }

    for(i = li->start; i < li->start + li->count; i++) {

      g = items[i].g;
      if(g == NULL)
	continue;

      pen_x += items[i].kerning;
      
      pen.x = start_x + pen_x + 31;
      pen.y = start_y + pen_y + origin_y + 31 - li->descender;

      pen.x &= ~63;
      pen.y &= ~63;

      pen.x >>= 6;
      pen.y >>= 6;


      
      if(items[i].outline > 0 && g->outline == NULL) {
	g->outline = g->orig_glyph;
	
	if(stroker == NULL)
	  FT_Stroker_New(text_library, &stroker);
	
	FT_Stroker_Set(stroker,
		       items[i].outline,
		       FT_STROKER_LINECAP_ROUND,
		       FT_STROKER_LINEJOIN_ROUND,
		       0);
	if(FT_Glyph_StrokeBorder(&g->outline, stroker, 0, 0))
	  g->outline = NULL;
	else if(FT_Glyph_To_Bitmap(&g->outline, FT_RENDER_MODE_NORMAL, NULL, 1))
	  g->outline = NULL;
      }
      
      if(g->bmp == NULL) {
	g->bmp = g->orig_glyph;
	if(FT_Glyph_To_Bitmap(&g->bmp, FT_RENDER_MODE_NORMAL, NULL, 0))
	  g->bmp = NULL;
      }
      if(items[i].shadow && (g->outline != NULL || g->bmp != NULL)) {
	FT_BitmapGlyph bmp = (FT_BitmapGlyph)(g->outline ?: g->bmp);
	draw_glyph(pm,
		   bmp->left + items[i].shadow + margin + pen.x,
		   target_height - bmp->top + items[i].shadow + margin - pen.y,
		   &bmp->bitmap, 
		   items[i].shadow_color);
      }

      if(items[i].outline > 0 && g->outline != NULL) {
	FT_BitmapGlyph bmp = (FT_BitmapGlyph)g->outline;
	draw_glyph(pm,
		   bmp->left + margin + pen.x,
		   target_height - bmp->top + margin - pen.y,
		   &bmp->bitmap, 
		   items[i].outline_color);
      }

      if(g->bmp != NULL) {
	FT_BitmapGlyph bmp = (FT_BitmapGlyph)g->bmp;
	draw_glyph(pm,
		   bmp->left + margin + pen.x,
		   target_height - bmp->top + margin - pen.y,
		   &bmp->bitmap, 
		   items[i].color);

	if(pm->pm_charpos != NULL) {
	  pm->pm_charpos[i * 2 + 0] = bmp->left + pen.x;
	  pm->pm_charpos[i * 2 + 1] = bmp->left + bmp->bitmap.width + pen.x;
	}
      }

      if(pm->pm_charpos != NULL && items[i].code == ' ')
	pm->pm_charpos[2 * i + 0] = pen_x / 64;

      pen_x += items[i].adv_x;
      if(items[i].code == ' ')
	pen_x += li->xspace;

      if(pm->pm_charpos != NULL && items[i].code == ' ')
	pm->pm_charpos[2 * i + 1] = pen_x / 64;
    }
  }
  free(items);

  if(stroker != NULL)
    FT_Stroker_Done(stroker);

  return pm;
}


/**
 *
 */
struct pixmap *
text_render(const uint32_t *uc, const int len, int flags, int default_size,
	    float scale, int alignment, int max_width, int max_lines,
	    const char *family)
{
  struct pixmap *pm;

  hts_mutex_lock(&text_mutex);

  pm = text_render0(uc, len, flags, default_size, scale, alignment, 
		    max_width, max_lines, family);
  while(num_glyphs > 512)
    glyph_flush_one();

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

  f = face_create_from_uri(url);
  if(f != NULL)
    f->persistent = 1;  // Make sure it never is auto unloaded

  hts_mutex_unlock(&text_mutex);
}


/**
 *
 */
void *
freetype_load_font_from_memory(const void *ptr, size_t len)
{
  face_t *f;
  hts_mutex_lock(&text_mutex);

  f = face_create_from_memory(ptr, len);
  if(f != NULL)
    f->persistent = 1;  // Make sure it never is auto unloaded

  hts_mutex_unlock(&text_mutex);
  return f;
}


/**
 *
 */
void
freetype_unload_font(void *ref)
{
  face_t *f = ref;
  hts_mutex_lock(&text_mutex);
  face_destroy(f);
  hts_mutex_unlock(&text_mutex);
}


/**
 *
 */
int
freetype_family_id(const char *str)
{
  hts_mutex_lock(&text_mutex);
  int id = family_get(str);
  hts_mutex_unlock(&text_mutex);
  return id;
}
