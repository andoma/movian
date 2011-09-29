/*
 *  GL Widgets, view loader, evaluator
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
#include <math.h>

#include "misc/strtab.h"
#include "misc/string.h"
#include "glw_view.h"
#include "glw.h"
#include "glw_event.h"
#include "backend/backend.h"
#include "misc/pixmap.h"
#include "settings.h"
#include "prop/prop_grouper.h"
#include "prop/prop_nodefilter.h"
#include "arch/arch.h"

LIST_HEAD(clone_list, clone);

static token_t t_zero = {
  .type = TOKEN_INT,
};

/**
 *
 */
typedef struct glw_prop_sub_pending {
  prop_t *gpsp_prop;
  TAILQ_ENTRY(glw_prop_sub_pending) gpsp_link;
} glw_prop_sub_pending_t;

TAILQ_HEAD(glw_prop_sub_pending_queue, glw_prop_sub_pending);


#define GPS_VALUE   2
#define GPS_CLONER  3
#define GPS_COUNTER 4

/**
 *
 */
typedef struct glw_prop_sub {
  LIST_ENTRY(glw_prop_sub) gps_link;
  glw_t *gps_widget;

  prop_sub_t *gps_sub;
  prop_t *gps_prop_view;

  token_t *gps_rpn;

  token_t *gps_token;


#ifdef GLW_VIEW_ERRORINFO
  rstr_t *gps_file;
  uint16_t gps_line;
#endif
  uint16_t gps_type;

} glw_prop_sub_t;


/**
 *
 */
typedef struct sub_cloner {
  glw_prop_sub_t sc_sub;

  token_t *sc_cloner_body;
  const glw_class_t *sc_cloner_class;
  
  struct glw_prop_sub_pending_queue sc_pending;
  prop_t *sc_pending_select;

  int sc_entries;

  prop_t *sc_originating_prop;

  prop_t *sc_view_prop;
  prop_t *sc_view_args;

  glw_t *sc_anchor;

  char sc_positions_valid;

  int sc_lowest_active;
  int sc_highest_active;

  char sc_have_more;

  struct clone_list sc_clones;

} sub_cloner_t;


/**
 *
 */
typedef struct sub_counter {
  glw_prop_sub_t sc_sub;
  int sc_entries;

} sub_counter_t;



/**
 *
 */
typedef struct clone {
  LIST_ENTRY(clone) c_link;
  sub_cloner_t *c_sc;
  glw_t *c_w;
  int c_pos;
  prop_t *c_prop;

  char c_evaluated;

  prop_t *c_clone_root;

} clone_t;


static int subscribe_prop(glw_view_eval_context_t *ec, struct token *self,
			  int type);

static void clone_free(clone_t *c);


/**
 *
 */
static void
cloner_cleanup(sub_cloner_t *sc)
{
  clone_t *c;

  while((c = LIST_FIRST(&sc->sc_clones)) != NULL) {
    prop_tag_clear(c->c_prop, sc);
    clone_free(c);
  }

  if(sc->sc_cloner_body != NULL)
    glw_view_free_chain(sc->sc_cloner_body);
}

/**
 *
 */
void
glw_prop_subscription_destroy_list(struct glw_prop_sub_list *l)
{
  glw_prop_sub_t *gps;
  sub_cloner_t *sc;

  while((gps = LIST_FIRST(l)) != NULL) {

    if(gps->gps_sub != NULL)
      prop_unsubscribe(gps->gps_sub);

    if(gps->gps_token != NULL)
      glw_view_token_free(gps->gps_token);

    LIST_REMOVE(gps, gps_link);

    switch(gps->gps_type) {
    case GPS_VALUE:
    case GPS_COUNTER:
      break;

    case GPS_CLONER:
      sc = (sub_cloner_t *)gps;
 
      cloner_cleanup(sc);

      if(sc->sc_originating_prop)
	prop_ref_dec(sc->sc_originating_prop);

      if(sc->sc_view_prop)
	prop_ref_dec(sc->sc_view_prop);

      if(sc->sc_view_args)
	prop_ref_dec(sc->sc_view_args);
      break;
    }
#ifdef GLW_VIEW_ERRORINFO
    rstr_release(gps->gps_file);
#endif
    prop_ref_dec(gps->gps_prop_view);
    free(gps);
  }
}


/**
 *
 */
void
glw_prop_subscription_suspend_list(struct glw_prop_sub_list *l)
{
  glw_prop_sub_t *gps;

  LIST_FOREACH(gps, l, gps_link) {
    if(gps->gps_sub != NULL) {
      prop_unsubscribe(gps->gps_sub);
      gps->gps_sub = NULL;
    }
  }
}



static void eval_dynamic(glw_t *w, token_t *rpn, struct glw_rctx *rc,
			 prop_t *view);

static int glw_view_eval_rpn0(token_t *t0, glw_view_eval_context_t *ec);

/**
 *
 */
static void
eval_push(glw_view_eval_context_t *ec, token_t *t)
{
  t->tmp = ec->stack;
  ec->stack = t;
}


/**
 *
 */
static token_t *
eval_pop(glw_view_eval_context_t *ec)
{
  token_t *r = ec->stack;
  if(r != NULL)
    ec->stack = r->tmp;
  return r;
}


/**
 *
 */
static token_t *
eval_alloc_sized(token_t *src, glw_view_eval_context_t *ec, 
		 token_type_t type, size_t size)
{
  token_t *r = calloc(1, size);

#ifdef GLW_VIEW_ERRORINFO
  if(src->file != NULL)
    r->file = rstr_dup(src->file);
  r->line = src->line;
#endif

  r->type = type;
  r->next = ec->alloc;
  ec->alloc = r;
  return r;
}

/**
 *
 */
static token_t *
eval_alloc(token_t *src, glw_view_eval_context_t *ec, token_type_t type)
{
  return eval_alloc_sized(src, ec, type, sizeof(token_t));
}


/**
 *
 */
static token_t *
token_resolve_ex(glw_view_eval_context_t *ec, token_t *t, int type)
{
  if(t == NULL) {
    glw_view_seterr(ec->ei, t, "Missing operand");
    return NULL;
  }

  if((t->type == TOKEN_PROPERTY_VALUE_NAME ||
      t->type == TOKEN_PROPERTY_REF) && subscribe_prop(ec, t, type))
    return NULL;
  
  if(t->type == TOKEN_PROPERTY_SUBSCRIPTION) {
    ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_PROP;
    t = t->propsubr->gps_token ?: eval_alloc(t, ec, TOKEN_VOID);
  }
  return t;
}


static token_t *
token_resolve(glw_view_eval_context_t *ec, token_t *t)
{
  return token_resolve_ex(ec, t, GPS_VALUE);
}

/**
 *
 */
static float eval_op_fadd(float a, float b) { return a + b; }
static float eval_op_fsub(float a, float b) { return a - b; }
static float eval_op_fmul(float a, float b) { return a * b; }
static float eval_op_fdiv(float a, float b) { return a / b; }
static float eval_op_fmod(float a, float b) { return (int)a % (int)b; }

static int eval_op_iadd(int a, int b) { return a + b; }
static int eval_op_isub(int a, int b) { return a - b; }
static int eval_op_imul(int a, int b) { return a * b; }
static int eval_op_imod(int a, int b) { return a % b; }

/**
 *
 */
static int
eval_op(glw_view_eval_context_t *ec, struct token *self)
{
  token_t *b = eval_pop(ec), *a = eval_pop(ec), *r;
  float (*f_fn)(float, float);
  int   (*i_fn)(int, int);
  int i, al, bl;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;
  if((b = token_resolve(ec, b)) == NULL)
    return -1;

  if(a->type == TOKEN_VOID)
    a = &t_zero;

  if(b->type == TOKEN_VOID)
    b = &t_zero;

  switch(self->type) {
  case TOKEN_ADD:
    if(token_is_string(a) && token_is_string(b)) {
      /* Concatenation of strings */
      al = strlen(rstr_get(a->t_rstring));
      bl = strlen(rstr_get(b->t_rstring));

      r = eval_alloc(self, ec, TOKEN_STRING);
      r->t_rstring = rstr_allocl(NULL, al + bl);
      memcpy(rstr_data(r->t_rstring),      rstr_get(a->t_rstring), al);
      memcpy(rstr_data(r->t_rstring) + al, rstr_get(b->t_rstring), bl);
      eval_push(ec, r);
      return 0;
    }

    f_fn = eval_op_fadd;
    i_fn = eval_op_iadd;
    break;
  case TOKEN_SUB:
    f_fn = eval_op_fsub;
    i_fn = eval_op_isub;
    break;
  case TOKEN_MULTIPLY:
    f_fn = eval_op_fmul;
    i_fn = eval_op_imul;
    break;
  case TOKEN_DIVIDE:
    f_fn = eval_op_fdiv;
    i_fn = NULL;
    break;
  case TOKEN_MODULO:
    f_fn = eval_op_fmod;
    i_fn = eval_op_imod;
    break;

  default:
    abort();
    break;
  }

  if(a->type == TOKEN_INT && b->type == TOKEN_INT) {

    if(i_fn == NULL) {
      r = eval_alloc(self, ec, TOKEN_FLOAT);
      r->t_float = f_fn(a->t_int, b->t_int);
    } else {
      r = eval_alloc(self, ec, TOKEN_INT);
      r->t_int = i_fn(a->t_int, b->t_int);
    }

  } else if(a->type == TOKEN_FLOAT && b->type == TOKEN_FLOAT) {
    r = eval_alloc(self, ec, TOKEN_FLOAT);
    r->t_float = f_fn(a->t_float, b->t_float);

  } else if(a->type == TOKEN_INT && b->type == TOKEN_FLOAT) {
    r = eval_alloc(self, ec, TOKEN_FLOAT);
    r->t_float = f_fn(a->t_int, b->t_float);

  } else if(a->type == TOKEN_FLOAT && b->type == TOKEN_INT) {
    r = eval_alloc(self, ec, TOKEN_FLOAT);
    r->t_float = f_fn(a->t_float, b->t_int);

  } else if(a->type == TOKEN_VECTOR_FLOAT && b->type == TOKEN_VECTOR_FLOAT) {
    if(a->t_elements != b->t_elements)
      return glw_view_seterr(ec->ei, self, 
			      "Arithmetic op is invalid for "
			      "non-equal sized vectors");
    
    r = eval_alloc_sized(self, ec, TOKEN_VECTOR_FLOAT, sizeof(token_t) + 
			 sizeof(a->u) * (a->t_elements - 1));

    r->t_elements = a->t_elements;
    for(i = 0; i < a->t_elements; i++)
      r->t_float_vector[i] = f_fn(a->t_float_vector[i], b->t_float_vector[i]);

  } else if(a->type == TOKEN_VECTOR_FLOAT && b->type == TOKEN_FLOAT) {

    r = eval_alloc_sized(self, ec, TOKEN_VECTOR_FLOAT, sizeof(token_t) + 
			 sizeof(a->u) * (a->t_elements - 1));

    r->t_elements = a->t_elements;
    for(i = 0; i < a->t_elements; i++)
      r->t_float_vector[i] = f_fn(a->t_float_vector[i], b->t_float);

  } else if(a->type == TOKEN_FLOAT && b->type == TOKEN_VECTOR_FLOAT) {

    r = eval_alloc_sized(self, ec, TOKEN_VECTOR_FLOAT, sizeof(token_t) + 
			 sizeof(b->u) * (b->t_elements - 1));

    r->t_elements = b->t_elements;
    for(i = 0; i < b->t_elements; i++)
      r->t_float_vector[i] = f_fn(a->t_float, b->t_float_vector[i]);
  } else {
    r = eval_alloc(self, ec, TOKEN_VOID);
  }

  eval_push(ec, r);
  return 0;
}

/**
 *
 */
static int
token2int(token_t *t)
{
  switch(t->type) {
  case TOKEN_INT:
    return t->t_int;
  case TOKEN_FLOAT:
    return t->t_float;
  default:
    return 0;
  }
}


/**
 *
 */
static float
token2float(token_t *t)
{
  switch(t->type) {
  case TOKEN_INT:
    return t->t_int;
  case TOKEN_FLOAT:
    return t->t_float;
  default:
    return 0;
  }
}


/**
 *
 */
static int
token2bool(token_t *t)
{
  switch(t->type) {
  case TOKEN_VOID:
    return 0;
  case TOKEN_STRING:
    return !!rstr_get(t->t_rstring)[0];
  case TOKEN_INT:
    return !!t->t_int;
  case TOKEN_FLOAT:
    return !!t->t_float;
  case TOKEN_IDENTIFIER:
    return !strcmp(rstr_get(t->t_rstring), "true");
  default:
    return 1;
  }
}

static int eval_op_xor(int a, int b) { return a ^ b; }
static int eval_op_or (int a, int b) { return a | b; }
static int eval_op_and(int a, int b) { return a & b; }

/**
 *
 */
static int
eval_bool_op(glw_view_eval_context_t *ec, struct token *self)
{
  token_t *b = eval_pop(ec), *a = eval_pop(ec), *r;
  int   (*fn)(int, int);
  int aa, bb;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;
  if((b = token_resolve(ec, b)) == NULL)
    return -1;

  aa = token2bool(a);
  bb = token2bool(b);

  switch(self->type) {
  case TOKEN_BOOLEAN_XOR:
    fn = eval_op_xor;
    break;
  case TOKEN_BOOLEAN_OR:
    fn = eval_op_or;
    break;
  case TOKEN_BOOLEAN_AND:
    fn = eval_op_and;
    break;

  default:
    abort();
    break;
  }

  r = eval_alloc(self, ec, TOKEN_INT);
  r->t_int = fn(aa, bb);
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static int
eval_bool_not(glw_view_eval_context_t *ec, struct token *self)
{
  token_t *a = eval_pop(ec), *r;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;

  r = eval_alloc(self, ec, TOKEN_INT);
  r->t_int = !token2bool(a);
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static int
eval_eq(glw_view_eval_context_t *ec, struct token *self, int neq)
{
  token_t *b = eval_pop(ec), *a = eval_pop(ec), *r;
  int rr;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;
  if((b = token_resolve(ec, b)) == NULL)
    return -1;

  if(token_is_string(a) && token_is_string(b)) {
    rr = !strcmp(rstr_get(a->t_rstring), rstr_get(b->t_rstring));
  } else if(a->type != b->type) {
    rr = 0;
  } else {

    switch(a->type) {
    case TOKEN_INT:
      rr = a->t_int == b->t_int;
      break;
    case TOKEN_FLOAT:
      rr = a->t_float == b->t_float;
      break;
    default:
      rr = 0;
    }
  }

  r = eval_alloc(self, ec, TOKEN_INT);
  r->t_int = rr ^ neq;
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static int
eval_lt(glw_view_eval_context_t *ec, struct token *self, int gt)
{
  token_t *b = eval_pop(ec), *a = eval_pop(ec), *r;
  int rr;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;
  if((b = token_resolve(ec, b)) == NULL)
    return -1;

  if(token_is_string(a) && token_is_string(b)) {
    rr = 0;
  } else if(a->type != b->type) {
    rr = 0;
  } else {

    switch(a->type) {
    case TOKEN_INT:
      rr = a->t_int < b->t_int;
      break;
    case TOKEN_FLOAT:
      rr = a->t_float < b->t_float;
      break;
    default:
      rr = 0;
    }
  }

  r = eval_alloc(self, ec, TOKEN_INT);
  r->t_int = rr ^ gt;
  eval_push(ec, r);
  return 0;
}


/**
 * Returns the second argument if the first is void, otherwise returns
 * the first arg
 */
static int 
eval_null_coalesce(glw_view_eval_context_t *ec, struct token *self)
{
  token_t *b = eval_pop(ec), *a = eval_pop(ec);
 
  if((a = token_resolve(ec, a)) == NULL)
    return -1;
  if((b = token_resolve(ec, b)) == NULL)
    return -1;

  if(a->type == TOKEN_VOID) {
    eval_push(ec, b);
  } else {
    eval_push(ec, a);
  }
  return 0;
}


/**
 *
 */
static int
resolve_property_name(glw_view_eval_context_t *ec, token_t *a, int follow_links)
{
  token_t *t;
  const char *pname[16];
  prop_t *p, *ui;
  int i;
  
  for(i = 0, t = a; t != NULL && i < 15; t = t->child)
    pname[i++]  = rstr_get(t->t_rstring);
  pname[i] = NULL;
  
  ui = ec->w ? ec->w->glw_root->gr_uii.uii_prop : NULL;
  p = prop_get_by_name(pname, follow_links,
		       PROP_TAG_NAMED_ROOT, ec->prop, "self",
		       PROP_TAG_NAMED_ROOT, ec->prop_parent, "parent",
		       PROP_TAG_NAMED_ROOT, ec->prop_viewx, "view",
		       PROP_TAG_NAMED_ROOT, ec->prop_clone, "clone",
		       PROP_TAG_NAMED_ROOT, ec->prop_args, "args",
		       PROP_TAG_ROOT, ui,
		       NULL);

  if(p == NULL)
    return glw_view_seterr(ec->ei, a, "Unable to resolve property %s",
			   pname[i-1]);
  
  /* Transform TOKEN_PROPERTY_NAME -> TOKEN_PROPERTY */
  
  glw_view_free_chain(a->child);
  a->child = NULL;
  rstr_release(a->t_rstring);
  a->type = TOKEN_PROPERTY_REF;
  a->t_prop = p;
  return 0;
}


/**
 *
 */
static token_t *
resolve_property_name2(glw_view_eval_context_t *ec, token_t *t)
{
  switch(t->type) {
  case TOKEN_PROPERTY_VALUE_NAME:
    if(resolve_property_name(ec, t, 1))
      return NULL;
    /* FALLTHRU */
  case TOKEN_PROPERTY_REF:
    break;
  default:
    glw_view_seterr(ec->ei, t, "Argument is not a property");
    return NULL;
  }
  return t;
}

/**
 *
 */
static int
set_prop_from_token(prop_t *p, token_t *t)
{
  switch(t->type) {
  case TOKEN_VOID:
    prop_set_void(p);
    break;

  case TOKEN_STRING:
    prop_set_rstring(p, t->t_rstring);
    break;

  case TOKEN_LINK:
    prop_set_link(p, rstr_get(t->t_link_rtitle), rstr_get(t->t_link_rurl));
    break;

  case TOKEN_INT:
    prop_set_int(p, t->t_int);
    break;

  case TOKEN_FLOAT:
    prop_set_float(p, t->t_float);
    break;

  case TOKEN_PROPERTY_REF:
    prop_link(t->t_prop, p);
    break;

  default:
    return -1;
  }
  return 0;
}


/**
 *
 */
static int
eval_assign(glw_view_eval_context_t *ec, struct token *self, int conditional)
{
  token_t *b = eval_pop(ec), *a = eval_pop(ec);
  int r = 0;

  /* Catch some special rvalues here */

  if(b->type == TOKEN_PROPERTY_VALUE_NAME && 
     !strcmp(rstr_get(b->t_rstring), "event")) {
    /* Assignment from $event, if our eval context has an event use it */
    if(ec->event == NULL || ec->event->e_type_x != EVENT_KEYDESC)
      return 0;
    b = eval_alloc(self, ec, TOKEN_STRING);
    b->t_rstring = rstr_alloc(ec->event->e_payload);
  } else if(b->type == TOKEN_BLOCK) {
    glw_view_eval_context_t n;

    memset(&n, 0, sizeof(n));
    n.w = ec->w;
    n.prop = ec->prop;
    n.prop_parent = ec->prop_parent;
    n.prop_viewx = ec->prop_viewx;
    n.prop_clone = ec->prop_clone;
    n.prop_args = ec->prop_args;
    n.ei = ec->ei;
    n.gr = ec->gr;
    n.rc = ec->rc;
    n.sublist = ec->sublist;

    n.tgtprop = prop_create_root(NULL);

    if(glw_view_eval_block(b, &n))
      return -1;

    b = eval_alloc(b, ec, TOKEN_PROPERTY_OWNER);
    b->t_prop = n.tgtprop;

  } else if((b = token_resolve(ec, b)) == NULL) {
    return -1;
  }

  if(conditional && b->type == TOKEN_VOID) {
    eval_push(ec, b);
    return 0;
  }

  switch(a->type) {
  case TOKEN_IDENTIFIER:
    if(ec->tgtprop == NULL)
      return glw_view_seterr(ec->ei, self, "Invalid assignment outside block");

    if(set_prop_from_token(prop_create(ec->tgtprop, 
				       rstr_get(a->t_rstring)), b))
      return glw_view_seterr(ec->ei, self, 
			     "Unable to assign %s to block property",
			     token2name(b));
    break;

  case TOKEN_OBJECT_ATTRIBUTE:
    r = a->t_attrib->set(ec, a->t_attrib, b);
    break;

  case TOKEN_PROPERTY_VALUE_NAME:
    if(resolve_property_name(ec, a, 1))
      return -1;
    if(0)
  case TOKEN_PROPERTY_CANONICAL_NAME:
    if(resolve_property_name(ec, a, 0))
      return -1;

  case TOKEN_PROPERTY_REF:
    
    switch(b->type) {
    case TOKEN_STRING:
      prop_set_rstring(a->t_prop, b->t_rstring);
      break;
    case TOKEN_LINK:
      prop_set_link(a->t_prop, rstr_get(b->t_link_rtitle),
		    rstr_get(b->t_link_rurl));
      break;
    case TOKEN_INT:
      prop_set_int(a->t_prop, b->t_int);
      break;
    case TOKEN_FLOAT:
      prop_set_float(a->t_prop, b->t_float);
      break;
    case TOKEN_PROPERTY_REF:
      if(b->t_prop != a->t_prop)
	prop_link(b->t_prop, a->t_prop);
      break;
    default:
      prop_set_void(a->t_prop);
      break;
    }
    r = 0;
    break;

  default:
    return glw_view_seterr(ec->ei, self, "Invalid assignment %s = %s",
			   token2name(a), token2name(b));
  }

  eval_push(ec, b);
  return r;
}

/**
 *
 */
static int
eval_dynamic_every_frame_sig(glw_t *w, void *opaque, 
			     glw_signal_t signal, void *extra)
{
  if(signal == GLW_SIGNAL_LAYOUT)
    eval_dynamic(w, opaque, extra, NULL);
  return 0;
}

/**
 *
 */
static int
eval_dynamic_focused_child_change_sig(glw_t *w, void *opaque, 
				      glw_signal_t signal, void *extra)
{
  if(signal == GLW_SIGNAL_FOCUS_CHILD_INTERACTIVE ||
     signal == GLW_SIGNAL_FOCUS_CHILD_AUTOMATIC)
    eval_dynamic(w, opaque, NULL, NULL);
  return 0;
}


/**
 *
 */
static int
eval_dynamic_fhp_change_sig(glw_t *w, void *opaque, 
			    glw_signal_t signal, void *extra)
{
  if(signal == GLW_SIGNAL_FHP_PATH_CHANGED)
    eval_dynamic(w, opaque, NULL, NULL);
  return 0;
}


/**
 *
 */
static int
eval_dynamic_widget_meta_sig(glw_t *w, void *opaque, 
			     glw_signal_t signal, void *extra)
{
  if(signal == GLW_SIGNAL_ACTIVE || signal == GLW_SIGNAL_INACTIVE ||
     signal == GLW_SIGNAL_CAN_SCROLL_CHANGED ||
     signal == GLW_SIGNAL_FULLWINDOW_CONSTRAINT_CHANGED ||
     signal == GLW_SIGNAL_READY || signal == GLW_SIGNAL_FOCUS_DISTANCE_CHANGED)
    eval_dynamic(w, opaque, NULL, NULL);
  return 0;
}

/**
 *
 */
static void
eval_dynamic(glw_t *w, token_t *rpn, struct glw_rctx *rc, prop_t *view)
{
  glw_view_eval_context_t ec;

  memset(&ec, 0, sizeof(ec));
  ec.w = w;
  ec.gr = w->glw_root;
  ec.rc = rc;
  ec.prop_viewx = view;

  ec.sublist = &w->glw_prop_subscriptions;

  glw_view_eval_rpn0(rpn, &ec);

  glw_view_free_chain(ec.alloc);

  if(ec.dynamic_eval & GLW_VIEW_DYNAMIC_EVAL_EVERY_FRAME)
    glw_signal_handler_register(w, eval_dynamic_every_frame_sig, rpn, 1000);
  else
    glw_signal_handler_unregister(w, eval_dynamic_every_frame_sig, rpn);

  if(ec.dynamic_eval & GLW_VIEW_DYNAMIC_EVAL_FOCUSED_CHILD_CHANGE)
    glw_signal_handler_register(w, eval_dynamic_focused_child_change_sig,
				rpn, 1000);
  else
    glw_signal_handler_unregister(w, eval_dynamic_focused_child_change_sig,
				  rpn);

  if(ec.dynamic_eval & GLW_VIEW_DYNAMIC_EVAL_FHP_CHANGE)
    glw_signal_handler_register(w, eval_dynamic_fhp_change_sig, rpn, 1000);
  else
    glw_signal_handler_unregister(w, eval_dynamic_fhp_change_sig, rpn);

  if(ec.dynamic_eval & GLW_VIEW_DYNAMIC_EVAL_WIDGET_META)
    glw_signal_handler_register(w, eval_dynamic_widget_meta_sig, rpn, 1000);
  else
    glw_signal_handler_unregister(w, eval_dynamic_widget_meta_sig, rpn);
}



static void cloner_resequence(sub_cloner_t *sc);



/**
 *
 */
static void
clone_eval(clone_t *c)
{
  sub_cloner_t *sc = c->c_sc;
  glw_view_eval_context_t n;
  token_t *body = glw_view_clone_chain(sc->sc_cloner_body);
  const glw_class_t *gc = c->c_w->glw_class;

  if(gc->gc_freeze != NULL)
    gc->gc_freeze(c->c_w);

  memset(&n, 0, sizeof(n));
  n.prop = c->c_prop;
  n.prop_parent = sc->sc_originating_prop;
  n.prop_viewx = sc->sc_view_prop;
  n.prop_clone = c->c_clone_root;
  n.prop_args = sc->sc_view_args;

  n.gr = c->c_w->glw_root;

  n.w = c->c_w;

  n.sublist = &n.w->glw_prop_subscriptions;
  glw_view_eval_block(body, &n);
  glw_view_free_chain(body);

  if(gc->gc_thaw != NULL)
    gc->gc_thaw(c->c_w);
}


/**
 *
 */
static void
cloner_pagination_check(sub_cloner_t *sc)
{
  if(!sc->sc_have_more)
    return;

  if(sc->sc_highest_active <= sc->sc_entries * 0.95)
    return;

  sc->sc_have_more = 0;
  if(sc->sc_sub.gps_sub != NULL)
    prop_want_more_childs(sc->sc_sub.gps_sub);
}



/**
 *
 */
static int
clone_sig_handler(glw_t *w, void *opaque, glw_signal_t signal, void *extra)
{
  clone_t *c = opaque;
  sub_cloner_t *sc = c->c_sc;

  switch(signal) {
  case GLW_SIGNAL_ACTIVE:
    if(!sc->sc_positions_valid)
      cloner_resequence(sc);
    
    if(c->c_pos < sc->sc_lowest_active)
      sc->sc_lowest_active = c->c_pos;

    if(c->c_pos > sc->sc_highest_active)
      sc->sc_highest_active = c->c_pos;

    cloner_pagination_check(sc);
    break;

  case GLW_SIGNAL_INACTIVE:
    if(!sc->sc_positions_valid)
      cloner_resequence(sc);

    if(c->c_pos >= sc->sc_lowest_active && c->c_pos < sc->sc_highest_active)
      sc->sc_lowest_active = c->c_pos + 1;

    if(c->c_pos <= sc->sc_highest_active && c->c_pos > sc->sc_lowest_active)
      sc->sc_highest_active = c->c_pos - 1;

    cloner_pagination_check(sc);
    break;

  case GLW_SIGNAL_DESTROY:
    sc->sc_entries--;
    if(TAILQ_NEXT(w, glw_parent_link) != NULL)
      sc->sc_positions_valid = 0;
    c->c_w = NULL;
    clone_free(c);
    break;

  default:
    break;
  }
  return 0;
}


/**
 *
 */
static void
cloner_resequence(sub_cloner_t *sc)
{
  glw_t *w;
  glw_signal_handler_t *gsh;
  int pos = 0;
  TAILQ_FOREACH(w, &sc->sc_sub.gps_widget->glw_childs, glw_parent_link) {
    LIST_FOREACH(gsh, &w->glw_signal_handlers, gsh_link) {
      if(gsh->gsh_func == clone_sig_handler) {
	clone_t *c = gsh->gsh_opaque;
	c->c_pos = pos;
	break;
      }
    }
    pos++;
  }
  sc->sc_positions_valid = 1;
}





/**
 *
 */
static void
cloner_add_child0(sub_cloner_t *sc, prop_t *p, prop_t *before, 
		  glw_t *parent, errorinfo_t *ei, int flags)
{
  glw_t *b;
  clone_t *c = calloc(1, sizeof(clone_t));

  LIST_INSERT_HEAD(&sc->sc_clones, c, c_link);

  if(before != NULL) {
    clone_t *bb = prop_tag_get(before, sc);
    assert(bb != NULL);
    sc->sc_positions_valid = 0;
    b = bb->c_w;
  } else {
    b = sc->sc_anchor;
    c->c_pos = sc->sc_entries;
  }
  c->c_sc = sc;

  c->c_prop = prop_ref_inc(p);

  sc->sc_entries++;

  c->c_clone_root = prop_create_root(NULL);

  c->c_w = glw_create(parent->glw_root, sc->sc_cloner_class, parent, b, p);
  c->c_w->glw_clone = c;

  glw_set(c->c_w,
	  GLW_ATTRIB_PROPROOTS, p, sc->sc_originating_prop,
	  NULL);

  prop_tag_set(p, sc, c);

  glw_signal_handler_register(c->c_w, clone_sig_handler, c, 1000);

  if(flags & PROP_ADD_SELECTED && parent->glw_class->gc_select_child != NULL)
    parent->glw_class->gc_select_child(parent, c->c_w, NULL);

  clone_eval(c);
}


/**
 *
 */
static void
cloner_add_child(sub_cloner_t *sc, prop_t *p, prop_t *before,
		 glw_t *parent, errorinfo_t *ei, int flags)
{
  glw_prop_sub_pending_t *gpsp, *b;

  if(sc->sc_cloner_body != NULL) {
    cloner_add_child0(sc, p, before, parent, ei, flags);
    return;
  }

  /*
   * The cloner body has not been evaluated yet so we can not
   * create the child. This happens when we subscribe initially.
   * Put it on a pending list and add it once the cloner has been
   * setup.
   */
  
  b = before ? prop_tag_get(before, &sc->sc_pending) : NULL;

  gpsp = malloc(sizeof(glw_prop_sub_pending_t));
  gpsp->gpsp_prop = prop_ref_inc(p);

  prop_tag_set(p, &sc->sc_pending, gpsp);

  if(before) {
    TAILQ_INSERT_BEFORE(b, gpsp, gpsp_link);
  } else {
    TAILQ_INSERT_TAIL(&sc->sc_pending, gpsp, gpsp_link);
  }
  if(flags & PROP_ADD_SELECTED)
    sc->sc_pending_select = p;
}


/**
 *
 */
static void
cloner_move_child0(sub_cloner_t *sc, prop_t *p, prop_t *before,
		   glw_t *parent, errorinfo_t *ei)
{
  clone_t *c =          prop_tag_get(p, sc);
  clone_t *b = before ? prop_tag_get(before, sc) : NULL;

  sc->sc_positions_valid = 0;
  glw_move(c->c_w, b ? b->c_w : NULL);
}


/**
 *
 */
static void
cloner_move_child(sub_cloner_t *sc, prop_t *p, prop_t *before,
		  glw_t *parent, errorinfo_t *ei)
{
  glw_prop_sub_pending_t *t, *b;

  if(sc->sc_cloner_body != NULL) {
    cloner_move_child0(sc, p, before, parent, ei);
    return;
  }

  t =          prop_tag_get(p, &sc->sc_pending);
  b = before ? prop_tag_get(before, &sc->sc_pending) : NULL;

  TAILQ_REMOVE(&sc->sc_pending, t, gpsp_link);

  if(b != NULL) {
    TAILQ_INSERT_BEFORE(b, t, gpsp_link);
  } else {
    TAILQ_INSERT_TAIL(&sc->sc_pending, t, gpsp_link);
  }
}


/**
 *
 */
static void
clone_free(clone_t *c)
{
  glw_t *w = c->c_w;
  if(w != NULL) {
    w->glw_clone = NULL;
    glw_signal_handler_unregister(w, clone_sig_handler, c);
    glw_retire_child(w);
  }

  LIST_REMOVE(c, c_link);
  prop_ref_dec(c->c_prop);
  prop_destroy(c->c_clone_root);
  free(c);
}


/**
 *
 */
static void
cloner_del_child(sub_cloner_t *sc, prop_t *p, glw_t *parent)
{
  clone_t *c;
  glw_prop_sub_pending_t *gpsp;

  if((c = prop_tag_clear(p, sc)) != NULL) {
    sc->sc_entries--;
    glw_t *w = c->c_w;

    if(TAILQ_NEXT(w, glw_parent_link) != NULL)
      sc->sc_positions_valid = 0;

    clone_free(c);
    return;
  }

  if(sc->sc_pending_select == p)
    sc->sc_pending_select = NULL;

  if((gpsp = prop_tag_clear(p, &sc->sc_pending)) == NULL)
    return;

  assert(gpsp->gpsp_prop == p);
  prop_ref_dec(p);
  TAILQ_REMOVE(&sc->sc_pending, gpsp, gpsp_link);
  free(gpsp);
}

/**
 *
 */
static void
cloner_select_child(sub_cloner_t *sc, prop_t *p, glw_t *parent, prop_t *extra)
{
  clone_t *c;
  if(p == NULL) {
    parent->glw_class->gc_select_child(parent, NULL, extra);
    return;
  }

  if((c = prop_tag_get(p, sc)) != NULL) {
    if(parent->glw_class->gc_select_child != NULL)
      parent->glw_class->gc_select_child(parent, c->c_w, extra);
    sc->sc_pending_select = NULL;
    return;
  }

  sc->sc_pending_select = p;
}


/**
 *
 */
static token_t *
prop_callback_alloc_token(glw_prop_sub_t *gps, token_type_t type)
{
  token_t *t = calloc(1, sizeof(token_t));
  t->type = type;

#ifdef GLW_VIEW_ERRORINFO
  t->file = rstr_dup(gps->gps_file);
  t->line = gps->gps_line;
#endif    
  return t;
}


/**
 *
 */
static void
prop_callback_cloner(void *opaque, prop_event_t event, ...)
{
  sub_cloner_t *sc = opaque;
  glw_prop_sub_t *gps = &sc->sc_sub;
  prop_t *p, *p2;
  prop_vec_t *pv;
  token_t *rpn = NULL, *t = NULL;
  int flags, i;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_SET_VOID:
  case PROP_SET_RSTRING:
  case PROP_SET_INT:
  case PROP_SET_FLOAT:
  case PROP_SET_PIXMAP:
    t = prop_callback_alloc_token(gps, TOKEN_VOID);
    t->propsubr = gps;
    rpn = gps->gps_rpn;
    break;

  case PROP_SET_DIR:
    t = prop_callback_alloc_token(gps, TOKEN_DIRECTORY);
    t->propsubr = gps;
    rpn = gps->gps_rpn;
    break;

  case PROP_ADD_CHILD:
    p = va_arg(ap, prop_t *);
    flags = va_arg(ap, int);
    cloner_add_child(sc, p, NULL, gps->gps_widget, NULL, flags);
    break;

  case PROP_ADD_CHILD_VECTOR:
    pv = va_arg(ap, prop_vec_t *);
    for(i = 0; i < prop_vec_len(pv); i++)
      cloner_add_child(sc, prop_vec_get(pv, i), NULL, gps->gps_widget, NULL, 0);
    break;

  case PROP_ADD_CHILD_BEFORE:
    p = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    flags = va_arg(ap, int);
    cloner_add_child(sc, p, p2, gps->gps_widget, NULL, flags);
    break;

  case PROP_ADD_CHILD_VECTOR_BEFORE:
    pv = va_arg(ap, prop_vec_t *);
    p2 = va_arg(ap, prop_t *);
    for(i = 0; i < prop_vec_len(pv); i++)
      cloner_add_child(sc, prop_vec_get(pv, i), p2, gps->gps_widget, NULL, 0);
    break;

  case PROP_MOVE_CHILD:
    p = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    cloner_move_child(sc, p, p2, gps->gps_widget, NULL);
    break;

  case PROP_DEL_CHILD:
    p = va_arg(ap, prop_t *);
    cloner_del_child(sc, p, gps->gps_widget);
    break;

  case PROP_SELECT_CHILD:
    p = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    cloner_select_child(sc, p, gps->gps_widget, p2);
    break;

  case PROP_SET_RLINK:
    t = prop_callback_alloc_token(gps, TOKEN_LINK);
    t->propsubr = gps;
    t->t_link_rtitle = rstr_dup(va_arg(ap, rstr_t *));
    t->t_link_rurl   = rstr_dup(va_arg(ap, rstr_t *));
    rpn = gps->gps_rpn;
    break;

  case PROP_HAVE_MORE_CHILDS:
    sc->sc_have_more = 1;
    cloner_pagination_check(sc);
    break;

  case PROP_REQ_NEW_CHILD:
  case PROP_REQ_DELETE_VECTOR:
  case PROP_DESTROYED:
  case PROP_EXT_EVENT:
  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
  case PROP_WANT_MORE_CHILDS:
    break;

  }

  if(t != NULL) {
      
    if(gps->gps_token != NULL) {
      glw_view_token_free(gps->gps_token);
      gps->gps_token = NULL;
    }
    gps->gps_token = t;
  }

  if(rpn != NULL) 
    eval_dynamic(gps->gps_widget, rpn, NULL, gps->gps_prop_view);
}


/**
 *
 */
static void
prop_callback_value(void *opaque, prop_event_t event, ...)
{
  glw_prop_sub_t *gps = opaque;
  token_t *rpn = NULL, *t = NULL;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_SET_VOID:
    t = prop_callback_alloc_token(gps, TOKEN_VOID);
    t->propsubr = gps;
    rpn = gps->gps_rpn;
    break;

  case PROP_SET_RSTRING:
    t = prop_callback_alloc_token(gps, TOKEN_STRING);
    t->propsubr = gps;
    t->t_rstring =rstr_dup(va_arg(ap, rstr_t *));
    (void)va_arg(ap, prop_t *);
    t->t_rstrtype = va_arg(ap, prop_str_type_t);
    rpn = gps->gps_rpn;
    break;

  case PROP_SET_INT:
    t = prop_callback_alloc_token(gps, TOKEN_INT);
    t->propsubr = gps;
    t->t_int = va_arg(ap, int);
    rpn = gps->gps_rpn;
    break;

  case PROP_SET_FLOAT:
    t = prop_callback_alloc_token(gps, TOKEN_FLOAT);
    t->propsubr = gps;
    t->t_float = va_arg(ap, double);
    (void)va_arg(ap, prop_t *);
    t->t_float_how = va_arg(ap, int);
    rpn = gps->gps_rpn;
    break;

  case PROP_SET_PIXMAP:
    t = prop_callback_alloc_token(gps, TOKEN_PIXMAP);
    t->propsubr = gps;
    t->t_pixmap = pixmap_dup(va_arg(ap, pixmap_t *));
    rpn = gps->gps_rpn;
    break;

  case PROP_SET_RLINK:
    t = prop_callback_alloc_token(gps, TOKEN_LINK);
    t->propsubr = gps;
    t->t_link_rtitle = rstr_dup(va_arg(ap, rstr_t *));
    t->t_link_rurl   = rstr_dup(va_arg(ap, rstr_t *));
    rpn = gps->gps_rpn;
    break;

  case PROP_SET_DIR:
    t = prop_callback_alloc_token(gps, TOKEN_PROPERTY_REF);
    t->propsubr = gps;
    rpn = gps->gps_rpn;
    t->t_prop = prop_ref_inc(va_arg(ap, prop_t *));
    break;

  case PROP_ADD_CHILD:
  case PROP_ADD_CHILD_VECTOR:
  case PROP_ADD_CHILD_BEFORE:
  case PROP_ADD_CHILD_VECTOR_BEFORE:
  case PROP_MOVE_CHILD:
  case PROP_DEL_CHILD:
  case PROP_SELECT_CHILD:
  case PROP_REQ_NEW_CHILD:
  case PROP_REQ_DELETE_VECTOR:
  case PROP_DESTROYED:
  case PROP_EXT_EVENT:
  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
  case PROP_HAVE_MORE_CHILDS:
  case PROP_WANT_MORE_CHILDS:
    break;
  }

  if(t != NULL) {
      
    if(gps->gps_token != NULL) {
      glw_view_token_free(gps->gps_token);
      gps->gps_token = NULL;
    }
    gps->gps_token = t;
  }

  if(rpn != NULL) 
    eval_dynamic(gps->gps_widget, rpn, NULL, gps->gps_prop_view);
}



/**
 * Special prop callback that counts number of entries in a node
 */
static void
prop_callback_counter(void *opaque, prop_event_t event, ...)
{
  sub_counter_t *sc = opaque;
  glw_prop_sub_t *gps = &sc->sc_sub;
  prop_vec_t *pv;
  token_t *rpn = NULL, *t = NULL;
  va_list ap;
  va_start(ap, event);

  gps = opaque;

  switch(event) {
  case PROP_SET_VOID:
  case PROP_SET_RSTRING:
  case PROP_SET_INT:
  case PROP_SET_FLOAT:
  case PROP_SET_DIR:
  case PROP_SET_PIXMAP:
  case PROP_SET_RLINK:
    sc->sc_entries = 0;
    break;

  case PROP_ADD_CHILD:
  case PROP_ADD_CHILD_BEFORE:
    sc->sc_entries++;
    break;

  case PROP_ADD_CHILD_VECTOR:
  case PROP_ADD_CHILD_VECTOR_BEFORE:
    pv = va_arg(ap, prop_vec_t *);
    sc->sc_entries += prop_vec_len(pv);
    break;
    
  case PROP_DEL_CHILD:
    sc->sc_entries--;
    break;

  case PROP_MOVE_CHILD:
  case PROP_SELECT_CHILD:
  case PROP_REQ_NEW_CHILD:
  case PROP_REQ_DELETE_VECTOR:
  case PROP_DESTROYED:
  case PROP_EXT_EVENT:
  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
  case PROP_HAVE_MORE_CHILDS:
  case PROP_WANT_MORE_CHILDS:
    break;
  }

  t = prop_callback_alloc_token(gps, TOKEN_INT);
  t->propsubr = gps;
  t->t_int = sc->sc_entries;

  rpn = gps->gps_rpn;

  if(gps->gps_token != NULL)
    glw_view_token_free(gps->gps_token);

  gps->gps_token = t;

  if(rpn != NULL) 
    eval_dynamic(gps->gps_widget, rpn, NULL, gps->gps_prop_view);
}



/**
 * Transform a property reference (a chain of names) into
 * a resolved subscription.
 */
static int
subscribe_prop(glw_view_eval_context_t *ec, struct token *self, int type)
{
  glw_prop_sub_t *gps;
  prop_sub_t *s;
  glw_t *w = ec->w;
  int i = 0;
  token_t *t;
  const char *propname[16];
  prop_callback_t *cb;
  prop_t *prop = NULL;

  if(w == NULL) 
    return glw_view_seterr(ec->ei, self, 
			    "Properties can not be mapped in this scope");

  switch(self->type) {
  case TOKEN_PROPERTY_VALUE_NAME:
    for(t = self; t != NULL && i < 15; t = t->child)
      propname[i++]  = rstr_get(t->t_rstring);
    propname[i] = NULL;
    break;

  case TOKEN_PROPERTY_REF:
    prop = self->t_prop;
    break;

  default:
    abort();
  }


  switch(type) {
  case GPS_VALUE:
    gps = calloc(1, sizeof(glw_prop_sub_t));
    cb = prop_callback_value;
    break;

  case GPS_CLONER: do {
      sub_cloner_t *sc = calloc(1, sizeof(sub_cloner_t));
      gps = &sc->sc_sub;

      sc->sc_have_more = 1;

      sc->sc_originating_prop = prop_ref_inc(ec->prop);
      sc->sc_view_prop        = prop_ref_inc(ec->prop_viewx);
      sc->sc_view_args        = prop_ref_inc(ec->prop_args);
      
      TAILQ_INIT(&sc->sc_pending);
    } while(0);
    cb = prop_callback_cloner;
    break;

  case GPS_COUNTER:
    gps = calloc(1, sizeof(sub_counter_t));
    cb = prop_callback_counter;
    break;

  default:
    abort();
  }

  gps->gps_type = type;
  gps->gps_prop_view = prop_ref_inc(ec->prop_viewx);

#ifdef GLW_VIEW_ERRORINFO
  gps->gps_file = rstr_dup(self->file);
  gps->gps_line = self->line;
#endif

  int f = PROP_SUB_DIRECT_UPDATE;

  if(ec->w->glw_class->gc_flags & GLW_EXPEDITE_SUBSCRIPTIONS)
    f |= PROP_SUB_EXPEDITE;

  if(ec->debug || ec->w->glw_flags & GLW_DEBUG)
    f |= PROP_SUB_DEBUG;

  if(prop != NULL) {

    s = prop_subscribe(f,
		       PROP_TAG_CALLBACK, cb, gps,
		       PROP_TAG_COURIER, w->glw_root->gr_courier,
		       PROP_TAG_ROOT, prop,
		       NULL);
    // prop came from self->t_prop which we are going to overwrite
    // (since we are changing type of this token) so release our reference
    prop_ref_dec(prop);

  } else {

    s = prop_subscribe(f,
		       PROP_TAG_CALLBACK, cb, gps,
		       PROP_TAG_NAME_VECTOR, propname,
		       PROP_TAG_COURIER, w->glw_root->gr_courier,
		       PROP_TAG_NAMED_ROOT, ec->prop, "self",
		       PROP_TAG_NAMED_ROOT, ec->prop_parent, "parent",
		       PROP_TAG_NAMED_ROOT, ec->prop_viewx, "view",
		       PROP_TAG_NAMED_ROOT, ec->prop_args, "args",
		       PROP_TAG_NAMED_ROOT, ec->prop_clone, "clone",
		       PROP_TAG_ROOT, w->glw_root->gr_uii.uii_prop,
		       NULL);
  }

  gps->gps_sub = s;

  gps->gps_widget = w;
  LIST_INSERT_HEAD(ec->sublist, gps, gps_link);

  gps->gps_rpn = ec->passive_subscriptions ? NULL : ec->rpn;

  if(self->type == TOKEN_PROPERTY_VALUE_NAME) {
    rstr_release(self->t_rstring);
    glw_view_free_chain(self->child);
    self->child = NULL;
  }

  self->propsubr = gps;
  self->type = TOKEN_PROPERTY_SUBSCRIPTION;
  return 0;
}


/**
 *
 */
static int
invoke_func(glw_view_eval_context_t *ec, token_t *t)
{
  token_t **vec;
  int i;

  if(t->t_func->nargs >= 0 && t->t_func->nargs != t->t_num_args) {
    glw_view_seterr(ec->ei, t, "%s(): Invalid number of arguments: %d, "
		     "expected %d", t->t_func->name, t->t_num_args,
		     t->t_func->nargs);
    return -1;
  }

  if(t->t_num_args == 0)
    return t->t_func->cb(ec, t, NULL, 0);

  vec = alloca(t->t_num_args * sizeof(token_t *));

  for(i = t->t_num_args - 1; i >= 0; i--)
    vec[i] = eval_pop(ec);

  return t->t_func->cb(ec, t, vec, t->t_num_args);
}


/**
 *
 */
static int
make_vector(glw_view_eval_context_t *ec, token_t *t)
{
  token_t *a, *r;
  int i;

  if(t->t_num_args < 1)
    return glw_view_seterr(ec->ei, t, "Invalid vector length (%d)",
			   t->t_num_args);

  r = eval_alloc_sized(t, ec, TOKEN_VECTOR_FLOAT, sizeof(token_t) + 
		       sizeof(t->u) * (t->t_num_args - 1));
  r->t_elements = t->t_num_args;
 
  for(i = t->t_num_args - 1; i >= 0; i--) {
    if((a = token_resolve(ec, eval_pop(ec))) == NULL)
      return -1;
    r->t_float_vector[i] = token2float(a);
  }
  eval_push(ec, r);
  return 0;
}



/**
 *
 */
static int
glw_view_eval_rpn0(token_t *t0, glw_view_eval_context_t *ec)
{
  token_t *t;

  for(t = t0->child; t != NULL; t = t->next) {
    switch(t->type) {
    case TOKEN_BLOCK:
    case TOKEN_STRING:
    case TOKEN_LINK:
    case TOKEN_FLOAT:
    case TOKEN_INT:
    case TOKEN_IDENTIFIER:
    case TOKEN_OBJECT_ATTRIBUTE:
    case TOKEN_VOID:
    case TOKEN_PROPERTY_REF:
    case TOKEN_PROPERTY_OWNER:
    case TOKEN_PROPERTY_VALUE_NAME:
    case TOKEN_PROPERTY_CANONICAL_NAME:
    case TOKEN_PROPERTY_SUBSCRIPTION:
      eval_push(ec, t);
      break;

    case TOKEN_ADD:
    case TOKEN_SUB:
    case TOKEN_MULTIPLY:
    case TOKEN_DIVIDE:
    case TOKEN_MODULO:
      if(eval_op(ec, t))
	return -1;
      break;

    case TOKEN_BOOLEAN_OR:
    case TOKEN_BOOLEAN_XOR:
    case TOKEN_BOOLEAN_AND:
      if(eval_bool_op(ec, t))
	return -1;
      break;

    case TOKEN_BOOLEAN_NOT:
      if(eval_bool_not(ec, t))
	return -1;
      break;
	
    case TOKEN_NULL_COALESCE:
      if(eval_null_coalesce(ec, t))
	return -1;
      break;

    case TOKEN_EQ:
    case TOKEN_NEQ:
      if(eval_eq(ec, t, t->type == TOKEN_NEQ))
	return -1;
      break;

    case TOKEN_LT:
    case TOKEN_GT:
      if(eval_lt(ec, t, t->type == TOKEN_GT))
	return -1;
      break;

    case TOKEN_FUNCTION:
#if 0
      printf("Invoking %s with %d arguments\n",
	     t->t_func->name, t->t_num_args);
#endif
      if(invoke_func(ec, t))
	return -1;
      break;

    case TOKEN_LEFT_BRACKET:
      if(make_vector(ec, t))
	return -1;
      break;

    case TOKEN_ASSIGNMENT:
      if(eval_assign(ec, t, 0))
	return -1;
      break;

    case TOKEN_COND_ASSIGNMENT:
      if(eval_assign(ec, t, 1))
	return -1;
      break;

    default:
      fprintf(stderr, "Can not handle token %s\n", token2name(t));
      abort();
    }
  }
  return 0;
}


/**
 *
 */
static int
glw_view_eval_rpn(token_t *t, glw_view_eval_context_t *pec, int *copyp)
{
  glw_view_eval_context_t ec;
  int r;

  memset(&ec, 0, sizeof(ec));
  ec.debug = pec->debug;
  ec.ei = pec->ei;
  ec.prop = pec->prop;
  ec.prop_parent = pec->prop_parent;
  ec.prop_viewx = pec->prop_viewx;
  ec.prop_clone = pec->prop_clone;
  ec.prop_args = pec->prop_args;
  ec.w = pec->w;
  ec.rpn = t;
  ec.gr = pec->gr;
  ec.rc = pec->rc;
  ec.passive_subscriptions = pec->passive_subscriptions;
  ec.sublist = pec->sublist;
  ec.event = pec->event;
  ec.tgtprop = pec->tgtprop;

  r = glw_view_eval_rpn0(t, &ec);

  *copyp = ec.dynamic_eval;
  glw_view_free_chain(ec.alloc);
  return r;
}


/**
 *
 */
int
glw_view_eval_block(token_t *t, glw_view_eval_context_t *ec)
{
  int copy;
  token_t **p;
  glw_t *w;

  assert(ec->dynamic_eval == 0);

  p = &t->child;

  while((t = *p) != NULL) {

    switch(t->type) {
    case TOKEN_NOP:
      break;
    case TOKEN_RPN:
      if(glw_view_eval_rpn(t, ec, &copy))
	return -1;

      if(!copy) 
	break;

      *p = t->next;

      w = ec->w;
      t->next =  w->glw_dynamic_expressions;
      w->glw_dynamic_expressions = t;

      if(copy & GLW_VIEW_DYNAMIC_EVAL_EVERY_FRAME)
	glw_signal_handler_register(w, eval_dynamic_every_frame_sig, t, 1000);

      if(copy & GLW_VIEW_DYNAMIC_EVAL_FOCUSED_CHILD_CHANGE)
	glw_signal_handler_register(w, eval_dynamic_focused_child_change_sig,
				    t, 1000);

      if(copy & GLW_VIEW_DYNAMIC_EVAL_FHP_CHANGE)
	glw_signal_handler_register(w, eval_dynamic_fhp_change_sig, t, 1000);

      if(copy & GLW_VIEW_DYNAMIC_EVAL_WIDGET_META)
	glw_signal_handler_register(w, eval_dynamic_widget_meta_sig, t, 1000);

      continue;

    default:
      glw_view_seterr(ec->ei, t, "Unexpected token");
      return -1;
    }
    p = &t->next;
  }
  return 0;
}

/**
 *
 */
static int 
glwf_widget(glw_view_eval_context_t *ec, struct token *self,
	    token_t **argv, unsigned int argc)
{
  int r;
  const glw_class_t *c;
  glw_view_eval_context_t n;
  token_t *a = argv[0];
  token_t *b = argv[1];

  if(ec->w == NULL) 
    return glw_view_seterr(ec->ei, self, 
			    "Widget can not be created in this scope");

  if(a->type != TOKEN_IDENTIFIER)
    return glw_view_seterr(ec->ei, self, 
			    "widget: Invalid first argument, "
			    "expected widget class");
    
  if(b->type != TOKEN_BLOCK)
    return glw_view_seterr(ec->ei, self, 
			    "widget: Invalid second argument, "
			    "expected block");

  if((c = glw_class_find_by_name(rstr_get(a->t_rstring))) == NULL)
    return glw_view_seterr(ec->ei, self, "widget: Invalid class");

  memset(&n, 0, sizeof(n));
  n.prop = ec->prop;
  n.prop_parent = ec->prop_parent;
  n.prop_viewx = ec->prop_viewx;
  n.prop_clone = ec->prop_clone;
  n.prop_args = ec->prop_args;
  n.ei = ec->ei;
  n.gr = ec->gr;
  n.rc = ec->rc;
  n.w = glw_create(ec->gr, c, ec->w, NULL, NULL);

  if(c->gc_freeze != NULL)
    c->gc_freeze(n.w);

  glw_set(n.w,
	  GLW_ATTRIB_PROPROOTS, ec->prop, ec->prop_parent,
	  NULL);

  n.sublist = &n.w->glw_prop_subscriptions;

  r = glw_view_eval_block(b, &n);

  if(c->gc_thaw != NULL)
    c->gc_thaw(n.w);

  return r ? -1 : 0;
}


/**
 *
 */
static int 
glwf_cloner(glw_view_eval_context_t *ec, struct token *self,
	    token_t **argv, unsigned int argc)
{
  token_t *a = argc > 0 ? argv[0] : NULL;
  token_t *b = argc > 1 ? argv[1] : NULL;
  token_t *c = argc > 2 ? argv[2] : NULL;
  glw_prop_sub_pending_t *gpsp;
  int f;
  const glw_class_t *cl;
  glw_t *parent = ec->w;
  glw_t *w;

  if(parent == NULL) 
    return glw_view_seterr(ec->ei, self, 
			    "Cloner can not be created in this scope");

  if((a = token_resolve_ex(ec, a, GPS_CLONER)) == NULL)
    return -1;
    
  if(b->type != TOKEN_IDENTIFIER)
    return glw_view_seterr(ec->ei, self, 
			    "cloner: Invalid second argument, "
			    "expected widget class");
    
 if((cl = glw_class_find_by_name(rstr_get(b->t_rstring))) == NULL)
     return glw_view_seterr(ec->ei, self, "cloner: Invalid class");

  if(c->type != TOKEN_BLOCK)
    return glw_view_seterr(ec->ei, self, 
			    "cloner: Invalid third argument, "
			    "expected block");

  if(!(parent->glw_class->gc_flags & GLW_CAN_HIDE_CHILDS)) {
    fprintf(stderr, "Parent %s can not hide childs, cannot clone\n",
	    parent->glw_class->gc_name);
    abort();
  }


  if(self->t_extra == NULL) {
    static const glw_class_t *dummy;

    if(dummy == NULL)
      dummy = glw_class_find_by_name("dummy");
    
    self->t_extra = glw_create(ec->gr, dummy, parent, NULL, NULL);

    glw_hide(self->t_extra);
  }

  /* Destroy any previous cloned entries */
  while((w = TAILQ_PREV((glw_t *)self->t_extra,
			glw_queue, glw_parent_link)) != NULL &&
	w->glw_clone != NULL) {
    prop_tag_clear(w->glw_clone->c_prop, w->glw_clone->c_sc);
    clone_free(w->glw_clone);
  }

  if(a->type == TOKEN_DIRECTORY) {
    sub_cloner_t *sc = (sub_cloner_t *)a->propsubr;
    sc->sc_anchor = self->t_extra;

    cloner_cleanup(sc);

    sc->sc_cloner_body = glw_view_clone_chain(c);
    sc->sc_cloner_class = cl;

    /* Create pending childs */
    while((gpsp = TAILQ_FIRST(&sc->sc_pending)) != NULL) {
      TAILQ_REMOVE(&sc->sc_pending, gpsp, gpsp_link);

      f = gpsp->gpsp_prop == sc->sc_pending_select ? PROP_ADD_SELECTED : 0;
	
      cloner_add_child0(sc, gpsp->gpsp_prop, NULL, parent, ec->ei, f);
      prop_tag_clear(gpsp->gpsp_prop, &sc->sc_pending);
      prop_ref_dec(gpsp->gpsp_prop);
      free(gpsp);
    }
    sc->sc_pending_select = NULL;
  }

  return 0;
}



/**
 *
 */
static int 
glwf_space(glw_view_eval_context_t *ec, struct token *self,
	   token_t **argv, unsigned int argc)

{
  token_t *a = argv[0];
  static const glw_class_t *dummy;
  if(ec->w == NULL) 
    return glw_view_seterr(ec->ei, self, 
			    "Widget can not be created in this scope");

  if((a = token_resolve(ec, a)) == NULL)
    return -1;

  if(dummy == NULL)
    dummy = glw_class_find_by_name("dummy");

  glw_t *w = glw_create(ec->gr, dummy, ec->w, NULL, NULL);
  glw_set_constraints(w, 0, 0, token2float(a),
		      GLW_CONSTRAINT_W, GLW_CONSTRAINT_CONF_W);
  return 0;
}




/**
 *
 */
typedef struct glw_event_map_eval_block {
  glw_event_map_t map;
  token_t *block;
  prop_t *prop;
  prop_t *prop_parent;
  prop_t *prop_view;
  prop_t *prop_args;
  prop_t *prop_clone;
} glw_event_map_eval_block_t;


/**
 *
 */
static void
glw_event_map_eval_block_fire(glw_t *w, glw_event_map_t *gem, event_t *src)
{
  glw_event_map_eval_block_t *b = (glw_event_map_eval_block_t *)gem;
  token_t *body;
  glw_view_eval_context_t n;
  struct glw_prop_sub_list l;

  LIST_INIT(&l);

  memset(&n, 0, sizeof(n));
  n.prop = b->prop;
  n.prop_parent = b->prop_parent;
  n.prop_viewx = b->prop_view;
  n.prop_clone = b->prop_clone;
  n.prop_args = b->prop_args;
  n.gr = w->glw_root;
  n.rc = NULL;
  n.w = w;
  n.passive_subscriptions = 1;
  n.sublist = &l;
  n.event = src;

  body = glw_view_clone_chain(b->block);
  glw_view_eval_block(body, &n);
  glw_prop_subscription_destroy_list(&l);
  glw_view_free_chain(body);
}

/**
 *
 */
static void
glw_event_map_eval_block_dtor(glw_event_map_t *gem)
{
  glw_event_map_eval_block_t *b = (glw_event_map_eval_block_t *)gem;

  glw_view_free_chain(b->block);

  if(b->prop)
    prop_ref_dec(b->prop);

  if(b->prop_parent)
    prop_ref_dec(b->prop_parent);

  if(b->prop_view)
    prop_ref_dec(b->prop_view);

  if(b->prop_args)
    prop_ref_dec(b->prop_args);

  if(b->prop_clone)
    prop_ref_dec(b->prop_clone);

  free(b);
}



/**
 *
 */
static glw_event_map_t *
glw_event_map_eval_block_create(glw_view_eval_context_t *ec,
				struct token *block)
{
  glw_event_map_eval_block_t *b = malloc(sizeof(glw_event_map_eval_block_t));

  b->block = glw_view_clone_chain(block);

  b->prop        = prop_ref_inc(ec->prop);
  b->prop_parent = prop_ref_inc(ec->prop_parent);
  b->prop_view   = prop_ref_inc(ec->prop_viewx);
  b->prop_clone  = prop_ref_inc(ec->prop_clone);
  b->prop_args   = prop_ref_inc(ec->prop_args);

  b->map.gem_dtor = glw_event_map_eval_block_dtor;
  b->map.gem_fire = glw_event_map_eval_block_fire;
  return &b->map;
}



/**
 *
 */
static int 
glwf_onEvent(glw_view_eval_context_t *ec, struct token *self,
	     token_t **argv, unsigned int argc)

{
  token_t *a = argc > 0 ? argv[0] : NULL;  /* Source */
  token_t *b = argc > 1 ? argv[1] : NULL;  /* Target */
  token_t *c = argc > 2 ? argv[2] : NULL;  /* Enabled */
  int action, enabled;
  glw_t *w = ec->w;
  glw_event_map_t *gem;

  if(w == NULL) 
    return glw_view_seterr(ec->ei, self, 
			   "Events can not be mapped in this scope");

  if(a == NULL || b == NULL)
    return glw_view_seterr(ec->ei, self, "Missing operands");

  if(a->type != TOKEN_IDENTIFIER)
    return glw_view_seterr(ec->ei, a, "Invalid source event type");

  if(!strcmp(rstr_get(a->t_rstring), "KeyCode")) {
    action = -1;
  } else {
    action = action_str2code(rstr_get(a->t_rstring));

    if(action < 0)
      return glw_view_seterr(ec->ei, a, "Invalid source event type");
  }

  if(c != NULL) {
    if((c = token_resolve(ec, c)) == NULL)
      return -1;
    enabled = token2bool(c);
  } else {
    enabled = 1;
  }

  if(enabled) {

    switch(b->type) {

    case TOKEN_EVENT:
      b->type = TOKEN_NOP; /* Steal 'gem' pointer from this token.
			      It's okay since TOKEN_EVENT are always
			      generated dynamically. */
      gem = b->t_gem;
      break;

    case TOKEN_BLOCK:
      gem = glw_event_map_eval_block_create(ec, b);
      break;

    default:
      return glw_view_seterr(ec->ei, a, "onEvent: Second arg is invalid");
    }

    gem->gem_action = action;
    glw_event_map_add(w, gem);
    return 0;
    
  } else {
    
    glw_event_map_remove_by_action(w, action);
    return 0;
  }
}


/**
 *
 */
static int 
glwf_navOpen(glw_view_eval_context_t *ec, struct token *self,
	     token_t **argv, unsigned int argc)
{
  token_t *a, *b, *c, *r;
  const char *url;
  const char *view = NULL;
  prop_t *origin = NULL;

  if(argc < 1 || argc > 3)
    return glw_view_seterr(ec->ei, self, "navOpen(): Invalid number of args");

  if((a = token_resolve(ec, argv[0])) == NULL)
    return -1;

  if(a->type == TOKEN_VOID)
    url = NULL;
  else if(a->type == TOKEN_STRING)
    url = rstr_get(a->t_rstring);
  else if(a->type == TOKEN_LINK)
    url = rstr_get(a->t_link_rurl);
  else
    return glw_view_seterr(ec->ei, a, "navOpen(): "
			    "First argument is not a string, link or (void)");

  if(argc > 1) {
    if((b = token_resolve(ec, argv[1])) == NULL)
      return -1;

    if(b->type == TOKEN_VOID)
      view = NULL;
    else if(b->type == TOKEN_STRING)
      view = rstr_get(b->t_rstring);
    else
      return glw_view_seterr(ec->ei, b, "navOpen(): "
			     "Second argument is not a string or (void)");
  }

  if(argc > 2) {
    if((c = resolve_property_name2(ec, argv[2])) == NULL)
      return -1;
    
    if(c->type != TOKEN_PROPERTY_REF)
      return glw_view_seterr(ec->ei, c, "navOpen(): "
			     "Third argument is not a property");
    origin = c->t_prop;
  }

  r = eval_alloc(self, ec, TOKEN_EVENT);
  r->t_gem = glw_event_map_navOpen_create(url, view, origin);
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static int 
glwf_deliverEvent(glw_view_eval_context_t *ec, struct token *self,
		  token_t **argv, unsigned int argc)
{
  token_t *a, *b, *r;
  rstr_t *action = NULL;

 if(argc < 1 || argc > 2)
    return glw_view_seterr(ec->ei, self,
			   "deliverEvent(): Invalid number of args");

  if((a = resolve_property_name2(ec, argv[0])) == NULL)
    return -1;

  if(a->type != TOKEN_PROPERTY_REF)
    return glw_view_seterr(ec->ei, a, "navOpen(): "
			    "First argument is not a property");

  if(argc == 2) {
    if((b = token_resolve(ec, argv[1])) == NULL)
      return -1;

    action = b->type == TOKEN_STRING ? b->t_rstring : NULL;
  }

  r = eval_alloc(self, ec, TOKEN_EVENT);
  r->t_gem = glw_event_map_deliverEvent_create(a->t_prop, action);
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static int 
glwf_playTrackFromSource(glw_view_eval_context_t *ec, struct token *self,
			 token_t **argv, unsigned int argc)
{
  token_t *a, *b, *r;

  if((a = resolve_property_name2(ec, argv[0])) == NULL)
    return -1;
  if((b = resolve_property_name2(ec, argv[1])) == NULL)
    return -1;

  r = eval_alloc(self, ec, TOKEN_EVENT);
  r->t_gem = glw_event_map_playTrack_create(a->t_prop, b->t_prop, 0);
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static int 
glwf_enqueueTrack(glw_view_eval_context_t *ec, struct token *self,
		  token_t **argv, unsigned int argc)
{
  token_t *a, *r;

  if((a = resolve_property_name2(ec, argv[0])) == NULL)
    return -1;

  r = eval_alloc(self, ec, TOKEN_EVENT);
  r->t_gem = glw_event_map_playTrack_create(a->t_prop, NULL, 0);
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static int 
glwf_selectTrack(glw_view_eval_context_t *ec, struct token *self,
		 token_t **argv, unsigned int argc, event_type_t type)
{
  token_t *a = argv[0];       /* ID */
  token_t *r;
  char buf[16];
  const char *str;

  a = token_resolve(ec, a);
  r = eval_alloc(self, ec, TOKEN_EVENT);


  if(a && a->type == TOKEN_STRING)
    str = rstr_get(a->t_rstring);
  else if(a && a->type == TOKEN_INT) {
    snprintf(buf, sizeof(buf), "%d", a->t_int);
    str = buf;
  } else {
    str = NULL;
  }
  r->t_gem = glw_event_map_selectTrack_create(str, type);
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static int 
glwf_selectAudioTrack(glw_view_eval_context_t *ec, struct token *self,
		 token_t **argv, unsigned int argc)
{
  return glwf_selectTrack(ec, self, argv, argc, EVENT_SELECT_AUDIO_TRACK);
}

/**
 *
 */
static int 
glwf_selectSubtitleTrack(glw_view_eval_context_t *ec, struct token *self,
		 token_t **argv, unsigned int argc)
{
  return glwf_selectTrack(ec, self, argv, argc, EVENT_SELECT_SUBTITLE_TRACK);
}


/**
 *
 */
static int 
glwf_fireEvent(glw_view_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];       /* Event */
  if(a->type != TOKEN_EVENT)
    return glw_view_seterr(ec->ei, a, "fireEvent(): "
			    "First argument is not an event");

  a->type = TOKEN_NOP; // Steal event
  glw_event_map_t *gem = a->t_gem;
  
  gem->gem_fire(ec->w, gem, NULL);
  return 0;
}


/**
 *
 */
static int 
glwf_targetedEvent(glw_view_eval_context_t *ec, struct token *self,
	   token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];       /* Target name */
  token_t *b = argv[1];       /* Event */
  token_t *r;
  int action;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;

  if(a->type != TOKEN_STRING)
    return glw_view_seterr(ec->ei, a, "event(): "
			    "First argument is not a string");
  
  if(b->type != TOKEN_IDENTIFIER ||
     (action = action_str2code(rstr_get(b->t_rstring))) < 0)
    return glw_view_seterr(ec->ei, b, "event(): "
			    "Invalid target event");
  
  r = eval_alloc(self, ec, TOKEN_EVENT);
  r->t_gem = glw_event_map_internal_create(rstr_get(a->t_rstring), action);
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static int 
glwf_event(glw_view_eval_context_t *ec, struct token *self,
	   token_t **argv, unsigned int argc)
{
  token_t *a, *r;
  int action;

  a = argv[0];

  if(a->type != TOKEN_IDENTIFIER ||
     (action = action_str2code(rstr_get(a->t_rstring))) < 0)
    return glw_view_seterr(ec->ei, a, "event(): Invalid target event");
  
  r = eval_alloc(self, ec, TOKEN_EVENT);
  r->t_gem = glw_event_map_internal_create(NULL, action);
  eval_push(ec, r);
  return 0;
}



typedef struct glwf_changed_extra {

  int threshold;
  token_type_t type;

  union {
    rstr_t *rstr;
    float value;
  } u;

  int transition;

} glwf_changed_extra_t;


/**
 *
 */
static int 
glwf_changed(glw_view_eval_context_t *ec, struct token *self,
	     token_t **argv, unsigned int argc)

{
  token_t *a, *b, *c, *r;
  glwf_changed_extra_t *e = self->t_extra;
  int change = 0;
  int supp_first = 0;

  if(argc < 2 || argc > 3)
    return glw_view_seterr(ec->ei, self, 
			    "changed(): Invalid number of arguments");

  if((a = token_resolve(ec, argv[0])) == NULL)
    return -1;
  if((b = token_resolve(ec, argv[1])) == NULL)
    return -1;
  if(argc == 3) {
    if((c = token_resolve(ec, argv[2])) == NULL)
      return -1;
    supp_first = token2bool(c);
  }

  if(a->type != TOKEN_FLOAT && a->type != TOKEN_STRING &&
     a->type != TOKEN_VOID)
    return glw_view_seterr(ec->ei, self, "Invalid first operand to changed()");

  if(b->type != TOKEN_FLOAT)
    return glw_view_seterr(ec->ei, self, 
			    "Invalid second operand to changed(), "
			    "expected scalar");

  if(a->type != e->type) {
    if(e->type == TOKEN_STRING)
      rstr_release(e->u.rstr);

    e->type = a->type;

    switch(a->type) {

    case TOKEN_STRING:
      e->u.rstr = rstr_dup(a->t_rstring);
      break;

    case TOKEN_FLOAT:
      e->u.value = a->t_float;
      break;

    case TOKEN_VOID:
    default:
      break;
    }

    change = 1;

  } else {

   switch(a->type) {

    case TOKEN_STRING:
      if(strcmp(rstr_get(e->u.rstr), rstr_get(a->t_rstring))) {
	change = 1;
	rstr_release(e->u.rstr);
	e->u.rstr = rstr_dup(a->t_rstring);
      }
      break;

    case TOKEN_FLOAT:
      if(e->u.value != a->t_float) {
	e->u.value = a->t_float;
	change = 1;
      }
      break;

    case TOKEN_VOID:
    default:
      break;
    }
  }

  if(change == 1) {
    if(e->transition > 0 || supp_first == 0)
      e->threshold = b->t_float * (1000000 / ec->gr->gr_frameduration);
    e->transition = 1;
  }

  if(e->threshold > 0)
    e->threshold--;

  r = eval_alloc(self, ec, TOKEN_FLOAT);
  if(e->threshold > 0) {
    r->t_float = 1;
    ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_EVERY_FRAME;
  }

  eval_push(ec, r);
  return 0;
}

/**
 *
 */
static void
glwf_changed_ctor(struct token *self)
{
  self->t_extra = calloc(1, sizeof(glwf_changed_extra_t));
}



/**
 *
 */
static void
glwf_changed_dtor(struct token *self)
{
  glwf_changed_extra_t *e = self->t_extra;

  if(e->type == TOKEN_STRING)
    rstr_release(e->u.rstr);
  free(e);
}


/**
 * Infinite Impulse Response filter
 */
static int 
glwf_iir(glw_view_eval_context_t *ec, struct token *self,
	 token_t **argv, unsigned int argc)
{
  token_t *a, *b, *c;
  token_t *r;
  float f;
  int springmode, x, y;

  if(argc < 2 || argc > 3) {
    glw_view_seterr(ec->ei, self, "iir(): Invalid number of arguments: %d",
		    argc);
    return -1;
  }

  if((a = token_resolve(ec, argv[0])) == NULL)
    return -1;
  if((b = token_resolve(ec, argv[1])) == NULL)
    return -1;
  if(argc == 3) {
    if((c = token_resolve(ec, argv[2])) == NULL)
      return -1;
    springmode = token2bool(c);
  } else {
    springmode = 0;
  }
  if(a == NULL || (a->type != TOKEN_FLOAT  && a->type != TOKEN_INT &&
		   a->type != TOKEN_STRING && a->type != TOKEN_VOID))
    return glw_view_seterr(ec->ei, self, "Invalid first operand to iir()");

  if(a->type == TOKEN_STRING || a->type == TOKEN_VOID)
    f = 0;
  else
    f = a->type == TOKEN_INT ? a->t_int : a->t_float;

  if(b == NULL || b->type != TOKEN_FLOAT)
    return glw_view_seterr(ec->ei, self, "Invalid second operand to iir()");

  x = self->t_extra_float * 1000.;

  if(springmode && f > self->t_extra_float)
    self->t_extra_float = f;
  else
    self->t_extra_float =  GLW_LP(b->t_float, self->t_extra_float, f);

  y = self->t_extra_float * 1000.;
  r = eval_alloc(self, ec, TOKEN_FLOAT);

  if(x != y)
    ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_EVERY_FRAME;
  else
    self->t_extra_float = f;

  r->t_float = self->t_extra_float;
  eval_push(ec, r);
  return 0;
}


typedef struct glw_scurve_extra {
  float x;
  float xd;

  float start;
  float current;
  float target;
  float time;

} glw_scurve_extra_t;


/**
 * Scurve model
 */
static int 
glwf_scurve(glw_view_eval_context_t *ec, struct token *self,
	    token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *b = argv[1];
  token_t *r;
  float f, v, t;
  glw_scurve_extra_t *s = self->t_extra;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;
  if((b = token_resolve(ec, b)) == NULL)
    return -1;

  f = token2float(a);
  t = token2float(b);

  if(s->target != f || s->time != t) {
    s->start = s->target;
    s->target = f;
    s->time = t;
    s->x = 0;
    s->xd = 1.0 / (1000000 * s->time / ec->w->glw_root->gr_frameduration);
  }

  s->x += s->xd;

  if(s->x < 1.0) {
    v = GLW_S(s->x);
    s->current = GLW_LERP(v, s->start, s->target);
    ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_EVERY_FRAME;
  } else {
    s->current = s->target;
  }

  r = eval_alloc(self, ec, TOKEN_FLOAT);
  r->t_float = s->current;
  eval_push(ec, r);
  return 0;
}

/**
 *
 */
static void
glwf_scurve_ctor(struct token *self)
{
  self->t_extra = calloc(1, sizeof(glw_scurve_extra_t));
}


/**
 *
 */
static void
glwf_scurve_dtor(struct token *self)
{
  free(self->t_extra);
}


/**
 *
 */
static int
fmt_add_string(char *out, int len, const char *str)
{
  char c;
  while((c = *str++)) {
    if(out)
      out[len] = c;
    len++;
  }
  return len;
}


/**
 *
 */
static void
build_fmt(char *fmt, int zeropad, int fl1, int fl2, char type)
{
  int fmtptr = 1;

  fmt[0] = '%';
  if(zeropad)
    fmt[fmtptr++] = '0';
  if(fl1 != -1)
    fmt[fmtptr++] = fl1 + '0';
  if(fl2 != -1) {
    fmt[fmtptr++] = '.';
    fmt[fmtptr++] = fl2 + '0';
  }
  fmt[fmtptr++] = type;
  fmt[fmtptr] = 0;
}


/**
 *
 */
static int
fmt_add_int(char *out, int len, int v, int zeropad, int fl1, int fl2)
{
  char fmt[32];
  char buf[64];

  build_fmt(fmt, zeropad, fl1, fl2, 'd');

  snprintf(buf, sizeof(buf), fmt, v);

  size_t l = strlen(buf);

  if(out)
    memcpy(out + len, buf, l);

  return len + l;
}


/**
 *
 */
static int
fmt_add_float(char *out, int len, float v, int zeropad, int fl1, int fl2)
{
  char fmt[32];
  char buf[64];

  build_fmt(fmt, zeropad, fl1, fl2, 'f');

  snprintf(buf, sizeof(buf), fmt, v);

  size_t l = strlen(buf);

  if(out)
    memcpy(out + len, buf, l);

  return len + l;
}


/**
 * String formating internal
 */
static size_t
dofmt(char *out, const char *fmt, token_t **argv, unsigned int argc)
{
  size_t len = 0;
  char c;
  int argptr = 0;

  while((c = *fmt) != 0) {
    if(c != '%') {
      if(out) 
	out[len] = c;
      len++;
      fmt++;
    } else {
      int zeropad = 0;
      int off = 1;
      int fl1 = -1;
      int fl2 = -1;

      if(fmt[off] == '0') {
	zeropad = 1;
	off++;
      }
      while(fmt[off] >= '0' && fmt[off] <= '9') {
	if(fl1 == -1)
	  fl1 = 0;
	fl1 = fl1 * 10 + fmt[off++] - '0';
      }

      if(fmt[off] == '.')  {
	off++;
	while(fmt[off] >= '0' && fmt[off] <= '9') {
	  if(fl2 == -1)
	    fl2 = 0;
	  fl2 = fl2 * 10 + fmt[off++] - '0';
	}
      }

      int type = fmt[off];
      if(type == 0)
	break;
      off++;
      fmt += off;

      if(type == '%') {
	if(out) 
	  out[len] = '%';
	len++;
	continue;
      }

      if(argptr == argc)
	continue;

      token_t *arg = argv[argptr];
      argptr++;
      int i32;
      float flt;

      switch(type) {
      case 'd':
	switch(arg->type) {
	case TOKEN_INT:
	  i32 = arg->t_int;
	  break;
	case TOKEN_FLOAT:
	  i32 = arg->t_float;
	  break;
	default:
	  i32 = 0;
	  break;
	}
	len = fmt_add_int(out, len, i32, zeropad, fl1, fl2);
	break;

      case 'f':
	switch(arg->type) {
	case TOKEN_INT:
	  flt = arg->t_int;
	  break;
	case TOKEN_FLOAT:
	  flt = arg->t_float;
	  break;
	default:
	  flt = 0;
	  break;
	}
	len = fmt_add_float(out, len, flt, zeropad, fl1, fl2);
	break;

      default:
	switch(arg->type) {
	case TOKEN_INT:
	  len = fmt_add_int(out, len, arg->t_int, zeropad, fl1, fl2);
	  break;
	case TOKEN_FLOAT:
	  len = fmt_add_float(out, len, arg->t_float, zeropad, fl1, fl2);
	  break;
	case TOKEN_STRING:
	  len = fmt_add_string(out, len, rstr_get(arg->t_rstring));
	  break;
	case TOKEN_LINK:
	  len = fmt_add_string(out, len, rstr_get(arg->t_link_rtitle));
	  break;
	default:
	  break;
	}
	break;
      }
    }
  }
  return len;

}

/**
 * String formating
 */
static int 
glwf_fmt(glw_view_eval_context_t *ec, struct token *self,
	 token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *r;
  int i;
  token_t **res_args;
  unsigned int num_res_args;

  if(argc < 1) 
    return glw_view_seterr(ec->ei, self,
			    "fmt() requires at least one arguments");
  
  if((a = token_resolve(ec, a)) == NULL)
    return -1;
  
  if(a->type != TOKEN_STRING)
    return glw_view_seterr(ec->ei, a,
			   "fmt() first argument is not a string");

  num_res_args = argc - 1;
  if(num_res_args > 0) {
    res_args = alloca(sizeof(token_t *) * num_res_args);
    for(i = 0; i < num_res_args; i++)
      if((res_args[i] = token_resolve(ec, argv[i+1])) == NULL)
	return -1;

  } else {
    res_args = NULL;
  }

  const char *str = rstr_get(a->t_rstring);
  size_t len = dofmt(NULL, str, res_args, num_res_args);
  r = eval_alloc(self, ec, TOKEN_STRING);
  r->t_rstring  = rstr_allocl(NULL, len);
  dofmt(rstr_data(r->t_rstring), str, res_args, num_res_args);
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static int 
token_cmp(token_t *a, token_t *b)
{
  if(a->type == TOKEN_INT   && b->type == TOKEN_FLOAT)
    return a->t_int != b->t_float;
  if(a->type == TOKEN_FLOAT && b->type == TOKEN_INT)
    return a->t_float != b->t_int;

  if(token_is_string(a) && token_is_string(b))
    return strcmp(rstr_get(a->t_rstring), rstr_get(b->t_rstring));

  if(a->type != b->type)
    return -1;

  switch(a->type) {
  case TOKEN_INT:
    return a->t_int - b->t_int;
  case TOKEN_FLOAT:
    return a->t_float - b->t_float;
  default:
    return -1;
  }
}

/**
 * Associative lookup
 */
static int 
glwf_translate(glw_view_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)

{
  token_t *idx, *def, *k, *v;
  int i;

  if(argc < 2) 
    return glw_view_seterr(ec->ei, self,
			    "translate() requires at least two arguments");

  if(argc & 1) 
    return glw_view_seterr(ec->ei, self,
			    "translate() requires even number of arguments");

  if((idx = token_resolve(ec, argv[0])) == NULL)
    return -1;
  if((def = token_resolve(ec, argv[1])) == NULL)
    return -1;

  argc -= 2;
  argv += 2;

  for(i = 0; i < argc; i+=2) {
    if((k = token_resolve(ec, *argv++)) == NULL)
      return -1;
    if((v = token_resolve(ec, *argv++)) == NULL)
      return -1;
    
    if(!token_cmp(idx, k)) {
      eval_push(ec, v);
      return 0;
    }
  }
  eval_push(ec, def);
  return 0;
}

/**
 * strftime support (only localtime)
 */
static int 
glwf_strftime(glw_view_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];  // unixtime
  token_t *b = argv[1];  // format
  token_t *r;
  char buf[50];
  struct tm tm;
  time_t t;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;
  if((b = token_resolve(ec, b)) == NULL)
    return -1;

  if(b->type != TOKEN_STRING)
    return glw_view_seterr(ec->ei, self, 
			    "Invalid second operand to strftime()");

  t = token2int(a);
  if(t != 0) {
    my_localtime(&t, &tm);
    strftime(buf, sizeof(buf), rstr_get(b->t_rstring), &tm);
  } else {
    buf[0] = 0;
  }
  r = eval_alloc(self, ec, TOKEN_STRING);
  r->t_rstring = rstr_alloc(buf);
  eval_push(ec, r);
  return 0;
}


/**
 * Return 1 if the given token is set (string is != "", value != 0)
 */
static int 
glwf_isset(glw_view_eval_context_t *ec, struct token *self,
	   token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *r;
  int rv;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;

  switch(a->type) {
  default:
    rv = 1;
    break;

  case TOKEN_STRING:
  case TOKEN_LINK:
    rv = rstr_get(a->t_rstring)[0] != 0;
    break;
  case TOKEN_FLOAT:
    rv = a->t_float != 0;
    break;
  case TOKEN_INT:
    rv = a->t_int != 0;
    break;
  case TOKEN_VOID:
    rv = 0;
    break;
  }

  r = eval_alloc(self, ec, TOKEN_INT);
  r->t_int = rv;
  eval_push(ec, r);
  return 0;
}


/**
 * Int to string
 */
static int 
glwf_value2duration(glw_view_eval_context_t *ec, struct token *self,
		    token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *r;
  char tmp[30];
  const char *str = NULL;
  int s = 0;

  a = token_resolve(ec, a);

  if(a == NULL) {
    str = "";
  } else {

    switch(a->type) {
    case TOKEN_STRING:
      str = rstr_get(a->t_rstring);
      break;
    case TOKEN_FLOAT:
      s = a->t_float;
      break;
    case TOKEN_INT:
      s = a->t_int;
      break;
    default:
      str = "";
      break;
    }

    if(str == NULL) {
      int m = s / 60;
      int h = s / 3600;
  
      if(h > 0) {
	snprintf(tmp, sizeof(tmp), "%d:%02d:%02d", h, m % 60, s % 60);
      } else {
	snprintf(tmp, sizeof(tmp), "%d:%02d", m % 60, s % 60);
      }
      str = tmp;
    }
  }

  r = eval_alloc(self, ec, TOKEN_STRING);
  r->t_rstring = rstr_alloc(str);
  eval_push(ec, r);
  return 0;
}



/**
 * Create a new child under the given property
 */
static int 
glwf_createchild(glw_view_eval_context_t *ec, struct token *self,
		 token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *t;
  const char *propname[16];
  prop_t *p, *r;
  int i;

  if(a->type != TOKEN_PROPERTY_VALUE_NAME)
    return 0;
  
  for(i = 0, t = a; t != NULL && i < 15; t = t->child)
    propname[i++]  = rstr_get(t->t_rstring);
  propname[i] = NULL;
  
  r = ec->w ? ec->w->glw_root->gr_uii.uii_prop : NULL;

  p = prop_get_by_name(propname, 1, 
		       PROP_TAG_NAMED_ROOT, ec->prop, "self",
		       PROP_TAG_NAMED_ROOT, ec->prop_parent, "parent",
		       PROP_TAG_ROOT, r,
		       NULL);

  if(p != NULL) {
    prop_request_new_child(p);
    prop_ref_dec(p);
  }
  return 0;
}



/**
 * Delete given property
 */
static int 
glwf_delete(glw_view_eval_context_t *ec, struct token *self,
	    token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];

  switch(a->type) {
  case TOKEN_PROPERTY_VALUE_NAME:
    if(resolve_property_name(ec, a, 1))
      return -1;
    if(0)
  case TOKEN_PROPERTY_CANONICAL_NAME:
    if(resolve_property_name(ec, a, 0))
      return -1;

  case TOKEN_PROPERTY_REF:
    break;

  default:
    return glw_view_seterr(ec->ei, a, 
			   "Invalid operand to delete()");
  }
  prop_request_delete(a->t_prop);
  return 0;
}



/**
 * Return 1 if the current widget is in focus
 */
static int 
glwf_isFocused(glw_view_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)
{
  token_t *r;

  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_FHP_CHANGE;

  r = eval_alloc(self, ec, TOKEN_INT);
  r->t_int = glw_is_focused(ec->w);
  eval_push(ec, r);
  return 0;
}


/**
 * Return 1 if the current widget is in focus
 */
static int 
glwf_isHovered(glw_view_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)
{
  token_t *r;

  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_FHP_CHANGE;

  r = eval_alloc(self, ec, TOKEN_INT);
  r->t_int = glw_is_hovered(ec->w);
  eval_push(ec, r);
  return 0;
}

/**
 * Return 1 if the current widget is depressed
 */
static int 
glwf_isPressed(glw_view_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)
{
  token_t *r;

  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_FHP_CHANGE;

  r = eval_alloc(self, ec, TOKEN_INT);
  r->t_int = glw_is_pressed(ec->w);
  eval_push(ec, r);
  return 0;
}


/**
 * Returns the focused child (or void if nothing is focused)
 */
static int 
glwf_focusedChild(glw_view_eval_context_t *ec, struct token *self,
		  token_t **argv, unsigned int argc)
{
  glw_t *w = ec->w, *c;
  token_t *r;

  if(w == NULL) 
    return glw_view_seterr(ec->ei, self, "focusedChild() without widget");

  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_FOCUSED_CHILD_CHANGE;

  c = w->glw_focused;
  if(c != NULL && c->glw_originating_prop != NULL) {
    r = eval_alloc(self, ec, TOKEN_PROPERTY_REF);
    r->t_prop = prop_ref_inc(c->glw_originating_prop);
    eval_push(ec, r);
    return 0;
  }

  r = eval_alloc(self, ec, TOKEN_VOID);
  eval_push(ec, r);
  return 0;
}




/**
 * Return caption from the given widget
 */
static int 
glwf_getCaption(glw_view_eval_context_t *ec, struct token *self,
		  token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *r;
  glw_t *w;
  char buf[100];

  if((a = token_resolve(ec, a)) == NULL)
    return -1;

  if(a != NULL && a->type == TOKEN_STRING) {
    w = glw_find_neighbour(ec->w, rstr_get(a->t_rstring));

    if(w != NULL && !glw_get_text(w, buf, sizeof(buf))) {
      r = eval_alloc(self, ec, TOKEN_STRING);
      r->t_rstring = rstr_alloc(buf);
      eval_push(ec, r);
      return 0;
    }
  }

  r = eval_alloc(self, ec, TOKEN_VOID);
  eval_push(ec, r);
  return 0;
}



/**
 * 
 */
static int 
glwf_bind(glw_view_eval_context_t *ec, struct token *self,
		  token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *t;
  const char *propname[16];
  int i;

  if(a != NULL && a->type == TOKEN_PROPERTY_VALUE_NAME) {

    for(i = 0, t = a; t != NULL && i < 15; t = t->child)
      propname[i++]  = rstr_get(t->t_rstring);
    propname[i] = NULL;

    if(ec->w->glw_class->gc_bind_to_property != NULL)
      ec->w->glw_class->gc_bind_to_property(ec->w,
					    ec->prop, propname,
					    ec->prop_viewx, ec->prop_args,
					    ec->prop_clone);

  } else if(a != NULL && a->type == TOKEN_STRING) {
    glw_set(ec->w, GLW_ATTRIB_BIND_TO_ID, rstr_get(a->t_rstring), NULL);

  } else {
    if(ec->w->glw_class->gc_bind_to_property != NULL)
      ec->w->glw_class->gc_bind_to_property(ec->w,
					    NULL, NULL, NULL, NULL, NULL);
  }
  return 0;
}


/**
 *
 */
typedef struct glwf_delta_extra {
  prop_t *p;
  float f;
} glwf_delta_extra_t;


/**
 * 
 */
static int 
glwf_delta(glw_view_eval_context_t *ec, struct token *self,
	   token_t **argv, unsigned int argc)
{
  glwf_delta_extra_t *de = self->t_extra;
  token_t *a = argv[0], *b = argv[1];
  float f;
  prop_t *p;

  if((a = resolve_property_name2(ec, a)) == NULL)
    return -1;

  if((b = token_resolve(ec, b)) == NULL)
    return -1;

  switch(b->type) {
  case TOKEN_FLOAT:
    f = b->t_float;
    break;
  case TOKEN_INT:
    f = b->t_int;
    break;
  case TOKEN_STRING:
    f = strlen(rstr_get(b->t_rstring)) > 0;
    break;
  case TOKEN_LINK:
    f = strlen(rstr_get(b->t_link_rtitle)) > 0;
    break;
  default:
    f = 0;
    break;
  }

  p = prop_ref_inc(a->t_prop);
 
  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_KEEP;

  if(p == de->p && de->f + f == 0) {
    prop_ref_dec(p);
    return 0;
  }

  prop_add_float(p, f);

  if(de->p) {
    prop_add_float(de->p, de->f);
    prop_ref_dec(de->p);
  }

  de->f = -f;
  de->p = p;

  return 0;
}


/**
 *
 */
static void
glwf_delta_ctor(struct token *self)
{
  self->t_extra = calloc(1, sizeof(glwf_delta_extra_t));
}


/**
 *
 */
static void
glwf_delta_dtor(struct token *self)
{
  glwf_delta_extra_t *de = self->t_extra;

  if(de->p) {
    prop_add_float(de->p, de->f);
    prop_ref_dec(de->p);
  }

  free(de);
}


/**
 * Return 1 if the current widget is visible (rendered)
 */
static int 
glwf_isVisible(glw_view_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)
{
  token_t *r;

  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_WIDGET_META;

  r = eval_alloc(self, ec, TOKEN_INT);

  r->t_int = ec->w->glw_flags & GLW_ACTIVE ? 1 : 0;
  eval_push(ec, r);
  return 0;
}



/**
 * Return 1 if the current widget is can to be scrolled / moved
 */
static int 
glwf_canScroll(glw_view_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)
{
  token_t *r;

  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_WIDGET_META;

  r = eval_alloc(self, ec, TOKEN_INT);

  r->t_int = ec->w->glw_flags & GLW_CAN_SCROLL ? 1 : 0;
  eval_push(ec, r);
  return 0;
}


/**
 * Return 1 if the current widget wants full screen
 */
static int 
glwf_wantFullWindow(glw_view_eval_context_t *ec, struct token *self,
		    token_t **argv, unsigned int argc)
{
  token_t *r;

  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_WIDGET_META;

  r = eval_alloc(self, ec, TOKEN_INT);

  r->t_int = ec->w->glw_flags & GLW_CONSTRAINT_F ? 1 : 0;
  eval_push(ec, r);
  return 0;
}


/**
 * Evals the first arg, if true, the second arg is returned. 
 * Otherwise the third arg is returned.
 * Equivivalent to the C ?: operator
 */
static int 
glwf_select(glw_view_eval_context_t *ec, struct token *self,
	    token_t **argv, unsigned int argc)
{
  token_t *a, *b, *c;

  if((a = token_resolve(ec, argv[0])) == NULL)
    return -1;
  if((b = token_resolve(ec, argv[1])) == NULL)
    return -1;
  if((c = token_resolve(ec, argv[2])) == NULL)
    return -1;

  eval_push(ec, token2bool(a) ? b : c);
  return 0;
}


/**
 * TRACE() the second argument, prefixed with the first (which must
 * be a string).
 */
static int 
glwf_trace(glw_view_eval_context_t *ec, struct token *self,
	   token_t **argv, unsigned int argc)
{
  token_t *a, *b;

  ec->debug++;

  if((a = token_resolve(ec, argv[0])) == NULL ||
     (b = token_resolve(ec, argv[1])) == NULL) {
    ec->debug--;
    return -1;
  }
  ec->debug--;

  if(a->type != TOKEN_STRING)
    return 0;

  switch(b->type) {
  case TOKEN_LINK:
  case TOKEN_STRING:
  case TOKEN_IDENTIFIER:
    TRACE(TRACE_DEBUG, "GLW", "%s: %s", rstr_get(a->t_rstring), 
	  rstr_get(b->t_rstring));
    break;
  case TOKEN_FLOAT:
    TRACE(TRACE_DEBUG, "GLW", "%s: %f", rstr_get(a->t_rstring), b->t_float);
    break;
  case TOKEN_INT:
    TRACE(TRACE_DEBUG, "GLW", "%s: %d", rstr_get(a->t_rstring), b->t_int);
    break;
  case TOKEN_VOID:
    TRACE(TRACE_DEBUG, "GLW", "%s: (void)", rstr_get(a->t_rstring), b->t_int);
    break;
  default:
    TRACE(TRACE_DEBUG, "GLW", "%s: ???", rstr_get(a->t_rstring));
    break;
  }
  return 0;
}


/**
 *
 */
typedef struct glwf_browse_extra {
  prop_t *p;
  rstr_t *url;
} glwf_browse_extra_t;

/**
 *
 */
static void
glwf_browse_ctor(struct token *self)
{
  self->t_extra = calloc(1, sizeof(glwf_browse_extra_t));
}


/**
 *
 */
static void
glwf_browse_dtor(struct token *self)
{
  glwf_browse_extra_t *be = self->t_extra;

  if(be->p)
    prop_destroy(be->p);

  rstr_release(be->url);
  free(be);
}


/**
 * 
 */
static int 
glwf_browse(glw_view_eval_context_t *ec, struct token *self,
	   token_t **argv, unsigned int argc)
{
  token_t *a = argv[0], *r;
  char errbuf[100];
  rstr_t *url;
  glwf_browse_extra_t *be = self->t_extra;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;

  switch(a->type) {
  case TOKEN_STRING:
    url = a->t_rstring;
    break;

  case TOKEN_LINK:
    url = a->t_link_rurl;
    break;

  default:
    return glw_view_seterr(ec->ei, a, "browse(): Invalid first arg (%s)",
			   token2name(a));
  }

  if(be->url == NULL || strcmp(rstr_get(be->url), rstr_get(url))) {
    rstr_release(be->url);
    be->url = NULL;

    if(be->p)
      prop_destroy(be->p);

    be->p = prop_create_root(NULL);

    if(backend_open(be->p, rstr_get(url))) {
      prop_destroy(be->p);
      be->p = NULL;
      return glw_view_seterr(ec->ei, a, "browse(%s): %s", 
			     rstr_get(url), errbuf);
    }
    be->url = rstr_dup(url);
  }

  r = eval_alloc(self, ec, TOKEN_PROPERTY_REF);
  r->t_prop = prop_ref_inc(be->p);
  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_KEEP;
  eval_push(ec, r);
  return 0;
}

typedef struct {
  prop_t *title;
  setting_t *s;

} glwf_setting_t;





/**
 *
 */
static void
glwf_null_ctor(struct token *self)
{
  self->t_extra = NULL;
}

/**
 *
 */
static void
glwf_setting_dtor(struct token *self)
{
  glwf_setting_t *gs = self->t_extra;
  if(gs == NULL)
    return;

  setting_destroy(gs->s);
  prop_destroy(gs->title);
  free(gs);
}


/**
 *
 */
static int
glw_settingInt(glw_view_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)
{
  token_t *title = argv[0];
  token_t *id    = argv[1];
  token_t *def   = argv[2];
  token_t *unit  = argv[3];
  token_t *min   = argv[4];
  token_t *max   = argv[5];
  token_t *step  = argv[6];
  token_t *prop  = argv[7];

  glw_root_t *gr = ec->w->glw_root;
  glwf_setting_t *gs = self->t_extra;


  if((title = token_resolve(ec, title)) == NULL)
    return -1;
  if((def  = token_resolve(ec, def)) == NULL)
    return -1;
  if((unit = token_resolve(ec, unit)) == NULL)
    return -1;
  if((min  = token_resolve(ec, min)) == NULL)
    return -1;
  if((max  = token_resolve(ec, max)) == NULL)
    return -1;
  if((step = token_resolve(ec, step)) == NULL)
    return -1;

  if(title->type != TOKEN_STRING)
    return glw_view_seterr(ec->ei, title, "Title argument is not a string");
  
  if(id->type != TOKEN_STRING)
    return glw_view_seterr(ec->ei, id, "ID argument is not a string");
  
  if(unit->type != TOKEN_STRING)
    return glw_view_seterr(ec->ei, unit, "Unit argument is not a string");

  
  if(gs == NULL) {
    if(resolve_property_name(ec, prop, 0))
      return -1;

    gs = malloc(sizeof(glwf_setting_t));
    gs->title = prop_create_root(NULL);

    gs->s = 
      settings_create_int(gr->gr_settings, rstr_get(id->t_rstring),
			  gs->title, 
			  token2int(def), gr->gr_settings_store, 
			  token2int(min), token2int(max), token2int(step),
			  NULL, NULL,
			  SETTINGS_INITIAL_UPDATE, rstr_get(unit->t_rstring),
			  gr->gr_courier,
			  glw_settings_save, gr);
    
    prop_link(settings_get_value(gs->s), prop->t_prop);
    self->t_extra = gs;
  }
  prop_set_rstring(gs->title, title->t_rstring);

  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_KEEP;
  return 0;
}

/**
 *
 */
static int
glw_settingBool(glw_view_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)
{
  token_t *title = argv[0];
  token_t *id    = argv[1];
  token_t *def   = argv[2];
  token_t *prop  = argv[3];

  glw_root_t *gr = ec->w->glw_root;
  glwf_setting_t *gs = self->t_extra;

  if((title = token_resolve(ec, title)) == NULL)
    return -1;
  if((def  = token_resolve(ec, def)) == NULL)
    return -1;

  if(title->type != TOKEN_STRING)
    return glw_view_seterr(ec->ei, title, "First argument is not a string");
  
  if(id->type != TOKEN_STRING)
    return glw_view_seterr(ec->ei, id, "Second argument is not a string");

  if(gs == NULL) {
    if(resolve_property_name(ec, prop, 0))
      return -1;

    gs = malloc(sizeof(glwf_setting_t));
    gs->title = prop_create_root(NULL);

    gs->s = 
      settings_create_bool(gr->gr_settings, rstr_get(id->t_rstring),
			   gs->title,
			   token2int(def), gr->gr_settings_store, 
			   NULL, NULL,
			   SETTINGS_INITIAL_UPDATE,
			   gr->gr_courier,
			   glw_settings_save, gr);
    
    prop_link(settings_get_value(gs->s), prop->t_prop);
    self->t_extra = gs;
  }
  prop_set_rstring(gs->title, title->t_rstring);

  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_KEEP;
  return 0;
}


/**
 * Return true if the passed argument is a link
 */
static int 
glwf_isLink(glw_view_eval_context_t *ec, struct token *self,
	    token_t **argv, unsigned int argc)
{
  token_t *a, *r;
  if((a = token_resolve(ec, argv[0])) == NULL)
    return -1;
  
  r = eval_alloc(self, ec, TOKEN_INT);
  r->t_int = a->type == TOKEN_LINK;
  eval_push(ec, r);
  return 0;
}


/**
 * sin(x)
 */
static int 
glwf_sin(glw_view_eval_context_t *ec, struct token *self,
	 token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *r;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;

  r = eval_alloc(self, ec, TOKEN_FLOAT);
  r->t_float = a->type == TOKEN_FLOAT ?  sin(a->t_float) : 0;
  eval_push(ec, r);
  return 0;
}


/**
 * 
 */
static int 
glwf_monotime(glw_view_eval_context_t *ec, struct token *self,
	      token_t **argv, unsigned int argc)
{
  token_t *r;
  glw_root_t *gr = ec->w->glw_root;
  double d = gr->gr_frames * (double)gr->gr_frameduration / 1000000.0;

  r = eval_alloc(self, ec, TOKEN_FLOAT);
  r->t_float = d;
  eval_push(ec, r);
  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_EVERY_FRAME;
  return 0;
}


typedef struct glwf_delay_extra {

  float oldval;
  float curval;
  int counter;

} glwf_delay_extra_t;


/**
 *
 */
static int 
glwf_delay(glw_view_eval_context_t *ec, struct token *self,
	   token_t **argv, unsigned int argc)

{
  token_t *a, *b, *c, *r;
  float f;
  glwf_delay_extra_t *e = self->t_extra;

  if((a = token_resolve(ec, argv[0])) == NULL)
    return -1;
  if((b = token_resolve(ec, argv[1])) == NULL)
    return -1;
  if((c = token_resolve(ec, argv[2])) == NULL)
    return -1;

  f = token2float(a);
  if(f != e->curval) {
    // trig
    e->oldval = e->curval;
    e->counter = token2float(f >= e->curval ? b : c) * 1000000.0 / 
      ec->w->glw_root->gr_frameduration;
    e->curval = f;
  }
  
  if(e->counter > 0) {
    f = e->oldval;
    e->counter--;
    ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_EVERY_FRAME;
  } else {
    eval_push(ec, a);
    return 0;
  }

  r = eval_alloc(self, ec, TOKEN_FLOAT);
  r->t_float = f;
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static void
glwf_delay_ctor(struct token *self)
{
  self->t_extra = calloc(1, sizeof(glwf_changed_extra_t));
}


/**
 *
 */
static void
glwf_delay_dtor(struct token *self)
{
  free(self->t_extra);
}


/**
 * Return 1 if the current widget is ready
 */
static int 
glwf_isReady(glw_view_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)
{
  token_t *r = eval_alloc(self, ec, TOKEN_INT);
  glw_t *w = ec->w;
  
 
  if(w->glw_class->gc_ready == NULL || w->glw_class->gc_ready(w)) {
    r->t_int = 1;
  } else {
    r->t_int = 0;
    ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_WIDGET_META;
  }
  eval_push(ec, r);
  return 0;
}


/**
 * Return focus distance
 */
static int 
glwf_focusDistance(glw_view_eval_context_t *ec, struct token *self,
		   token_t **argv, unsigned int argc)
{
  token_t *r = eval_alloc(self, ec, TOKEN_INT);
  glw_t *w = ec->w;
  r->t_int = w->glw_focus_distance;
  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_WIDGET_META;
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static int 
glwf_suggestFocus(glw_view_eval_context_t *ec, struct token *self,
		  token_t **argv, unsigned int argc)
{
  token_t *a;

  if((a = token_resolve(ec, argv[0])) == NULL)
    return -1;
  
  if(token2bool(a))
    glw_focus_suggest(ec->w);
  return 0;
}

/**
 *
 */
static int
glwf_count(glw_view_eval_context_t *ec, struct token *self,
		     token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];

  if((a = token_resolve_ex(ec, a, GPS_COUNTER)) == NULL)
    return -1;
  eval_push(ec, a);
  return 0;
}


/**
 *
 */
static void
glwf_propGrouper_dtor(struct token *self)
{
  if(self->t_extra != NULL)
    prop_grouper_destroy(self->t_extra);
}



/**
 *
 */
static int
glwf_propGrouper(glw_view_eval_context_t *ec, struct token *self,
		 token_t **argv, unsigned int argc)
{
  token_t *a, *b, *r;

  if((a = resolve_property_name2(ec, argv[0])) == NULL)
    return -1;
  if((b = token_resolve(ec, argv[1])) == NULL)
    return -1;

  if(b->type != TOKEN_STRING)
    return glw_view_seterr(ec->ei, a, "propGrouper(): "
			   "Second argument is not a string");
  
  if(self->t_extra != NULL)
    prop_grouper_destroy(self->t_extra);

  r = eval_alloc(self, ec, TOKEN_PROPERTY_REF);
  r->t_prop = prop_ref_inc(prop_create_root(NULL));
  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_KEEP;
  eval_push(ec, r);

  self->t_extra = prop_grouper_create(r->t_prop, a->t_prop,
				      rstr_get(b->t_rstring),
				      PROP_GROUPER_TAKE_DST_OWNERSHIP);
  return 0;
}


/**
 *
 */
static void
glwf_propSorter_dtor(struct token *self)
{
  if(self->t_extra != NULL)
    prop_nf_release(self->t_extra);
}


/**
 *
 */
static int
glwf_propSorter(glw_view_eval_context_t *ec, struct token *self,
		token_t **argv, unsigned int argc)
{
  token_t *a, *b, *r;

  if((a = resolve_property_name2(ec, argv[0])) == NULL)
    return -1;
  if((b = token_resolve(ec, argv[1])) == NULL)
    return -1;

  if(b->type != TOKEN_STRING)
    return glw_view_seterr(ec->ei, a, "propSorter(): "
			   "Second argument is not a string");
  
  if(self->t_extra != NULL)
    prop_nf_release(self->t_extra);

  r = eval_alloc(self, ec, TOKEN_PROPERTY_REF);
  r->t_prop = prop_ref_inc(prop_create_root(NULL));
  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_KEEP;
  eval_push(ec, r);

  self->t_extra = prop_nf_create(r->t_prop, a->t_prop, NULL,
				 rstr_get(b->t_rstring),
				 PROP_NF_TAKE_DST_OWNERSHIP);
  return 0;
}


/**
 *
 */
static int
glwf_getLayer(glw_view_eval_context_t *ec, struct token *self,
	      token_t **argv, unsigned int argc)
{
  token_t *r;

  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_EVERY_FRAME;

  r = eval_alloc(self, ec, TOKEN_INT);

  if(ec->rc == NULL) {
    r->t_int = 0;
  } else {
    r->t_int = ec->rc->rc_layer;
  }
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static int
glwf_getWidth(glw_view_eval_context_t *ec, struct token *self,
	      token_t **argv, unsigned int argc)
{
  token_t *r;
  glw_t *w = ec->w;

  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_EVERY_FRAME;

  if(w->glw_flags & GLW_CONSTRAINT_X) {
    r = eval_alloc(self, ec, TOKEN_INT);
    r->t_int = w->glw_req_size_x;
  } else if(ec->rc == NULL) {
    r = eval_alloc(self, ec, TOKEN_VOID);
  } else {
    r = eval_alloc(self, ec, TOKEN_INT);
    r->t_int = ec->rc->rc_width;
  }
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static int
glwf_getHeight(glw_view_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)
{
  token_t *r;
  glw_t *w = ec->w;

  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_EVAL_EVERY_FRAME;

  if(w->glw_flags & GLW_CONSTRAINT_X) {
    r = eval_alloc(self, ec, TOKEN_INT);
    r->t_int = w->glw_req_size_y;
  } else if(ec->rc == NULL) {
    r = eval_alloc(self, ec, TOKEN_VOID);
  } else {
    r = eval_alloc(self, ec, TOKEN_INT);
    r->t_int = ec->rc->rc_height;
  }
  eval_push(ec, r);
  return 0;
}


/**
 * 
 */
static int 
glwf_preferTentative(glw_view_eval_context_t *ec, struct token *self,
		     token_t **argv, unsigned int argc)
{
  token_t *a = argv[0], *r;
  int how;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;

  switch(a->type) {
  case TOKEN_FLOAT:
    how = a->t_float_how;
    break;
  default:
    how = 0;
    break;
  }

  switch(how) {
  case PROP_SET_NORMAL:
    r = self->t_extra ?: a;
    break;

  case PROP_SET_TENTATIVE:
    if(self->t_extra != NULL)
      glw_view_token_free(self->t_extra);
    self->t_extra = r = glw_view_token_copy(a);
    break;

  case PROP_SET_COMMIT:
    if(self->t_extra != NULL)
      glw_view_token_free(self->t_extra);
    self->t_extra = NULL;
    r = a;
    break;
  default:
    abort();
  }

  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_KEEP;
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static void
glwf_freetoken_dtor(struct token *self)
{
  if(self->t_extra != NULL)
    glw_view_token_free(self->t_extra);
}



/**
 * 
 */
static int 
glwf_ignoreTentative(glw_view_eval_context_t *ec, struct token *self,
		     token_t **argv, unsigned int argc)
{
  token_t *a = argv[0], *r;
  int how;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;

  switch(a->type) {
  case TOKEN_FLOAT:
    how = a->t_float_how;
    break;
  default:
    how = 0;
    break;
  }

  switch(how) {
  case PROP_SET_NORMAL:
  case PROP_SET_COMMIT:
    if(self->t_extra != NULL)
      glw_view_token_free(self->t_extra);
    self->t_extra = r = glw_view_token_copy(a);
    break;

  case PROP_SET_TENTATIVE:
    r = self->t_extra ?: a;
    break;
  default:
    abort();
  }

  ec->dynamic_eval |= GLW_VIEW_DYNAMIC_KEEP;
  eval_push(ec, r);
  return 0;
}


/**
 * Cast to int
 */
static int 
glwf_int(glw_view_eval_context_t *ec, struct token *self,
	     token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *r;
  
  if((a = token_resolve(ec, a)) == NULL)
    return -1;
  
  r = eval_alloc(self, ec, TOKEN_INT);
  r->t_int = token2int(a);
  eval_push(ec, r);
  return 0;
}



/**
 * Clamp
 */
static int 
glwf_clamp(glw_view_eval_context_t *ec, struct token *self,
	   token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *b = argv[1];
  token_t *c = argv[2];
  token_t *r;
  
  if((a = token_resolve(ec, a)) == NULL)
    return -1;
  if((b = token_resolve(ec, b)) == NULL)
    return -1;
  if((c = token_resolve(ec, c)) == NULL)
    return -1;

  switch(a->type) {
  case TOKEN_INT:
    r = eval_alloc(self, ec, TOKEN_INT);
    r->t_int = GLW_CLAMP(a->t_int, token2int(b), token2int(c));
    break;
  case TOKEN_FLOAT:
    r = eval_alloc(self, ec, TOKEN_FLOAT);
    r->t_float = GLW_CLAMP(a->t_float, token2float(b), token2float(c));
    break;
  default:
    r = eval_alloc(self, ec, TOKEN_VOID);
    break;
  }
  eval_push(ec, r);
  return 0;
}


static int 
glwf_join(glw_view_eval_context_t *ec, struct token *self,
	  token_t **argv, unsigned int argc)
{
  token_t *sep, *t;
  int i;
  char *ret = NULL;
  const char *s = NULL;
  
  if(argc < 2)
    return glw_view_seterr(ec->ei, self,
			   "join() requires at least two arguments");
  
  if((sep = token_resolve(ec, argv[0])) == NULL)
    return -1;
  
  if(sep->type != TOKEN_STRING)
    return glw_view_seterr(ec->ei, sep,
			   "first arg (separator) must be a string");

  for(i = 1; i < argc; i++)  {
    if((t = token_resolve(ec, argv[i])) == NULL)
      continue;
    if(t->type != TOKEN_STRING)
      continue;
    if(s != NULL)
      strappend(&ret, s);
    strappend(&ret, rstr_get(t->t_rstring));
    if(s == NULL)
      s = rstr_get(sep->t_rstring);
  }
  token_t *r = eval_alloc(self, ec, TOKEN_STRING);
  r->t_rstring = rstr_alloc(ret);
  free(ret);
  eval_push(ec, r);
  return 0;
}

/**
 *
 */
static const token_func_t funcvec[] = {
  {"widget", 2, glwf_widget},
  {"cloner", 3, glwf_cloner},
  {"space", 1, glwf_space},
  {"onEvent", -1, glwf_onEvent},
  {"navOpen", -1, glwf_navOpen},
  {"playTrackFromSource", 2, glwf_playTrackFromSource},
  {"enqueuetrack", 1, glwf_enqueueTrack},
  {"selectAudioTrack", 1, glwf_selectAudioTrack},
  {"selectSubtitleTrack", 1, glwf_selectSubtitleTrack},
  {"targetedEvent", 2, glwf_targetedEvent},
  {"fireEvent", 1, glwf_fireEvent},
  {"event", 1, glwf_event},
  {"changed", -1, glwf_changed, glwf_changed_ctor, glwf_changed_dtor},
  {"iir", -1, glwf_iir},
  {"scurve", 2, glwf_scurve, glwf_scurve_ctor, glwf_scurve_dtor},
  {"translate", -1, glwf_translate},
  {"strftime", 2, glwf_strftime},
  {"isSet", 1, glwf_isset},
  {"value2duration", 1, glwf_value2duration},
  {"createChild", 1, glwf_createchild},
  {"delete", 1, glwf_delete},
  {"isFocused", 0, glwf_isFocused},
  {"isHovered", 0, glwf_isHovered},
  {"isPressed", 0, glwf_isPressed},
  {"focusedChild", 0, glwf_focusedChild},
  {"getCaption", 1, glwf_getCaption},
  {"bind", 1, glwf_bind},
  {"delta", 2, glwf_delta, glwf_delta_ctor, glwf_delta_dtor},
  {"isVisible", 0, glwf_isVisible},
  {"canScroll", 0, glwf_canScroll},
  {"wantFullWindow", 0, glwf_wantFullWindow},
  {"select", 3, glwf_select},
  {"trace", 2, glwf_trace},
  {"browse", 1, glwf_browse, glwf_browse_ctor, glwf_browse_dtor},
  {"settingInt", 8, glw_settingInt, glwf_null_ctor, glwf_setting_dtor},
  {"settingBool", 4, glw_settingBool, glwf_null_ctor, glwf_setting_dtor},
  {"isLink", 1, glwf_isLink},
  {"sin", 1, glwf_sin},
  {"monotime", 0, glwf_monotime},
  {"delay", 3, glwf_delay, glwf_delay_ctor, glwf_delay_dtor},
  {"isReady", 0, glwf_isReady},
  {"suggestFocus", 1, glwf_suggestFocus},
  {"focusDistance", 0, glwf_focusDistance},
  {"count", 1, glwf_count},
  {"deliverEvent", -1, glwf_deliverEvent},
  {"propGrouper", 2, glwf_propGrouper, glwf_null_ctor, glwf_propGrouper_dtor},
  {"propSorter", 2, glwf_propSorter, glwf_null_ctor, glwf_propSorter_dtor},
  {"getLayer", 0, glwf_getLayer},
  {"getWidth", 0, glwf_getWidth},
  {"getHeight", 0, glwf_getHeight},
  {"preferTentative", 1, glwf_preferTentative, glwf_null_ctor, glwf_freetoken_dtor},
  {"ignoreTentative", 1, glwf_ignoreTentative, glwf_null_ctor, glwf_freetoken_dtor},
  {"int", 1,glwf_int},
  {"clamp", 3, glwf_clamp},
  {"join", -1, glwf_join},
  {"fmt", -1, glwf_fmt},
};


/**
 *
 */
int 
glw_view_function_resolve(token_t *t)
{
  int i;

  for(i = 0; i < sizeof(funcvec) / sizeof(funcvec[0]); i++)
    if(!strcmp(funcvec[i].name, rstr_get(t->t_rstring))) {
      rstr_release(t->t_rstring);
      t->t_func = &funcvec[i];
      t->type = TOKEN_FUNCTION;
      if(t->t_func->ctor != NULL)
	t->t_func->ctor(t);
      return 0;
    }
  return -1;
}
