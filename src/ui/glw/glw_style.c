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
#include "glw_style.h"
#include "glw_view.h"

static glw_class_t glw_style;

static void glw_style_binding_destroy(glw_style_binding_t *gsb, glw_root_t *gr);

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

  union {
    glw_attribute_t gsa_attribute;
    char *gsa_unresolved_attribute;
  };
  gsa_type_t gsa_type;
  char gsa_unresolved;
  char gsa_local; // Not inherited

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

  rstr_t *gs_file;
  int gs_line;

  int gs_id;

  glw_style_t *gs_ancestor;
  token_t *gs_rpns;
  rstr_t *gs_name;
  struct glw_style_binding_list gs_bindings;
  struct glw_style_attribute_list gs_attributes;

  LIST_ENTRY(glw_style) gs_link;

  rstr_t *gs_source;
  int gs_source_flags;

  int gs_refcount;

  uint32_t gs_flags2_set;
  uint32_t gs_flags2_clr;
  uint32_t gs_flags2_local;

  uint32_t gs_image_flags_set;
  uint32_t gs_image_flags_clr;
  uint32_t gs_image_flags_local;

  uint32_t gs_text_flags_set;
  uint32_t gs_text_flags_clr;
  uint32_t gs_text_flags_local;

  uint32_t gs_flags;
  uint32_t gs_flags_local;
#define GS_SET_FOCUS_WEIGHT 0x1
#define GS_SET_ALPHA        0x2
#define GS_SET_BLUR         0x4
#define GS_SET_WEIGHT       0x8
#define GS_SET_WIDTH        0x10
#define GS_SET_HEIGHT       0x20
#define GS_SET_SOURCE       0x40
#define GS_SET_ALIGN        0x80
#define GS_SET_HIDDEN       0x100

  uint8_t gs_hidden;
};

static void gs_mod_flags2(struct glw *w, int set, int clr,
                          glw_style_t *origin);


/**
 *
 */
static glw_style_t *
glw_style_find(glw_t *w, const char *name)
{
  glw_styleset_t *gss = w->glw_styles;

  if(name != NULL && gss != NULL)
    for(int i = 0; i < gss->gss_numstyles; i++)
      if(!strcmp(name, rstr_get(gss->gss_styles[i]->gs_name)))
        return gss->gss_styles[i];

  return NULL;
}


/**
 * If widget does not respond to float callback, try with int callback
 */
static int
set_float_on_widget(glw_t *w, glw_style_attribute_t *gsa,
                    glw_style_t *o)
{
  const glw_class_t *gc = w->glw_class;
  int x;

  if(gsa->gsa_unresolved) {

    x = gc->gc_set_float_unresolved ?
      gc->gc_set_float_unresolved(w, gsa->gsa_unresolved_attribute,
                                  gsa->f, o) : -1;
    if(x == -1)
      x = gc->gc_set_int_unresolved ?
        gc->gc_set_int_unresolved(w, gsa->gsa_unresolved_attribute,
                                  gsa->f, o) : -1;

  } else {

    x = gc->gc_set_float ? gc->gc_set_float(w, gsa->gsa_attribute,
                                            gsa->f, o) : -1;
    if(x == -1)
      x = gc->gc_set_int ? gc->gc_set_int(w, gsa->gsa_attribute,
                                          gsa->f, o) : -1;
  }

  return x;
}


/**
 * If widget does not respond to int callback, try with float callback
 */
static int
set_int_on_widget(glw_t *w, glw_style_attribute_t *gsa,
                  glw_style_t *o)
{
  const glw_class_t *gc = w->glw_class;
  int x;

  if(gsa->gsa_unresolved) {

    x = gc->gc_set_int_unresolved ?
      gc->gc_set_int_unresolved(w, gsa->gsa_unresolved_attribute,
                                gsa->i32, o) : -1;
    if(x == -1)
      x = gc->gc_set_float_unresolved ?
        gc->gc_set_float_unresolved(w, gsa->gsa_unresolved_attribute,
                                    gsa->i32, o) : -1;

  } else {
    x = gc->gc_set_int ? gc->gc_set_int(w, gsa->gsa_attribute,
                                        gsa->i32, o) : -1;
    if(x == -1)
      x = gc->gc_set_float ? gc->gc_set_float(w, gsa->gsa_attribute,
                                              gsa->i32, o) : -1;
  }

  return x;
}



/**
 *
 */
static int
set_rstr_on_widget(glw_t *w, glw_style_attribute_t *gsa,
                   glw_style_t *o)
{
  const glw_class_t *gc = w->glw_class;
  int x;

  if(gsa->gsa_unresolved) {

    x = gc->gc_set_rstr_unresolved ?
      gc->gc_set_rstr_unresolved(w, gsa->gsa_unresolved_attribute,
                                 gsa->rstr, o) : -1;

  } else {

    x = gc->gc_set_rstr ? gc->gc_set_rstr(w, gsa->gsa_attribute,
                                          gsa->rstr, o) : -1;
  }

  return x;
}


/**
 *
 */
static void
set_flags2_on_widget(glw_t *w, int set, int clr, glw_style_t *origin)
{
  const glw_class_t *gc = w->glw_class;

  if(gc == &glw_style) {
    gs_mod_flags2(w, set, clr, origin);
    return;
  }

  glw_mod_flags2(w, set, clr);
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
glw_style_attribute_create(glw_style_t *gs, char unresolved)
{
  glw_style_attribute_t *gsa = malloc(sizeof(glw_style_attribute_t));
  gsa->gsa_unresolved = unresolved;
  gsa->gsa_local = 0;
  gsa->gsa_type = GSA_NONE;
  LIST_INSERT_HEAD(&gs->gs_attributes, gsa, gsa_link);
  return gsa;
}


/**
 *
 */
static glw_style_attribute_t *
glw_style_find_attribute(glw_style_t *gs, glw_attribute_t attrib, int *created)
{
  glw_style_attribute_t *gsa;
  LIST_FOREACH(gsa, &gs->gs_attributes, gsa_link) {
    if(!gsa->gsa_unresolved && gsa->gsa_attribute == attrib) {
      glw_style_attribute_clean(gsa);
      *created = 0;
      return gsa;
    }
  }

  gsa = glw_style_attribute_create(gs, 0);

  gsa->gsa_attribute = attrib;
  *created = 1;
  return gsa;
}


/**
 *
 */
static glw_style_attribute_t *
glw_style_find_unresolved_attribute(glw_style_t *gs, const char *attrib,
                                    int *created)
{
  glw_style_attribute_t *gsa;
  LIST_FOREACH(gsa, &gs->gs_attributes, gsa_link) {
    if(gsa->gsa_unresolved && !strcmp(gsa->gsa_unresolved_attribute, attrib)) {
      glw_style_attribute_clean(gsa);
      *created = 0;
      return gsa;
    }
  }

  gsa = glw_style_attribute_create(gs, 1);
  gsa->gsa_unresolved_attribute = strdup(attrib);
  *created = 1;
  return gsa;
}



/**
 *
 */
static glw_style_t * attribute_unused_result
glw_style_retain(glw_style_t *gs)
{
  if(gs != NULL)
    gs->gs_refcount++;
  return gs;
}


/**
 *
 */
static void
glw_style_release(glw_style_t *gs)
{
  if(gs == NULL)
    return;

  glw_style_binding_t *gsb;
  glw_style_attribute_t *gsa;
  glw_t *w = &gs->w;
  gs->gs_refcount--;
  if(gs->gs_refcount)
    return;

  while((gsb = LIST_FIRST(&w->glw_style_bindings)) != NULL)
    glw_style_binding_destroy(gsb, w->glw_root);

  glw_style_release(gs->gs_ancestor);
  glw_styleset_release(w->glw_styles);

  LIST_REMOVE(gs, gs_link);

  assert(LIST_FIRST(&gs->gs_bindings) == NULL);
  glw_prop_subscription_destroy_list(w->glw_root, &w->glw_prop_subscriptions);
  glw_view_free_chain(w->glw_root, w->glw_dynamic_expressions);


  while((gsa = LIST_FIRST(&gs->gs_attributes)) != NULL) {
    glw_style_attribute_clean(gsa);
    LIST_REMOVE(gsa, gsa_link);
    if(gsa->gsa_unresolved)
      free(gsa->gsa_unresolved_attribute);
    free(gsa);
  }

  glw_view_free_chain(w->glw_root, gs->gs_rpns);

  rstr_release(gs->gs_name);
  rstr_release(gs->gs_source);
  rstr_release(gs->gs_file);
  free(gs);
}


/**
 *
 */
static void
glw_style_binding_destroy(glw_style_binding_t *gsb, glw_root_t *gr)
{
  if(gsb->gsb_style != NULL) {
    LIST_REMOVE(gsb, gsb_style_link);
    glw_style_release(gsb->gsb_style);
  }
  LIST_REMOVE(gsb, gsb_widget_link);
  pool_put(gr->gr_style_binding_pool, gsb);
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
gsa_check_blocking(glw_style_attribute_t *gsa, glw_style_t *origin)
{
  // If setted attribute is inherited and we have a local conf, bail out
  if(origin != NULL && gsa->gsa_local)
    return 1;

  if(origin == NULL)
    gsa->gsa_local = 1;

  return 0;
}


/**
 *
 */
static int
gs_apply_float3(glw_style_t *gs, glw_style_attribute_t *gsa)
{
  int r = 0;
  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    assert(gsa->gsa_unresolved == 0);
    setr(w->glw_class->gc_set_float3(w, gsa->gsa_attribute, gsa->fvec, gs), &r);
  }
  return r;
}


/**
 *
 */
static int
gs_set_float3(struct glw *w, glw_attribute_t a, const float *vector,
              glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;
  int created;
  glw_style_attribute_t *gsa = glw_style_find_attribute(gs, a, &created);

  if(!created && !memcmp(gsa->fvec, vector, sizeof(float) * 3))
    return 0;

  if(gsa_check_blocking(gsa, origin))
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
  int r = 0;
  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    assert(gsa->gsa_unresolved == 0);
    if(w->glw_class->gc_set_int16_4 != NULL)
      setr(w->glw_class->gc_set_int16_4(w, gsa->gsa_attribute,
                                        gsa->i16vec, gs), &r);
  }
  return r;
}


/**
 *
 */
static int
gs_set_int16_4(struct glw *w, glw_attribute_t a, const int16_t *vector,
               glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;
  int created;
  glw_style_attribute_t *gsa = glw_style_find_attribute(gs, a, &created);

  if(!created &&
     !memcmp(gsa->i16vec, vector, sizeof(int16_t) * 4))
    return 0;

  if(gsa_check_blocking(gsa, origin))
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
  int r = 0;
  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    setr(set_rstr_on_widget(w, gsa, gs), &r);
  }
  return r;
}


/**
 *
 */
static int
gs_set_rstr(struct glw *w, glw_attribute_t a, rstr_t *rstr,
            glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;
  int created;
  glw_style_attribute_t *gsa = glw_style_find_attribute(gs, a, &created);

  if(!created && rstr_eq(rstr, gsa->rstr))
    return 0;

  if(gsa_check_blocking(gsa, origin))
    return 0;

  gsa->gsa_type = GSA_RSTR;
  gsa->rstr = rstr_dup(rstr);
  return gs_apply_rstr(gs, gsa);
}


/**
 *
 */
static int
gs_set_string_unresolved(struct glw *w, const char *a, rstr_t *value,
                         glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;
  int created;
  glw_style_attribute_t *gsa =
    glw_style_find_unresolved_attribute(gs, a, &created);

  if(!created && gsa->gsa_type == GSA_RSTR && rstr_eq(value, gsa->rstr))
    return 0;

  if(gsa_check_blocking(gsa, origin))
    return 0;

  gsa->gsa_type = GSA_RSTR;
  gsa->rstr = rstr_dup(value);
  return gs_apply_rstr(gs, gsa);
}


/**
 *
 */
static int
gs_apply_int(glw_style_t *gs, glw_style_attribute_t *gsa)
{
  int r = 0;
  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    setr(set_int_on_widget(w, gsa, gs), &r);
  }
  return r;
}


/**
 *
 */
static int
gs_set_int(struct glw *w, glw_attribute_t a, int i32,
           glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;
  int created;
  glw_style_attribute_t *gsa = glw_style_find_attribute(gs, a, &created);

  if(!created && i32 == gsa->i32)
    return 0;

  if(gsa_check_blocking(gsa, origin))
    return 0;

  gsa->gsa_type = GSA_INT;
  gsa->i32 = i32;
  return gs_apply_int(gs, gsa);
}


/**
 *
 */
static int
gs_set_int_unresolved(struct glw *w, const char *a, int i32,
                      glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;
  int created;
  glw_style_attribute_t *gsa =
    glw_style_find_unresolved_attribute(gs, a, &created);

  if(!created && i32 == gsa->i32)
    return 0;

  if(gsa_check_blocking(gsa, origin))
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
  int r = 0;
  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    setr(set_float_on_widget(w, gsa, gs), &r);
  }
  return r;
}


/**
 *
 */
static int
gs_set_float(struct glw *w, glw_attribute_t a, float f, glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;
  int created;
  glw_style_attribute_t *gsa = glw_style_find_attribute(gs, a, &created);

  if(!created && f == gsa->f)
    return 0;

  if(gsa_check_blocking(gsa, origin))
    return 0;

  gsa->gsa_type = GSA_FLOAT;
  gsa->f = f;
  return gs_apply_float(gs, gsa);
}


/**
 *
 */
static int
gs_set_float_unresolved(struct glw *w, const char *a, float f,
                        glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;
  int created;
  glw_style_attribute_t *gsa =
    glw_style_find_unresolved_attribute(gs, a, &created);

  if(!created && f == gsa->f)
    return 0;

  if(gsa_check_blocking(gsa, origin))
    return 0;

  gsa->gsa_type = GSA_FLOAT;
  gsa->f = f;
  return gs_apply_float(gs, gsa);
}


/**
 *
 */
static void
gs_mod_flags2(struct glw *w, int set, int clr, glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;

  if(origin == NULL) {
    gs->gs_flags2_local |= (set | clr);
  } else {
    set &= ~gs->gs_flags2_local;
    clr &= ~gs->gs_flags2_local;
  }

  gs->gs_flags2_set |= set;
  gs->gs_flags2_clr |= clr;

  gs->gs_flags2_clr &= ~set;
  gs->gs_flags2_set &= ~clr;

  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    set_flags2_on_widget(w, set, clr, gs);
  }
}


/**
 *
 */
static void
gs_mod_text_flags(struct glw *w, int set, int clr, glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;

  if(origin == NULL) {
    gs->gs_text_flags_local |= (set | clr);
  } else {
    set &= ~gs->gs_text_flags_local;
    clr &= ~gs->gs_text_flags_local;
  }

  gs->gs_text_flags_set |= set;
  gs->gs_text_flags_clr |= clr;

  gs->gs_text_flags_clr &= ~set;
  gs->gs_text_flags_set &= ~clr;

  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    if(w->glw_class->gc_mod_text_flags != NULL)
      w->glw_class->gc_mod_text_flags(w, set, clr, gs);
  }
}


/**
 *
 */
static void
gs_mod_image_flags(struct glw *w, int set, int clr, glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;

  if(origin == NULL) {
    gs->gs_image_flags_local |= (set | clr);
  } else {
    set &= ~gs->gs_image_flags_local;
    clr &= ~gs->gs_image_flags_local;
  }

  gs->gs_image_flags_set |= set;
  gs->gs_image_flags_clr |= clr;

  gs->gs_image_flags_clr &= ~set;
  gs->gs_image_flags_set &= ~clr;

  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    if(w->glw_class->gc_mod_image_flags != NULL)
      w->glw_class->gc_mod_image_flags(w, set, clr, gs);
  }
}


/**
 *
 */
static int
check_set_flags(glw_style_t *gs, int flag, glw_style_t *origin)
{
  if(origin != NULL) {
    if(gs->gs_flags_local & flag)
      return 1;
  } else {
    gs->gs_flags_local |= flag;
  }

  gs->gs_flags |= flag;
  return 0;
}


/**
 *
 */
static void
gs_set_focus_weight(struct glw *w, float v, glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;

  if(check_set_flags(gs, GS_SET_FOCUS_WEIGHT, origin))
    return;

  w->glw_focus_weight = v;

  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    glw_set_focus_weight(w, v, gs);
  }
}


/**
 *
 */
static void
gs_set_alpha(struct glw *w, float v, glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;

  if(check_set_flags(gs, GS_SET_ALPHA, origin))
    return;

  w->glw_alpha = v;

  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    glw_set_alpha(w, v, gs);
  }
}


/**
 *
 */
static void
gs_set_blur(struct glw *w, float v, glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;

  if(check_set_flags(gs, GS_SET_BLUR, origin))
    return;

  w->glw_sharpness = v; // We borrow this even though it's not sharpness

  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    glw_set_blur(w, v, gs);
  }
}


/**
 *
 */
static void
gs_set_weight(struct glw *w, float v, glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;

  if(check_set_flags(gs, GS_SET_WEIGHT, origin))
    return;

  w->glw_req_weight = v;

  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    glw_set_weight(w, v, gs);
  }
}


/**
 *
 */
static void
gs_set_width(struct glw *w, int v, glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;

  if(check_set_flags(gs, GS_SET_WIDTH, origin))
    return;

  w->glw_req_size_x = v;

  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    glw_set_width(w, v, gs);
  }
}


/**
 *
 */
static void
gs_set_height(struct glw *w, int v, glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;

  if(check_set_flags(gs, GS_SET_HEIGHT, origin))
    return;

  w->glw_req_size_y = v;

  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    glw_set_height(w, v, gs);
  }
}


/**
 *
 */
static void
gs_set_align(struct glw *w, int v, glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;

  if(check_set_flags(gs, GS_SET_ALIGN, origin))
    return;

  w->glw_alignment = v;

  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    glw_set_align(w, v, gs);
  }
}


/**
 *
 */
static void
gs_set_hidden(struct glw *w, int v, glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;

  if(check_set_flags(gs, GS_SET_HIDDEN, origin))
    return;

  gs->gs_hidden = v;

  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    glw_set_hidden(w, v, gs);
  }
}


/**
 *
 */
static void
gs_set_source(struct glw *w, rstr_t *r, int flags, glw_style_t *origin)
{
  glw_style_t *gs = (glw_style_t *)w;

  if(check_set_flags(gs, GS_SET_SOURCE, origin))
    return;

  rstr_set(&gs->gs_source, r);
  gs->gs_source_flags = flags;

  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    if(w->glw_class->gc_set_source != NULL)
      w->glw_class->gc_set_source(w, r, flags, gs);
  }
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
  .gc_set_source        = gs_set_source,
  .gc_set_align         = gs_set_align,
  .gc_set_hidden        = gs_set_hidden,

  .gc_set_float_unresolved  = gs_set_float_unresolved,
  .gc_set_int_unresolved    = gs_set_int_unresolved,
  .gc_set_rstr_unresolved = gs_set_string_unresolved,

};


/**
 *
 */
glw_style_t *
glw_style_create(glw_t *parent, rstr_t *name, rstr_t *file, int line,
                 int inherit)
{
  glw_style_t *ancestor =
    inherit ? glw_style_find(parent, rstr_get(name)) : NULL;

  glw_root_t *gr = parent->glw_root;
  glw_style_t *gs = calloc(1, sizeof(glw_style_t));
  LIST_INSERT_HEAD(&gr->gr_all_styles, gs, gs_link);
  gs->w.glw_class = &glw_style;
  gs->w.glw_root = gr;
  gs->w.glw_styles = glw_styleset_retain(parent->glw_styles);

  gs->gs_id = ++gr->gr_style_tally;
  gs->gs_name = rstr_dup(name);
  gs->gs_file = rstr_dup(file);
  gs->gs_line = line;
  gs->gs_ancestor = glw_style_retain(ancestor);

  glw_style_bind_ancestor(gs, ancestor);

  return gs;
}


/**
 *
 */
void
glw_style_attach_rpns(glw_style_t *gs, struct token *t)
{
  assert(gs->gs_rpns == NULL);
  gs->gs_rpns = t;

  for(;t != NULL; t = t->next) {
    assert(t->type == TOKEN_RPN);
    t->t_rpn_origin = gs->gs_id;
  }
}


/**
 *
 */
void
glw_styleset_release(glw_styleset_t *gss)
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
glw_styleset_t *
glw_styleset_add(glw_styleset_t *gss, glw_style_t *gs)
{
  int i, items;
  glw_styleset_t *copy;

  if(gss == NULL) {
    i = 0;
    items = 1;
  } else {

    for(i = 0; i < gss->gss_numstyles; i++) {
      if(!strcmp(rstr_get(gs->gs_name),
                 rstr_get(gss->gss_styles[i]->gs_name))) {
        break;
      }
    }
    items = i == gss->gss_numstyles ? i + 1 : gss->gss_numstyles;
  }

  copy = malloc(sizeof(glw_styleset_t) + sizeof(glw_style_t *) * items);
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

static void glw_style_remove_styling_rpns_on_widget(glw_t *w, int origin);


/**
 *
 */
static void
glw_style_remove_styling_rpns(glw_root_t *gr, token_t **p, int origin)
{
  token_t *t;

  while((t = *p) != NULL) {

    if(t->t_rpn_origin == origin) {
      assert(t->type == TOKEN_RPN);
      *p = t->next;
      t->next = NULL;
      glw_view_free_chain(gr, t);
    } else {
      p = &t->next;
    }
  }
}



/**
 *
 */
static void
glw_style_remove_styling_rpns_on_style(glw_style_t *gs, int origin)
{
  glw_root_t *gr = gs->w.glw_root;

  glw_style_remove_styling_rpns(gr, &gs->gs_rpns, origin);

  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &gs->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    glw_style_remove_styling_rpns_on_widget(w, origin);
  }
}


/**
 *
 */
static void
glw_style_remove_styling_rpns_on_widget(glw_t *w, int origin)
{
  if(w->glw_class == &glw_style) {
    glw_style_remove_styling_rpns_on_style((glw_style_t *)w, origin);
  } else {
    glw_style_remove_styling_rpns(w->glw_root, &w->glw_dynamic_expressions,
                                  origin);
  }
}



/**
 *
 */
static void
glw_style_insert_styling_rpns_on_widget(glw_t *w, token_t *rpns,
                                        glw_view_eval_context_t *ec0)
{
  token_t *rpn = glw_view_clone_chain(w->glw_root, rpns, NULL);
  int copy;

  glw_view_eval_context_t ec = *ec0;
  ec.w = w;

  while(rpn) {
    token_t *t = rpn;
    rpn = t->next;
    assert(t->type == TOKEN_RPN);
    assert(t->t_rpn_origin != 0);
    glw_view_eval_rpn(t, &ec, &copy);

    if(copy) {
      t->next = w->glw_dynamic_expressions;
      w->glw_dynamic_expressions = t;
      t->t_dynamic_eval = copy;
      w->glw_dynamic_eval |= copy;
    } else {
      t->next = NULL;
      glw_view_free_chain(w->glw_root, t);
    }
  }
}


/**
 *
 */
static void
glw_style_insert_styling_rpns_on_style(glw_style_t *dst, glw_style_t *src,
                                       glw_view_eval_context_t *ec)
{
  if(src->gs_rpns == NULL)
    return;

  glw_root_t *gr = src->w.glw_root;

  token_t *last;
  token_t *first = glw_view_clone_chain(gr, src->gs_rpns, &last);

  last->next = dst->gs_rpns;
  dst->gs_rpns = first;


  glw_style_binding_t *gsb;
  LIST_FOREACH(gsb, &dst->gs_bindings, gsb_style_link) {
    glw_t *w = gsb->gsb_widget;
    if(w->glw_class == &glw_style)
      glw_style_insert_styling_rpns_on_style((glw_style_t *)w, src, ec);
    else
      glw_style_insert_styling_rpns_on_widget(w, src->gs_rpns, ec);
  }
}


/**
 *
 */
static int
glw_style_insert(glw_t *w, glw_style_t *gs, glw_view_eval_context_t *ec)
{
  int r = 0;
  glw_style_attribute_t *gsa;
  glw_style_binding_t *gsb = pool_get(w->glw_root->gr_style_binding_pool);

  gsb->gsb_style = glw_style_retain(gs);
  gsb->gsb_widget = w;
  LIST_INSERT_HEAD(&w->glw_style_bindings, gsb, gsb_widget_link);
  LIST_INSERT_HEAD(&gs->gs_bindings, gsb, gsb_style_link);
  gsb->gsb_mark = 0;


  if(w->glw_class == &glw_style) {
    glw_style_t *dst = (glw_style_t *)w;
    glw_style_insert_styling_rpns_on_style(dst, gs, ec);
  } else {
    glw_style_insert_styling_rpns_on_widget(w, gs->gs_rpns, ec);
  }

  LIST_FOREACH(gsa, &gs->gs_attributes, gsa_link) {

    switch(gsa->gsa_type) {

    default:
      abort();
    case GSA_INT:
      setr(set_int_on_widget(w, gsa, gs), &r);
      break;
    case GSA_FLOAT:
      setr(set_float_on_widget(w, gsa, gs), &r);
      break;
    case GSA_RSTR:
      setr(set_rstr_on_widget(w, gsa, gs), &r);
      break;
    case GSA_FVEC3:
      if(w->glw_class->gc_set_float3 != NULL)
        setr(w->glw_class->gc_set_float3(w, gsa->gsa_attribute, gsa->fvec,
                                         gs), &r);
      break;
    case GSA_IVEC16_4:
      if(w->glw_class->gc_set_int16_4 != NULL)
        setr(w->glw_class->gc_set_int16_4(w, gsa->gsa_attribute,
                                          gsa->i16vec, gs), &r);
      break;
    }
  }

  if(gs->gs_flags2_set || gs->gs_flags2_clr) {
    set_flags2_on_widget(w, gs->gs_flags2_set, gs->gs_flags2_clr, gs);
    r = 1;
  }

  if(w->glw_class->gc_mod_image_flags &&
     (gs->gs_image_flags_set || gs->gs_image_flags_clr))
    w->glw_class->gc_mod_image_flags(w, gs->gs_image_flags_set,
                                     gs->gs_image_flags_clr, gs);

  if(w->glw_class->gc_mod_text_flags &&
     (gs->gs_text_flags_set || gs->gs_text_flags_clr))
    w->glw_class->gc_mod_text_flags(w, gs->gs_text_flags_set,
                                    gs->gs_text_flags_clr, gs);

  if(gs->gs_flags & GS_SET_FOCUS_WEIGHT)
    glw_set_focus_weight(w, gs->w.glw_focus_weight, gs);

  if(gs->gs_flags & GS_SET_ALPHA)
    glw_set_alpha(w, gs->w.glw_alpha, gs);

  if(gs->gs_flags & GS_SET_BLUR)
    glw_set_blur(w, gs->w.glw_sharpness, gs);

  if(gs->gs_flags & GS_SET_WEIGHT)
    glw_set_weight(w, gs->w.glw_req_weight, gs);

  if(gs->gs_flags & GS_SET_WIDTH)
    glw_set_width(w, gs->w.glw_req_size_x, gs);

  if(gs->gs_flags & GS_SET_HEIGHT)
    glw_set_height(w, gs->w.glw_req_size_y, gs);

  if(gs->gs_flags & GS_SET_ALIGN)
    glw_set_align(w, gs->w.glw_alignment, gs);

  if(gs->gs_flags & GS_SET_HIDDEN)
    glw_set_hidden(w, gs->gs_hidden, gs);

  if(gs->gs_flags & GS_SET_SOURCE)
    if(w->glw_class->gc_set_source != NULL)
      w->glw_class->gc_set_source(w, gs->gs_source, gs->gs_source_flags, gs);

  return r;
}


/**
 *
 */
static void
glw_style_bindings_sweep(glw_t *w, int all)
{
  glw_style_binding_t *gsb, *next;
  glw_root_t *gr = w->glw_root;

  for(gsb = LIST_FIRST(&w->glw_style_bindings); gsb != NULL; gsb = next) {
    next = LIST_NEXT(gsb, gsb_widget_link);

    if(all || gsb->gsb_mark) {

      glw_style_t *gs = gsb->gsb_style;

      LIST_REMOVE(gsb, gsb_style_link);

      do {
        glw_style_remove_styling_rpns_on_widget(w, gs->gs_id);
        gs = gs->gs_ancestor;
      } while(gs != NULL);

      glw_style_release(gsb->gsb_style);
      gsb->gsb_style = NULL;
      glw_style_binding_destroy(gsb, gr);
    }
  }
}


/**
 *
 */
int
glw_styleset_for_widget(glw_t *w, const char *name,
                        glw_view_eval_context_t *ec)
{
  glw_style_bindings_sweep(w, 1);

  glw_style_t *gs = glw_style_find(w, name);
  return gs != NULL ? glw_style_insert(w, gs, ec) : 0;
}


/**
 *
 */
int
glw_styleset_for_widget_multiple(glw_t *w, struct token *t,
                                 struct glw_view_eval_context *ec)
{
  glw_style_binding_t *gsb;
  int r = 0;

  LIST_FOREACH(gsb, &w->glw_style_bindings, gsb_widget_link)
    gsb->gsb_mark = 1;

  for(; t != NULL; t = t->next) {
    if(!(t->type == TOKEN_RSTRING || t->type == TOKEN_URI))
      continue;

    glw_style_t *gs = glw_style_find(w, rstr_get(t->t_rstring));
    if(gs == NULL)
      continue;

    LIST_FOREACH(gsb, &w->glw_style_bindings, gsb_widget_link)
      if(gsb->gsb_style == gs)
        break;

    if(gsb != NULL) {
      gsb->gsb_mark = 0;
      continue;
    }

    setr(glw_style_insert(w, gs, ec), &r);
  }
  glw_style_bindings_sweep(w, 0);
  return r;
}


/**
 *
 */
void
glw_style_bind_ancestor(glw_style_t *gs, glw_style_t *ancestor)
{
  if(ancestor != NULL)
    glw_style_insert(&gs->w, ancestor, NULL);
}


/**
 *
 */
void
glw_style_unbind_all(glw_t *w)
{
  glw_style_bindings_sweep(w, 1);
}



/**
 *
 */
void
glw_style_update_em(glw_root_t *gr)
{
  glw_style_t *gs;
  LIST_FOREACH(gs, &gr->gr_all_styles, gs_link)
    if(gs->w.glw_dynamic_eval & GLW_VIEW_EVAL_EM)
      glw_view_eval_dynamics(&gs->w, GLW_VIEW_EVAL_EM);


}


/**
 *
 */
void
glw_style_cleanup(glw_root_t *gr)
{
  glw_style_t *gs;
  LIST_FOREACH(gs, &gr->gr_all_styles, gs_link) {
    printf("Style %s %s:%d still in use ancestor:%p refcnt=%d\n",
           rstr_get(gs->gs_name),
           rstr_get(gs->gs_file),
           gs->gs_line,
           gs->gs_ancestor,
           gs->gs_refcount);
    printf("Bindings:%p\n", LIST_FIRST(&gs->gs_bindings));
  }
  assert(LIST_FIRST(&gr->gr_all_styles) == NULL);
}

