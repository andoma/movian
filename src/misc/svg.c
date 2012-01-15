/*
 *  Pixmaps - Helpers for transfer of images between modules
 *  Copyright (C) 2009 Andreas Öman
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

#include <stdio.h>
#include <sys/param.h>
#include "arch/atomic.h"
#include <string.h>
#include <stdlib.h>

#include "pixmap.h"
#include "dbl.h"
#include "htsmsg/htsmsg_xml.h"
#include "misc/string.h"
#include "showtime.h"


#include <ft2build.h>  
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_SYNTHESIS_H
#include FT_STROKER_H

static FT_Library ft_lib;
static hts_mutex_t svg_mutex;

typedef struct svg_state {
  float cur[2];
  float ctm[9];
  pixmap_t *pm;
  FT_Stroker stroker;
  int inpath;
  int *need_color;
  float scaling;
} svg_state_t;

static void svg_parse_g(svg_state_t *s0, htsmsg_t *c);


/**
 *
 */
static void
svg_mtx_identity(float *mtx)
{
  mtx[0] = 1;
  mtx[1] = 0;
  mtx[2] = 0;
  mtx[3] = 0;
  mtx[4] = 1;
  mtx[5] = 0;
  mtx[6] = 0;
  mtx[7] = 0;
  mtx[8] = 1;
}

#if 0
static void
svg_mtx_translate(float *mtx, float x, float y)
{
  mtx[6] += mtx[0]*x + mtx[3]*y;
  mtx[7] += mtx[1]*x + mtx[4]*y;
}
#endif

static void
svg_mtx_scale(float *mtx, float x, float y)
{
  mtx[0] *= x;
  mtx[3] *= y;

  mtx[1] *= x;
  mtx[4] *= y;

  mtx[2] *= x;
  mtx[5] *= y;
}


static void
svg_mtx_vec_mul(float *dst, const float *mtx, const float *a)
{
  dst[0] = mtx[0] * a[0] + mtx[3] * a[1] + mtx[6];
  dst[1] = mtx[1] * a[0] + mtx[4] * a[1] + mtx[7];
}

/**
 *
 */
static void
svg_state_apply_matrix(svg_state_t *state, const float *B)
{
  float t[9];
  float b[9];

  b[0] = B[0];
  b[1] = B[1];
  b[2] = 0;
  b[3] = B[2];
  b[4] = B[3];
  b[5] = 0;
  b[6] = B[4];
  b[7] = B[5];
  b[8] = 1;

  const float *a = state->ctm;

  t[0] = a[0] * b[0] + a[3] * b[1] + a[6] * b[2];
  t[3] = a[0] * b[3] + a[3] * b[4] + a[6] * b[5];
  t[6] = a[0] * b[6] + a[3] * b[7] + a[6] * b[8];

  t[1] = a[1] * b[0] + a[4] * b[1] + a[7] * b[2];
  t[4] = a[1] * b[3] + a[4] * b[4] + a[7] * b[5];
  t[7] = a[1] * b[6] + a[4] * b[7] + a[7] * b[8];

  t[2] = a[2] * b[0] + a[5] * b[1] + a[8] * b[2];
  t[5] = a[2] * b[3] + a[5] * b[4] + a[8] * b[5];
  t[8] = a[2] * b[6] + a[5] * b[7] + a[8] * b[8];

  memcpy(state->ctm, t, 9*sizeof(float));
}

static void
svg_state_translate(svg_state_t *state, const float *p)
{
  state->ctm[6] += state->ctm[0]*p[0] + state->ctm[3]*p[1];
  state->ctm[7] += state->ctm[1]*p[0] + state->ctm[4]*p[1];
}

static void __attribute__((unused))
printmtx(const float *m)
{
  printf("%f\t%f\t%f\n", m[0], m[1], m[2]);
  printf("%f\t%f\t%f\n", m[3], m[4], m[5]);
  printf("%f\t%f\t%f\n", m[6], m[7], m[8]);
}


/**
 *
 */
static void
svg_parse_transform(svg_state_t *s, const char *t)
{
  float values[6];
  int nargs = 0, i;
  void (*fn)(svg_state_t *s, const float *p);
  while(*t < 33 && *t)
    t++;
  if(!*t)
    return;
  if(!strncmp(t, "matrix", strlen("matrix"))) {
    fn = svg_state_apply_matrix;
    nargs = 6;
    t += strlen("matrix");
  } else if(!strncmp(t, "translate", strlen("translate"))) {
    fn = svg_state_translate;
    nargs = 2;
    t += strlen("translate");
  } else {
    return;
  }

  while(*t < 33 && *t)
    t++;
  if(*t != '(')
    return;
  t++;
  for(i = 0; i < nargs; i++) {
    const char *end;
    values[i] = my_str2double(t, &end);
    if(t == end)
      return;
    t = end;
    while(*t < 33 && *t)
      t++;
    if(*t == ',')
      t++;
  }
  fn(s, values);
}

static void
toVector(FT_Vector *v, const float *f)
{
  v->x = f[0] * 64;
  v->y = f[1] * 64;
}

static void
cmd_move(svg_state_t *state, int seq)
{
  float pt[2];
  FT_Vector v;
  svg_mtx_vec_mul(pt, state->ctm, state->cur);

  toVector(&v, pt);
  if(seq) {
    if(state->inpath)
      FT_Stroker_LineTo(state->stroker, &v);
  } else {

    if(state->inpath)
      FT_Stroker_EndSubPath(state->stroker);

    FT_Stroker_BeginSubPath(state->stroker, &v, 0);
    state->inpath = 1;
  }
}


static void
cmd_move_abs(svg_state_t *state, const float *p, int seq)
{
  state->cur[0] = p[0];
  state->cur[1] = p[1];
  cmd_move(state, seq);
}

static void
cmd_move_rel(svg_state_t *state, const float *p, int seq)
{
  state->cur[0] += p[0];
  state->cur[1] += p[1];
  cmd_move(state, seq);
}



static void
cmd_curve(svg_state_t *state, const float *c, const float *d, const float *e)
{
  FT_Vector C,D,E;
  toVector(&C, c);
  toVector(&D, d);
  toVector(&E, e);

  if(state->inpath)
    FT_Stroker_CubicTo(state->stroker, &C, &D, &E);
}

static void
cmd_curveto_rel(svg_state_t *state, const float *p, int seq)
{
  float s[2], c[2], d[3], e[2];

  s[0] = state->cur[0];
  s[1] = state->cur[1];

  c[0] = state->cur[0] + p[0];
  c[1] = state->cur[1] + p[1];

  d[0] = state->cur[0] + p[2];
  d[1] = state->cur[1] + p[3];

  e[0] = state->cur[0] + p[4];
  e[1] = state->cur[1] + p[5];
  
  state->cur[0] = e[0];
  state->cur[1] = e[1];


  float ts[2], tc[2], td[3], te[2];

  svg_mtx_vec_mul(ts, state->ctm, s);
  svg_mtx_vec_mul(tc, state->ctm, c);
  svg_mtx_vec_mul(td, state->ctm, d);
  svg_mtx_vec_mul(te, state->ctm, e);

  cmd_curve(state, tc, td, te);
}


static void
cmd_curveto_abs(svg_state_t *state, const float *p, int seq)
{
  float s[2], c[2], d[3], e[2];

  s[0] = state->cur[0];
  s[1] = state->cur[1];

  c[0] = p[0];
  c[1] = p[1];

  d[0] = p[2];
  d[1] = p[3];

  e[0] = p[4];
  e[1] = p[5];
  
  state->cur[0] = e[0];
  state->cur[1] = e[1];


  float ts[2], tc[2], td[3], te[2];

  svg_mtx_vec_mul(ts, state->ctm, s);
  svg_mtx_vec_mul(tc, state->ctm, c);
  svg_mtx_vec_mul(td, state->ctm, d);
  svg_mtx_vec_mul(te, state->ctm, e);

  cmd_curve(state, tc, td, te);
}

static void
cmd_lineto(svg_state_t *state)
{
  float pt[2];
  svg_mtx_vec_mul(pt, state->ctm, state->cur);

  FT_Vector v;
  toVector(&v, pt);
  if(state->inpath)
    FT_Stroker_LineTo(state->stroker, &v);
}


static void
cmd_lineto_rel(svg_state_t *state, const float *p, int seq)
{
  state->cur[0] += p[0];
  state->cur[1] += p[1];

  cmd_lineto(state);
}




static void
cmd_lineto_abs(svg_state_t *state, const float *p, int seq)
{
  state->cur[0] = p[0];
  state->cur[1] = p[1];
  cmd_lineto(state);
}


/**
 *
 */
static void
cmd_close(svg_state_t *state)
{
  if(state->inpath)
    FT_Stroker_EndSubPath(state->stroker);
  state->inpath = 0;
}


/**
 *
 */
static void
stroke_path(svg_state_t *state, const char *str)
{
  float values[6];
  int num_params = 0;
  int cur_param = 0;
  void (*cur_cmd)(svg_state_t *state, const float *params, int seq) = NULL; 
  int seq = 0;

  while(*str) {
    if(*str < 33) {
      str++;
      continue;
    }

    if(cur_cmd != NULL) {
      const char *endptr;

      double d = my_str2double(str, &endptr);

      if(endptr != str) {
	values[cur_param] = d;
	cur_param++;
	if(cur_param == num_params) {
	  cur_cmd(state, values, seq);
	  cur_param = 0;
	  seq++;
	}

	str = endptr;

	while(*str < 33 && *str)
	  str++;
	if(*str == ',')
	  str++;
	while(*str < 33 && *str)
	  str++;
	continue;
      } 
    }

    while(*str < 33 && *str)
      str++;

    char mode = *str++;
    switch(mode) {
    case 'M':
      cur_cmd = cmd_move_abs;
      num_params = 2;
      break;
    case 'm':
      cur_cmd = cmd_move_rel;
      num_params = 2;
      break;
    case 'c':
      cur_cmd = cmd_curveto_rel;
      num_params = 6;
      break;

    case 'C':
      cur_cmd = cmd_curveto_abs;
      num_params = 6;
      break;

    case 'l':
      cur_cmd = cmd_lineto_rel;
      num_params = 2;
      break;

    case 'L':
      cur_cmd = cmd_lineto_abs;
      num_params = 2;
      break;

    case 'z':
    case 'Z':
      cur_cmd = NULL;
      num_params = 0;
      cmd_close(state);
      break;
    default:
      printf("Cant handle mode %c\n", mode);
      return;
    }
    seq = 0;
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
  d = pm->pm_data + pm->pm_linesize * y;

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
  d = pm->pm_data + pm->pm_linesize * y;

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


static int
stroke_path_element(svg_state_t *s, htsmsg_t *attribs)
{
  const char *d = htsmsg_get_str(attribs, "d");
  if(d == NULL)
    return -1;
  stroke_path(s, d);
  return 0;
}



static int
stroke_rect_element(svg_state_t *s, htsmsg_t *attribs)
{
  const char *str_width  = htsmsg_get_str(attribs, "width");
  const char *str_height = htsmsg_get_str(attribs, "height");
  const char *str_x      = htsmsg_get_str(attribs, "x");
  const char *str_y      = htsmsg_get_str(attribs, "y");
  
  if(str_width == NULL || str_height == NULL || str_x == NULL || str_y == NULL)
    return -1;

  float width  = my_str2double(str_width,  NULL);
  float height = my_str2double(str_height, NULL);
  float x      = my_str2double(str_x,      NULL);
  float y      = my_str2double(str_y,      NULL);

  float v[2];

  v[0] = x;
  v[1] = y;
  cmd_move_abs(s, v, 0);
  v[0] += width;
  cmd_lineto_abs(s, v, 0);
  v[1] += height;
  cmd_lineto_abs(s, v, 0);
  v[0] = x;
  cmd_lineto_abs(s, v, 0);
  cmd_close(s);
  return 0;
}


/**
 *
 */
static void
svg_parse_element(svg_state_t *s0, htsmsg_t *element, 
		  int (*element_parser)(svg_state_t *s, htsmsg_t *element))
{
  svg_state_t s = *s0;
  FT_Outline ol;
  FT_UInt points;
  FT_UInt contours;

  int fill_color = 0xffffffff;
  int stroke_color = 0xffffffff;
  int stroke_width = 0;

  htsmsg_t *a = htsmsg_get_map(element, "attrib");
  if(a == NULL)
    return;

  const char *st = htsmsg_get_str(a, "style");
  if(st != NULL) {
    char *style, *tmp = NULL, *n, *attr;
    n = style = strdup(st);

    while((attr = strtok_r(n, ";", &tmp)) != NULL) {
      char *value = strchr(attr, ':');
      if(value != NULL) {
	*value++ = 0;
	while(*value < 33 && *value)
	  value++;

	if(!strcmp(attr, "fill")) {

	  if(!strcmp(value, "none"))
	    fill_color = 0;
	  else {
	    fill_color = (fill_color & 0xff000000) | html_makecolor(value);
	    if(s.need_color)
	      *s.need_color |= color_is_not_gray(fill_color);
	  }
	} else if(!strcmp(attr, "stroke")) {

	  if(!strcmp(value, "none"))
	    stroke_color = 0;
	  else {
	    stroke_color = (stroke_color & 0xff000000) | html_makecolor(value);
	    if(s.need_color)
	      *s.need_color |= color_is_not_gray(stroke_color);
	  }
	} else if(!strcmp(attr, "stroke-width")) {
	  stroke_width = atoi(value);
	}
      }
      n = NULL;
    }
    free(style);
  }

  if(s.pm == NULL)
    return;

  const char *transform = htsmsg_get_str(a, "transform");
  if(transform != NULL)
    svg_parse_transform(&s, transform);


  FT_Stroker_Rewind(s.stroker);

  if(stroke_width)
    FT_Stroker_Set(s.stroker,
		   64 * stroke_width * s.scaling,
		   FT_STROKER_LINECAP_BUTT,
		   FT_STROKER_LINEJOIN_BEVEL,
		   0);

  s.cur[0] = 0;
  s.cur[1] = 0;

  if(element_parser(&s, a))
    return;

  if(s.inpath) {
    FT_Stroker_EndSubPath(s.stroker);
    s.inpath = 0;
  }

  FT_Raster_Params params;
  struct raster_params rp;

  memset(&params, 0, sizeof(params));
  params.flags = FT_RASTER_FLAG_AA | FT_RASTER_FLAG_DIRECT;
  params.user = &rp;
  
  switch(s.pm->pm_type) {
  case PIXMAP_BGR32:
    params.gray_spans = rasterize_bgr32;
    break;
  case PIXMAP_IA:
    params.gray_spans = rasterize_ia;
    break;
  default:
    break;
  }
  rp.pm = s.pm;
  rp.left = 0;
  rp.top = 0;


  if(fill_color) {
    rp.color = fill_color;
    FT_Stroker_GetBorderCounts(s.stroker, FT_STROKER_BORDER_LEFT,
			       &points, &contours);

    FT_Outline_New(ft_lib, points, contours, &ol);

    ol.n_contours = 0;
    ol.n_points = 0;

    FT_Stroker_ExportBorder(s.stroker, FT_STROKER_BORDER_LEFT, &ol);
    FT_Outline_Render(ft_lib, &ol, &params);
    FT_Outline_Done(ft_lib, &ol);
  }

  if(stroke_width) {
    rp.color = stroke_color;
    FT_Stroker_GetCounts(s.stroker, &points, &contours);

    FT_Outline_New(ft_lib, points, contours, &ol);

    ol.n_contours = 0;
    ol.n_points = 0;

    FT_Stroker_Export(s.stroker, &ol);
    FT_Outline_Render(ft_lib, &ol, &params);
    FT_Outline_Done(ft_lib, &ol);
  }
}



/**
 *
 */
static void
svg_parse_root(svg_state_t *s, htsmsg_t *tags)
{
  htsmsg_field_t *f;
  HTSMSG_FOREACH(f, tags) {
    htsmsg_t *c;
    if((c = htsmsg_get_map_by_field(f)) == NULL)
      continue;
    if(!strcmp(f->hmf_name, "path"))
      svg_parse_element(s, c, stroke_path_element);
    else if(!strcmp(f->hmf_name, "rect"))
      svg_parse_element(s, c, stroke_rect_element);
    else if(!strcmp(f->hmf_name, "g"))
      svg_parse_g(s, c);
  }
}


/**
 *
 */
static void
svg_parse_g(svg_state_t *s0, htsmsg_t *c)
{
  svg_state_t s = *s0;
  const char *transform = htsmsg_get_str_multi(c, "attrib", "transform", NULL);
  if(transform != NULL)
    svg_parse_transform(&s, transform);

  htsmsg_t *tags = htsmsg_get_map(c, "tags");
  if(tags != NULL)
    svg_parse_root(&s, tags);
}


/**
 *
 */
static pixmap_t *
svg_decode1(htsmsg_t *doc, const image_meta_t *im,
	    char *errbuf, size_t errlen)
{
  svg_state_t state;
  int need_color = 0;
  htsmsg_t *attrs = htsmsg_get_map_multi(doc, "tags", "svg", "attrib", NULL);
  if(attrs == NULL) {
    snprintf(errbuf, errlen, "Missing SVG attributes");
    return NULL;
  }

  int orig_width  = htsmsg_get_u32_or_default(attrs, "width", 0);
  int orig_height = htsmsg_get_u32_or_default(attrs, "height", 0);
  if(orig_width < 1 || orig_height < 1) {
    snprintf(errbuf, errlen, "Invalid SVG dimensions (%d x %d)", 
	     orig_width, orig_height);
    return NULL;
  }

  int w, h;
  if(im->im_req_width != -1 && im->im_req_height != -1) {
    snprintf(errbuf, errlen, "Aspect distortion not supported for SVG");
    return NULL;
  } else if(im->im_req_width != -1) {
    w = im->im_req_width;
    h = im->im_req_width * orig_height / orig_width;
  } else if(im->im_req_height != -1) {
    w = im->im_req_height * orig_width / orig_height;
    h = im->im_req_height;
  } else {
    w = orig_width;
    h = orig_height;
  }

  htsmsg_t *tags = htsmsg_get_map_multi(doc, "tags", "svg", "tags", NULL);
  if(tags == NULL) {
    snprintf(errbuf, errlen, "No relevant tags in SVG");
    return NULL;
  }

  memset(&state, 0, sizeof(state));
  state.need_color = &need_color;
  svg_parse_root(&state, tags);
  state.need_color = NULL;

  state.pm = pixmap_create(w, h, need_color ? PIXMAP_BGR32 : PIXMAP_IA, 1);
  state.scaling = (float)w / orig_width;
  svg_mtx_identity(state.ctm);
  svg_mtx_scale(state.ctm, (float)w / orig_width, (float)h / orig_height);
    
  FT_Stroker_New(ft_lib, &state.stroker);
  svg_parse_root(&state, tags);
  FT_Stroker_Done(state.stroker);
  return state.pm;
}



pixmap_t *
svg_decode(pixmap_t *pm, const image_meta_t *im,
	   char *errbuf, size_t errlen)
{
  pixmap_t *res;

  htsmsg_t *doc = htsmsg_xml_deserialize(pm->pm_data, errbuf, errlen);
  pm->pm_data = NULL;
  if(doc == NULL) {
    pixmap_release(pm);
    return NULL;
  }

  hts_mutex_lock(&svg_mutex);
  res = svg_decode1(doc, im, errbuf, errlen);
  hts_mutex_unlock(&svg_mutex);
  pixmap_release(pm);
  htsmsg_destroy(doc);
  return res;
}



void
svg_init(void)
{
  FT_Init_FreeType(&ft_lib);
  hts_mutex_init(&svg_mutex);
}
