/*
 *  GL Widgets, view loader, widget attributes
 *  Copyright (C) 2008 Andreas Öman
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
#include <stdio.h>
#include <stdarg.h>

#include "misc/strtab.h"
#include "glw_view.h"
#include "glw.h"
#include "fileaccess/fileaccess.h" // for relative path resolving

/**
 *
 */
static int
set_string(glw_view_eval_context_t *ec, const token_attrib_t *a,
	   struct token *t)
{
  char buf[30];
  const char *str;

  switch(t->type) {
  case TOKEN_VOID:
    str = "";
    break;

  case TOKEN_CSTRING:
    str = t->t_cstring;
    break;

  case TOKEN_RSTRING:
  case TOKEN_LINK:
    str = rstr_get(t->t_rstring);
    break;

  case TOKEN_INT:
    snprintf(buf, sizeof(buf), "%d", t->t_int);
    str = buf;
    break;

  case TOKEN_FLOAT:
    snprintf(buf, sizeof(buf), "%f", t->t_float);
    str = buf;
    break;

  default:
    return glw_view_seterr(ec->ei, t, 
			   "Attribute '%s' expects a string or scalar, got %s",
			   a->name, token2name(t));
  }

  void (*fn)(struct glw *w, const char *str) = a->fn;
  fn(ec->w, str);
  return 0;
}


static void
set_id(glw_t *w, const char *str)
{
  mystrset(&w->glw_id, str);
}

static void
set_how(glw_t *w, const char *str)
{
  if(w->glw_class->gc_set_how != NULL)
    w->glw_class->gc_set_how(w, str);
}

static void
set_description(glw_t *w, const char *str)
{
  if(w->glw_class->gc_set_desc != NULL)
    w->glw_class->gc_set_desc(w, str);
}


/**
 *
 */
static int
set_caption(glw_view_eval_context_t *ec, const token_attrib_t *a,
	    struct token *t)
{
  char buf[30];
  const char *str;
  prop_str_type_t type = 0;

  switch(t->type) {
  case TOKEN_VOID:
    str = NULL;
    break;

  case TOKEN_CSTRING:
    str = t->t_cstring;
    break;

  case TOKEN_RSTRING:
    type = t->t_rstrtype;
    /* FALLTHRU */
  case TOKEN_LINK:
    str = rstr_get(t->t_rstring);
    break;

  case TOKEN_INT:
    snprintf(buf, sizeof(buf), "%d", t->t_int);
    str = buf;
    break;

  case TOKEN_FLOAT:
    snprintf(buf, sizeof(buf), "%f", t->t_float);
    str = buf;
    break;

  default:
    return glw_view_seterr(ec->ei, t, 
			   "Attribute '%s' expects a string or scalar, got %s",
			   a->name, token2name(t));
  }

  if(ec->w->glw_class->gc_set_caption != NULL)
    ec->w->glw_class->gc_set_caption(ec->w, str, type);
  return 0;
}


/**
 *
 */
static int
set_font(glw_view_eval_context_t *ec, const token_attrib_t *a,
	    struct token *t)
{
  rstr_t *str;

  if(t->type == TOKEN_RSTRING)
    str = t->t_rstring;
  else
    str = NULL;

  str = str ? fa_absolute_path(str, t->file) : NULL;

  if(ec->w->glw_class->gc_set_font != NULL)
    ec->w->glw_class->gc_set_font(ec->w, str);
  rstr_release(str);
  return 0;
}



/**
 *
 */
static int
set_fs(glw_view_eval_context_t *ec, const token_attrib_t *a,
       struct token *t)
{
  rstr_t *str;

  if(t->type == TOKEN_RSTRING)
    str = t->t_rstring;
  else
    str = NULL;

  str = str ? fa_absolute_path(str, t->file) : NULL;

  if(ec->w->glw_class->gc_set_fs != NULL)
    ec->w->glw_class->gc_set_fs(ec->w, str);
  rstr_release(str);
  return 0;
}


/**
 *
 */
static int
set_float(glw_view_eval_context_t *ec, const token_attrib_t *a, 
	  struct token *t)
{
  float v;

  switch(t->type) {
  case TOKEN_CSTRING:
    v = strtod(t->t_cstring, NULL);
    break;

  case TOKEN_RSTRING:
  case TOKEN_LINK:
    v = strtod(rstr_get(t->t_rstring), NULL);
    break;

  case TOKEN_FLOAT:
    v = t->t_float;
    break;

  case TOKEN_INT:
    v = t->t_int;
    break;

  case TOKEN_VOID:
    v = 0.0f;
    break;
  default:
    return glw_view_seterr(ec->ei, t, "Attribute '%s' expects a scalar, got %s",
			   a->name, token2name(t));
  }

  void (*fn)(struct glw *w, float v) = a->fn;
  if(fn)
    fn(ec->w, v);
  else
    glw_set(ec->w, a->attrib, v, NULL);
  return 0;
}


/**
 *
 */
static void
set_weight(glw_t *w, float v)
{
  glw_conf_constraints(w, 0, 0, v, GLW_CONSTRAINT_CONF_W);
}

static void
set_alpha(glw_t *w, float v)
{
  w->glw_alpha = v;
}

static void
set_blur(glw_t *w, float v)
{
  w->glw_sharpness = GLW_CLAMP(1 - v, 0, 1);
}

/**
 *
 */
static void
set_alpha_self(glw_t *w, float v)
{
  if(w->glw_class->gc_set_alpha_self != NULL)
    w->glw_class->gc_set_alpha_self(w, v);
}


/**
 *
 */
static void
set_size_scale(glw_t *w, float v)
{
  if(w->glw_class->gc_set_size_scale != NULL)
    w->glw_class->gc_set_size_scale(w, v);
}



/**
 *
 */
static void
set_size(glw_t *w, float v)
{
  if(w->glw_class->gc_set_default_size != NULL)
    w->glw_class->gc_set_default_size(w, v);
}


/**
 *
 */
static void
set_min_size(glw_t *w, float v)
{
  if(w->glw_class->gc_set_min_size != NULL)
    w->glw_class->gc_set_min_size(w, v);
}



/**
 *
 */
static int
set_int(glw_view_eval_context_t *ec, const token_attrib_t *a, 
	  struct token *t)
{
  int v;

  switch(t->type) {
  case TOKEN_CSTRING:
    v = atoi(t->t_cstring);
    break;
    
  case TOKEN_RSTRING:
  case TOKEN_LINK:
    v = atoi(rstr_get(t->t_rstring));
    break;

  case TOKEN_FLOAT:
    v = t->t_float;
    break;

  case TOKEN_INT:
    v = t->t_int;
    break;

  case TOKEN_VOID:
    v = 0;
    break;
  default:
    return glw_view_seterr(ec->ei, t, "Attribute '%s' expects a scalar, got %s",
			   a->name, token2name(t));
  }

  void (*fn)(struct glw *w, int v) = a->fn;
  if(fn)
    fn(ec->w, v);
  else
    glw_set(ec->w, a->attrib, v, NULL);
  return 0;
}


/**
 *
 */
static void
set_width(glw_t *w, int v)
{
  glw_conf_constraints(w, v, 0, 0, GLW_CONSTRAINT_CONF_X);
}


/**
 *
 */
static void
set_height(glw_t *w, int v)
{
  glw_conf_constraints(w, 0, v, 0, GLW_CONSTRAINT_CONF_Y);
}


/**
 *
 */
static void
set_divider(glw_t *w, int v)
{
  glw_conf_constraints(w, 0, 0, 0, GLW_CONSTRAINT_CONF_D);
}


/**
 *
 */
static void
set_maxlines(glw_t *w, int v)
{
  if(w->glw_class->gc_set_max_lines != NULL)
    w->glw_class->gc_set_max_lines(w, v);
}


/**
 *
 */
static int
set_float3(glw_view_eval_context_t *ec, const token_attrib_t *a, 
	   struct token *t)
{
  const float *vec3;
  float v[3];

  switch(t->type) {
  case TOKEN_VECTOR_FLOAT:

    switch(t->t_elements) {

    case 3:
      vec3 = t->t_float_vector_int;
      break;

    default:
      return glw_view_seterr(ec->ei, t,
			     "Attribute '%s': invalid vector size %d",
			     a->name, t->t_elements);
    }
    break;

  case TOKEN_FLOAT:
    v[0] = v[1] = v[2] = t->t_float;
    vec3 = v;
    break;

  case TOKEN_INT:
    v[0] = v[1] = v[2] = t->t_int;
    vec3 = v;
    break;
  default:
    return glw_view_seterr(ec->ei, t, "Attribute '%s' expects a vec3, got %s",
			   a->name, token2name(t));
  }


  void (*fn)(struct glw *w, const float *v3) = a->fn;
  fn(ec->w, vec3);
  return 0;
}


/**
 *
 */
static void
set_rgb(glw_t *w, const float *rgb)
{
  if(w->glw_class->gc_set_rgb != NULL)
    w->glw_class->gc_set_rgb(w, rgb);
}

/**
 *
 */
static void
set_color1(glw_t *w, const float *rgb)
{
  if(w->glw_class->gc_set_color1 != NULL)
    w->glw_class->gc_set_color1(w, rgb);
}

/**
 *
 */
static void
set_color2(glw_t *w, const float *rgb)
{
  if(w->glw_class->gc_set_color2 != NULL)
    w->glw_class->gc_set_color2(w, rgb);
}

/**
 *
 */
static void
set_scaling(glw_t *w, const float *xyz)
{
  if(w->glw_class->gc_set_scaling != NULL)
    w->glw_class->gc_set_scaling(w, xyz);
}


/**
 *
 */
static void
set_translation(glw_t *w, const float *xyz)
{
  if(w->glw_class->gc_set_translation != NULL)
    w->glw_class->gc_set_translation(w, xyz);
}



/**
 *
 */
static int
set_float4(glw_view_eval_context_t *ec, const token_attrib_t *a, 
	   struct token *t)
{
  const float *vec4;
  float v[4];

  switch(t->type) {
  case TOKEN_VECTOR_FLOAT:

    switch(t->t_elements) {

    case 4:
      vec4 = t->t_float_vector_int;
      break;

    case 2:
      v[0] = t->t_float_vector_int[0];
      v[1] = t->t_float_vector_int[1];
      v[2] = t->t_float_vector_int[0];
      v[3] = t->t_float_vector_int[1];
      vec4 = v;
      break;

    default:
      return glw_view_seterr(ec->ei, t,
			     "Attribute '%s': invalid vector size %d",
			     a->name, t->t_elements);
    }
    break;

  case TOKEN_FLOAT:
    v[0] = v[1] = v[2] = v[3] = t->t_float;
    vec4 = v;
    break;

  case TOKEN_INT:
    v[0] = v[1] = v[2] = v[3] = t->t_int;
    vec4 = v;
    break;
  default:
    return glw_view_seterr(ec->ei, t, "Attribute '%s' expects a vec4, got %s",
			   a->name, token2name(t));
  }

  void (*fn)(struct glw *w, const float *v4) = a->fn;
  fn(ec->w, vec4);
  return 0;
}


/**
 *
 */
static int
set_int16_4(glw_view_eval_context_t *ec, const token_attrib_t *a, 
	   struct token *t)
{
  int16_t v[4];

  switch(t->type) {
  case TOKEN_VECTOR_FLOAT:

    switch(t->t_elements) {

    case 4:
      v[0] = t->t_float_vector_int[0];
      v[1] = t->t_float_vector_int[1];
      v[2] = t->t_float_vector_int[2];
      v[3] = t->t_float_vector_int[3];
      break;

    case 2:
      v[0] = t->t_float_vector_int[0];
      v[1] = t->t_float_vector_int[1];
      v[2] = t->t_float_vector_int[0];
      v[3] = t->t_float_vector_int[1];
      break;

    default:
      return glw_view_seterr(ec->ei, t,
			     "Attribute '%s': invalid vector size %d",
			     a->name, t->t_elements);
    }
    break;

  case TOKEN_FLOAT:
    v[0] = v[1] = v[2] = v[3] = t->t_float;
    break;

  case TOKEN_INT:
    v[0] = v[1] = v[2] = v[3] = t->t_int;
    break;

  case TOKEN_VOID:
    v[0] = v[1] = v[2] = v[3] = 0;
    break;

  default:
    return glw_view_seterr(ec->ei, t, "Attribute '%s' expects a vec4, got %s",
			   a->name, token2name(t));
  }

  void (*fn)(struct glw *w, const int16_t *v4) = a->fn;
  fn(ec->w, v);
  return 0;
}



/**
 *
 */
static void
set_padding(glw_t *w, const int16_t *vec4)
{
  if(w->glw_class->gc_set_padding != NULL)
    w->glw_class->gc_set_padding(w, vec4);
}


/**
 *
 */
static void
set_border(glw_t *w, const int16_t *vec4)
{
  if(w->glw_class->gc_set_border != NULL)
    w->glw_class->gc_set_border(w, vec4);
}


/**
 *
 */
static void
set_margin(glw_t *w, const int16_t *vec4)
{
  if(w->glw_class->gc_set_margin != NULL)
    w->glw_class->gc_set_margin(w, vec4);
}


/**
 *
 */
static void
set_rotation(glw_t *w, const float *xyz)
{
  if(w->glw_class->gc_set_rotation != NULL)
    w->glw_class->gc_set_rotation(w, xyz);
}


/**
 *
 */
static void
set_clipping(glw_t *w, const float *xyzw)
{
  if(w->glw_class->gc_set_clipping != NULL)
    w->glw_class->gc_set_clipping(w, xyzw);
}


/**
 *
 */
static void
set_plane(glw_t *w, const float *xyzw)
{
  if(w->glw_class->gc_set_plane != NULL)
    w->glw_class->gc_set_plane(w, xyzw);
}


static struct strtab aligntab[] = {
  { "center",        LAYOUT_ALIGN_CENTER},
  { "left",          LAYOUT_ALIGN_LEFT},
  { "right",         LAYOUT_ALIGN_RIGHT},
  { "top",           LAYOUT_ALIGN_TOP},
  { "bottom",        LAYOUT_ALIGN_BOTTOM},
  { "topLeft",       LAYOUT_ALIGN_TOP_LEFT},
  { "topRight",      LAYOUT_ALIGN_TOP_RIGHT},
  { "bottomLeft",    LAYOUT_ALIGN_BOTTOM_LEFT},
  { "bottomRight",   LAYOUT_ALIGN_BOTTOM_RIGHT},
  { "justified",     LAYOUT_ALIGN_JUSTIFIED},

};


/**
 *
 */
static int
set_align(glw_view_eval_context_t *ec, const token_attrib_t *a, 
	  struct token *t)
{
  int v;
  if(t->type != TOKEN_IDENTIFIER || (v = str2val(rstr_get(t->t_rstring), aligntab)) < 0)
    return glw_view_seterr(ec->ei, t, "Invalid assignment for attribute %s",
			    a->name);
  ec->w->glw_alignment = v;
  return 0;
}



static struct strtab transitiontab[] = {
  { "blend",             GLW_TRANS_BLEND},
  { "flipHorizontal",    GLW_TRANS_FLIP_HORIZONTAL},
  { "flipVertical",      GLW_TRANS_FLIP_VERTICAL},
  { "slideHorizontal",   GLW_TRANS_SLIDE_HORIZONTAL},
  { "slideVertical",     GLW_TRANS_SLIDE_VERTICAL},
};


/**
 *
 */
static int
set_transition_effect(glw_view_eval_context_t *ec, const token_attrib_t *a, 
		      struct token *t)
{
  int v;
  if(t->type != TOKEN_IDENTIFIER || (v = str2val(rstr_get(t->t_rstring),
						 transitiontab)) < 0)
    return glw_view_seterr(ec->ei, t, "Invalid assignment for attribute %s",
			    a->name);
  glw_set(ec->w, GLW_ATTRIB_TRANSITION_EFFECT, v, NULL);
  return 0;
}


/**
 *
 */
static int
mod_flag(glw_view_eval_context_t *ec, const token_attrib_t *a, 
	 struct token *t)
{
  int v = 0;

  if(t->type == TOKEN_INT)
    v = t->t_int;
  else if(t->type == TOKEN_FLOAT)
    v = t->t_float > 0.5;
  else if(t->type == TOKEN_VOID)
    v = 0;
  else
    return glw_view_seterr(ec->ei, t, "Invalid assignment for attribute %s",
			    a->name);

  void (*fn)(glw_t *w, int set, int clr) = a->fn;

  if(v)
    fn(ec->w, a->attrib, 0);
  else
    fn(ec->w, 0, a->attrib);
  return 0;
}


/**
 *
 */
static int
mod_hidden(glw_view_eval_context_t *ec, const token_attrib_t *a, 
	   struct token *t)
{
  int v = 0;

  if(t->type == TOKEN_INT)
    v = t->t_int;
  else if(t->type == TOKEN_FLOAT)
    v = t->t_float > 0.5;
  else if(t->type == TOKEN_VOID)
    v = 0;
  else
    return glw_view_seterr(ec->ei, t, "Invalid assignment for attribute %s",
			    a->name);

  if(v)
    glw_hide(ec->w);
  else
    glw_unhide(ec->w);
  return 0;
}


/**
 *
 */
static void
mod_flags2(glw_t *w, int set, int clr)
{
  set &= ~w->glw_flags2;
  w->glw_flags2 |= set;

  clr &= w->glw_flags2;
  w->glw_flags2 &= ~clr;

  if((set | clr) && w->glw_class->gc_mod_flags2 != NULL)
    w->glw_class->gc_mod_flags2(w, set, clr);
}


/**
 *
 */
static void
mod_text_flags(glw_t *w, int set, int clr)
{
  if(w->glw_class->gc_mod_text_flags != NULL)
    w->glw_class->gc_mod_text_flags(w, set, clr);
}


/**
 *
 */
static void
mod_img_flags(glw_t *w, int set, int clr)
{
  if(w->glw_class->gc_mod_image_flags != NULL)
    w->glw_class->gc_mod_image_flags(w, set, clr);
}


/**
 *
 */
static void
mod_video_flags(glw_t *w, int set, int clr)
{
  if(w->glw_class->gc_mod_video_flags != NULL)
    w->glw_class->gc_mod_video_flags(w, set, clr);
}



/**
 *
 */
static rstr_t **
build_rstr_vector(struct token *t0)
{
  int cnt = 1;
  token_t *t;
  rstr_t **rv;

  for(t = t0->child; t != NULL; t = t->next)
    if(t->type == TOKEN_RSTRING || t->type == TOKEN_LINK)
      cnt++;
  
  rv = malloc(sizeof(rstr_t *) * cnt);
  cnt = 0;

  for(t = t0->child; t != NULL; t = t->next)
    if(t->type == TOKEN_RSTRING || t->type == TOKEN_LINK)
      rv[cnt++] = rstr_dup(t->t_rstring);
  rv[cnt++] = NULL;
  return rv;
}



/**
 *
 */
static int
set_alt(glw_view_eval_context_t *ec, const token_attrib_t *a,
	struct token *t)
{
  glw_t *w = ec->w;

  rstr_t *r;

  switch(t->type) {
  default:
    if(w->glw_class->gc_set_alt != NULL)
      w->glw_class->gc_set_alt(w, NULL);
    return 0;

  case TOKEN_RSTRING:
    r = t->t_rstring;
    break;
  case TOKEN_LINK:
    r = t->t_link_rurl;
    break;
  }

  r = fa_absolute_path(r, t->file);

  if(w->glw_class->gc_set_alt != NULL)
    w->glw_class->gc_set_alt(w, r);
  rstr_release(r);
  return 0;
}


/**
 *
 */
static int
set_source(glw_view_eval_context_t *ec, const token_attrib_t *a,
	   struct token *t)
{
  glw_t *w = ec->w;

  rstr_t *r;

  switch(t->type) {
  default:
    if(w->glw_class->gc_set_source != NULL)
      w->glw_class->gc_set_source(w, NULL);
    return 0;

  case TOKEN_VECTOR:
    if(w->glw_class->gc_set_sources != NULL)
      w->glw_class->gc_set_sources(w, build_rstr_vector(t));
    return 0;

  case TOKEN_RSTRING:
    r = t->t_rstring;
    break;
  case TOKEN_LINK:
    r = t->t_link_rurl;
    break;
  }

  r = fa_absolute_path(r, t->file);

  if(w->glw_class->gc_set_source != NULL)
    w->glw_class->gc_set_source(w, r);
  rstr_release(r);
  return 0;
}


/**
 *
 */
static int
set_args(glw_view_eval_context_t *ec, const token_attrib_t *a,
	   struct token *t)
{
  if(t->type != TOKEN_PROPERTY_OWNER &&
     t->type != TOKEN_PROPERTY_REF)
    return glw_view_seterr(ec->ei, t,
			   "Attribute '%s' expects a property, got %s",
			   a->name, token2name(t));

  glw_set(ec->w, GLW_ATTRIB_ARGS, t->t_prop, NULL);
  return 0;
}


/**
 *
 */
static int
set_propref(glw_view_eval_context_t *ec, const token_attrib_t *a,
	   struct token *t)
{
  if(t->type == TOKEN_VOID) {
    glw_set(ec->w, a->attrib, NULL, NULL);
    return 0;
  }
  if(t->type != TOKEN_PROPERTY_REF)
    return glw_view_seterr(ec->ei, t,
			   "Attribute '%s' expects a property ref, got %s",
			   a->name, token2name(t));

  glw_set(ec->w, a->attrib, t->t_prop, NULL);
  return 0;
}


/**
 *
 */
static int
set_page(glw_view_eval_context_t *ec, const token_attrib_t *a,
	   struct token *t)
{
  const char *str;

  switch(t->type) {
  default:
    glw_set(ec->w, GLW_ATTRIB_PAGE_BY_ID, NULL, NULL);
    break;

  case TOKEN_CSTRING:
    glw_set(ec->w, GLW_ATTRIB_PAGE_BY_ID, t->t_cstring, NULL);
    break;

  case TOKEN_RSTRING:
  case TOKEN_LINK:
    str = rstr_get(t->t_rstring);
    glw_set(ec->w, GLW_ATTRIB_PAGE_BY_ID, str, NULL);
    break;

  case TOKEN_INT:
    glw_set(ec->w, GLW_ATTRIB_PAGE, t->t_int, NULL);
    break;

  case TOKEN_FLOAT:
    glw_set(ec->w, GLW_ATTRIB_PAGE, (int)t->t_float, NULL);
    break;
  }
  return 0;
}


/**
 *
 */
static const token_attrib_t attribtab[] = {
  {"id",              set_string, 0, set_id},
  {"how",             set_string, 0, set_how},
  {"description",     set_string, 0, set_description},
  {"caption",         set_caption, 0},
  {"font",            set_font, 0},
  {"fragmentShader",  set_fs, 0},
  {"source",          set_source},
  {"alt",             set_alt},

  {"hidden",                mod_hidden},
  {"filterConstraintX",     mod_flag, GLW2_CONSTRAINT_IGNORE_X,    mod_flags2},
  {"filterConstraintY",     mod_flag, GLW2_CONSTRAINT_IGNORE_Y,    mod_flags2},
  {"filterConstraintWeight",mod_flag, GLW2_CONSTRAINT_IGNORE_W,    mod_flags2},
  {"debug",                 mod_flag, GLW2_DEBUG,                  mod_flags2},
  {"noInitialTransform",    mod_flag, GLW2_NO_INITIAL_TRANS,       mod_flags2},
  {"focusOnClick",          mod_flag, GLW2_FOCUS_ON_CLICK,         mod_flags2},
  {"autoRefocusable",       mod_flag, GLW2_AUTOREFOCUSABLE,        mod_flags2},
  {"navFocusable",          mod_flag, GLW2_NAV_FOCUSABLE,          mod_flags2},
  {"homogenous",            mod_flag, GLW2_HOMOGENOUS,             mod_flags2},
  {"enabled",               mod_flag, GLW2_ENABLED,                mod_flags2},
  {"alwaysLayout",          mod_flag, GLW2_ALWAYS_LAYOUT,          mod_flags2},
  {"alwaysGrabKnob",        mod_flag, GLW2_ALWAYS_GRAB_KNOB,       mod_flags2},
  {"autohide",              mod_flag, GLW2_AUTOHIDE,               mod_flags2},
  {"shadow",                mod_flag, GLW2_SHADOW,                 mod_flags2},
  {"autofade",              mod_flag, GLW2_AUTOFADE,               mod_flags2},
  {"automargin",            mod_flag, GLW2_AUTOMARGIN,             mod_flags2},
  {"expediteSubscriptions", mod_flag, GLW2_EXPEDITE_SUBSCRIPTIONS, mod_flags2},
  {"reverseRender",         mod_flag, GLW2_REVERSE_RENDER,         mod_flags2},

  {"fixedSize",       mod_flag, GLW_IMAGE_FIXED_SIZE,   mod_img_flags},
  {"bevelLeft",       mod_flag, GLW_IMAGE_BEVEL_LEFT,   mod_img_flags},
  {"bevelTop",        mod_flag, GLW_IMAGE_BEVEL_TOP,    mod_img_flags},
  {"bevelRight",      mod_flag, GLW_IMAGE_BEVEL_RIGHT,  mod_img_flags},
  {"bevelBottom",     mod_flag, GLW_IMAGE_BEVEL_BOTTOM, mod_img_flags},
  {"aspectConstraint",mod_flag, GLW_IMAGE_SET_ASPECT,   mod_img_flags},
  {"additive",        mod_flag, GLW_IMAGE_ADDITIVE,     mod_img_flags},
  {"borderOnly",      mod_flag, GLW_IMAGE_BORDER_ONLY,  mod_img_flags},
  {"leftBorder",      mod_flag, GLW_IMAGE_BORDER_LEFT,  mod_img_flags},
  {"rightBorder",     mod_flag, GLW_IMAGE_BORDER_RIGHT, mod_img_flags},

  {"aspectFixedBorders",mod_flag, GLW_IMAGE_ASPECT_FIXED_BORDERS, mod_img_flags},
  {"cornerTopLeft",     mod_flag, GLW_IMAGE_CORNER_TOPLEFT,       mod_img_flags},
  {"cornerTopRight",    mod_flag, GLW_IMAGE_CORNER_TOPRIGHT,      mod_img_flags},
  {"cornerBottomLeft",  mod_flag, GLW_IMAGE_CORNER_BOTTOMLEFT,    mod_img_flags},
  {"cornerBottomRight", mod_flag, GLW_IMAGE_CORNER_BOTTOMRIGHT,   mod_img_flags},


  {"password",        mod_flag,  GTB_PASSWORD, mod_text_flags},
  {"ellipsize",       mod_flag,  GTB_ELLIPSIZE, mod_text_flags},
  {"bold",            mod_flag,  GTB_BOLD, mod_text_flags},
  {"italic",          mod_flag,  GTB_ITALIC, mod_text_flags},
  {"outline",         mod_flag,  GTB_OUTLINE, mod_text_flags},
  {"permanentCursor", mod_flag,  GTB_PERMANENT_CURSOR, mod_text_flags},
  {"oskPassword",     mod_flag,  GTB_OSK_PASSWORD, mod_text_flags},
  
  {"primary",         mod_flag, GLW_VIDEO_PRIMARY, mod_video_flags},
  {"noAudio",         mod_flag, GLW_VIDEO_NO_AUDIO, mod_video_flags},

  {"alpha",           set_float,  0, set_alpha},
  {"blur",            set_float,  0, set_blur},
  {"alphaSelf",       set_float,  0, set_alpha_self},
  {"saturation",      set_float,  GLW_ATTRIB_SATURATION},
  {"weight",          set_float,  0, set_weight},
  {"time",            set_float,  GLW_ATTRIB_TIME},
  {"transitionTime",  set_float,  GLW_ATTRIB_TRANSITION_TIME},
  {"angle",           set_float,  GLW_ATTRIB_ANGLE},
  {"expansion",       set_float,  GLW_ATTRIB_EXPANSION},
  {"min",             set_float,  GLW_ATTRIB_INT_MIN},
  {"max",             set_float,  GLW_ATTRIB_INT_MAX},
  {"step",            set_float,  GLW_ATTRIB_INT_STEP},
  {"value",           set_float,  GLW_ATTRIB_VALUE},
  {"sizeScale",       set_float,  0, set_size_scale},
  {"size",            set_float,  0, set_size},
  {"minSize",         set_float,  0, set_min_size},
  {"focusable",       set_float,  0, glw_set_focus_weight},
  {"childAspect",     set_float,  GLW_ATTRIB_CHILD_ASPECT},
  {"center",          set_float,  GLW_ATTRIB_CENTER},

  {"height",          set_int,  0, set_height},
  {"width",           set_int,  0, set_width},

  {"fill",            set_float,  GLW_ATTRIB_FILL},

  {"childWidth",      set_int,    GLW_ATTRIB_CHILD_WIDTH},
  {"childHeight",     set_int,    GLW_ATTRIB_CHILD_HEIGHT},

  {"childTilesX",     set_int,    GLW_ATTRIB_CHILD_TILES_X},
  {"childTilesY",     set_int ,   GLW_ATTRIB_CHILD_TILES_Y},

  {"page",            set_page,   0},

  {"alphaEdges",      set_int,    GLW_ATTRIB_ALPHA_EDGES},
  {"priority",        set_int,    GLW_ATTRIB_PRIORITY},
  {"maxlines",        set_int,    0, set_maxlines},
  {"spacing",         set_int,    GLW_ATTRIB_SPACING},
  {"Xspacing",        set_int,    GLW_ATTRIB_X_SPACING},
  {"Yspacing",        set_int,    GLW_ATTRIB_Y_SPACING},
  {"scrollThreshold", set_int,    GLW_ATTRIB_SCROLL_THRESHOLD},
  {"divider",         set_int,    0, set_divider},
  {"cornerRadius",    set_int,    GLW_ATTRIB_RADIUS},

  {"color",           set_float3, 0, set_rgb},
  {"translation",     set_float3, 0, set_translation},
  {"scaling",         set_float3, 0, set_scaling},
  {"color1",          set_float3, 0, set_color1},
  {"color2",          set_float3, 0, set_color2},

  {"padding",         set_int16_4, 0, set_padding},
  {"border",          set_int16_4, 0, set_border},
  {"margin",          set_int16_4, 0, set_margin},
  {"rotation",        set_float4, 0, set_rotation},
  {"clipping",        set_float4, 0, set_clipping},
  {"plane",           set_float4, 0, set_plane},
  {"alphaFallOff",    set_float, GLW_ATTRIB_ALPHA_FALLOFF},
  {"blurFallOff",     set_float, GLW_ATTRIB_BLUR_FALLOFF},
  


  {"align",           set_align,  0},
  {"effect",          set_transition_effect,  0},

  {"args",            set_args,  0},
  {"parent",          set_propref, GLW_ATTRIB_PROP_PARENT},
  {"self",            set_propref, GLW_ATTRIB_PROP_SELF},
  {"model",           set_propref, GLW_ATTRIB_PROP_MODEL},
};


/**
 *
 */
int 
glw_view_attrib_resolve(token_t *t)
{
  int i;

  for(i = 0; i < sizeof(attribtab) / sizeof(attribtab[0]); i++)
    if(!strcmp(attribtab[i].name, rstr_get(t->t_rstring))) {
      rstr_release(t->t_rstring);
      t->t_attrib = &attribtab[i];
      t->type = TOKEN_OBJECT_ATTRIBUTE;
      return 0;
    }
  return -1;
}



/**
 *
 */
static int
or_flags2_fn(glw_view_eval_context_t *ec, const token_attrib_t *a, 
             struct token *t)
{
  mod_flags2(ec->w, t->t_set, t->t_clr);
  return 0;
}





/**
 *
 */
static int
or_img_flags_fn(glw_view_eval_context_t *ec, const token_attrib_t *a,
                struct token *t)
{
  mod_img_flags(ec->w, t->t_set, t->t_clr);
  return 0;
}




/**
 *
 */
static int
or_txt_flags_fn(glw_view_eval_context_t *ec, const token_attrib_t *a,
                 struct token *t)
{
  mod_text_flags(ec->w, t->t_set, t->t_clr);
  return 0;
}



static const token_attrib_t or_flags2    = {"or_flags2",    or_flags2_fn};
static const token_attrib_t or_img_flags = {"or_img_flags", or_img_flags_fn};
static const token_attrib_t or_txt_flags = {"or_txt_flags", or_txt_flags_fn};


/**
 *
 */
static int
merge_token(token_t *t, glw_root_t *gr, token_t **p, token_t **fp,
            const token_attrib_t *a)
{
  if(*fp == NULL) {
    *fp = t;
    if(t->t_int) {
      t->t_set = t->t_attrib->attrib;
      t->t_clr = 0;
    } else {
      t->t_set = 0;
      t->t_clr = t->t_attrib->attrib;
    }
    t->t_attrib = a;
    t->type = TOKEN_MOD_FLAGS;
    return 0;
  } else {
    if(t->t_int) {
      (*fp)->t_set |= t->t_attrib->attrib;
    } else {
      (*fp)->t_clr |= t->t_attrib->attrib;
    }
    *p = t->next;
    glw_view_token_free(gr, t);
    return 1;
  }
}


/**
 *
 */
void
glw_view_attrib_optimize(token_t *t, glw_root_t *gr)
{
  token_t *f2 = NULL, *img = NULL, *txt = NULL, **p = &t;

  while((t = *p) != NULL) {
    if(t->type == TOKEN_INT) {
      if(t->t_attrib->fn == &mod_flags2 &&
         merge_token(t, gr, p, &f2, &or_flags2))
        continue;

      if(t->t_attrib->fn == &mod_img_flags &&
         merge_token(t, gr, p, &img, &or_img_flags))
        continue;

      if(t->t_attrib->fn == &mod_text_flags &&
         merge_token(t, gr, p, &txt, &or_txt_flags))
        continue;
    }
    p = &t->next;
  }
}
