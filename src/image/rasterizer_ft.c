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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "misc/minmax.h"
#include "pixmap.h"
#include "main.h"
#include "image.h"
#include "vector.h"

#include <ft2build.h>  
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_SYNTHESIS_H
#include FT_STROKER_H

static FT_Library ft_lib;
static HTS_MUTEX_DECL(ft_mutex);

typedef struct state {
  FT_Stroker stroker;
  int inpath;
  int stroke_width;
  int stroke_color;
  int fill_enable;
  int fill_color;
  float cur[2];
} state_t;




static void
toVector(FT_Vector *v, const float *f)
{
  v->x = f[0] * 64;
  v->y = f[1] * 64;
}


static void
cmd_move(state_t *state, const float *pt)
{
  FT_Vector v;
  state->cur[0] = pt[0];
  state->cur[1] = pt[1];
  toVector(&v, pt);

  if(state->inpath > 1)
    FT_Stroker_EndSubPath(state->stroker);
  FT_Stroker_BeginSubPath(state->stroker, &v, 0);
  state->inpath = 1;
}



#define FT_EPSILON  2
#define FT_IS_SMALL( x )  ( (x) > -FT_EPSILON && (x) < FT_EPSILON )

static void
cmd_curve(state_t *state, const float *s, const float *c,
	  const float *d, const float *e)
{
  FT_Vector S,C,D,E;

  toVector(&S, s);
  toVector(&C, c);
  toVector(&D, d);
  toVector(&E, e);


  // if all control points are coincident this is a no-op
  // This is checked by freetype but not reported and drawing
  // a subpath with 0 elements will result in a segfault
  // so we need to check this ourselves
  if(FT_IS_SMALL( S.x - C.x) &&
     FT_IS_SMALL( S.y - C.y) &&
     FT_IS_SMALL( C.x - D.x) &&
     FT_IS_SMALL( C.y - D.y) &&
     FT_IS_SMALL( D.x - E.x) &&
     FT_IS_SMALL( D.y - E.y))
    return;

  
  if(state->inpath) {
    FT_Stroker_CubicTo(state->stroker, &C, &D, &E);
    state->inpath++;

    state->cur[0] = e[0];
    state->cur[1] = e[1];
  }
}


static void
cmd_line(state_t *state, const float *a)
{
  FT_Vector v;
  state->cur[0] = a[0];
  state->cur[1] = a[1];
  toVector(&v, a);
  if(state->inpath) {
    FT_Stroker_LineTo(state->stroker, &v);
    state->inpath++;
  }
}


/**
 *
 */
static void
cmd_close(state_t *state)
{
  if(state->inpath > 1) {
    FT_Stroker_EndSubPath(state->stroker);
    state->inpath = 0;
  } else {
    state->inpath = 1;
  }
}




struct raster_params {
  pixmap_t *pm;
  int left;
  int top;
  int color;
};

#define DIV255(x) (((((x)+255)>>8)+(x))>>8)
#define FIXMUL(a, b) (((a) * (b) + 255) >> 8)
#define FIX3MUL(a, b, c) (((a) * (b) * (c) + 65535) >> 16)


static void
rasterize_bgr32(const int yy, const int count, const FT_Span * const spans,
		void * const user)
{
  struct raster_params *rp = user;
  pixmap_t *pm = rp->pm;
  int s, y, x, len, j;
  uint8_t *d;
  uint32_t rgba = rp->color;
  uint8_t a0 = rp->color >> 24;
  uint8_t b0 = rp->color >> 16;
  uint8_t g0 = rp->color >> 8;
  uint8_t r0 = rp->color;

  y = yy;

  if(y < 0 || y >= pm->pm_height)
    return;

  d = pm_pixel(pm, 0, y);

  for(s = 0; s < count; s++) {
    x = rp->left + spans[s].x;
    len = spans[s].len;
    if(x < 0) {
      len += x;
      x = 0;
    }
    if(x + len >= pm->pm_width)
      len = pm->pm_width - x;
  
    if(spans[s].coverage == 0xff && a0 == 0xff) {
      uint32_t *dd = (uint32_t *)d + x;
      for(j = 0; j < len; j++)
	*dd++ = rgba;

    } else {
      uint32_t *dst = (uint32_t *)d + x;
      uint32_t u32;
      int SA0 = DIV255(spans[s].coverage * a0);
      for(j = 0; j < len; j++) {

	int SA = SA0;
	int SR = r0;
	int SG = g0;
	int SB = b0;

	u32 = *dst;
	int DR =  u32        & 0xff;
	int DG = (u32 >> 8)  & 0xff;
	int DB = (u32 >> 16) & 0xff;
	int DA = (u32 >> 24) & 0xff;

	int FA = SA + DIV255((255 - SA) * DA);

	if(FA == 0) {
	  SA = 0;
	  u32 = 0;
	} else {
	  if(FA != 255)
	    SA = SA * 255 / FA;

	  DA = 255 - SA;
	  
	  DB = DIV255(SB * SA + DB * DA);
	  DG = DIV255(SG * SA + DG * DA);
	  DR = DIV255(SR * SA + DR * DA);
	  
	  u32 = FA << 24 | DB << 16 | DG << 8 | DR;
	}
	*dst = u32;
	dst++;
      }
    }
  }
}

static void
rasterize_ia(const int yy, const int count, const FT_Span * const spans,
	     void * const user)
{
  struct raster_params *rp = user;
  pixmap_t *pm = rp->pm;
  int s, y, x, len, j;
  uint8_t *d;
  uint8_t a0 = rp->color >> 24;
  uint8_t i0 = rp->color;
#if defined(__BIG_ENDIAN__)
  uint16_t col = rp->color << 8;
#elif defined(__LITTLE_ENDIAN__) || (__BYTE_ORDER == __LITTLE_ENDIAN)
  uint8_t col = rp->color;
#else
#error Dont know endian
#endif
  y = yy;

  if(y < 0 || y >= pm->pm_height)
    return;

  d = pm_pixel(pm, 0, y);

  for(s = 0; s < count; s++) {
    x = rp->left + spans[s].x;
    len = spans[s].len;
    if(x < 0) {
      len += x;
      x = 0;
    }
    if(x + len >= pm->pm_width)
      len = pm->pm_width - x;

    if(spans[s].coverage == 0xff && a0 == 0xff) {
      uint16_t *dd, u16;
      dd = (uint16_t *)d + x;
#if defined(__BIG_ENDIAN__)
      u16 = col | spans[s].coverage;
#elif defined(__LITTLE_ENDIAN__) || (__BYTE_ORDER == __LITTLE_ENDIAN)
      u16 = col | spans[s].coverage << 8;
#else
#error Dont know endian
#endif

      for(j = 0; j < len; j++)
	*dd++ = u16;

    } else {
      uint8_t *dst = d + x * 2;
      int i, a, pa, y;

      y = FIXMUL(a0, spans[s].coverage);

      for(j = 0; j < len; j++) {
	i = dst[0];
	a = dst[1];

	pa = a;
	a = y + FIXMUL(a, 255 - y);

	if(a) {
	  i = ((FIXMUL(i0, y) + FIX3MUL(i, pa, (255 - y))) * 255) / a;
	} else {
	  i = 0;
	}
	dst[0] = i;
	dst[1] = a;
	dst+=2;
      }
    }
  }
}




/**
 *
 */
static void
rasterize(state_t *s, pixmap_t *pm)
{
  FT_Outline ol;
  FT_UInt points;
  FT_UInt contours;
  FT_Raster_Params params;
  struct raster_params rp;

  memset(&params, 0, sizeof(params));
  params.flags = FT_RASTER_FLAG_AA | FT_RASTER_FLAG_DIRECT;
  params.user = &rp;
  
  switch(pm->pm_type) {
  case PIXMAP_BGR32:
    params.gray_spans = rasterize_bgr32;
    break;
  case PIXMAP_IA:
    params.gray_spans = rasterize_ia;
    break;
  default:
    break;
  }
  rp.pm = pm;
  rp.left = 0;
  rp.top = 0;


  if(s->fill_color) {
    rp.color = s->fill_color;
    FT_Stroker_GetBorderCounts(s->stroker, FT_STROKER_BORDER_LEFT,
			       &points, &contours);

    FT_Outline_New(ft_lib, points, contours, &ol);

    ol.n_contours = 0;
    ol.n_points = 0;

    FT_Stroker_ExportBorder(s->stroker, FT_STROKER_BORDER_LEFT, &ol);
    FT_Outline_Render(ft_lib, &ol, &params);
    FT_Outline_Done(ft_lib, &ol);
  }

  if(s->stroke_width) {
    rp.color = s->stroke_color;
    FT_Stroker_GetCounts(s->stroker, &points, &contours);

    FT_Outline_New(ft_lib, points, contours, &ol);

    ol.n_contours = 0;
    ol.n_points = 0;

    FT_Stroker_Export(s->stroker, &ol);
    FT_Outline_Render(ft_lib, &ol, &params);
    FT_Outline_Done(ft_lib, &ol);
  }

}


/**
 *
 */
image_t *
image_rasterize_ft(const image_component_t *ic,
                   int width, int height, int margin)
{
  const image_component_vector_t *icv = &ic->vector;

  state_t s;
  memset(&s, 0, sizeof(state_t));

  s.stroke_color = 0xffffffff;
  s.fill_color = 0xffffffff;


  pixmap_t *pm = pixmap_create(width, height,
                               icv->icv_colorized ? PIXMAP_BGR32 : PIXMAP_IA,
                               margin);


  hts_mutex_lock(&ft_mutex);

  FT_Stroker_New(ft_lib, &s.stroker);

  const int32_t *i32 = icv->icv_int;
  const float *flt   = icv->icv_flt;

  for(int i = 0; i < icv->icv_used;) {
    switch(i32[i++]) {
    default:
      abort();
    case VC_SET_FILL_ENABLE:
      s.fill_enable = i32[i++];
      break;
    case VC_SET_FILL_COLOR:
      s.fill_color = i32[i++];
      break;
    case VC_SET_STROKE_WIDTH:
      s.stroke_width = i32[i++];
      break;
    case VC_SET_STROKE_COLOR:
      s.stroke_color = i32[i++];
      break;

    case VC_BEGIN:
      FT_Stroker_Rewind(s.stroker);
      FT_Stroker_Set(s.stroker,
                     64 * s.stroke_width ?: 1,
                     FT_STROKER_LINECAP_BUTT,
                     FT_STROKER_LINEJOIN_BEVEL,
                     0);
      break;

    case VC_MOVE_TO:
      cmd_move(&s, flt + i);
      i+=2;
      break;
    case VC_LINE_TO:
      cmd_line(&s, flt + i);
      i+=2;
      break;
    case VC_CUBIC_TO:
      cmd_curve(&s, &s.cur[0], flt + i, flt + i + 2, flt + i + 4);
      i += 6;
      break;
    case VC_END:
      cmd_close(&s);
      rasterize(&s, pm);
      break;
    case VC_CLOSE:
      cmd_close(&s);
      break;
    }
  }

  FT_Stroker_Done(s.stroker);
  hts_mutex_unlock(&ft_mutex);

  image_t *r = image_create_from_pixmap(pm);
  pixmap_release(pm);
  return r;
}


/**
 *
 */
static void
rasterizer_ft_init(void)
{
  int error = FT_Init_FreeType(&ft_lib);
  if(error) {
    TRACE(TRACE_ERROR, "Freetype", "Freetype init error %d", error);
    exit(1);
  }
}

INITME(INIT_GROUP_GRAPHICS, rasterizer_ft_init, NULL, 0);
