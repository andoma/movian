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
#include "misc/minmax.h"
#include <assert.h>

#include "main.h"
#include "misc/queue.h"
#include "misc/str.h"
#include "image/pixmap.h"
#include "image/image.h"
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
static FT_Stroker text_stroker;
static hts_mutex_t text_mutex;
static int font_domain_tally = 10;

#define GLYPH_HASH_SIZE 128
#define GLYPH_HASH_MASK (GLYPH_HASH_SIZE-1)
TAILQ_HEAD(glyph_queue, glyph);
LIST_HEAD(glyph_list, glyph);
LIST_HEAD(face_list, face);
LIST_HEAD(idmap_list, idmap);

//----------------- generica name <-> id map --------------

typedef struct idmap {
  LIST_ENTRY(idmap) link;
  char *name;
  int id;
  int domain;
} idmap_t;

static int idmap_id_tally;
static struct  idmap_list idmaps;

//------------------------- Faces -----------------------

typedef struct face {
  LIST_ENTRY(face) link;

  FT_Face face;
  char *url;
  int current_size;
  char *family;
  char *fullname;
  uint8_t style;
  int font_domain;
  struct glyph_list glyphs;
  int prio;
  int refcount;
  buf_t *buf;  // Used when faces are loaded from memory
  // For glyph caching

  char *lookup_name;
  int lookup_font_domain;

} face_t;

static struct face_list static_faces;
static struct face_list dynamic_faces;

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
  int outline_amt;
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
id_from_str(const char *str, int domain)
{
  idmap_t *im;
  LIST_FOREACH(im, &idmaps, link)
    if(!strcmp(str, im->name) && im->domain == domain)
      return im->id;
  im = malloc(sizeof(idmap_t));
  im->name = strdup(str);
  im->domain = domain;
  im->id = ++idmap_id_tally;
  LIST_INSERT_HEAD(&idmaps, im, link);
  return im->id;
}


/**
 *
 */
static idmap_t *
idmap_find(int id)
{
  idmap_t *im;
  LIST_FOREACH(im, &idmaps, link)
    if(id == im->id)
      return im;
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
	f->face->family_name, f->face->style_name, f->url);
  LIST_REMOVE(f, link);
  buf_release(f->buf);
  free(f->url);
  free(f->family);
  free(f->fullname);
  free(f->lookup_name);
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
  for(f = LIST_FIRST(&dynamic_faces); f != NULL; f = n) {
    n = LIST_NEXT(f, link);

    if(f->refcount == 0 && LIST_FIRST(&f->glyphs) == NULL)
      face_destroy(f);
  }
}


/**
 *
 */
static void
faces_flush_lookup(void)
{
  face_t *f;
  LIST_FOREACH(f, &dynamic_faces, link) {
    mystrset(&f->lookup_name, NULL);
    f->lookup_font_domain = -1;
  }
  LIST_FOREACH(f, &static_faces, link) {
    mystrset(&f->lookup_name, NULL);
    f->lookup_font_domain = -1;
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
static int
face_cmp(const face_t *a, const face_t *b)
{
  return a->prio - b->prio;
}

/**
 *
 */
static face_t *
face_create_epilogue(face_t *face, const char *url,
		     struct face_list *faces, int prio, int font_domain)
{
  const char *family = face->face->family_name;
  const char *style = face->face->style_name;
  char buf[256];

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
  face->url = strdup(url);
  face->family = strdup(family);
  snprintf(buf, sizeof(buf), "%s %s", family, style);
  face->fullname = strdup(buf);

  mystrlower(face->family);
  mystrlower(face->fullname);

  TRACE(TRACE_DEBUG, "Freetype",
	"Loaded font family='%s' fullname='%s' style='%s' from %s domain:%d",
	face->family, face->fullname, style, face->url, font_domain);



  FT_Select_Charmap(face->face, FT_ENCODING_UNICODE);

  // Flush any lookup caches
  faces_flush_lookup();

  face->font_domain = font_domain;
  face->prio = prio;
  if(prio == 0)
    LIST_INSERT_HEAD(faces, face, link);
  else
    LIST_INSERT_SORTED(faces, face, link, face_cmp, face_t);

  face->lookup_font_domain = -1;

  return face;
}


/**
 *
 */
static face_t *
face_create_from_fh(fa_handle_t *fh, const char *url,
		    char *errbuf, size_t errlen,
		    struct face_list *faces, int prio, int font_domain)
{
  FT_Open_Args oa = {0};
  FT_Error err;
  int64_t s;
  face_t *face;

  s = fa_fsize(fh);
  if(s < 0) {
    snprintf(errbuf, errlen, "Not a seekable file");
    fa_close(fh);
    return NULL;
  }

  face = calloc(1, sizeof(face_t));

  FT_Stream srec = calloc(1, sizeof(FT_StreamRec));
  srec->size = fa_fsize(fh);
  srec->descriptor.pointer = fh;
  srec->read = face_read;
  srec->close = face_close;

  oa.stream = srec;
  oa.flags = FT_OPEN_STREAM;

  if((err = FT_Open_Face(text_library, &oa, 0, &face->face)) != 0) {
    snprintf(errbuf, errlen, "Unable to open face: %d", err);
    free(face);
    free(srec);
    return NULL;
  }

  return face_create_epilogue(face, url, faces, prio, font_domain);
}


/**
 *
 */
static face_t *
face_create_from_mem(buf_t *b, char *errbuf, size_t errlen,
		    struct face_list *faces, int prio, int font_domain)
{
  FT_Open_Args oa = {0};
  FT_Error err;
  face_t *face;

  face = calloc(1, sizeof(face_t));
  oa.flags = FT_OPEN_MEMORY;
  oa.memory_base = (const void *)buf_cstr(b);
  oa.memory_size = buf_len(b);

  if((err = FT_Open_Face(text_library, &oa, 0, &face->face)) != 0) {
    snprintf(errbuf, errlen, "Unable to open face: %d", err);
    free(face);
    return NULL;
  }

  face->buf = buf_retain(b);
  return face_create_epilogue(face, "memory", faces, prio, font_domain);
}


/**
 *
 */
static face_t *
face_create_from_uri(const char *path, fa_resolver_t *far,
		     struct face_list *faces, int prio, int font_domain)
{
  char errbuf[256];
  face_t *face;

  LIST_FOREACH(face, faces, link) {
    if(!strcmp(face->url, path) && face->font_domain == font_domain) {
      return face;
    }
  }

  fa_handle_t *fh = fa_open_resolver(path, far, errbuf, sizeof(errbuf), 0,
                                     NULL);
  if(fh == NULL) {
    TRACE(TRACE_ERROR, "Freetype", "Unable to load font: %s -- %s",
	  path, errbuf);
    return NULL;
  }

  face = face_create_from_fh(fh, path, errbuf, sizeof(errbuf),
			     faces, prio, font_domain);
  if(face == NULL) {
    TRACE(TRACE_ERROR, "Freetype", "Unable to load font: %s -- %s",
	  path, errbuf);
    return NULL;
  }
  return face;
}


/**
 *
 */
static face_t *
face_resolve(int uc, uint8_t style, const char *name, int font_domain,
	     fa_resolver_t *far)
{
  face_t *f;
  if(name != NULL) {

    if(fa_can_handle(name, NULL, 0)) {
      f = face_create_from_uri(name, far, &dynamic_faces, 0, font_domain);
      if(f != NULL && FT_Get_Char_Index(f->face, uc))
	return f;
    }

    face_t *best = NULL;
    int best_score = 0; // Higher is better

    // Try to find best matching font amongst our loaded faces

    char *lcname = mystrdupa(name);
    mystrlower(lcname);

    LIST_FOREACH(f, &dynamic_faces, link) {
      /*
       * Faces that can't render our glyph is bad
       */
      if(!FT_Get_Char_Index(f->face, uc))
	continue;
      /*
       * Always want to match font domain here
       */
      if(f->font_domain != font_domain)
	continue;

      if(!strcmp(f->url, name))
	return f;

      /*
       * If we want no style and face have style, ignore it since we can
       * create italic and bold glyphs from normal faces but not the
       * other way around
       */
      if(f->style && style == 0)
	continue;

      int score = 0;

      // Correct style give some extra points
      if(f->style == style)
	score += 4;

      if(!strcmp(f->fullname, lcname)) {
	score += 100;
      } else if(!strcmp(f->family, lcname)) {
	score += 90;
      } else if(strstr(lcname, f->fullname)) {
	score += 80;
      } else if(strstr(lcname, f->family)) {
	score += 70;
      } else if(strstr(f->fullname, lcname)) {
	score += 60;
      } else if(strstr(f->family, lcname)) {
	score += 50;
      }

      if(score > best_score && score > 10) {
	best_score = score;
	best = f;
      }
    }

    if(best)
      return best;
  }


  LIST_FOREACH(f, &static_faces, link) {
    if(f->style == style && FT_Get_Char_Index(f->face, uc))
      return f;
  }

  LIST_FOREACH(f, &static_faces, link) {
    if(f->style == 0 && FT_Get_Char_Index(f->face, uc))
      return f;
  }

#if ENABLE_LIBFONTCONFIG
  char url[URL_MAX];
  if(!fontconfig_resolve(uc, style, name, url, sizeof(url))) {
    f = face_create_from_uri(url, NULL, &dynamic_faces, 0, font_domain);
    if(f != NULL)
      return f;
  }
#endif

  // Last resort, anything that has the glyph
  LIST_FOREACH(f, &dynamic_faces, link)
    if(FT_Get_Char_Index(f->face, uc))
      return f;
  return NULL;
}


/**
 *
 */
static face_t *
face_find(int uc, uint8_t style, const char *name, int font_domain,
	  fa_resolver_t *far)
{
  face_t *f = face_resolve(uc, style, name, font_domain, far);
#if 0
  printf("Resolv %c (0x%x) [style=0x%x, font: %s] -> %s\n",
	 uc, uc, style, name ?: "<unset>",
	 f ? f->url : "<none>");
#endif
  if(f == NULL)
    return NULL;

  f->lookup_font_domain = font_domain;
  if(strcmp(name ?: "", f->lookup_name ?: ""))
    mystrset(&f->lookup_name, name);

  return f;
}


/**
 *
 */
static void
face_set_size(face_t *f, int size)
{
  if(f->current_size == size)
    return;

  FT_Size_RequestRec  req;
  req.type = FT_SIZE_REQUEST_TYPE_REAL_DIM;
  req.width = 0;
  req.height = size << 6;
  req.horiResolution = 0;
  req.vertResolution = 0;
  FT_Request_Size(f->face, &req);
  f->current_size = size;
}


/**
 *
 */
static glyph_t *
glyph_get(int uc, int size, uint8_t style, const char *font,
	  int font_domain, fa_resolver_t *far)
{
  int err, hash = (uc ^ size ^ style) & GLYPH_HASH_MASK;
  glyph_t *g;
  FT_GlyphSlot gs;

  LIST_FOREACH(g, &glyph_hash[hash], hash_link) {
    if(g->uc != uc || g->size != size || g->style != style)
      continue;

    if(!strcmp(g->face->lookup_name ?: "", font ?: "") &&
       g->face->lookup_font_domain == font_domain)
      break;
  }

  if(g == NULL) {
    face_t *f;
    FT_UInt gi = 0;

    f = face_find(uc, style, font, font_domain, far);

    if(f == NULL) {
      f = face_find(uc, 0, font, font_domain, far);
      if(f == NULL)
	return NULL;
    }

    gi = FT_Get_Char_Index(f->face, uc);

    face_set_size(f, size);

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
  src.pm_data = bmp->buffer;
  src.pm_width = bmp->width;
  src.pm_height = bmp->rows;
  src.pm_linesize = bmp->width;
  pixmap_composite(pm, &src, left, top, color);
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
  int xoffset;
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
  char set_margin;
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
static void
draw_glyphs(pixmap_t *pm, struct line_queue *lq, int target_height,
	    int siz_x, item_t *items, int start_x, int start_y,
	    int origin_y, int margin, int pass,
            image_component_text_info_t *ti)
{
  FT_Vector pen;
  line_t *li;
  int i;
  int pen_y = 0;
  int pen_x = 0;
  glyph_t *g;

  TAILQ_FOREACH(li, lq, link) {

    pen_y -= li->height * 64;

    if(li->type == LINE_TYPE_HR) {
      int ypos = 0;
      ypos = target_height - (pen_y + li->height * 64);

      ypos = ypos >> 6;
      ypos = MIN(target_height, MAX(0, ypos));


      switch(pm->pm_type) {
      case PIXMAP_BGR32:
	{
	  uint32_t *yptr = (uint32_t *)(pm->pm_data + ypos * pm->pm_linesize);
	  int i;
	  for(i = 0; i < pm->pm_width; i++)
	    *yptr++ = li->color;

	  yptr = (uint32_t *)(pm->pm_data + (ypos + 1) * pm->pm_linesize);
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
	  uint8_t *yptr = pm->pm_data + ypos * pm->pm_linesize;
	  int i;
	  uint8_t r = li->color;
	  uint8_t a = li->color >> 24;
	  for(i = 0; i < pm->pm_width; i++) {
	    *yptr++ = li->color;
	    *yptr++ = li->color >> 24;
	  }

	  yptr = pm->pm_data + (ypos + 1) * pm->pm_linesize;

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


    switch(li->alignment) {
    case TR_ALIGN_LEFT:
    case TR_ALIGN_JUSTIFIED:
      pen_x = 0;
      break;
    case TR_ALIGN_CENTER:
      pen_x = (siz_x - li->width) / 2;
      break;
    case TR_ALIGN_RIGHT:
      pen_x = siz_x - li->width;
      break;
    }

    pen_x = MAX(pen_x, 0) + li->xoffset;

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



      if(items[i].outline > 0 && (g->outline == NULL ||
				  g->outline_amt != items[i].outline)) {
	if(g->outline)
	  FT_Done_Glyph(g->outline);

	g->outline = g->orig_glyph;
	FT_Stroker_Set(text_stroker,
		       items[i].outline,
		       FT_STROKER_LINECAP_ROUND,
		       FT_STROKER_LINEJOIN_ROUND,
		       0);
	g->outline_amt = items[i].outline;
	if(FT_Glyph_StrokeBorder(&g->outline, text_stroker, 0, 0))
	  g->outline = NULL;
	else if(FT_Glyph_To_Bitmap(&g->outline, FT_RENDER_MODE_NORMAL, NULL, 1))
	  g->outline = NULL;
      }

      if(g->bmp == NULL) {
	g->bmp = g->orig_glyph;
	if(FT_Glyph_To_Bitmap(&g->bmp, FT_RENDER_MODE_NORMAL, NULL, 0))
	  g->bmp = NULL;
      }
      if(pass == 0 && items[i].shadow && (g->outline != NULL || g->bmp != NULL)) {
	FT_BitmapGlyph bmp = (FT_BitmapGlyph)(g->outline ?: g->bmp);
	draw_glyph(pm,
		   bmp->left + items[i].shadow + margin + pen.x,
		   target_height - bmp->top + items[i].shadow + margin - pen.y,
		   &bmp->bitmap,
		   items[i].shadow_color);
      }

      if(pass == 1 && items[i].outline > 0 && g->outline != NULL) {
	FT_BitmapGlyph bmp = (FT_BitmapGlyph)g->outline;
	draw_glyph(pm,
		   bmp->left + margin + pen.x,
		   target_height - bmp->top + margin - pen.y,
		   &bmp->bitmap,
		   items[i].outline_color);
      }

      if(pass == 2 && g->bmp != NULL) {
	FT_BitmapGlyph bmp = (FT_BitmapGlyph)g->bmp;
	draw_glyph(pm,
		   bmp->left + margin + pen.x,
		   target_height - bmp->top + margin - pen.y,
		   &bmp->bitmap,
		   items[i].color);

	if(ti != NULL && ti->ti_charpos != NULL) {
	  ti->ti_charpos[i * 2 + 0] = bmp->left + pen.x;
	  ti->ti_charpos[i * 2 + 1] = bmp->left + bmp->bitmap.width + pen.x;
	}
      }

      if(ti != NULL && ti->ti_charpos != NULL && items[i].code == ' ')
	ti->ti_charpos[2 * i + 0] = pen_x / 64;

      pen_x += items[i].adv_x;
      if(items[i].code == ' ')
	pen_x += li->xspace;

      if(ti != NULL && ti->ti_charpos != NULL && items[i].code == ' ')
	ti->ti_charpos[2 * i + 1] = pen_x / 64;
    }
  }
}

/**
 *
 */
static struct image *
text_render0(const uint32_t *uc, const int len,
	     int flags, int default_size, float scale,
	     int global_alignment, int max_width, int max_lines,
	     const char *default_font, int default_domain,
	     int min_size, fa_resolver_t *far)
{
  FT_UInt prev = 0;
  FT_BBox bbox = {0};
  FT_Vector delta;
  FT_Stroker stroker = NULL;

  int i, j;
  glyph_t *g = NULL;
  int siz_x, start_x, start_y;
  int lines = 0;
  line_t *li, *lix;
  struct line_queue lq;
  item_t *items;

  int ti_flags = 0;
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

  int need_shadow_pass = 0;
  int need_outline_pass = 0;

  const char *current_font = default_font;
  int current_domain = default_domain;
  idmap_t *im;

  if(min_size > 0 && current_size < min_size) {
    scale = (float)min_size / current_size;
    current_size = default_size * scale;
  }

  if(current_size < 3 || scale < 0.001)
    return NULL;

  max_width *= 64;

  TAILQ_INIT(&lq);


  /* Compute position for each glyph */
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
  int set_margin = 0;

  for(i = 0; i < len; i++) {

    if(li == NULL) {
      li = alloca(sizeof(line_t));
      li->default_height = current_size;
      li->type = LINE_TYPE_TEXT;
      li->start = -1;
      li->count = 0;
      li->xspace = 0;
      li->xoffset = 0;
      li->alignment = alignment;
      TAILQ_INSERT_TAIL(&lq, li, link);
      prev = 0;
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
      li->xoffset = 0;
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

      current_font   = default_font;
      current_domain = default_domain;
      break;

    case TR_CODE_SIZE_PX ... TR_CODE_SIZE_PX + 0xffff:
      current_size = (uc[i] & 0xffff) * scale;
      break;

    case TR_CODE_COLOR ... TR_CODE_COLOR + 0xffffff:
      current_color = uc[i] & 0xffffff; // BGR host order
      color_output |= color_is_not_gray(current_color);
      break;

    case TR_CODE_SHADOW_COLOR ... TR_CODE_SHADOW_COLOR + 0xffffff:
      current_shadow_color = uc[i] & 0xffffff; // BGR host order
      color_output |= color_is_not_gray(current_shadow_color);
      break;

    case TR_CODE_OUTLINE_COLOR ... TR_CODE_OUTLINE_COLOR + 0xffffff:
      current_outline_color = uc[i] & 0xffffff; // BGR host order
      color_output |= color_is_not_gray(current_outline_color);
      break;

    case  TR_CODE_FONT_FAMILY ...  TR_CODE_FONT_FAMILY + 0xffffff:
      im = idmap_find(uc[i] & 0xffffff);
      if(im != NULL) {
	current_font   = im->name;
	current_domain = im->domain;
      }

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

    case TR_CODE_SHADOW_US ... TR_CODE_SHADOW_US + 0xffff:
      current_shadow = (uc[i] & 0xffff);
      break;

    case TR_CODE_OUTLINE_US ... TR_CODE_OUTLINE_US + 0xffff:
      current_outline = 64 * (uc[i] & 0xffff);
      break;

    case TR_CODE_FONT_SIZE + 1 ... TR_CODE_FONT_SIZE + 7:
      current_size = legacy_size_mult[uc[i] & 0xf] * default_size * scale;
      break;

    case TR_CODE_SET_MARGIN:
      set_margin = 1;
      break;

    default:
      break;
    }

    if(uc[i] >= 0x70000000)
      continue;

    if(li->start == -1)
      li->start = out;

    if((g = glyph_get(uc[i], current_size, style, current_font, current_domain,
		      far)) == NULL)
      continue;

    if(FT_HAS_KERNING(g->face->face) && g->gi && prev) {
      face_set_size(g->face, current_size);
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

    need_outline_pass |= items[out].outline;

    if(current_shadow == -1)
      items[out].shadow = 1 + current_size / 20;
    else
      items[out].shadow = current_shadow;

    items[out].set_margin = set_margin;
    set_margin = 0;

    need_shadow_pass |= items[out].shadow;

    items[out].shadow_color = current_shadow_color | current_shadow_alpha;

    prev = g->gi;
    li->count++;
    out++;
  }

  lines = 0;
  siz_x = 0;
  int wrap_margin = 0;

  line_t *next;
  for(li = TAILQ_FIRST(&lq); li != NULL ; li = next) {
    next = TAILQ_NEXT(li, link);

    if(lines == max_lines) {
      TAILQ_REMOVE(&lq, li, link);
      continue;
    }

    int w = li->xoffset;

    if(li->type == LINE_TYPE_HR)
      continue;

    for(j = 0; j < li->count; j++) {

      w += items[li->start +j].adv_x;

      if(j > 0)
        w += items[li->start + j].kerning;

      if(j == 0 && (g = items[li->start + j].g) != NULL) {
	w += g->bbox.xMin;
	bbox.xMin = MIN(g->bbox.xMin, bbox.xMin);
      }

      if(items[li->start + j].set_margin)
	wrap_margin = w;

      if(lines < max_lines - 1 && w >= max_width) {
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
	  lix->xoffset = wrap_margin;
	  lix->alignment = global_alignment;
	  TAILQ_INSERT_AFTER(&lq, li, lix, link);
	  next= lix;
	  ti_flags |= IMAGE_TEXT_WRAPPED;
	  k--;
	  w2 -= items[li->start + k].adv_x +
	    (k > 0 ? items[li->start + k].kerning : 0);

	  li->count = k;
	  w = w2;
	  break;
	}
      }


      if(lines == max_lines - 1 && g != NULL && max_width) {

	if(flags & TR_RENDER_ELLIPSIZE) {
	  glyph_t *eg = glyph_get(HORIZONTAL_ELLIPSIS_UNICODE, g->size, 0,
				  g->face->url, g->face->font_domain,
				  far);
	  if(w > max_width - eg->adv_x) {

	    while(j > 0 && items[li->start + j - 1].code == ' ') {
	      j--;
	      w -= items[li->start + j].adv_x +
		(j > 0 ? items[li->start + j].kerning : 0);
	    }

	    items[li->start + j].g = eg;
	    items[li->start + j].kerning = 0;
	    ti_flags |= IMAGE_TEXT_TRUNCATED;

	    w += eg->adv_x;
	    li->count = j + 1;
	    break;
	  }
	} else {

	  if(w > max_width) {
	    ti_flags |= IMAGE_TEXT_TRUNCATED;
	    li->count = j;
	    break;
	  }
	}
      }
    }

    li->width = w;
    siz_x = MAX(w, siz_x);
    lines++;

  }

  if(siz_x < 5) {
    free(items);
    return NULL;
  }

  if(max_width && siz_x > max_width) {
    siz_x = max_width;
  }

  int target_width  = siz_x / 64;
  int target_height = 0;

  TAILQ_FOREACH(li, &lq, link) {
    if(li->type == LINE_TYPE_HR)
      continue;

    int w = 0;
    for(j = 0; j < li->count; j++) {
      int d = items[li->start + j].adv_x +
        (j > 0 ? items[li->start + j].kerning : 0);

      if ((g = items[li->start + j].g) != NULL) {
        bbox.xMin = MIN(w + g->bbox.xMin, bbox.xMin);
        bbox.xMax = MAX(w + g->bbox.xMax, bbox.xMax);
      }

      w += d;
    }
  }


  if(max_width && bbox.xMax > max_width)
    bbox.xMax = max_width;

  int margin = MAX(-MIN(bbox.xMin, 0), MAX(0, bbox.xMax - siz_x));

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
  start_x = 0;
  start_y = 0;

  margin = (margin + 63) / 64;

  // --- allocate and init image

  image_t *img = image_alloc(flags & TR_RENDER_NO_OUTPUT ? 1 : 2);

  img->im_width  = target_width  + margin * 2;
  img->im_height = target_height + margin * 2;
  img->im_margin = margin;

  pixmap_t *pm = NULL;

  if(!(flags & TR_RENDER_NO_OUTPUT)) {
    pm = pixmap_create(target_width, target_height,
                       color_output ? PIXMAP_BGR32 : PIXMAP_IA, margin);

    img->im_components[1].type = IMAGE_PIXMAP;
    img->im_components[1].pm = pm;
  }
  image_component_text_info_t *ti = &img->im_components[0].text_info;
  img->im_components[0].type = IMAGE_TEXT_INFO;

  ti->ti_lines = lines;
  ti->ti_flags = ti_flags;


  if(flags & TR_RENDER_CHARACTER_POS) {
    ti->ti_charposlen = len;
    ti->ti_charpos = malloc(2 * len * sizeof(int));
  }

  if(pm != NULL) {

    if(flags & TR_RENDER_DEBUG) {
      uint8_t *data = pm->pm_data;
      for(i = 0; i < pm->pm_height; i+=3)
        memset(data + i * pm->pm_linesize, 0xc0, pm->pm_linesize);

      int y;
      int l = color_output ? 4 : 2;
      for(i = 0; i < pm->pm_width; i+=3)
        for(y = 0; y < pm->pm_height; y++)
          memset(data + y * pm->pm_linesize + i * l, 0xc0, l);
    }

    if(need_shadow_pass) {
      draw_glyphs(pm, &lq, target_height, siz_x, items, start_x, start_y,
                  origin_y, margin, 0, NULL);
      pixmap_box_blur(pm, 4, 4);
    }

    if(need_outline_pass)
      draw_glyphs(pm, &lq, target_height, siz_x, items, start_x, start_y,
                  origin_y, margin, 1, NULL);


    draw_glyphs(pm, &lq, target_height, siz_x, items, start_x, start_y,
                origin_y, margin, 2, ti);
  }
  free(items);

  if(stroker != NULL)
    FT_Stroker_Done(stroker);

  return img;
}


/**
 *
 */
struct image *
text_render(const uint32_t *uc, const int len, int flags, int default_size,
	    float scale, int alignment, int max_width, int max_lines,
	    const char *family, int context, int min_size,
            fa_resolver_t *far)
{
  struct image *im;

  hts_mutex_lock(&text_mutex);

  im = text_render0(uc, len, flags, default_size, scale, alignment,
		    max_width, max_lines, family, context, min_size,
		    far);
  while(num_glyphs > 512)
    glyph_flush_one();

  faces_purge();

  hts_mutex_unlock(&text_mutex);

  return im;
}


/**
 *
 */
void
freetype_load_default_font(const char *url, int prio)
{
  hts_mutex_lock(&text_mutex);
  face_create_from_uri(url, NULL, &static_faces, prio, 0);
  hts_mutex_unlock(&text_mutex);
}


/**
 *
 */
static void
freetype_init(void)
{
  int error;
  char url[512];

  error = FT_Init_FreeType(&text_library);
  if(error) {
    TRACE(TRACE_ERROR, "Freetype", "Freetype init error %d", error);
    exit(1);
  }
  FT_Stroker_New(text_library, &text_stroker);
  TAILQ_INIT(&allglyphs);
  hts_mutex_init(&text_mutex);

  snprintf(url, sizeof(url),
	   "%s/res/fonts/liberation/LiberationSans-Regular.ttf",
	   app_dataroot());

  freetype_load_default_font(url, 0);

#ifdef __APPLE__
  freetype_load_default_font("file:///Library/Fonts/Arial Unicode.ttf", 1);
#endif
}

INITME(INIT_GROUP_GRAPHICS, freetype_init, NULL, 0);


/**
 *
 */
void *
freetype_load_dynamic_font_fh(fa_handle_t *fh, const char *url, int font_domain,
			      char *errbuf, size_t errlen)
{
  hts_mutex_lock(&text_mutex);

  face_t *f = face_create_from_fh(fh, url, errbuf, errlen,
				  &dynamic_faces, 0, font_domain);
  if(f != NULL)
    f->refcount++;

  hts_mutex_unlock(&text_mutex);
  return f;
}


/**
 *
 */
void *
freetype_load_dynamic_font_buf(buf_t *b, int font_domain,
                               char *errbuf, size_t errlen)
{
  hts_mutex_lock(&text_mutex);

  face_t *f = face_create_from_mem(b, errbuf, errlen,
                                   &dynamic_faces, 0, font_domain);
  if(f != NULL)
    f->refcount++;

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
  if(--f->refcount == 0)
    face_destroy(f);
  hts_mutex_unlock(&text_mutex);
}


/**
 *
 */
int
freetype_family_id(const char *str, int font_domain)
{
  hts_mutex_lock(&text_mutex);
  int id = id_from_str(str, font_domain);
  hts_mutex_unlock(&text_mutex);
  return id;
}


/**
 *
 */
int
freetype_get_context(void)
{
  int id;
  hts_mutex_lock(&text_mutex);
  id = ++font_domain_tally;
  hts_mutex_unlock(&text_mutex);
  return id;
}
