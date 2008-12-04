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
#include <libhts/hts_strtab.h>

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
    str = t->t_string;
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
  float f;

  switch(t->type) {
  case TOKEN_STRING:
    f = strtod(t->t_string, NULL);
    break;

  case TOKEN_FLOAT:
    f = t->t_float;
    break;

  case TOKEN_INT:
    f = t->t_int;
    break;

  case TOKEN_VOID:
    f = 0;
    break;

  default:
    return glw_model_seterr(ec->ei, t, "Attribute '%s' expects a scalar",
			    a->name);
  }

  glw_set_i(ec->w, a->attrib, f, NULL);
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
    v = atoi(t->t_string);
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
  { "default",       GLW_ALIGN_DEFAULT},
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
  if(t->type != TOKEN_IDENTIFIER || (v = str2val(t->t_string, aligntab)) < 0)
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
  if(t->type != TOKEN_IDENTIFIER || (v = str2val(t->t_string,
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

  if(t->type == TOKEN_IDENTIFIER)
    set = !strcmp(t->t_string, "true");
  else if(t->type == TOKEN_INT)
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
set_mirror(glw_model_eval_context_t *ec, const token_attrib_t *a, 
	   struct token *t)
{
  int set = 0;

  if(t->type == TOKEN_IDENTIFIER)
    set = !strcmp(t->t_string, "true");
  else if(t->type == TOKEN_INT)
    set = t->t_int;
  else if(t->type == TOKEN_FLOAT)
    set = t->t_float > 0.5;
  else if(t->type == TOKEN_VOID)
    set = 0;
  else
    return glw_model_seterr(ec->ei, t, "Invalid assignment for attribute %s",
			    a->name);

  if(set)
    glw_set_i(ec->w, GLW_ATTRIB_MIRROR, a->attrib, NULL);

  return 0;
}


/**
 *
 */
static int
set_focus(glw_model_eval_context_t *ec, const token_attrib_t *a, 
	  struct token *t)
{
  glw_focus_mode_t gfm;

  if(t->type == TOKEN_IDENTIFIER) {

    if(!strcmp(t->t_string, "target") || !strcmp(t->t_string, "true"))
      gfm = GLW_FOCUS_TARGET;
    else if(!strcmp(t->t_string, "leader"))
      gfm = GLW_FOCUS_LEADER;
    else
      gfm = GLW_FOCUS_NONE;

  } else if(t->type == TOKEN_INT)
    gfm = t->t_int ? GLW_FOCUS_TARGET : 0;
  else if(t->type == TOKEN_FLOAT)
    gfm = t->t_float > 0.5  ? GLW_FOCUS_TARGET : 0;
  else if(t->type == TOKEN_VOID)
    gfm = GLW_FOCUS_NONE;
  else
    return glw_model_seterr(ec->ei, t, "Invalid assignment for attribute %s",
			    a->name);
  
  glw_set_i(ec->w, GLW_ATTRIB_FOCUSABLE, gfm, NULL);

  return 0;
}




/**
 *
 */
static const token_attrib_t attribtab[] = {
  {"id",              set_string, GLW_ATTRIB_ID},
  {"caption",         set_string, GLW_ATTRIB_CAPTION},
  {"source",          set_string, GLW_ATTRIB_SOURCE},

  {"expandChilds",    set_flag,   GLW_EXPAND_CHILDS},
  {"borderScale",     set_flag,   GLW_BORDER_SCALE_CHILDS},
  {"focusCursor",     set_flag,   GLW_FOCUS_DRAW_CURSOR},
  {"keepAspect",      set_flag,   GLW_KEEP_ASPECT},
  {"debug",           set_flag,   GLW_DEBUG},
  {"skeleton",        set_flag,   GLW_DRAW_SKEL},
  {"borderBlend",     set_flag,   GLW_BORDER_BLEND},
  {"password",        set_flag,   GLW_PASSWORD},
  {"mirrorx",         set_mirror, GLW_MIRROR_X},
  {"mirrory",         set_mirror, GLW_MIRROR_Y},

  {"slices",          set_int,    GLW_ATTRIB_SLICES},
  {"min",             set_int,    GLW_ATTRIB_INT_MIN},
  {"max",             set_int,    GLW_ATTRIB_INT_MAX},
  {"step",            set_int,    GLW_ATTRIB_INT_STEP},
  {"value",           set_int,    GLW_ATTRIB_INT},
  {"xslices",         set_int,    GLW_ATTRIB_X_SLICES},
  {"yslices",         set_int,    GLW_ATTRIB_Y_SLICES},

  {"alpha",           set_float,  GLW_ATTRIB_ALPHA},
  {"aspect",          set_float,  GLW_ATTRIB_ASPECT},
  {"weight",          set_float,  GLW_ATTRIB_WEIGHT},
  {"time",            set_float,  GLW_ATTRIB_TIME},
  {"angle",           set_float,  GLW_ATTRIB_ANGLE},
  {"xfill",           set_float,  GLW_ATTRIB_XFILL},
  {"expand",          set_float,  GLW_ATTRIB_EXPAND_REQUEST},

  {"displacement",    set_float3, GLW_ATTRIB_DISPLACEMENT},
  {"color",           set_float3, GLW_ATTRIB_RGB},
  {"textureBorders",  set_float4, GLW_ATTRIB_TEXTURE_BORDERS},
  {"vertexBorders",   set_float4, GLW_ATTRIB_VERTEX_BORDERS},

  {"align",           set_align,  0},
  {"effect",          set_transition_effect,  0},
  {"focus",           set_focus, 0},
};


/**
 *
 */
int 
glw_model_attrib_resolve(token_t *t)
{
  int i;

  for(i = 0; i < sizeof(attribtab) / sizeof(attribtab[0]); i++)
    if(!strcmp(attribtab[i].name, t->t_string)) {
      free(t->t_string);
      t->t_attrib = &attribtab[i];
      t->type = TOKEN_OBJECT_ATTRIBUTE;
      return 0;
    }
  return -1;
}
