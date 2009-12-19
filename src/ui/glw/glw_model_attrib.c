/*
 *  GL Widgets, model loader, widget attributes
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
#include "glw_model.h"
#include "glw.h"

/**
 *
 */
static int
set_string(glw_model_eval_context_t *ec, const token_attrib_t *a,
	   struct token *t)
{
  char buf[30];
  const char *str;

  switch(t->type) {
  case TOKEN_VOID:
    str = "";
    break;

  case TOKEN_STRING:
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
    return glw_model_seterr(ec->ei, t, 
			    "Attribute '%s' expects a string or scalar",
			    a->name);
  }

  glw_set_i(ec->w, a->attrib, str, NULL);
  return 0;
}


/**
 *
 */
static int
set_float(glw_model_eval_context_t *ec, const token_attrib_t *a, 
	  struct token *t)
{
  double d;

  switch(t->type) {
  case TOKEN_STRING:
  case TOKEN_LINK:
    d = strtod(rstr_get(t->t_rstring), NULL);
    break;

  case TOKEN_FLOAT:
    d = t->t_float;
    break;

  case TOKEN_INT:
    d = t->t_int;
    break;

  case TOKEN_VOID:
    d = 0;
    break;
  default:
    return glw_model_seterr(ec->ei, t, "Attribute '%s' expects a scalar",
			    a->name);
  }

  glw_set_i(ec->w, a->attrib, d, NULL);
  return 0;
}

/**
 *
 */
static int
set_int(glw_model_eval_context_t *ec, const token_attrib_t *a, 
	  struct token *t)
{
  int v;

  switch(t->type) {
  case TOKEN_STRING:
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
    return glw_model_seterr(ec->ei, t, "Attribute '%s' expects a scalar",
			    a->name);
  }

  glw_set_i(ec->w, a->attrib, v, NULL);
  return 0;
}


/**
 *
 */
static int
set_float3(glw_model_eval_context_t *ec, const token_attrib_t *a, 
	   struct token *t)
{
  if(t->type != TOKEN_VECTOR_FLOAT || t->t_elements != 3)
    return glw_model_seterr(ec->ei, t, "Attribute '%s' expects a vec3",
			    a->name);

  glw_set_i(ec->w, a->attrib, 
	    t->t_float_vector[0],
	    t->t_float_vector[1],
	    t->t_float_vector[2],
	    NULL);
  return 0;
}


/**
 *
 */
static int
set_float4(glw_model_eval_context_t *ec, const token_attrib_t *a, 
	   struct token *t)
{
  if(t->type != TOKEN_VECTOR_FLOAT || t->t_elements != 4)
    return glw_model_seterr(ec->ei, t, "Attribute '%s' expects a vec4",
			    a->name);

  glw_set_i(ec->w, a->attrib, 
	    t->t_float_vector[0],
	    t->t_float_vector[1],
	    t->t_float_vector[2],
	    t->t_float_vector[3],
	    NULL);
  return 0;
}


static struct strtab aligntab[] = {
  { "center",        GLW_ALIGN_CENTER},
  { "left",          GLW_ALIGN_LEFT},
  { "right",         GLW_ALIGN_RIGHT},
  { "top",           GLW_ALIGN_TOP},
  { "bottom",        GLW_ALIGN_BOTTOM}
};


/**
 *
 */
static int
set_align(glw_model_eval_context_t *ec, const token_attrib_t *a, 
	  struct token *t)
{
  int v;
  if(t->type != TOKEN_IDENTIFIER || (v = str2val(rstr_get(t->t_rstring), aligntab)) < 0)
    return glw_model_seterr(ec->ei, t, "Invalid assignment for attribute %s",
			    a->name);
  glw_set_i(ec->w, GLW_ATTRIB_ALIGNMENT, v, NULL);
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
set_transition_effect(glw_model_eval_context_t *ec, const token_attrib_t *a, 
		      struct token *t)
{
  int v;
  if(t->type != TOKEN_IDENTIFIER || (v = str2val(rstr_get(t->t_rstring),
						 transitiontab)) < 0)
    return glw_model_seterr(ec->ei, t, "Invalid assignment for attribute %s",
			    a->name);
  glw_set_i(ec->w, GLW_ATTRIB_TRANSITION_EFFECT, v, NULL);
  return 0;
}


/**
 *
 */
static int
set_flag(glw_model_eval_context_t *ec, const token_attrib_t *a, 
	  struct token *t)
{
  int set = 0;

  if(t->type == TOKEN_INT)
    set = t->t_int;
  else if(t->type == TOKEN_FLOAT)
    set = t->t_float > 0.5;
  else if(t->type == TOKEN_VOID)
    set = 0;
  else
    return glw_model_seterr(ec->ei, t, "Invalid assignment for attribute %s",
			    a->name);

  if(set)
    glw_set_i(ec->w, GLW_ATTRIB_SET_FLAGS, a->attrib, NULL);
  else
    glw_set_i(ec->w, GLW_ATTRIB_CLR_FLAGS, a->attrib, NULL);

  return 0;
}



/**
 *
 */
static int
set_bitmap_flag(glw_model_eval_context_t *ec, const token_attrib_t *a, 
		struct token *t)
{
  int set = 0;

  if(t->type == TOKEN_INT)
    set = t->t_int;
  else if(t->type == TOKEN_FLOAT)
    set = t->t_float > 0.5;
  else if(t->type == TOKEN_VOID)
    set = 0;
  else
    return glw_model_seterr(ec->ei, t, "Invalid assignment for attribute %s",
			    a->name);

  if(set)
    glw_set_i(ec->w, GLW_ATTRIB_SET_BITMAP_FLAGS, a->attrib, NULL);
  else
    glw_set_i(ec->w, GLW_ATTRIB_CLR_BITMAP_FLAGS, a->attrib, NULL);

  return 0;
}

/**
 *
 */
static int
set_source(glw_model_eval_context_t *ec, const token_attrib_t *a,
	   struct token *t)
{
  switch(t->type) {
  case TOKEN_VOID:
    glw_set_i(ec->w, GLW_ATTRIB_SOURCE, NULL, NULL);
    break;

  case TOKEN_STRING:
  case TOKEN_LINK:
    glw_set_i(ec->w, GLW_ATTRIB_SOURCE, rstr_get(t->t_rstring), NULL);
    break;

  case TOKEN_PIXMAP:
    glw_set_i(ec->w, GLW_ATTRIB_PIXMAP, t->t_pixmap, NULL);
    break;

  default:
    return glw_model_seterr(ec->ei, t, 
			    "Attribute '%s' expects a string or scalar",
			    a->name);
  }
  return 0;
}



/**
 *
 */
static const token_attrib_t attribtab[] = {
  {"id",              set_string, GLW_ATTRIB_ID},
  {"caption",         set_string, GLW_ATTRIB_CAPTION},
  {"source",          set_source, 0},

  {"debug",           set_flag,   GLW_DEBUG},
  {"skeleton",        set_flag,   GLW_DRAW_SKEL},
  {"password",        set_flag,   GLW_PASSWORD},
  {"filterConstraintX",       set_flag, GLW_CONSTRAINT_IGNORE_X},
  {"filterConstraintY",       set_flag, GLW_CONSTRAINT_IGNORE_Y},
  {"filterConstraintAspect",  set_flag, GLW_CONSTRAINT_IGNORE_A},
  {"filterConstraintWeight",  set_flag, GLW_CONSTRAINT_IGNORE_W},
  {"ellipsize",               set_flag, GLW_ELLIPSIZE},
  {"hidden",                  set_flag, GLW_HIDDEN},
  {"noInitialTransform",      set_flag, GLW_NO_INITIAL_TRANS},

  {"mirrorx",         set_bitmap_flag, GLW_MIRROR_X},
  {"mirrory",         set_bitmap_flag, GLW_MIRROR_Y},

  {"borderLeft",      set_bitmap_flag, GLW_BORDER_LEFT},
  {"borderRight",     set_bitmap_flag, GLW_BORDER_RIGHT},
  {"borderTop",       set_bitmap_flag, GLW_BORDER_TOP},
  {"borderBottom",    set_bitmap_flag, GLW_BORDER_BOTTOM},
  {"constraintX",     set_bitmap_flag, GLW_NOFILL_X},
  {"constraintY",     set_bitmap_flag, GLW_NOFILL_Y},


  {"alpha",           set_float,  GLW_ATTRIB_ALPHA},
  {"alphaSelf",       set_float,  GLW_ATTRIB_ALPHA_SELF},
  {"aspect",          set_float,  GLW_ATTRIB_ASPECT},
  {"weight",          set_float,  GLW_ATTRIB_WEIGHT},
  {"time",            set_float,  GLW_ATTRIB_TIME},
  {"angle",           set_float,  GLW_ATTRIB_ANGLE},
  {"expansion",       set_float,  GLW_ATTRIB_EXPANSION},
  {"min",             set_float,  GLW_ATTRIB_INT_MIN},
  {"max",             set_float,  GLW_ATTRIB_INT_MAX},
  {"step",            set_float,  GLW_ATTRIB_INT_STEP},
  {"value",           set_float,  GLW_ATTRIB_VALUE},
  {"sizeScale",       set_float,  GLW_ATTRIB_SIZE_SCALE},
  {"sizeBias",        set_float,  GLW_ATTRIB_SIZE_BIAS},
  {"focusable",       set_float,  GLW_ATTRIB_FOCUS_WEIGHT},
  {"childAspect",     set_float,  GLW_ATTRIB_CHILD_ASPECT},

  {"height",          set_float,  GLW_ATTRIB_HEIGHT},
  {"width",           set_float,  GLW_ATTRIB_WIDTH},

  {"childWidth",      set_int,    GLW_ATTRIB_CHILD_WIDTH},
  {"childHeight",     set_int,    GLW_ATTRIB_CHILD_HEIGHT},

  {"childTilesX",     set_int,    GLW_ATTRIB_CHILD_TILES_X},
  {"childTilesY",     set_int ,   GLW_ATTRIB_CHILD_TILES_Y},

  {"color",           set_float3, GLW_ATTRIB_RGB},
  {"borderSize",      set_float4, GLW_ATTRIB_BORDER_SIZE},
  {"padding",         set_float4, GLW_ATTRIB_PADDING},

  {"align",           set_align,  0},
  {"effect",          set_transition_effect,  0},
};


/**
 *
 */
int 
glw_model_attrib_resolve(token_t *t)
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
