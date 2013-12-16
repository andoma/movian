/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
#include "arch/atomic.h"
#include "pixmap.h"
#include "dbl.h"
#include "htsmsg/htsmsg_xml.h"
#include "misc/str.h"
#include "showtime.h"

typedef struct svg_state {
  float cur[2];
  float ctm[9];
  pixmap_t *pm;
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
cmd_move(svg_state_t *state)
{
  float pt[2];
  svg_mtx_vec_mul(pt, state->ctm, state->cur);

  vec_emit_f1(state->pm, VC_MOVE_TO, pt);
}


static void
cmd_move_abs(svg_state_t *state, const float *p)
{
  state->cur[0] = p[0];
  state->cur[1] = p[1];
  cmd_move(state);
}

static void
cmd_move_rel(svg_state_t *state, const float *p)
{
  state->cur[0] += p[0];
  state->cur[1] += p[1];
  cmd_move(state);
}


#define FT_EPSILON  2
#define FT_IS_SMALL( x )  ( (x) > -FT_EPSILON && (x) < FT_EPSILON )

static void
cmd_curve(svg_state_t *state, const float *s, const float *c,
	  const float *d, const float *e)
{
  vec_emit_f3(state->pm, VC_CUBIC_TO, c, d, e);
}

static void
cmd_curveto_rel(svg_state_t *state, const float *p)
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

  cmd_curve(state, ts, tc, td, te);
}


static void
cmd_curveto_abs(svg_state_t *state, const float *p)
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

  cmd_curve(state, ts, tc, td, te);
}

static void
cmd_lineto(svg_state_t *state)
{
  float pt[2];
  svg_mtx_vec_mul(pt, state->ctm, state->cur);
  vec_emit_f1(state->pm, VC_LINE_TO, pt);
}


static void
cmd_lineto_rel(svg_state_t *state, const float *p)
{
  state->cur[0] += p[0];
  state->cur[1] += p[1];

  cmd_lineto(state);
}




static void
cmd_lineto_abs(svg_state_t *state, const float *p)
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
  vec_emit_0(state->pm, VC_CLOSE);
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
  void (*cur_cmd)(svg_state_t *state, const float *params) = NULL; 
  void (*next_cmd)(svg_state_t *state, const float *params) = NULL; 

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
	  cur_cmd(state, values);
	  cur_cmd = next_cmd;
	  cur_param = 0;
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
      next_cmd = cmd_lineto_abs;
      num_params = 2;
      break;
    case 'm':
      cur_cmd = cmd_move_rel;
      next_cmd = cmd_lineto_rel;
      num_params = 2;
      break;

    case 'c':
      next_cmd = cur_cmd = cmd_curveto_rel;
      num_params = 6;
      break;

    case 'C':
      next_cmd = cur_cmd = cmd_curveto_abs;
      num_params = 6;
      break;

    case 'l':
      next_cmd = cur_cmd = cmd_lineto_rel;
      num_params = 2;
      break;

    case 'L':
      next_cmd = cur_cmd = cmd_lineto_abs;
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
  }
}


/**
 *
 */
static int
stroke_path_element(svg_state_t *s, htsmsg_t *attribs)
{
  const char *d = htsmsg_get_str(attribs, "d");
  if(d == NULL)
    return -1;
  stroke_path(s, d);
  return 0;
}


/**
 *
 */
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
  cmd_move_abs(s, v);
  v[0] += width;
  cmd_lineto_abs(s, v);
  v[1] += height;
  cmd_lineto_abs(s, v);
  v[0] = x;
  cmd_lineto_abs(s, v);
  cmd_close(s);
  return 0;
}


/**
 *
 */
static void
svg_parse_element(const svg_state_t *s0, htsmsg_t *element, 
		  int (*element_parser)(svg_state_t *s, htsmsg_t *element))
{
  svg_state_t s = *s0;
  //  FT_Outline ol;
  //  FT_UInt points;
  //  FT_UInt contours;

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
	  }
	} else if(!strcmp(attr, "stroke")) {

	  if(!strcmp(value, "none"))
	    stroke_color = 0;
	  else {
	    stroke_color = (stroke_color & 0xff000000) | html_makecolor(value);
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


  vec_emit_0(s.pm, VC_BEGIN);

  if(fill_color) {
    vec_emit_i1(s.pm, VC_SET_FILL_ENABLE, 1);
    vec_emit_i1(s.pm, VC_SET_FILL_COLOR, fill_color);
  }

  if(stroke_width) {
    vec_emit_i1(s.pm, VC_SET_STROKE_WIDTH, stroke_width);
    vec_emit_i1(s.pm, VC_SET_STROKE_COLOR, stroke_color);
  }

  s.cur[0] = 0;
  s.cur[1] = 0;

  if(element_parser(&s, a))
    return;

  cmd_close(&s);
  vec_emit_0(s.pm, VC_END);
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

  state.scaling = (float)w / orig_width;
  svg_mtx_identity(state.ctm);
  svg_mtx_scale(state.ctm, (float)w / orig_width, (float)h / orig_height);

  state.pm = pixmap_create_vector(w, h);
  state.pm->pm_margin = im->im_margin;
  svg_parse_root(&state, tags);
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

  res = svg_decode1(doc, im, errbuf, errlen);
  pixmap_release(pm);
  htsmsg_destroy(doc);
  return res;
}
