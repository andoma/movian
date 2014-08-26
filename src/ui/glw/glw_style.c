/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2014 Lonelycoder AB
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

#include "glw_style.h"
#include "glw_view.h"

LIST_HEAD(glw_style_attribute_list, glw_style_attribute);

typedef enum {
  GSA_NONE,
  GSA_INT,
  GSA_FLOAT,
  GSA_RSTR,
  GSA_FVEC3,
  GSA_IVEC16_4,
} gsa_type_t;



typedef struct glw_style_attribute {
  LIST_ENTRY(glw_style_attribute) gsa_link;
  glw_attribute_t gsa_attribute;
  gsa_type_t gsa_type;

  union {
    int i32;
    float f;
    rstr_t *rstr;
    float fvec[4];
    int16_t i16vec[4];

    struct {
      int set;
      int clr;
    } flags;
  };

} glw_style_attribute_t;


/**
 *
 */
struct glw_style {
  glw_t w;
  int gs_refcount;
  rstr_t *gs_name;
  struct glw_head gs_widgets;
  struct glw_style_attribute_list gs_attributes;

  uint32_t gs_flags2_set;
  uint32_t gs_flags2_clr;

  uint32_t gs_image_flags_set;
  uint32_t gs_image_flags_clr;
  uint32_t gs_text_flags_set;
  uint32_t gs_text_flags_clr;

  uint32_t gs_flags;
#define GS_SET_FOCUS_WEIGHT 0x1
#define GS_SET_ALPHA        0x2
#define GS_SET_BLUR         0x4
#define GS_SET_WEIGHT       0x8
#define GS_SET_WIDTH        0x10
#define GS_SET_HEIGHT       0x20





};


/**
 * If widget does not respond to float callback, try with int callback
 */
static int
set_float_on_widget(glw_t *w, glw_style_attribute_t *gsa)
{
  const glw_class_t *gc = w->glw_class;
  int x;

  x = gc->gc_set_float ? gc->gc_set_float(w, gsa->gsa_attribute, gsa->f) : -1;
  if(x == -1)
    x = gc->gc_set_int ? gc->gc_set_int(w, gsa->gsa_attribute, gsa->f) : -1;
  return x;
}


/**
 * If widget does not respond to int callback, try with float callback
 */
static int
set_int_on_widget(glw_t *w, glw_style_attribute_t *gsa)
{
  const glw_class_t *gc = w->glw_class;
  int x;

  x = gc->gc_set_int ? gc->gc_set_int(w, gsa->gsa_attribute, gsa->i32) : -1;
  if(x == -1)
    x = gc->gc_set_float ? gc->gc_set_float(w, gsa->gsa_attribute, gsa->i32):-1;
  return x;
}


/**
 *
 */
static void
set_flags2_on_widget(glw_t *w, int set, int clr)
{
  const glw_class_t *gc = w->glw_class;

  set &= ~w->glw_flags2;
  w->glw_flags2 |= set;

  clr &= w->glw_flags2;
  w->glw_flags2 &= ~clr;

  if((set | clr) && gc->gc_mod_flags2 != NULL)
    gc->gc_mod_flags2(w, set, clr);
}


/**
 *
 */
static void
glw_style_attribute_clean(glw_style_attribute_t *gsa)
{
  if(gsa->gsa_type == GSA_RSTR) {
    rstr_release(gsa->rstr);
    gsa->gsa_type = GSA_NONE;
  }
}


/**
 *
 */
static glw_style_attribute_t *
glw_style_find_attribute(glw_style_t *gs, glw_attribute_t attrib, int *created)
{
  glw_style_attribute_t *gsa;
  LIST_FOREACH(gsa, &gs->gs_attributes, gsa_link) {
    if(gsa->gsa_attribute == attrib) {
      glw_style_attribute_clean(gsa);
      *created = 0;
      return gsa;
    }
  }

  gsa = malloc(sizeof(glw_style_attribute_t));
  gsa->gsa_attribute = attrib;
  gsa->gsa_type = GSA_NONE;
  LIST_INSERT_HEAD(&gs->gs_attributes, gsa, gsa_link);
  *created = 1;
  return gsa;
}


/**
 *
 */
static glw_style_t * attribute_unused_result
glw_style_retain(glw_style_t *gs)
{
  gs->gs_refcount++;
  return gs;
}


/**
 *
 */
static void
glw_style_release(glw_style_t *gs)
{
  glw_style_attribute_t *gsa;
  glw_t *w = &gs->w;
  gs->gs_refcount--;
  if(gs->gs_refcount)
    return;

  assert(LIST_FIRST(&gs->gs_widgets) == NULL);
  glw_prop_subscription_destroy_list(w->glw_root, &w->glw_prop_subscriptions);
  glw_view_free_chain(w->glw_root, w->glw_dynamic_expressions);

  rstr_release(gs->gs_name);

  while((gsa = LIST_FIRST(&gs->gs_attributes)) != NULL) {
    glw_style_attribute_clean(gsa);
    LIST_REMOVE(gsa, gsa_link);
    free(gsa);
  }
  free(gs);
}


/**
 *
 */
static void
setr(int in, int *outp)
{
  if(in == -1)    // Widget does not respond to attribute, skip
    return;
  if(*outp == 1)  // We will already rerender, avoid setting to layout only
    return;
  if(in > 0)      // Need to do something
    *outp = in;
}


/**
 *
 */
static int
gs_apply_float3(glw_style_t *gs, glw_style_attribute_t *gsa)
{
  glw_t *w;
  int r = 0;
  LIST_FOREACH(w, &gs->gs_widgets, glw_style_link)
    setr(w->glw_class->gc_set_float3(w, gsa->gsa_attribute, gsa->fvec), &r);
  return r;
}


/**
 *
 */
static int
gs_set_float3(struct glw *w, glw_attribute_t a, const float *vector)
{
  glw_style_t *gs = (glw_style_t *)w;
  int created;
  glw_style_attribute_t *gsa = glw_style_find_attribute(gs, a, &created);

  if(!created && !memcmp(gsa->fvec, vector, sizeof(float) * 3))
    return 0;

  gsa->gsa_type = GSA_FVEC3;
  memcpy(gsa->fvec, vector, sizeof(float) * 3);
  return gs_apply_float3(gs, gsa);
}


/**
 *
 */
static int
gs_apply_int16_4(glw_style_t *gs, glw_style_attribute_t *gsa)
{
  glw_t *w;
  int r = 0;
  LIST_FOREACH(w, &gs->gs_widgets, glw_style_link)
    setr(w->glw_class->gc_set_int16_4(w, gsa->gsa_attribute, gsa->i16vec), &r);
  return r;
}


/**
 *
 */
static int
gs_set_int16_4(struct glw *w, glw_attribute_t a, const int16_t *vector)
{
  glw_style_t *gs = (glw_style_t *)w;
  int created;
  glw_style_attribute_t *gsa = glw_style_find_attribute(gs, a, &created);

  if(!created &&
     !memcmp(gsa->i16vec, vector, sizeof(int16_t) * 4))
    return 0;

  gsa->gsa_type = GSA_IVEC16_4;
  memcpy(gsa->i16vec, vector, sizeof(int16_t) * 4);
  return gs_apply_int16_4(gs, gsa);
}


/**
 *
 */
static int
gs_apply_rstr(glw_style_t *gs, glw_style_attribute_t *gsa)
{
  glw_t *w;
  int r = 0;
  LIST_FOREACH(w, &gs->gs_widgets, glw_style_link)
    setr(w->glw_class->gc_set_rstr(w, gsa->gsa_attribute, gsa->rstr), &r);
  return r;
}


/**
 *
 */
static int
gs_set_rstr(struct glw *w, glw_attribute_t a, rstr_t *rstr)
{
  glw_style_t *gs = (glw_style_t *)w;
  int created;
  glw_style_attribute_t *gsa = glw_style_find_attribute(gs, a, &created);

  if(!created && rstr_eq(rstr, gsa->rstr))
    return 0;

  gsa->gsa_type = GSA_RSTR;
  gsa->rstr = rstr_dup(rstr);
  return gs_apply_rstr(gs, gsa);
}


/**
 *
 */
static int
gs_apply_int(glw_style_t *gs, glw_style_attribute_t *gsa)
{
  glw_t *w;
  int r = 0;
  LIST_FOREACH(w, &gs->gs_widgets, glw_style_link)
    setr(set_int_on_widget(w, gsa), &r);
  return r;
}


/**
 *
 */
static int
gs_set_int(struct glw *w, glw_attribute_t a, int i32)
{
  glw_style_t *gs = (glw_style_t *)w;
  int created;
  glw_style_attribute_t *gsa = glw_style_find_attribute(gs, a, &created);

  if(!created && i32 == gsa->i32)
    return 0;

  gsa->gsa_type = GSA_INT;
  gsa->i32 = i32;
  return gs_apply_int(gs, gsa);
}


/**
 *
 */
static int
gs_apply_float(glw_style_t *gs, glw_style_attribute_t *gsa)
{
  glw_t *w;
  int r = 0;
  LIST_FOREACH(w, &gs->gs_widgets, glw_style_link)
    setr(set_float_on_widget(w, gsa), &r);
  return r;
}


/**
 *
 */
static int
gs_set_float(struct glw *w, glw_attribute_t a, float f)
{
  glw_style_t *gs = (glw_style_t *)w;
  int created;
  glw_style_attribute_t *gsa = glw_style_find_attribute(gs, a, &created);

  if(!created && f == gsa->f)
    return 0;

  gsa->gsa_type = GSA_FLOAT;
  gsa->f = f;
  return gs_apply_float(gs, gsa);
}


/**
 *
 */
static void
gs_mod_flags2(struct glw *w, int set, int clr)
{
  glw_style_t *gs = (glw_style_t *)w;

  gs->gs_flags2_set |= set;
  gs->gs_flags2_clr |= clr;

  gs->gs_flags2_clr &= ~set;
  gs->gs_flags2_set &= ~clr;

  LIST_FOREACH(w, &gs->gs_widgets, glw_style_link)
    set_flags2_on_widget(w, set, clr);
}


/**
 *
 */
static void
gs_mod_text_flags(struct glw *w, int set, int clr)
{
  glw_style_t *gs = (glw_style_t *)w;

  gs->gs_text_flags_set |= set;
  gs->gs_text_flags_clr |= clr;

  gs->gs_text_flags_clr &= ~set;
  gs->gs_text_flags_set &= ~clr;

  LIST_FOREACH(w, &gs->gs_widgets, glw_style_link)
    if(w->glw_class->gc_mod_text_flags != NULL)
      w->glw_class->gc_mod_text_flags(w, set, clr);
}


/**
 *
 */
static void
gs_mod_image_flags(struct glw *w, int set, int clr)
{
  glw_style_t *gs = (glw_style_t *)w;

  gs->gs_image_flags_set |= set;
  gs->gs_image_flags_clr |= clr;

  gs->gs_image_flags_clr &= ~set;
  gs->gs_image_flags_set &= ~clr;

  LIST_FOREACH(w, &gs->gs_widgets, glw_style_link)
    if(w->glw_class->gc_mod_image_flags != NULL)
      w->glw_class->gc_mod_image_flags(w, set, clr);
}


/**
 *
 */
static void
gs_set_focus_weight(struct glw *w, float v)
{
  glw_style_t *gs = (glw_style_t *)w;

  gs->gs_flags |= GS_SET_FOCUS_WEIGHT;

  w->glw_focus_weight = v;

  LIST_FOREACH(w, &gs->gs_widgets, glw_style_link)
    glw_set_focus_weight(w, v);
}


/**
 *
 */
static void
gs_set_alpha(struct glw *w, float v)
{
  glw_style_t *gs = (glw_style_t *)w;

  gs->gs_flags |= GS_SET_ALPHA;

  w->glw_alpha = v;

  LIST_FOREACH(w, &gs->gs_widgets, glw_style_link)
    glw_set_alpha(w, v);
}


/**
 *
 */
static void
gs_set_blur(struct glw *w, float v)
{
  glw_style_t *gs = (glw_style_t *)w;

  gs->gs_flags |= GS_SET_BLUR;

  w->glw_sharpness = v; // We borrow this even though it's not sharpness

  LIST_FOREACH(w, &gs->gs_widgets, glw_style_link)
    glw_set_blur(w, v);
}


/**
 *
 */
static void
gs_set_weight(struct glw *w, float v)
{
  glw_style_t *gs = (glw_style_t *)w;

  gs->gs_flags |= GS_SET_WEIGHT;

  w->glw_req_weight = v;

  LIST_FOREACH(w, &gs->gs_widgets, glw_style_link)
    glw_set_weight(w, v);
}


/**
 *
 */
static void
gs_set_width(struct glw *w, int v)
{
  glw_style_t *gs = (glw_style_t *)w;

  gs->gs_flags |= GS_SET_WIDTH;

  w->glw_req_size_x = v;

  LIST_FOREACH(w, &gs->gs_widgets, glw_style_link)
    glw_set_width(w, v);
}


/**
 *
 */
static void
gs_set_height(struct glw *w, int v)
{
  glw_style_t *gs = (glw_style_t *)w;

  gs->gs_flags |= GS_SET_HEIGHT;

  w->glw_req_size_y = v;

  LIST_FOREACH(w, &gs->gs_widgets, glw_style_link)
    glw_set_height(w, v);
}


/**
 *
 */
static glw_class_t glw_style = {
  .gc_name              = "style",
  .gc_set_float3        = gs_set_float3,
  .gc_set_int16_4       = gs_set_int16_4,
  .gc_set_rstr          = gs_set_rstr,
  .gc_set_float         = gs_set_float,
  .gc_set_int           = gs_set_int,

  .gc_mod_flags2_always = gs_mod_flags2,
  .gc_mod_text_flags    = gs_mod_text_flags,
  .gc_mod_image_flags   = gs_mod_image_flags,

  .gc_set_focus_weight  = gs_set_focus_weight,
  .gc_set_alpha         = gs_set_alpha,
  .gc_set_blur          = gs_set_blur,
  .gc_set_weight        = gs_set_weight,
  .gc_set_width         = gs_set_width,
  .gc_set_height        = gs_set_height,
};


/**
 *
 */
glw_style_t *
glw_style_create(glw_root_t *gr, rstr_t *name)
{
  glw_style_t *gs = calloc(1, sizeof(glw_style_t));
  gs->w.glw_class = &glw_style;
  gs->w.glw_root = gr;
  gs->gs_name = rstr_dup(name);
  return gs;
}


/**
 *
 */
void
glw_style_set_release(glw_style_set_t *gss)
{
  int i;

  if(gss == NULL)
    return;

  gss->gss_refcount--;
  if(gss->gss_refcount)
    return;

  for(i = 0; i < gss->gss_numstyles; i++)
    glw_style_release(gss->gss_styles[i]);

  free(gss);
}


/**
 *
 */
glw_style_set_t *
glw_style_set_add(glw_style_set_t *gss, glw_style_t *gs)
{
  int i, items;
  glw_style_set_t *copy;

  if(gss == NULL) {
    i = 0;
    items = 1;
  } else {

    for(i = 0; i < gss->gss_numstyles; i++) {
      if(!strcmp(rstr_get(gs->gs_name),
                 rstr_get(gss->gss_styles[i]->gs_name))) {
        glw_style_release(gss->gss_styles[i]); // Will be replaced
        break;
      }
    }
    items = i == gss->gss_numstyles ? i + 1 : gss->gss_numstyles;
  }

  copy = malloc(sizeof(glw_style_set_t) + sizeof(glw_style_t *) * items);
  copy->gss_refcount = 1;
  copy->gss_numstyles = items;
  if(gss != NULL) {
    int j;
    for(j = 0; j < gss->gss_numstyles; j++) {
      if(j != i)
        copy->gss_styles[j] = glw_style_retain(gss->gss_styles[j]);
    }
  }
  copy->gss_styles[i] = glw_style_retain(gs);
  return copy;
}


/**
 *
 */
int
glw_style_bind(glw_t *w, glw_style_t *gs)
{
  glw_style_attribute_t *gsa;
  int r;

  if(w->glw_style != NULL)
    LIST_REMOVE(w, glw_style_link);

  w->glw_style = gs;

  if(gs == NULL)
    return 0;


  LIST_INSERT_HEAD(&gs->gs_widgets, w, glw_style_link);

  LIST_FOREACH(gsa, &gs->gs_attributes, gsa_link) {

    switch(gsa->gsa_type) {

    default:
      abort();
    case GSA_INT:
      setr(set_int_on_widget(w, gsa), &r);
      break;
    case GSA_FLOAT:
      setr(set_float_on_widget(w, gsa), &r);
      break;
    case GSA_RSTR:
      setr(w->glw_class->gc_set_rstr(w, gsa->gsa_attribute, gsa->rstr), &r);
      break;
    case GSA_FVEC3:
      setr(w->glw_class->gc_set_float3(w, gsa->gsa_attribute, gsa->fvec), &r);
      break;
    case GSA_IVEC16_4:
      setr(w->glw_class->gc_set_int16_4(w, gsa->gsa_attribute, gsa->i16vec),&r);
      break;
    }
  }

  if(gs->gs_flags2_set || gs->gs_flags2_clr) {
    set_flags2_on_widget(w, gs->gs_flags2_set, gs->gs_flags2_clr);
    r = 1;
  }

  if(w->glw_class->gc_mod_image_flags &&
     (gs->gs_image_flags_set || gs->gs_image_flags_clr))
    w->glw_class->gc_mod_image_flags(w, gs->gs_image_flags_set,
                                     gs->gs_image_flags_clr);

  if(w->glw_class->gc_mod_text_flags &&
     (gs->gs_text_flags_set || gs->gs_text_flags_clr))
    w->glw_class->gc_mod_text_flags(w, gs->gs_text_flags_set,
                                    gs->gs_text_flags_clr);

  if(gs->gs_flags & GS_SET_FOCUS_WEIGHT)
    glw_set_focus_weight(w, gs->w.glw_focus_weight);

  if(gs->gs_flags & GS_SET_ALPHA)
    glw_set_alpha(w, gs->w.glw_alpha);

  if(gs->gs_flags & GS_SET_BLUR)
    glw_set_blur(w, gs->w.glw_sharpness);

  if(gs->gs_flags & GS_SET_WEIGHT)
    glw_set_weight(w, gs->w.glw_req_weight);

  if(gs->gs_flags & GS_SET_WIDTH)
    glw_set_width(w, gs->w.glw_req_size_x);

  if(gs->gs_flags & GS_SET_HEIGHT)
    glw_set_height(w, gs->w.glw_req_size_y);

  return r;
}


/**
 *
 */
int
glw_style_set_for_widget(glw_t *w, const char *name)
{
  glw_style_set_t *gss = w->glw_styles;
  int i;

  if(name != NULL && gss != NULL) {
    for(i = 0; i < gss->gss_numstyles; i++) {
      if(!strcmp(name, rstr_get(gss->gss_styles[i]->gs_name))) {
        return glw_style_bind(w, gss->gss_styles[i]);
      }
    }
  }

  return glw_style_bind(w, NULL);
}
