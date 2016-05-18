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
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>

#include "misc/strtab.h"
#include "misc/str.h"
#include "glw_view.h"
#include "glw.h"
#include "glw_event.h"
#include "glw_style.h"

#include "fileaccess/fileaccess.h" // for relative path resolving

/**
 *
 */
rstr_t *
glw_resolve_path(rstr_t *filename, rstr_t *at, glw_root_t *gr, int *flags)
{
  if(filename == NULL)
    return NULL;

  const char *x = mystrbegins(rstr_get(filename), "skin://");
  if(x != NULL) {
    char buf[PATH_MAX];
    fa_pathjoin(buf, sizeof(buf), gr->gr_skin, x);
    if(flags != NULL)
      *flags = GLW_SOURCE_FLAG_ALWAYS_LOCAL;
    return rstr_alloc(buf);
  }

  if(flags != NULL) {
    const char *x = mystrbegins(rstr_get(filename), "dataroot://");
    if(x != NULL) {
      *flags = GLW_SOURCE_FLAG_ALWAYS_LOCAL;
    }
  }

  return fa_absolute_path(filename, at);
}




/**
 *
 */
static void
respond_error(glw_t *w, const token_t *t, const char *name)
{
  const glw_class_t *gc = w->glw_class;
  TRACE(TRACE_DEBUG, "GLW", "Widget %s (%s:%d) "
        "assignment at %s:%d does not respond to attribute %s",
        gc->gc_name,
        rstr_get(w->glw_file), w->glw_line,
        rstr_get(t->file), t->line, name);
}




/**
 *
 */
static int
set_rstring(glw_view_eval_context_t *ec, const token_attrib_t *a,
            struct token *t)
{
  rstr_t *rstr;
  char buf[30];
  void (*fn)(struct glw *w, rstr_t *str) = a->fn;
  glw_t *w = ec->w;

  switch(t->type) {
  case TOKEN_VOID:
    buf[0] = 0;
    break;

  case TOKEN_CSTRING:
    rstr = rstr_alloc(t->t_cstring);
    fn(w, rstr);
    rstr_release(rstr);
    return 0;

  case TOKEN_RSTRING:
  case TOKEN_URI:
    fn(w, t->t_rstring);
    return 0;

  case TOKEN_INT:
    snprintf(buf, sizeof(buf), "%d", t->t_int);
    break;

  case TOKEN_FLOAT:
    snprintf(buf, sizeof(buf), "%f", t->t_float);
    break;

  case TOKEN_EM:
    ec->dynamic_eval |= GLW_VIEW_EVAL_EM;
    snprintf(buf, sizeof(buf), "%f", t->t_float *
             w->glw_root->gr_current_size);
    break;

  default:
    return glw_view_seterr(ec->ei, t, 
			   "Attribute '%s' expects a string or scalar, got %s",
			   a->name, token2name(t));
  }

  rstr = rstr_alloc(buf);
  fn(w, rstr);
  rstr_release(rstr);
  return 0;
}


static void
set_id_rstr(glw_t *w, rstr_t *rstr)
{
  rstr_set(&w->glw_id_rstr, rstr);
}

static void
set_how_rstr(glw_t *w, rstr_t *rstr)
{
  if(w->glw_class->gc_set_how != NULL)
    w->glw_class->gc_set_how(w, rstr_get(rstr));
}

static void
set_description_rstr(glw_t *w, rstr_t *str)
{
  if(w->glw_class->gc_set_desc != NULL)
    w->glw_class->gc_set_desc(w, rstr_get(str));
}

static void
set_parent_url_rstr(glw_t *w, rstr_t *rstr)
{
  if(w->glw_class->gc_set_rstr != NULL)
    w->glw_class->gc_set_rstr(w, GLW_ATTRIB_PARENT_URL, rstr, NULL);
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
  glw_t *w = ec->w;

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
  case TOKEN_URI:
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

  case TOKEN_EM:
    ec->dynamic_eval |= GLW_VIEW_EVAL_EM;
    snprintf(buf, sizeof(buf), "%f",
             t->t_float * w->glw_root->gr_current_size);
    str = buf;
    break;

  default:
    return glw_view_seterr(ec->ei, t, 
			   "Attribute '%s' expects a string or scalar, got %s",
			   a->name, token2name(t));
  }

  if(w->glw_class->gc_set_caption != NULL)
    w->glw_class->gc_set_caption(ec->w, str, type);
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

  glw_t *w = ec->w;

  str = glw_resolve_path(str, t->file, w->glw_root, NULL);

  if(w->glw_class->gc_set_rstr != NULL)
    w->glw_class->gc_set_rstr(w, GLW_ATTRIB_FONT, str, NULL);
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

  glw_t *w = ec->w;

  str = glw_resolve_path(str, t->file, w->glw_root, NULL);

  if(w->glw_class->gc_set_fs != NULL)
    w->glw_class->gc_set_fs(ec->w, str);
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
  glw_t *w = ec->w;

  switch(t->type) {
  case TOKEN_CSTRING:
    v = strtod(t->t_cstring, NULL);
    break;

  case TOKEN_RSTRING:
  case TOKEN_URI:
    v = strtod(rstr_get(t->t_rstring), NULL);
    break;

  case TOKEN_FLOAT:
    v = t->t_float;
    break;

  case TOKEN_EM:
    ec->dynamic_eval |= GLW_VIEW_EVAL_EM;
    v = t->t_float * w->glw_root->gr_current_size;
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

  void (*fn)(struct glw *w, float v, glw_style_t *origin) = a->fn;
  assert(fn != NULL);
  fn(w, v, NULL);
  return 0;
}


/**
 *
 */
void
glw_set_weight(glw_t *w, float v, glw_style_t *origin)
{
  if(w->glw_class->gc_set_weight != NULL) {
    w->glw_class->gc_set_weight(w, v, origin);
    return;
  }

  glw_conf_constraints(w, 0, 0, v, GLW_CONSTRAINT_CONF_W);
  glw_need_refresh(w->glw_root, 0);
}


/**
 *
 */
void
glw_set_alpha(glw_t *w, float v, glw_style_t *origin)
{
  if(w->glw_class->gc_set_alpha != NULL) {
    w->glw_class->gc_set_alpha(w, v, origin);
    return;
  }

  if(w->glw_alpha == v)
    return;

  w->glw_alpha = v;
  glw_need_refresh(w->glw_root, 0);
}


/**
 *
 */
void
glw_set_blur(glw_t *w, float v, glw_style_t *origin)
{
  if(w->glw_class->gc_set_blur != NULL) {
    w->glw_class->gc_set_blur(w, v, origin);
    return;
  }

  v = GLW_CLAMP(1 - v, 0, 1);

  if(w->glw_sharpness == v)
    return;
  w->glw_sharpness = v;
  glw_need_refresh(w->glw_root, 0);
}


/**
 *
 */
static void
attr_need_refresh(glw_root_t *gr, const token_t *t,
                  const char *attribname, int how)
{

  int flags = GLW_REFRESH_FLAG_LAYOUT;

  if(how != GLW_REFRESH_LAYOUT_ONLY)
    flags |= GLW_REFRESH_FLAG_RENDER;

  if((gr->gr_need_refresh & flags) == flags)
    return;

  gr->gr_need_refresh |= flags;


#ifdef GLW_TRACK_REFRESH
  printf("%s%s refresh requested by %s:%d attribute %s\n",
         flags & GLW_REFRESH_FLAG_LAYOUT ? "Layout " : "",
         flags & GLW_REFRESH_FLAG_RENDER ? "Render " : "",
         rstr_get(t->file), t->line, attribname);
#endif
}


/**
 *
 */
static void
set_number_int(glw_t *w, const token_attrib_t *a, const token_t *t, int v)
{
  const glw_class_t *gc = w->glw_class;
  int r;

  r = gc->gc_set_int ? gc->gc_set_int(w, a->attrib, v, NULL) : -1;

  if(r == -1)
    r = gc->gc_set_float ? gc->gc_set_float(w, a->attrib, v, NULL) : -1;

  if(r == -1) {
    respond_error(w, t, a->name);
    return;
  }

  if(r)
    attr_need_refresh(w->glw_root, t, a->name, r);
}


/**
 *
 */
static void
set_number_float(glw_t *w, const token_attrib_t *a, const token_t *t, float v)
{
  const glw_class_t *gc = w->glw_class;
  int r;

  r = gc->gc_set_float ? gc->gc_set_float(w, a->attrib, v, NULL) : -1;

  if(r == -1)
    r = gc->gc_set_int ? gc->gc_set_int(w, a->attrib, v, NULL) : -1;

  if(r == -1) {
    respond_error(w, t, a->name);
    return;
  }

  if(r)
    attr_need_refresh(w->glw_root, t, a->name, r);
}


/**
 *
 */
static void
set_number_em(glw_t *w, const token_attrib_t *a, const token_t *t,
              glw_view_eval_context_t *ec)
{
  const glw_class_t *gc = w->glw_class;
  int r;
  float v = t->t_float;
  glw_root_t *gr = w->glw_root;

  r = gc->gc_set_em ? gc->gc_set_em(w, a->attrib, v) : -1;

  if(r == -1) {

    v *= gr->gr_current_size;

    ec->dynamic_eval |= GLW_VIEW_EVAL_EM;

    r = gc->gc_set_float ? gc->gc_set_float(w, a->attrib, v, NULL) : -1;

    if(r == -1)
      r = gc->gc_set_int ? gc->gc_set_int(w, a->attrib, v, NULL) : -1;
  }

  if(r == -1) {
    respond_error(w, t, a->name);
    return;
  }

  if(r)
    attr_need_refresh(gr, t, a->name, r);
}


/**
 *
 */
static int
set_number(glw_view_eval_context_t *ec, const token_attrib_t *a,
           struct token *t)
{
  int v;
  glw_t *w = ec->w;

  switch(t->type) {
  case TOKEN_CSTRING:
    v = atoi(t->t_cstring);
    set_number_int(w, a, t, v);
    break;

  case TOKEN_RSTRING:
  case TOKEN_URI:
    v = atoi(rstr_get(t->t_rstring));
    set_number_int(w, a, t, v);
    break;

  case TOKEN_FLOAT:
    set_number_float(w, a, t, t->t_float);
    break;

  case TOKEN_EM:
    set_number_em(w, a, t, ec);
    break;

  case TOKEN_INT:
    set_number_int(w, a, t, t->t_int);
    break;

  case TOKEN_VOID:
    set_number_int(w, a, t, 0);
    break;

  default:
    return glw_view_seterr(ec->ei, t, "Attribute '%s' expects a scalar, got %s",
			   a->name, token2name(t));
  }



  return 0;
}



/**
 *
 */
static int
set_int(glw_view_eval_context_t *ec, const token_attrib_t *a, 
	  struct token *t)
{
  int v;
  glw_t *w = ec->w;

  switch(t->type) {
  case TOKEN_CSTRING:
    v = atoi(t->t_cstring);
    break;
    
  case TOKEN_RSTRING:
  case TOKEN_URI:
    v = atoi(rstr_get(t->t_rstring));
    break;

  case TOKEN_FLOAT:
    v = t->t_float;
    break;

  case TOKEN_EM:
    ec->dynamic_eval |= GLW_VIEW_EVAL_EM;
    v = t->t_float * w->glw_root->gr_current_size;
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

  void (*fn)(struct glw *w, int v, glw_style_t *origin) = a->fn;
  assert(fn != NULL);
  fn(w, v, NULL);
  return 0;
}


/**
 *
 */
void
glw_set_width(glw_t *w, int v, glw_style_t *origin)
{
  if(w->glw_class->gc_set_width != NULL) {
    w->glw_class->gc_set_width(w, v, origin);
    return;
  }

  glw_conf_constraints(w, v, 0, 0, GLW_CONSTRAINT_CONF_X);
  glw_need_refresh(w->glw_root, 0);
}


/**
 *
 */
void
glw_set_height(glw_t *w, int v, glw_style_t *origin)
{
  if(w->glw_class->gc_set_height != NULL) {
    w->glw_class->gc_set_height(w, v, origin);
    return;
  }

  glw_conf_constraints(w, 0, v, 0, GLW_CONSTRAINT_CONF_Y);
  glw_need_refresh(w->glw_root, 0);
}


/**
 *
 */
void
glw_set_align(glw_t *w, int v, glw_style_t *origin)
{
  if(w->glw_class->gc_set_align != NULL) {
    w->glw_class->gc_set_align(w, v, origin);
    return;
  }

  if(w->glw_alignment != v) {
    w->glw_alignment = v;
    glw_need_refresh(w->glw_root, 0);
  }
}


/**
 *
 */
void
glw_set_hidden(glw_t *w, int v, glw_style_t *origin)
{
  if(unlikely(w->glw_class->gc_set_hidden != NULL)) {
    w->glw_class->gc_set_hidden(w, v, origin);
    return;
  }

  if(v) {
    if(w->glw_flags & GLW_HIDDEN)
      return;

    glw_hide(w);
  } else {
    if(!(w->glw_flags & GLW_HIDDEN))
      return;
    glw_unhide(w);
  }
  glw_need_refresh(w->glw_root, 0);
  return;
}


/**
 *
 */
void
glw_set_divider(glw_t *w, int v)
{
  glw_conf_constraints(w, 0, 0, 0, GLW_CONSTRAINT_CONF_D);
  glw_need_refresh(w->glw_root, 0);
}


/**
 *
 */
static void
glw_set_zoffset(glw_t *w, int v)
{
  /*
    XXX: FIXME
  if(w->glw_class->gc_set_width != NULL) {
    w->glw_class->gc_set_width(w, v);
    return;
  }
  */
  w->glw_zoffset = v;
  glw_need_refresh(w->glw_root, 0);
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
  glw_t *w = ec->w;
  const char *s;

  switch(t->type) {
  case TOKEN_VECTOR_FLOAT:

    switch(t->t_elements) {

    case 3:
      vec3 = t->t_float_vector;
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

  case TOKEN_EM:
    ec->dynamic_eval |= GLW_VIEW_EVAL_EM;
    v[0] = v[1] = v[2] = t->t_float * w->glw_root->gr_current_size;
    vec3 = v;
    break;

  case TOKEN_INT:
    v[0] = v[1] = v[2] = t->t_int;
    vec3 = v;
    break;

  case TOKEN_VOID:
    v[0] = v[1] = v[2] = 0;
    vec3 = v;
    break;

  case TOKEN_RSTRING:
    s = rstr_get(t->t_rstring);
    if(s[0] == '#') {
      rgbstr_to_floatvec(s + 1, v);
      vec3 = v;
      break;
    }
    // FALLTHRU
  default:
    return glw_view_seterr(ec->ei, t, "Attribute '%s' expects a vec3, got %s",
			   a->name, token2name(t));
  }

  const glw_class_t *gc = w->glw_class;

  int r = gc->gc_set_float3 ? gc->gc_set_float3(w, a->attrib, vec3, NULL) : -1;

  if(r == -1) {
    respond_error(w, t, a->name);
    return 0;
  }
  if(r)
    attr_need_refresh(w->glw_root, t, a->name, r);

  return 0;
}


/**
 *
 */
static int
set_float4(glw_view_eval_context_t *ec, const token_attrib_t *a, 
	   struct token *t)
{
  const float *vec4;
  glw_t *w = ec->w;
  float v[4];

  switch(t->type) {
  case TOKEN_VECTOR_FLOAT:

    switch(t->t_elements) {

    case 4:
      vec4 = t->t_float_vector;
      break;

    case 2:
      v[0] = t->t_float_vector[0];
      v[1] = t->t_float_vector[1];
      v[2] = t->t_float_vector[0];
      v[3] = t->t_float_vector[1];
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

  case TOKEN_EM:
    ec->dynamic_eval |= GLW_VIEW_EVAL_EM;
    v[0] = v[1] = v[2] = v[3] = t->t_float * w->glw_root->gr_current_size;
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

  const glw_class_t *gc = w->glw_class;

  int r = gc->gc_set_float4 ? gc->gc_set_float4(w, a->attrib, vec4) : -1;

  if(r == -1) {
    respond_error(w, t, a->name);
    return 0;
  }
  if(r)
    attr_need_refresh(w->glw_root, t, a->name, r);

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
  glw_t *w = ec->w;

  switch(t->type) {
  case TOKEN_VECTOR_FLOAT:

    switch(t->t_elements) {

    case 4:
      v[0] = t->t_float_vector[0];
      v[1] = t->t_float_vector[1];
      v[2] = t->t_float_vector[2];
      v[3] = t->t_float_vector[3];
      break;

    case 2:
      v[0] = t->t_float_vector[0];
      v[1] = t->t_float_vector[1];
      v[2] = t->t_float_vector[0];
      v[3] = t->t_float_vector[1];
      break;

    default:
      return glw_view_seterr(ec->ei, t,
			     "Attribute '%s': invalid vector size %d",
			     a->name, t->t_elements);
    }
    break;

  case TOKEN_EM:
    ec->dynamic_eval |= GLW_VIEW_EVAL_EM;
    v[0] = v[1] = v[2] = v[3] = t->t_float * w->glw_root->gr_current_size;
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


  const glw_class_t *gc = w->glw_class;

  int r = gc->gc_set_int16_4 ? gc->gc_set_int16_4(w, a->attrib, v, NULL) : -1;

  if(r == -1) {
    respond_error(w, t, a->name);
    return 0;
  }
  if(r)
    attr_need_refresh(w->glw_root, t, a->name, r);

  return 0;
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
  if(t->type != TOKEN_IDENTIFIER ||
     (v = str2val(rstr_get(t->t_rstring), aligntab)) < 0)
    return glw_view_seterr(ec->ei, t, "Invalid assignment for attribute %s",
                           a->name);

  glw_set_align(ec->w, v, NULL);
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

  if(ec->w->glw_class->gc_set_int != NULL)
    ec->w->glw_class->gc_set_int(ec->w, GLW_ATTRIB_TRANSITION_EFFECT, v, NULL);
  glw_need_refresh(ec->w->glw_root, 0);
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

  glw_set_hidden(ec->w, v, NULL);
  return 0;
}


/**
 *
 */
static void
mod_flags2(glw_t *w, int set, int clr)
{
  const glw_class_t *gc = w->glw_class;

  if(gc->gc_mod_flags2_always != NULL)
    gc->gc_mod_flags2_always(w, set, clr, NULL);

  glw_mod_flags2(w, set, clr);

  glw_need_refresh(w->glw_root, 0);
}


/**
 *
 */
static void
mod_text_flags(glw_t *w, int set, int clr)
{
  if(w->glw_class->gc_mod_text_flags != NULL)
    w->glw_class->gc_mod_text_flags(w, set, clr, NULL);
  glw_need_refresh(w->glw_root, 0);
}


/**
 *
 */
static void
mod_img_flags(glw_t *w, int set, int clr)
{
  if(w->glw_class->gc_mod_image_flags != NULL)
    w->glw_class->gc_mod_image_flags(w, set, clr, NULL);
  glw_need_refresh(w->glw_root, 0);
}


/**
 *
 */
static void
mod_video_flags(glw_t *w, int set, int clr)
{
  if(w->glw_class->gc_mod_video_flags != NULL)
    w->glw_class->gc_mod_video_flags(w, set, clr);
  glw_need_refresh(w->glw_root, 0);
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
    if(t->type == TOKEN_RSTRING || t->type == TOKEN_URI)
      cnt++;
  
  rv = malloc(sizeof(rstr_t *) * cnt);
  cnt = 0;

  for(t = t0->child; t != NULL; t = t->next)
    if(t->type == TOKEN_RSTRING || t->type == TOKEN_URI)
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
  case TOKEN_URI:
    r = t->t_uri;
    break;
  }

  r = glw_resolve_path(r, t->file, w->glw_root, NULL);

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
      w->glw_class->gc_set_source(w, NULL, 0, NULL);
    return 0;

  case TOKEN_VECTOR:
    if(w->glw_class->gc_set_sources != NULL)
      w->glw_class->gc_set_sources(w, build_rstr_vector(t));
    return 0;

  case TOKEN_RSTRING:
    r = t->t_rstring;
    break;
  case TOKEN_URI:
    r = t->t_uri;
    break;
  }

  int flags = 0;
  r = glw_resolve_path(r, t->file, w->glw_root, &flags);

  if(w->glw_class->gc_set_source != NULL)
    w->glw_class->gc_set_source(w, r, flags, NULL);

  glw_need_refresh(w->glw_root, 0);
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
  if(t->type == TOKEN_PROPERTY_OWNER || t->type == TOKEN_PROPERTY_REF) {
    if(ec->w->glw_class->gc_set_prop != NULL)
      ec->w->glw_class->gc_set_prop(ec->w, GLW_ATTRIB_ARGS, t->t_prop);
  }

  return 0;
}


/**
 *
 */
static int
set_propref(glw_view_eval_context_t *ec, const token_attrib_t *a,
	   struct token *t)
{
  if(ec->w->glw_class->gc_set_prop == NULL)
    return 0;

  if(t->type == TOKEN_VOID) {
    ec->w->glw_class->gc_set_prop(ec->w, a->attrib, NULL);
    return 0;
  }
  if(t->type != TOKEN_PROPERTY_REF)
    return glw_view_seterr(ec->ei, t,
			   "Attribute '%s' expects a property ref, got %s",
			   a->name, token2name(t));

  prop_t *p = prop_get_prop(t->t_prop);
  ec->w->glw_class->gc_set_prop(ec->w, a->attrib, p);
  prop_ref_dec(p);
  return 0;
}


/**
 *
 */
static int
set_style(glw_view_eval_context_t *ec, const token_attrib_t *a,
          struct token *t)
{
  int r;

  switch(t->type) {
  default:
    r = glw_styleset_for_widget(ec->w, NULL, ec);
    break;

  case TOKEN_CSTRING:
    r = glw_styleset_for_widget(ec->w, t->t_cstring, ec);
    break;

  case TOKEN_RSTRING:
  case TOKEN_URI:
    r = glw_styleset_for_widget(ec->w, rstr_get(t->t_rstring), ec);
    break;

  case TOKEN_VECTOR:
    r = glw_styleset_for_widget_multiple(ec->w, t->child, ec);
    break;
  }

  if(r)
    attr_need_refresh(ec->w->glw_root, t, a->name, r);
  return 0;
}


/**
 *
 */
static const token_attrib_t attribtab[] = {
  {"style",           set_style},

  {"id",              set_rstring, 0, set_id_rstr},
  {"how",             set_rstring, 0, set_how_rstr},
  {"description",     set_rstring, 0, set_description_rstr},
  {"parentUrl",       set_rstring, 0, set_parent_url_rstr},
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
  {"alwaysGrabKnob",        mod_flag, GLW2_ALWAYS_GRAB_KNOB,       mod_flags2},
  {"autohide",              mod_flag, GLW2_AUTOHIDE,               mod_flags2},
  {"shadow",                mod_flag, GLW2_SHADOW,                 mod_flags2},
  {"autofade",              mod_flag, GLW2_AUTOFADE,               mod_flags2},
  {"automargin",            mod_flag, GLW2_AUTOMARGIN,             mod_flags2},
  {"expediteSubscriptions", mod_flag, GLW2_EXPEDITE_SUBSCRIPTIONS, mod_flags2},
  {"navWrap",               mod_flag, GLW2_NAV_WRAP,               mod_flags2},
  {"autoFocusLimit",        mod_flag, GLW2_AUTO_FOCUS_LIMIT,       mod_flags2},
  {"cursor",                mod_flag, GLW2_CURSOR,                 mod_flags2},
  {"navPositional",         mod_flag, GLW2_POSITIONAL_NAVIGATION,  mod_flags2},
  {"clickable",             mod_flag, GLW2_CLICKABLE,              mod_flags2},
  {"fhpSpill",              mod_flag, GLW2_FHP_SPILL,              mod_flags2},

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
  {"fileRequest",     mod_flag,  GTB_FILE_REQUEST, mod_text_flags},
  {"dirRequest",      mod_flag,  GTB_DIR_REQUEST, mod_text_flags},

  {"primary",         mod_flag, GLW_VIDEO_PRIMARY, mod_video_flags},
  {"noAudio",         mod_flag, GLW_VIDEO_NO_AUDIO, mod_video_flags},

  {"alpha",           set_float,  0, glw_set_alpha},
  {"blur",            set_float,  0, glw_set_blur},
  {"weight",          set_float,  0, glw_set_weight},
  {"focusable",       set_float,  0, glw_set_focus_weight},
  {"height",          set_int,    0, glw_set_height},
  {"width",           set_int,    0, glw_set_width},
  {"divider",         set_int,    0, glw_set_divider},
  {"zoffset",         set_int,    0, glw_set_zoffset},

  {"maxlines",        set_number, GLW_ATTRIB_MAX_LINES},
  {"sizeScale",       set_number, GLW_ATTRIB_SIZE_SCALE},
  {"size",            set_number, GLW_ATTRIB_SIZE},
  {"maxWidth",        set_number, GLW_ATTRIB_MAX_WIDTH},
  {"alphaSelf",       set_number, GLW_ATTRIB_ALPHA_SELF},
  {"saturation",      set_number, GLW_ATTRIB_SATURATION},
  {"time",            set_number, GLW_ATTRIB_TIME},
  {"transitionTime",  set_number, GLW_ATTRIB_TRANSITION_TIME},
  {"angle",           set_number, GLW_ATTRIB_ANGLE},
  {"expansion",       set_number, GLW_ATTRIB_EXPANSION},
  {"min",             set_number, GLW_ATTRIB_INT_MIN},
  {"max",             set_number, GLW_ATTRIB_INT_MAX},
  {"step",            set_number, GLW_ATTRIB_INT_STEP},
  {"value",           set_number, GLW_ATTRIB_VALUE},
  {"childAspect",     set_number, GLW_ATTRIB_CHILD_ASPECT},
  {"center",          set_number, GLW_ATTRIB_CENTER},
  {"audioVolume",     set_number, GLW_ATTRIB_AUDIO_VOLUME},
  {"aspect",          set_number, GLW_ATTRIB_ASPECT},
  {"alphaFallOff",    set_number, GLW_ATTRIB_ALPHA_FALLOFF},
  {"blurFallOff",     set_number, GLW_ATTRIB_BLUR_FALLOFF},
  {"fill",            set_number, GLW_ATTRIB_FILL},
  {"childScale",      set_number, GLW_ATTRIB_CHILD_SCALE},

  {"childTilesX",     set_number, GLW_ATTRIB_CHILD_TILES_X},
  {"childTilesY",     set_number, GLW_ATTRIB_CHILD_TILES_Y},

  {"alphaEdges",      set_number, GLW_ATTRIB_ALPHA_EDGES},
  {"priority",        set_number, GLW_ATTRIB_PRIORITY},
  {"spacing",         set_number, GLW_ATTRIB_SPACING},
  {"Xspacing",        set_number, GLW_ATTRIB_X_SPACING},
  {"Yspacing",        set_number, GLW_ATTRIB_Y_SPACING},
  {"cornerRadius",    set_number, GLW_ATTRIB_RADIUS},

  {"color",           set_float3, GLW_ATTRIB_RGB},
  {"translation",     set_float3, GLW_ATTRIB_TRANSLATION},
  {"scaling",         set_float3, GLW_ATTRIB_SCALING},
  {"color1",          set_float3, GLW_ATTRIB_COLOR1},
  {"color2",          set_float3, GLW_ATTRIB_COLOR2},

  {"rotation",        set_float4, GLW_ATTRIB_ROTATION},
  {"plane",           set_float4, GLW_ATTRIB_PLANE},

  {"padding",         set_int16_4, GLW_ATTRIB_PADDING},
  {"border",          set_int16_4, GLW_ATTRIB_BORDER},
  {"margin",          set_int16_4, GLW_ATTRIB_MARGIN},

  {"align",           set_align,  0},
  {"effect",          set_transition_effect,  0},

  {"args",            set_args},
  {"self",            set_propref, GLW_ATTRIB_PROP_SELF, NULL,
   GLW_ATTRIB_FLAG_NO_SUBSCRIPTION},
  {"itemModel",       set_propref, GLW_ATTRIB_PROP_ITEM_MODEL, NULL,
   GLW_ATTRIB_FLAG_NO_SUBSCRIPTION},
  {"parentModel",     set_propref, GLW_ATTRIB_PROP_PARENT_MODEL, NULL,
   GLW_ATTRIB_FLAG_NO_SUBSCRIPTION},
  {"tentative",       set_propref, GLW_ATTRIB_TENTATIVE_VALUE, NULL,
   GLW_ATTRIB_FLAG_NO_SUBSCRIPTION},
};




/**
 *
 */
static void
unresolved_set_float(glw_t *w, const char *attrib, const token_t *t,
                     float val)
{
  const glw_class_t *gc = w->glw_class;
  int r;

  r = gc->gc_set_float_unresolved ?
    gc->gc_set_float_unresolved(w, attrib, val, NULL) : -1;

  if(r == -1)
    r = gc->gc_set_int_unresolved ?
      gc->gc_set_int_unresolved(w, attrib, val, NULL) : -1;

  if(r == -1) {
    respond_error(w, t, attrib);
    return;
  }

  if(r)
    attr_need_refresh(w->glw_root, t, attrib, r);
}


/**
 *
 */
static void
unresolved_set_int(glw_t *w, const char *attrib, const token_t *t,
                   int val)
{
  const glw_class_t *gc = w->glw_class;
  int r;

  r = gc->gc_set_int_unresolved ?
    gc->gc_set_int_unresolved(w, attrib, val, NULL) : -1;

  if(r == -1)
    r = gc->gc_set_float_unresolved ?
      gc->gc_set_float_unresolved(w, attrib, val, NULL) : -1;

  if(r == -1) {
    respond_error(w, t, attrib);
    return;
  }

  if(r)
    attr_need_refresh(w->glw_root, t, attrib, r);
}


/**
 *
 */
static void
unresolved_set_str(glw_t *w, const char *attrib, const token_t *t,
                   rstr_t *val)
{
  const glw_class_t *gc = w->glw_class;
  int r;

  r = gc->gc_set_rstr_unresolved ?
    gc->gc_set_rstr_unresolved(w, attrib, val, NULL) : -1;

  if(r == -1) {
    respond_error(w, t, attrib);
    return;
  }

  if(r)
    attr_need_refresh(w->glw_root, t, attrib, r);
}


/**
 *
 */
int
glw_view_unresolved_attribute_set(glw_view_eval_context_t *ec,
                                  const char *attrib,
                                  struct token *t)
{
  glw_t *w = ec->w;
  rstr_t *rstr;

  switch(t->type) {
  case TOKEN_VOID:
    break;

  case TOKEN_CSTRING:
    rstr = rstr_alloc(t->t_cstring);
    unresolved_set_str(w, attrib, t, rstr);
    rstr_release(rstr);
    break;

  case TOKEN_RSTRING:
  case TOKEN_URI:
    unresolved_set_str(w, attrib, t, t->t_rstring);
    break;

  case TOKEN_INT:
    unresolved_set_int(w, attrib, t, t->t_int);
    break;

  case TOKEN_FLOAT:
    unresolved_set_float(w, attrib, t, t->t_float);
    break;

  case TOKEN_EM:
    ec->dynamic_eval |= GLW_VIEW_EVAL_EM;
    unresolved_set_float(w, attrib, t,
                         t->t_float * w->glw_root->gr_current_size);
    break;
  default:
    return glw_view_seterr(ec->ei, t,
			   "Attribute '%s' expects a different type",
			   attrib, token2name(t));
  }
  return 0;
}


/**
 *
 */
void
glw_view_attrib_resolve(token_t *t)
{
  int i;

  for(i = 0; i < sizeof(attribtab) / sizeof(attribtab[0]); i++) {
    if(!strcmp(attribtab[i].name, rstr_get(t->t_rstring))) {
      rstr_release(t->t_rstring);
      t->t_attrib = &attribtab[i];
      t->type = TOKEN_RESOLVED_ATTRIBUTE;
      return;
    }
  }

  t->type = TOKEN_UNRESOLVED_ATTRIBUTE;
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
