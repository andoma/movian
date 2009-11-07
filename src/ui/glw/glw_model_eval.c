/*
 *  GL Widgets, model loader, evaluator
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
#include "glw_model.h"
#include "glw.h"
#include "glw_event.h"
#include "navigator.h"


/**
 *
 */
typedef struct glw_prop_sub_pending {
  prop_t *gpsp_prop;
  TAILQ_ENTRY(glw_prop_sub_pending) gpsp_link;
} glw_prop_sub_pending_t;

TAILQ_HEAD(glw_prop_sub_pending_queue, glw_prop_sub_pending);

/**
 *
 */
typedef struct glw_prop_sub {
  LIST_ENTRY(glw_prop_sub) gps_link;
  glw_t *gps_widget;

  prop_sub_t *gps_sub;

  token_t *gps_rpn;

  token_t *gps_token;

  token_t *gps_cloner_body;
  glw_class_t gps_cloner_class;
  
  struct glw_prop_sub_pending_queue gps_pending;
  prop_t *gps_pending_select;

#ifdef GLW_MODEL_ERRORINFO
  refstr_t *gps_file;
  int gps_line;
#endif

  prop_t *gps_originating_prop;

} glw_prop_sub_t;


static int subscribe_prop(glw_model_eval_context_t *ec, struct token *self);

/**
 *
 */
void
glw_prop_subscription_destroy_list(struct glw_prop_sub_list *l)
{
  glw_prop_sub_t *gps;

  while((gps = LIST_FIRST(l)) != NULL) {

    gps->gps_sub->hps_opaque = NULL;

    prop_unsubscribe(gps->gps_sub);

    if(gps->gps_token != NULL)
      glw_model_token_free(gps->gps_token);

    if(gps->gps_cloner_body != NULL)
      glw_model_token_free(gps->gps_cloner_body);
    
    LIST_REMOVE(gps, gps_link);

    if(gps->gps_originating_prop)
      prop_ref_dec(gps->gps_originating_prop);

    free(gps);
  }
}




static void eval_dynamic(glw_t *w, token_t *rpn);

static int glw_model_eval_rpn0(token_t *t0, glw_model_eval_context_t *ec);

/**
 *
 */
static void
eval_push(glw_model_eval_context_t *ec, token_t *t)
{
  t->tmp = ec->stack;
  ec->stack = t;
}


/**
 *
 */
static token_t *
eval_pop(glw_model_eval_context_t *ec)
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
eval_alloc_sized(token_t *src, glw_model_eval_context_t *ec, 
		 token_type_t type, size_t size)
{
  token_t *r = calloc(1, size);

#ifdef GLW_MODEL_ERRORINFO
  if(src->file != NULL)
    r->file = refstr_dup(src->file);
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
eval_alloc(token_t *src, glw_model_eval_context_t *ec, token_type_t type)
{
  return eval_alloc_sized(src, ec, type, sizeof(token_t));
}


/**
 *
 */
static token_t *
token_resolve(glw_model_eval_context_t *ec, token_t *t)
{
  glw_prop_sub_t *gps;

  if(t == NULL) {
    glw_model_seterr(ec->ei, t, "Missing operand");
    return NULL;
  }

  if(t->type == TOKEN_PROPERTY_NAME && subscribe_prop(ec, t))
      return NULL;
  
  if(t->type == TOKEN_PROPERTY_SUBSCRIPTION) {
    ec->dynamic_eval |= GLW_MODEL_DYNAMIC_EVAL_PROP;

    gps = t->propsubr;

    if((t = gps->gps_token) == NULL)
      t = eval_alloc(t, ec, TOKEN_VOID);
  }
  return t;
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
eval_op(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *b = eval_pop(ec), *a = eval_pop(ec), *r;
  float (*f_fn)(float, float);
  int   (*i_fn)(int, int);
  int i, al, bl;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;
  if((b = token_resolve(ec, b)) == NULL)
    return -1;

  switch(self->type) {
  case TOKEN_ADD:
    if(a->type == TOKEN_STRING && b->type == TOKEN_STRING) {
      /* Concatenation of strings */
      al = strlen(a->t_string);
      bl = strlen(b->t_string);

      r = eval_alloc(self, ec, TOKEN_STRING);
      r->t_string = malloc(al + bl + 1);
      memcpy(r->t_string,      a->t_string, al);
      memcpy(r->t_string + al, b->t_string, bl);
      r->t_string[al + bl] = 0;
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
      return glw_model_seterr(ec->ei, self, 
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
token2bool(token_t *t)
{
  switch(t->type) {
  case TOKEN_VOID:
    return 0;
  case TOKEN_INT:
    return !!t->t_int;
  case TOKEN_FLOAT:
    return t->t_float > 0.5;
  case TOKEN_IDENTIFIER:
    return !strcmp(t->t_string, "true");
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
eval_bool_op(glw_model_eval_context_t *ec, struct token *self)
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
eval_eq(glw_model_eval_context_t *ec, struct token *self, int neq)
{
  token_t *b = eval_pop(ec), *a = eval_pop(ec), *r;
  int rr;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;
  if((b = token_resolve(ec, b)) == NULL)
    return -1;

  if(a->type != b->type) {
    rr = 0;
  } else {

    switch(a->type) {
    case TOKEN_INT:
      rr = a->t_int == b->t_int;
      break;
    case TOKEN_FLOAT:
      rr = a->t_float == b->t_float;
      break;
    case TOKEN_STRING:
      rr = !strcmp(a->t_string, b->t_string);
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
eval_array(glw_model_eval_context_t *pec, token_t *t0)
{
  token_t *t, *out = NULL;
  int nelem = 0, n = 0;
  token_type_t vt;

  glw_model_eval_context_t ec;

  for(t = t0->child; t != NULL; t = t->next)
    nelem++;
  

  memset(&ec, 0, sizeof(ec));
  ec.debug = pec->debug;
  ec.ei = pec->ei;
  ec.prop0 = pec->prop0;
  ec.prop_parent = pec->prop_parent;
  ec.rpn = pec->rpn;
  ec.gr = pec->gr;
  ec.passive_subscriptions = pec->passive_subscriptions;
  ec.sublist = pec->sublist;
  ec.event = pec->event;

  for(t = t0->child; t != NULL; t = t->next) {
    ec.alloc = NULL;

    if(glw_model_eval_rpn0(t, &ec))
      goto err;

    if(ec.stack == NULL) {
      glw_model_seterr(pec->ei, t, "Missing vector component");
      goto err;
    }

    if(ec.stack->next != NULL) {
      glw_model_seterr(pec->ei, t, "Vector component not singular");
      goto err;
    }

    switch(ec.stack->type) {
    case TOKEN_FLOAT:
      vt = TOKEN_VECTOR_FLOAT;
      break;
      
    case TOKEN_STRING:
      vt = TOKEN_VECTOR_STRING;
      break;

    case TOKEN_INT:
      vt = TOKEN_VECTOR_INT;
      break;
      
    default:
      glw_model_seterr(pec->ei, t, "Invalid vector component (%d)", 
		       ec.stack->type);
      goto err;
    }

    if(out == NULL) {
      out = eval_alloc_sized(t0, pec, vt, sizeof(token_t) + 
			     sizeof(t->u) * (nelem - 1));
      out->t_elements = nelem;
    } else if(vt != out->type) {
      glw_model_seterr(pec->ei, t, "Hetrogenous vectors are invalid");
      goto err;
    }

    if(out->type == TOKEN_VECTOR_STRING) {
      out->t_string_vector[n] = strdup(ec.stack->t_string);
    } else if(out->type == TOKEN_VECTOR_INT) {
      out->t_int_vector[n] = ec.stack->t_int;
    } else {
      out->t_float_vector[n] = ec.stack->t_float;
    }
    n++;
    glw_model_free_chain(ec.alloc);

    pec->dynamic_eval |= ec.dynamic_eval;
  }

  eval_push(pec, out);
  return 0;
  
 err:  
  glw_model_free_chain(ec.alloc);
  return -1;
}


/**
 *
 */
static int
eval_assign(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *b = eval_pop(ec), *a = eval_pop(ec), *t;
  prop_t *p, *ui;
  int r, i;
  const char *propname[16];
  event_keydesc_t *ek;

  /* Catch some special rvalues here */

  if(b->type == TOKEN_PROPERTY_NAME && !strcmp(b->t_string, "event")) {
    /* Assignment from $event, if our eval context has an event use it */
    if(ec->event == NULL || ec->event->e_type_x != EVENT_KEYDESC)
      return 0;
    
    ek = (event_keydesc_t *)ec->event;

    b = eval_alloc(self, ec, TOKEN_STRING);
    b->t_string = strdup(ek->desc);
  } else if((b = token_resolve(ec, b)) == NULL) {
    return -1;
  }


  switch(a->type) {
  case TOKEN_OBJECT_ATTRIBUTE:
    r = a->t_attrib->set(ec, a->t_attrib, b);
    break;

  case TOKEN_PROPERTY_NAME:
    for(i = 0, t = a; t != NULL && i < 15; t = t->child)
      propname[i++]  = t->t_string;
    propname[i] = NULL;

    ui = ec->w ? ec->w->glw_root->gr_uii.uii_prop : NULL;
    p = prop_get_by_name(propname, 0, 
			 PROP_TAG_NAMED_ROOT, ec->prop0, "self",
			 PROP_TAG_NAMED_ROOT, ec->prop_parent, "parent",
			 PROP_TAG_ROOT, ui,
			 NULL);

    if(p == NULL)
      return glw_model_seterr(ec->ei, a, "Unable to resolve property");

    /* Transform TOKEN_PROPERTY_NAME -> TOKEN_PROPERTY */

    glw_model_free_chain(a->child);
    a->child = NULL;
    
    a->type = TOKEN_PROPERTY;
    a->t_prop = p;

  case TOKEN_PROPERTY:
    
    switch(b->type) {
    case TOKEN_STRING:
      prop_set_string(a->t_prop, b->t_string);
      break;
    case TOKEN_INT:
      prop_set_int(a->t_prop, b->t_int);
      break;
    case TOKEN_FLOAT:
      prop_set_float(a->t_prop, b->t_float);
      break;
    case TOKEN_PROPERTY:
      prop_link(b->t_prop, a->t_prop);
      break;
    default:
      prop_set_void(a->t_prop);
      break;
    }
    r = 0;
    break;

  default:
    return glw_model_seterr(ec->ei, self, "Invalid assignment");
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
    eval_dynamic(w, opaque);
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
    eval_dynamic(w, opaque);
  return 0;
}


/**
 *
 */
static int
eval_dynamic_focus_hover_change_sig(glw_t *w, void *opaque, 
				    glw_signal_t signal, void *extra)
{
  if(signal == GLW_SIGNAL_FOCUS_HOVER_PATH_CHANGED)
    eval_dynamic(w, opaque);
  return 0;
}


/**
 *
 */
static int
eval_dynamic_visibility_sig(glw_t *w, void *opaque, 
			    glw_signal_t signal, void *extra)
{
  if(signal == GLW_SIGNAL_ACTIVE || signal == GLW_SIGNAL_INACTIVE)
    eval_dynamic(w, opaque);
  return 0;
}

/**
 *
 */
static void
eval_dynamic(glw_t *w, token_t *rpn)
{
  glw_model_eval_context_t ec;

  memset(&ec, 0, sizeof(ec));
  ec.w = w;
  ec.gr = w->glw_root;

  glw_model_eval_rpn0(rpn, &ec);

  glw_model_free_chain(ec.alloc);

  if(ec.dynamic_eval & GLW_MODEL_DYNAMIC_EVAL_EVERY_FRAME)
    glw_signal_handler_register(w, eval_dynamic_every_frame_sig, rpn, 1000);
  else
    glw_signal_handler_unregister(w, eval_dynamic_every_frame_sig, rpn);

  if(ec.dynamic_eval & GLW_MODEL_DYNAMIC_EVAL_FOCUSED_CHILD_CHANGE)
    glw_signal_handler_register(w, eval_dynamic_focused_child_change_sig,
				rpn, 1000);
  else
    glw_signal_handler_unregister(w, eval_dynamic_focused_child_change_sig,
				  rpn);

  if(ec.dynamic_eval & GLW_MODEL_DYNAMIC_EVAL_FOCUS_HOVER_CHANGE)
    glw_signal_handler_register(w, eval_dynamic_focus_hover_change_sig,
				rpn, 1000);
  else
    glw_signal_handler_unregister(w, eval_dynamic_focus_hover_change_sig,
				  rpn);

  if(ec.dynamic_eval & GLW_MODEL_DYNAMIC_EVAL_VISIBILITY)
    glw_signal_handler_register(w, eval_dynamic_visibility_sig, rpn, 1000);
  else
    glw_signal_handler_unregister(w, eval_dynamic_visibility_sig, rpn);
}


/**
 *
 */
static glw_t *
cloner_find_child(prop_t *p, glw_t *parent)
{
  glw_t *w;

  TAILQ_FOREACH(w, &parent->glw_childs, glw_parent_link)
    if(w->glw_originating_prop == p)
      return w;

  fprintf(stderr, "glw: Cloner searches for unknown child in widget list\n");
  fprintf(stderr, "glw: This is a programming error, bailing out\n");
  abort();
}


/**
 *
 */
static glw_prop_sub_pending_t *
find_in_pendinglist(prop_t *p, glw_prop_sub_t *gps)
{
  glw_prop_sub_pending_t *gpsp;

  TAILQ_FOREACH(gpsp, &gps->gps_pending, gpsp_link)
    if(gpsp->gpsp_prop == p)
      return gpsp;

  fprintf(stderr, "glw: Cloner searches for unknown child in pending list\n");
  fprintf(stderr, "glw: This is a programming error, bailing out\n");
  abort();
}


/**
 *
 */
static void
cloner_add_child0(glw_prop_sub_t *gps, prop_t *p, prop_t *before, 
		  glw_t *parent, errorinfo_t *ei, int flags)
{
  token_t *body;
  glw_model_eval_context_t n;
  glw_t *b;

  body = glw_model_clone_chain(gps->gps_cloner_body);

  b = before ? cloner_find_child(before, parent) : NULL;

  memset(&n, 0, sizeof(n));
  n.prop0 = p;
  n.prop_parent = gps->gps_originating_prop;
  n.ei = ei;
  n.gr = parent->glw_root;

  n.w = glw_create_i(parent->glw_root,
		     gps->gps_cloner_class,
		     GLW_ATTRIB_PARENT_BEFORE, parent, b,
		     GLW_ATTRIB_PROPROOTS, p, gps->gps_originating_prop,
		     GLW_ATTRIB_ORIGINATING_PROP, p,
		     NULL);

  if(flags & PROP_ADD_SELECTED)
    glw_signal0(parent, GLW_SIGNAL_SELECT, n.w);

  n.sublist = &n.w->glw_prop_subscriptions;

  glw_model_eval_block(body, &n);
  glw_model_free_chain(body);

  if(n.gr->gr_last_focused_interactive == p)
    glw_focus_set(n.w->glw_root, n.w, 0);
}


/**
 *
 */
static void
cloner_add_child(glw_prop_sub_t *gps, prop_t *p, prop_t *before,
		 glw_t *parent, errorinfo_t *ei, int flags)
{
  glw_prop_sub_pending_t *gpsp, *b;

  if(gps->gps_cloner_body != NULL) {
    cloner_add_child0(gps, p, before, parent, ei, flags);
    return;
  }

  /*
   * The cloner body has not been evaluated yet so we can not
   * create the child. This happens when we subscribe initially.
   * Put it on a pending list and add it once the cloner has been
   * setup.
   */
  
  b = before ? find_in_pendinglist(before, gps) : NULL;

  gpsp = malloc(sizeof(glw_prop_sub_pending_t));
  gpsp->gpsp_prop = p;
  prop_ref_inc(p);

  if(before) {
    TAILQ_INSERT_BEFORE(b, gpsp, gpsp_link);
  } else {
    TAILQ_INSERT_TAIL(&gps->gps_pending, gpsp, gpsp_link);
  }
  if(flags & PROP_ADD_SELECTED)
    gps->gps_pending_select = p;
}


/**
 *
 */
static void
cloner_move_child0(glw_prop_sub_t *gps, prop_t *p, prop_t *before,
		  glw_t *parent, errorinfo_t *ei)
{
  glw_t *w =          cloner_find_child(p,      parent);
  glw_t *b = before ? cloner_find_child(before, parent) : NULL;

  glw_move(w, b);
}


/**
 *
 */
static void
cloner_move_child(glw_prop_sub_t *gps, prop_t *p, prop_t *before,
		  glw_t *parent, errorinfo_t *ei)
{
  glw_prop_sub_pending_t *t, *b;

  if(gps->gps_cloner_body != NULL) {
    cloner_move_child0(gps, p, before, parent, ei);
    return;
  }

  t =          find_in_pendinglist(p,      gps);
  b = before ? find_in_pendinglist(before, gps) : NULL;

  TAILQ_REMOVE(&gps->gps_pending, t, gpsp_link);

  if(b != NULL) {
    TAILQ_INSERT_BEFORE(b, t, gpsp_link);
  } else {
    TAILQ_INSERT_TAIL(&gps->gps_pending, t, gpsp_link);
  }
}


/**
 *
 */
static void
cloner_del_child(glw_prop_sub_t *gps, prop_t *p, glw_t *parent)
{
  glw_t *w;
  glw_prop_sub_pending_t *gpsp;

  if((w = cloner_find_child(p, parent)) != NULL) {
    glw_detach0(w);
    return;
  }

  // It must be in the pending list
  gpsp = find_in_pendinglist(p, gps);
  if(gps->gps_pending_select == p)
    gps->gps_pending_select = NULL;
  
  prop_ref_dec(p);
  TAILQ_REMOVE(&gps->gps_pending, gpsp, gpsp_link);
  free(gpsp);
}

/**
 *
 */
static void
cloner_select_child(glw_prop_sub_t *gps, prop_t *p, glw_t *parent)
{
  glw_t *w;

  if(p == NULL) {
    glw_signal0(parent, GLW_SIGNAL_SELECT, NULL);
    return;
  }

  if((w = cloner_find_child(p, parent)) != NULL) {
    glw_signal0(parent, GLW_SIGNAL_SELECT, w);
    gps->gps_pending_select = NULL;
    return;
  }

  gps->gps_pending_select = p;
}


/**
 *
 */
static token_t *
prop_callback_alloc_token(glw_prop_sub_t *gps, token_type_t type)
{
  token_t *t = calloc(1, sizeof(token_t));
  t->type = type;

#ifdef GLW_MODEL_ERRORINFO
  if(gps->gps_file != NULL)
    t->file = refstr_dup(gps->gps_file);
  t->line = gps->gps_line;
#endif    
  return t;
}


/**
 * Entry point from htsprop.
 *
 * The prop mutex is not held upon entry.
 *
 */
static void
prop_callback(void *opaque, prop_event_t event, ...)
{
  glw_prop_sub_t *gps;
  prop_t *p, *p2;
  token_t *rpn = NULL, *t = NULL;
  int flags;
  va_list ap;
  va_start(ap, event);

  gps = opaque;

  switch(event) {
  case PROP_SET_VOID:
    t = prop_callback_alloc_token(gps, TOKEN_VOID);
    t->propsubr = gps;
    rpn = gps->gps_rpn;
    break;

  case PROP_SET_STRING:
    t = prop_callback_alloc_token(gps, TOKEN_STRING);
    t->propsubr = gps;
    t->t_string = strdup(va_arg(ap, char *));
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
    rpn = gps->gps_rpn;
    break;

  case PROP_SET_DIR:
    t = prop_callback_alloc_token(gps, TOKEN_DIRECTORY);
    t->propsubr = gps;
    rpn = gps->gps_rpn;
    break;

  case PROP_SET_PIXMAP:
    t = prop_callback_alloc_token(gps, TOKEN_PIXMAP);
    t->propsubr = gps;
    t->t_pixmap = va_arg(ap, prop_pixmap_t *);
    prop_pixmap_ref_inc(t->t_pixmap);
    rpn = gps->gps_rpn;
    break;

  case PROP_ADD_CHILD:
    p = va_arg(ap, prop_t *);
    flags = va_arg(ap, int);
    cloner_add_child(gps, p, NULL, gps->gps_widget, NULL, flags);
    break;

  case PROP_ADD_CHILD_BEFORE:
    p = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    flags = va_arg(ap, int);
    cloner_add_child(gps, p, p2, gps->gps_widget, NULL, flags);
    break;

  case PROP_MOVE_CHILD:
    p = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    cloner_move_child(gps, p, p2, gps->gps_widget, NULL);
    break;

  case PROP_DEL_CHILD:
    p = va_arg(ap, prop_t *);
    cloner_del_child(gps, p, gps->gps_widget);
    break;

  case PROP_SELECT_CHILD:
    p = va_arg(ap, prop_t *);
    cloner_select_child(gps, p, gps->gps_widget);
    break;

  default:
    return;
  }

  if(t != NULL) {
      
    if(gps->gps_token != NULL) {
      glw_model_token_free(gps->gps_token);
      gps->gps_token = NULL;
    }
    gps->gps_token = t;
  }

  if(rpn != NULL) 
    eval_dynamic(gps->gps_widget, rpn);
}



/**
 * Transform a property reference (a chain of names) into
 * a resolved subscription.
 */
static int
subscribe_prop(glw_model_eval_context_t *ec, struct token *self)
{
  glw_prop_sub_t *gps;
  prop_sub_t *s;
  glw_t *w = ec->w;
  int i = 0;
  token_t *t;
  const char *propname[16];

  if(w == NULL) 
    return glw_model_seterr(ec->ei, self, 
			    "Properties can not be mapped in this scope");

  for(t = self; t != NULL && i < 15; t = t->child)
    propname[i++]  = t->t_string;

  propname[i] = NULL;

  gps = calloc(1, sizeof(glw_prop_sub_t));

  gps->gps_originating_prop = ec->prop0;
  if(ec->prop0 != NULL)
    prop_ref_inc(ec->prop0);

  TAILQ_INIT(&gps->gps_pending);

#ifdef GLW_MODEL_ERRORINFO
  gps->gps_file = refstr_dup(self->file);
  gps->gps_line = self->line;
#endif

  s = prop_subscribe(PROP_SUB_DIRECT_UPDATE | (ec->debug ? PROP_SUB_DEBUG : 0),
		     PROP_TAG_CALLBACK, prop_callback, gps,
		     PROP_TAG_NAME_VECTOR, propname,
		     PROP_TAG_COURIER, w->glw_root->gr_courier,
		     PROP_TAG_NAMED_ROOT, ec->prop0, "self",
		     PROP_TAG_NAMED_ROOT, ec->prop_parent, "parent",
		     PROP_TAG_ROOT, w->glw_root->gr_uii.uii_prop,
		     NULL);

  if(s == NULL) {

    if(ec->prop0 != NULL)
      prop_ref_dec(ec->prop0);

    refstr_unref(gps->gps_file);
    free(gps);
    return glw_model_seterr(ec->ei, self, "Property does not exist %p",
			    ec->prop_parent);
  }

  gps->gps_sub = s;

  gps->gps_widget = w;
  LIST_INSERT_HEAD(ec->sublist, gps, gps_link);

  gps->gps_rpn = ec->passive_subscriptions ? NULL : ec->rpn;

  free(self->t_string);
  self->propsubr = gps;

  self->type = TOKEN_PROPERTY_SUBSCRIPTION;

  glw_model_free_chain(self->child);
  self->child = NULL;
  return 0;
}


/**
 *
 */
static int
invoke_func(glw_model_eval_context_t *ec, token_t *t)
{
  token_t **vec;
  int i;

  if(t->t_func->nargs >= 0 && t->t_func->nargs != t->t_num_args) {
    glw_model_seterr(ec->ei, t, "%s(): Invalid number of arguments: %d, "
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
glw_model_eval_rpn0(token_t *t0, glw_model_eval_context_t *ec)
{
  token_t *t;

  for(t = t0->child; t != NULL; t = t->next) {
    switch(t->type) {
    case TOKEN_ARRAY:
      if(eval_array(ec, t))
	return -1;
      break;

    case TOKEN_BLOCK:
    case TOKEN_STRING:
    case TOKEN_FLOAT:
    case TOKEN_IDENTIFIER:
    case TOKEN_OBJECT_ATTRIBUTE:
    case TOKEN_VOID:
    case TOKEN_PROPERTY:
    case TOKEN_PROPERTY_NAME:
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
	
    case TOKEN_EQ:
    case TOKEN_NEQ:
      if(eval_eq(ec, t, t->type == TOKEN_NEQ))
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

    case TOKEN_ASSIGNMENT:
      if(eval_assign(ec, t))
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
glw_model_eval_rpn(token_t *t, glw_model_eval_context_t *pec, int *copyp)
{
  glw_model_eval_context_t ec;
  int r;

  memset(&ec, 0, sizeof(ec));
  ec.debug = pec->debug;
  ec.ei = pec->ei;
  ec.prop0 = pec->prop0;
  ec.prop_parent = pec->prop_parent;
  ec.w = pec->w;
  ec.rpn = t;
  ec.gr = pec->gr;
  ec.passive_subscriptions = pec->passive_subscriptions;
  ec.sublist = pec->sublist;
  ec.event = pec->event;

  r = glw_model_eval_rpn0(t, &ec);

  *copyp = ec.dynamic_eval;
  glw_model_free_chain(ec.alloc);
  return r;
}


/**
 *
 */
int
glw_model_eval_block(token_t *t, glw_model_eval_context_t *ec)
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
      if(glw_model_eval_rpn(t, ec, &copy))
	return -1;

      if(!copy) 
	break;

      *p = t->next;

      w = ec->w;
      t->next =  w->glw_dynamic_expressions;
      w->glw_dynamic_expressions = t;

      if(copy & GLW_MODEL_DYNAMIC_EVAL_EVERY_FRAME)
	glw_signal_handler_register(w, eval_dynamic_every_frame_sig, t, 1000);

      if(copy & GLW_MODEL_DYNAMIC_EVAL_FOCUSED_CHILD_CHANGE)
	glw_signal_handler_register(w, eval_dynamic_focused_child_change_sig,
				    t, 1000);

      if(copy & GLW_MODEL_DYNAMIC_EVAL_FOCUS_HOVER_CHANGE)
	glw_signal_handler_register(w, eval_dynamic_focus_hover_change_sig,
				    t, 1000);

      if(copy & GLW_MODEL_DYNAMIC_EVAL_VISIBILITY)
	glw_signal_handler_register(w, eval_dynamic_visibility_sig, t, 1000);

      continue;

    default:
      glw_model_seterr(ec->ei, t, "Unexpected token");
      return -1;
    }
    p = &t->next;
  }
  return 0;
}


/**
 *
 */
static struct strtab classtab[] = {
  { "dummy",         GLW_DUMMY},
  { "container_x",   GLW_CONTAINER_X},
  { "container_y",   GLW_CONTAINER_Y},
  { "container_z",   GLW_CONTAINER_Z},
  { "icon",          GLW_ICON},
  { "image",         GLW_IMAGE},
  { "backdrop",      GLW_BACKDROP},
  { "label",         GLW_LABEL},
  { "text",          GLW_TEXT},
  { "integer",       GLW_INTEGER},
  { "array",         GLW_ARRAY},
  { "list_x",        GLW_LIST_X},
  { "list_y",        GLW_LIST_Y},
  { "deck",          GLW_DECK},
  { "expander_x",    GLW_EXPANDER_X},
  { "expander_y",    GLW_EXPANDER_Y},
  { "slideshow",     GLW_SLIDESHOW},
  { "freefloat",     GLW_FREEFLOAT},
  { "cursor",        GLW_CURSOR},
  { "mirror",        GLW_MIRROR},
  { "rotator",       GLW_ROTATOR},
  { "animator",      GLW_ANIMATOR},
  { "video",         GLW_VIDEO},
  { "fx_texrot",     GLW_FX_TEXROT},
  { "slider_x",      GLW_SLIDER_X},
  { "slider_y",      GLW_SLIDER_Y},
  { "layer",         GLW_LAYER},
  { "bloom",         GLW_BLOOM},
  { "cube",          GLW_CUBE},
};

/**
 *
 */
static int 
glwf_widget(glw_model_eval_context_t *ec, struct token *self,
	    token_t **argv, unsigned int argc)
{
  int c;
  glw_model_eval_context_t n;
  token_t *a = argv[0];
  token_t *b = argv[1];

  if(ec->w == NULL) 
    return glw_model_seterr(ec->ei, self, 
			    "Widget can not be created in this scope");

  if(a->type != TOKEN_IDENTIFIER)
    return glw_model_seterr(ec->ei, self, 
			    "widget: Invalid first argument, "
			    "expected widget class");
    
  if(b->type != TOKEN_BLOCK)
    return glw_model_seterr(ec->ei, self, 
			    "widget: Invalid second argument, "
			    "expected block");

  if((c = str2val(a->t_string, classtab)) < 0)
    return glw_model_seterr(ec->ei, self, "widget: Invalid class");

  memset(&n, 0, sizeof(n));
  n.prop0 = ec->prop0;
  n.prop_parent = ec->prop_parent;
  n.ei = ec->ei;
  n.gr = ec->gr;
  n.w = glw_create_i(ec->gr,
		     c,
		     GLW_ATTRIB_PARENT, ec->w,
		     GLW_ATTRIB_PROPROOTS, ec->prop0, ec->prop_parent,
		     NULL);
  
  n.sublist = &n.w->glw_prop_subscriptions;

  if(glw_model_eval_block(b, &n))
    return -1;

  return 0;
}


/**
 *
 */
static int 
glwf_cloner(glw_model_eval_context_t *ec, struct token *self,
	    token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *b = argv[1];
  token_t *c = argv[2];
  glw_prop_sub_t *gps;
  glw_prop_sub_pending_t *gpsp;
  int class, f;
  glw_t *w, *n;

  if(ec->w == NULL) 
    return glw_model_seterr(ec->ei, self, 
			    "Cloner can not be created in this scope");

  if((a = token_resolve(ec, a)) == NULL)
    return -1;

  if(b->type != TOKEN_IDENTIFIER)
    return glw_model_seterr(ec->ei, self, 
			    "cloner: Invalid second argument, "
			    "expected widget class");
    
  if((class = str2val(b->t_string, classtab)) < 0)
    return glw_model_seterr(ec->ei, self, "cloner: Invalid class");

  if(c->type != TOKEN_BLOCK)
    return glw_model_seterr(ec->ei, self, 
			    "cloner: Invalid third argument, "
			    "expected block");

  /* Destroy any previous cloned entries */
  for(w = TAILQ_FIRST(&ec->w->glw_childs); w != NULL; w = n) {
    n = TAILQ_NEXT(w, glw_parent_link);

    if(w->glw_originating_prop != NULL)
      glw_detach0(w);
  }

  if(a->type == TOKEN_DIRECTORY) {
    gps = a->propsubr;

    if(gps->gps_cloner_body != NULL)
      glw_model_free_chain(gps->gps_cloner_body);

    gps->gps_cloner_body = glw_model_clone_chain(c);
    gps->gps_cloner_class = class;

    /* Create pending childs */
    while((gpsp = TAILQ_FIRST(&gps->gps_pending)) != NULL) {
      TAILQ_REMOVE(&gps->gps_pending, gpsp, gpsp_link);

      f = gpsp->gpsp_prop == gps->gps_pending_select ? PROP_ADD_SELECTED : 0;
	
      cloner_add_child0(gps, gpsp->gpsp_prop, NULL, ec->w, ec->ei, f);
      prop_ref_dec(gpsp->gpsp_prop);
      free(gpsp);
    }
    gps->gps_pending_select = NULL;
  }

  return 0;
}



/**
 *
 */
static int 
glwf_space(glw_model_eval_context_t *ec, struct token *self,
	   token_t **argv, unsigned int argc)

{
  token_t *a = argv[0];

  if(ec->w == NULL) 
    return glw_model_seterr(ec->ei, self, 
			    "Widget can not be created in this scope");

  if((a = token_resolve(ec, a)) == NULL)
    return -1;

  if(a->type != TOKEN_FLOAT)
    return glw_model_seterr(ec->ei, self, 
			    "widget: Invalid first argument, "
			    "expected float");
  glw_create_i(ec->gr, 
	       GLW_DUMMY,
	       GLW_ATTRIB_PARENT, ec->w,
	       GLW_ATTRIB_WEIGHT, a->t_float,
	       NULL);
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
} glw_event_map_eval_block_t;


/**
 *
 */
static void
glw_event_map_eval_block_fire(glw_t *w, glw_event_map_t *gem, event_t *src)
{
  glw_event_map_eval_block_t *b = (glw_event_map_eval_block_t *)gem;
  token_t *body;
  glw_model_eval_context_t n;
  struct glw_prop_sub_list l;

  LIST_INIT(&l);

  memset(&n, 0, sizeof(n));
  n.prop0 = b->prop;
  n.prop_parent = b->prop_parent;
  n.ei = NULL;
  n.gr = w->glw_root;
  n.w = w;
  n.passive_subscriptions = 1;
  n.sublist = &l;
  n.event = src;

  body = glw_model_clone_chain(b->block);
  glw_model_eval_block(body, &n);
  glw_prop_subscription_destroy_list(&l);
  glw_model_free_chain(body);
}

/**
 *
 */
static void
glw_event_map_eval_block_dtor(glw_event_map_t *gem)
{
  glw_event_map_eval_block_t *b = (glw_event_map_eval_block_t *)gem;

  glw_model_token_free(b->block);

  if(b->prop)
    prop_ref_dec(b->prop);

  if(b->prop_parent)
    prop_ref_dec(b->prop_parent);

  free(b);
}



/**
 *
 */
static glw_event_map_t *
glw_event_map_eval_block_create(glw_model_eval_context_t *ec,
				struct token *block)
{
  glw_event_map_eval_block_t *b = malloc(sizeof(glw_event_map_eval_block_t));

  b->block = glw_model_clone_chain(block);

  b->prop = ec->prop0;
  if(b->prop)
    prop_ref_inc(b->prop);

  b->prop_parent = ec->prop_parent;
  if(b->prop_parent)
    prop_ref_inc(b->prop_parent);

  b->map.gem_dtor = glw_event_map_eval_block_dtor;
  b->map.gem_fire = glw_event_map_eval_block_fire;
  return &b->map;
}



/**
 *
 */
static int 
glwf_onEvent(glw_model_eval_context_t *ec, struct token *self,
	     token_t **argv, unsigned int argc)

{
  token_t *a = argv[0];  /* Source */
  token_t *b = argv[1];  /* Target */
  int action;
  glw_t *w = ec->w;
  glw_event_map_t *gem;

  if(w == NULL) 
    return glw_model_seterr(ec->ei, self, 
			    "Events can not be mapped in this scope");

  if(a == NULL || b == NULL)
    return glw_model_seterr(ec->ei, self, "Missing operands");

  if(a->type != TOKEN_IDENTIFIER)
    return glw_model_seterr(ec->ei, a, "Invalid source event type");

  if(!strcmp(a->t_string, "KeyCode")) {
    action = -1;
  } else {
    action = action_str2code(a->t_string);

    if(action < 0)
      return glw_model_seterr(ec->ei, a, "Invalid source event type");
  }

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
    return glw_model_seterr(ec->ei, a, "onEvent: Second arg is invalid");
  }

  gem->gem_action = action;
  glw_event_map_add(w, gem);
  return 0;
}


/**
 *
 */
static int 
glwf_navOpen(glw_model_eval_context_t *ec, struct token *self,
	     token_t **argv, unsigned int argc)
{
  token_t *a, *b = NULL, *c = NULL, *r;

  if(argc < 1 || argc > 3) 
    return glw_model_seterr(ec->ei, self, "navOpen(): "
			    "Invalid number of arguments");

  if((a = token_resolve(ec, argv[0])) == NULL)
    return -1;

  if(argc > 1 && (b = token_resolve(ec, argv[1])) == NULL)
    return -1;

  if(argc > 2 && (c = token_resolve(ec, argv[2])) == NULL)
    return -1;

  if(a->type != TOKEN_STRING && a->type != TOKEN_VOID)
    return glw_model_seterr(ec->ei, a, "navOpen(): "
			    "First argument is not a string or (void)");
  
  if(b != NULL && b->type != TOKEN_STRING && b->type != TOKEN_VOID)
    return glw_model_seterr(ec->ei, b, "navOpen(): "
			    "Second argument is not a string or (void)");

  if(c != NULL && c->type != TOKEN_STRING && c->type != TOKEN_VOID)
    return glw_model_seterr(ec->ei, c, "navOpen(): "
			    "Third argument is not a string or (void)");

  r = eval_alloc(self, ec, TOKEN_EVENT);
  r->t_gem = glw_event_map_navOpen_create(a && a->type == TOKEN_STRING ?
					  a->t_string : NULL,
					  b && b->type == TOKEN_STRING ?
					  b->t_string : NULL,
					  c && c->type == TOKEN_STRING ?
					  c->t_string : NULL);
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static int 
glwf_targetedEvent(glw_model_eval_context_t *ec, struct token *self,
	   token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];       /* Target name */
  token_t *b = argv[1];       /* Event */
  token_t *r;
  int action;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;

  if(a->type != TOKEN_STRING)
    return glw_model_seterr(ec->ei, a, "event(): "
			    "First argument is not a string");
  
  if(b->type != TOKEN_IDENTIFIER ||
     (action = action_str2code(b->t_string )) < 0)
    return glw_model_seterr(ec->ei, b, "event(): "
			    "Invalid target event");
  
  r = eval_alloc(self, ec, TOKEN_EVENT);
  r->t_gem = glw_event_map_internal_create(a->t_string, action);
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static int 
glwf_event(glw_model_eval_context_t *ec, struct token *self,
	   token_t **argv, unsigned int argc)
{
  token_t *a, *r;
  int action;

  a = argv[0];

  if(a->type != TOKEN_IDENTIFIER ||
     (action = action_str2code(a->t_string)) < 0)
    return glw_model_seterr(ec->ei, a, "event(): Invalid target event");
  
  r = eval_alloc(self, ec, TOKEN_EVENT);
  r->t_gem = glw_event_map_internal_create(NULL, action);
  eval_push(ec, r);
  return 0;
}



typedef struct glwf_changed_extra {

  int threshold;
  token_type_t type;

  union {
    char *str;
    float value;
  } u;

  int transition;

} glwf_changed_extra_t;


/**
 *
 */
static int 
glwf_changed(glw_model_eval_context_t *ec, struct token *self,
	     token_t **argv, unsigned int argc)

{
  token_t *a, *b, *c, *r;
  glwf_changed_extra_t *e = self->t_extra;
  int change = 0;
  int supp_first = 0;

  if(argc < 2 || argc > 3)
    return glw_model_seterr(ec->ei, self, 
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
    return glw_model_seterr(ec->ei, self, "Invalid first operand to changed()");

  if(b->type != TOKEN_FLOAT)
    return glw_model_seterr(ec->ei, self, 
			    "Invalid second operand to changed(), "
			    "expected scalar");

  if(a->type != e->type) {
    if(e->type == TOKEN_STRING)
      free(e->u.str);

    e->type = a->type;

    switch(a->type) {

    case TOKEN_STRING:
      e->u.str = strdup(a->t_string);
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
      if(strcmp(e->u.str, a->t_string)) {
	change = 1;
	free(e->u.str);
	e->u.str = strdup(a->t_string);
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
    ec->dynamic_eval |= GLW_MODEL_DYNAMIC_EVAL_EVERY_FRAME;
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
    free(e->u.str);
  free(e);
}


/**
 * Infinite Impulse Response filter
 */
static int 
glwf_iir(glw_model_eval_context_t *ec, struct token *self,
	 token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *b = argv[1];
  token_t *r;
  float f;

  int x, y;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;
  if((b = token_resolve(ec, b)) == NULL)
    return -1;

  if(a == NULL || (a->type != TOKEN_FLOAT  && a->type != TOKEN_INT &&
		   a->type != TOKEN_STRING && a->type != TOKEN_VOID))
    return glw_model_seterr(ec->ei, self, "Invalid first operand to iir()");

  if(a->type == TOKEN_STRING || a->type == TOKEN_VOID)
    f = 0;
  else
    f = a->type == TOKEN_INT ? a->t_int : a->t_float;

  if(b == NULL || b->type != TOKEN_FLOAT)
    return glw_model_seterr(ec->ei, self, "Invalid second operand to iir()");

  x = self->t_extra_float * 1000.;

  self->t_extra_float =  GLW_LP(b->t_float, self->t_extra_float, f);

  y = self->t_extra_float * 1000.;
  r = eval_alloc(self, ec, TOKEN_FLOAT);

  if(x != y)
    ec->dynamic_eval |= GLW_MODEL_DYNAMIC_EVAL_EVERY_FRAME;
  else
    self->t_extra_float = f;

  r->t_float = self->t_extra_float;
  eval_push(ec, r);
  return 0;
}

/**
 * Float to string
 */
static int 
glwf_float2str(glw_model_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *b = argv[1];
  token_t *r;
  float value;
  char buf[30];
  int prec;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;
  if((b = token_resolve(ec, b)) == NULL)
    return -1;

  switch(a->type) {
  case TOKEN_FLOAT:
    value = a->t_float;
    break;
  case TOKEN_INT:
    value = a->t_int;
    break;
  default:
    r = eval_alloc(self, ec, TOKEN_STRING);
    r->t_string = strdup("0");
    eval_push(ec, r);
    return 0;
  }
  
  prec = 2;
  if(b != NULL && b->type == TOKEN_FLOAT)
    prec = b->t_float;
  if(b != NULL && b->type == TOKEN_INT)
    prec = b->t_int;

  snprintf(buf, sizeof(buf), "%.*f", prec, value);

  r = eval_alloc(self, ec, TOKEN_STRING);
  r->t_string = strdup(buf);
  eval_push(ec, r);
  return 0;
}


/**
 * Int to string
 */
static int 
glwf_int2str(glw_model_eval_context_t *ec, struct token *self,
	     token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *r;
  char buf[30];

  if((a = token_resolve(ec, a)) == NULL)
    return -1;

  if(a->type == TOKEN_INT) {
    snprintf(buf, sizeof(buf), "%d", a->t_int);
    r = eval_alloc(self, ec, TOKEN_STRING);
    r->t_string = strdup(buf);
  } else {
    r = a;
  }
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

  if(a->type != b->type)
    return -1;

  switch(a->type) {
  case TOKEN_STRING:
    return strcmp(a->t_string, b->t_string);
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
glwf_translate(glw_model_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)

{
  token_t *idx, *def, *k, *v;
  int i;

  if(argc < 2) 
    return glw_model_seterr(ec->ei, self,
			    "translate() requires at least two arguments");

  if(argc & 1) 
    return glw_model_seterr(ec->ei, self,
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
glwf_strftime(glw_model_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)
{
  token_t *a = argv[0]; // format
  token_t *b = argv[1];  // unixtime
  token_t *r;
  char buf[50];
  struct tm tm;
  time_t t;

  if((a = token_resolve(ec, a)) == NULL)
    return -1;
  if((b = token_resolve(ec, b)) == NULL)
    return -1;

  if(a->type != TOKEN_INT)
    return glw_model_seterr(ec->ei, self, 
			    "Invalid first operand to strftime()");

  if(b->type != TOKEN_STRING)
    return glw_model_seterr(ec->ei, self, 
			    "Invalid second operand to strftime()");

  t = a->t_int;
  if(t != 0) {
    localtime_r(&t, &tm);
    strftime(buf, sizeof(buf), b->t_string, &tm);
  } else {
    buf[0] = 0;
  }
  r = eval_alloc(self, ec, TOKEN_STRING);
  r->t_string = strdup(buf);
  eval_push(ec, r);
  return 0;
}


/**
 * Return 1 if the given token is set (string is != "", value != 0)
 */
static int 
glwf_isset(glw_model_eval_context_t *ec, struct token *self,
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
    rv = a->t_string[0] != 0;
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
 * Return current time
 */
static int 
glwf_time(glw_model_eval_context_t *ec, struct token *self,
	   token_t **argv, unsigned int argc)
{
  token_t *r;
  time_t now;

  time(&now);

  r = eval_alloc(self, ec, TOKEN_INT);
  r->t_int = now;
  eval_push(ec, r);

  ec->dynamic_eval |= GLW_MODEL_DYNAMIC_EVAL_EVERY_FRAME;

  return 0;
}

/**
 * Int to string
 */
static int 
glwf_value2duration(glw_model_eval_context_t *ec, struct token *self,
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
      str = a->t_string;
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
  r->t_string = strdup(str);
  eval_push(ec, r);
  return 0;
}



/**
 * Create a new child under the given property
 */
static int 
glwf_createchild(glw_model_eval_context_t *ec, struct token *self,
		 token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *t;
  const char *propname[16];
  prop_t *p, *r;
  int i;

  if(a->type != TOKEN_PROPERTY_NAME)
    return 0;
  
  for(i = 0, t = a; t != NULL && i < 15; t = t->child)
    propname[i++]  = t->t_string;
  propname[i] = NULL;
  
  r = ec->w ? ec->w->glw_root->gr_uii.uii_prop : NULL;

  p = prop_get_by_name(propname, 1, 
		       PROP_TAG_NAMED_ROOT, ec->prop0, "self",
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
glwf_delete(glw_model_eval_context_t *ec, struct token *self,
	    token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];

  if((a = token_resolve(ec, a)) == NULL)
    return -1;

  if(a->propsubr == NULL) {
    return glw_model_seterr(ec->ei, self, 
			    "Invalid operand to delete()");
  }

  prop_request_delete_child_by_subscription(a->propsubr->gps_sub);
  return 0;
}



/**
 * Return 1 if the current widget is in focus
 */
static int 
glwf_isFocused(glw_model_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)
{
  token_t *r;

  ec->dynamic_eval |= GLW_MODEL_DYNAMIC_EVAL_FOCUS_HOVER_CHANGE;

  r = eval_alloc(self, ec, TOKEN_INT);
  r->t_int = glw_is_focused(ec->w);
  eval_push(ec, r);
  return 0;
}


/**
 * Return 1 if the current widget is in focus
 */
static int 
glwf_isHovered(glw_model_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)
{
  token_t *r;

  ec->dynamic_eval |= GLW_MODEL_DYNAMIC_EVAL_FOCUS_HOVER_CHANGE;

  r = eval_alloc(self, ec, TOKEN_INT);
  r->t_int = glw_is_hovered(ec->w);
  eval_push(ec, r);
  return 0;
}



/**
 * Returns the second argument if the first is void, otherwise returns
 * the first arg
 */
static int 
glwf_devoidify(glw_model_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *b = argv[1];

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
 * Returns the focused child (or void if nothing is focused)
 */
static int 
glwf_focusedChild(glw_model_eval_context_t *ec, struct token *self,
		  token_t **argv, unsigned int argc)
{
  glw_t *w = ec->w, *c;
  token_t *r;

  if(w == NULL) 
    return glw_model_seterr(ec->ei, self, "focusedChild() without widget");

  ec->dynamic_eval |= GLW_MODEL_DYNAMIC_EVAL_FOCUSED_CHILD_CHANGE;

  c = w->glw_focused;
  if(c != NULL && c->glw_originating_prop != NULL) {
    r = eval_alloc(self, ec, TOKEN_PROPERTY);
    r->t_prop = c->glw_originating_prop;
    prop_ref_inc(r->t_prop);
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
glwf_getCaption(glw_model_eval_context_t *ec, struct token *self,
		  token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *r;
  glw_t *w;
  char buf[100];

  if((a = token_resolve(ec, a)) == NULL)
    return -1;

  if(a != NULL && a->type == TOKEN_STRING) {
    w = glw_find_neighbour(ec->w, a->t_string);

    if(w != NULL && !glw_get_text0(w, buf, sizeof(buf))) {
      r = eval_alloc(self, ec, TOKEN_STRING);
      r->t_string = strdup(buf);
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
glwf_bind(glw_model_eval_context_t *ec, struct token *self,
		  token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  token_t *t;
  const char *propname[16];
  int i;

  if(a != NULL && a->type == TOKEN_PROPERTY_NAME) {

    for(i = 0, t = a; t != NULL && i < 15; t = t->child)
      propname[i++]  = t->t_string;
    propname[i] = NULL;

    glw_set_i(ec->w, GLW_ATTRIB_BIND_TO_PROPERTY, ec->prop0, propname, NULL);

  } else if(a != NULL && a->type == TOKEN_STRING) {
    glw_set_i(ec->w, GLW_ATTRIB_BIND_TO_ID, a->t_string, NULL);

  } else {
    glw_set_i(ec->w, GLW_ATTRIB_BIND_TO_PROPERTY, NULL, NULL, NULL);
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
glwf_delta(glw_model_eval_context_t *ec, struct token *self,
	   token_t **argv, unsigned int argc)
{
  glwf_delta_extra_t *de = self->t_extra;
  token_t *a = argv[0], *b = argv[1], *t;
  const char *propname[16];
  int i;
  float f;
  prop_t *p;

  if(a->type != TOKEN_PROPERTY_NAME)
    return glw_model_seterr(ec->ei, a, "delta() first arg is not a property");
  
  if(b->type != TOKEN_FLOAT && b->type != TOKEN_INT)
    return glw_model_seterr(ec->ei, b, "delta() second arg is not scalar");

  if(ec->w == NULL)
    return glw_model_seterr(ec->ei, b, "delta() in non widget scope");
  
  for(i = 0, t = a; t != NULL && i < 15; t = t->child)
    propname[i++]  = t->t_string;
  propname[i] = NULL;

  p = prop_get_by_name(propname, 0, 
		       PROP_TAG_NAMED_ROOT, ec->prop0, "self",
		       PROP_TAG_NAMED_ROOT, ec->prop_parent, "parent",
		       PROP_TAG_ROOT, ec->w->glw_root->gr_uii.uii_prop,
		       NULL);

  if(p == NULL)
    return glw_model_seterr(ec->ei, a, "Unable to resolve property");
  
  f = b->type == TOKEN_FLOAT ? b->t_float : b->t_int;

  ec->dynamic_eval |= GLW_MODEL_DYNAMIC_KEEP;

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
glwf_isVisible(glw_model_eval_context_t *ec, struct token *self,
	       token_t **argv, unsigned int argc)
{
  token_t *r;

  ec->dynamic_eval |= GLW_MODEL_DYNAMIC_EVAL_VISIBILITY;

  r = eval_alloc(self, ec, TOKEN_INT);

  r->t_int = ec->w->glw_flags & GLW_ACTIVE ? 1 : 0;
  eval_push(ec, r);
  return 0;
}


/**
 * Evals the first arg, if true, the second arg is returned. 
 * Otherwise the third arg is returned.
 * Equivivalent to the C ?: operator
 */
static int 
glwf_select(glw_model_eval_context_t *ec, struct token *self,
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
glwf_trace(glw_model_eval_context_t *ec, struct token *self,
	   token_t **argv, unsigned int argc)
{
  token_t *a, *b;

  ec->debug++;

  if((a = token_resolve(ec, argv[0])) == NULL ||
     (b = token_resolve(ec, argv[1])) == NULL) {
    ec->debug--;
    return -1;
  }

  if(a->type != TOKEN_STRING)
    return 0;

  switch(b->type) {
  case TOKEN_STRING:
  case TOKEN_IDENTIFIER:
    TRACE(TRACE_DEBUG, "GLW", "%s: %s", a->t_string, b->t_string);
    break;
  case TOKEN_FLOAT:
    TRACE(TRACE_DEBUG, "GLW", "%s: %f", a->t_string, b->t_float);
    break;
  case TOKEN_INT:
    TRACE(TRACE_DEBUG, "GLW", "%s: %d", a->t_string, b->t_int);
    break;
  case TOKEN_VOID:
    TRACE(TRACE_DEBUG, "GLW", "%s: (void)", a->t_string, b->t_int);
    break;
  default:
    TRACE(TRACE_DEBUG, "GLW", "%s: ???", a->t_string);
    break;
  }
  return 0;
}


/**
 * 
 */
static int 
glwf_browse(glw_model_eval_context_t *ec, struct token *self,
	   token_t **argv, unsigned int argc)
{
  token_t *a = argv[0];
  prop_t *p;
  char errbuf[100];
  
  if(a->type != TOKEN_PROPERTY) {

    if(a->type != TOKEN_STRING)
      return glw_model_seterr(ec->ei, a, "browse() first arg is not a string");
  
    p = nav_list(a->t_string, errbuf, sizeof(errbuf));
    if(p == NULL) 
      return glw_model_seterr(ec->ei, a, "browse(%s): %s", a->t_string, errbuf);
    /* Transform TOKEN_STRING -> TOKEN_PROPERTY */

    free(a->t_string);
    a->t_prop = p;
    a->type = TOKEN_PROPERTY;
  }

  eval_push(ec, a);
  return 0;
}



/**
 *
 */
static const token_func_t funcvec[] = {
  {"widget", 2, glwf_widget},
  {"cloner", 3, glwf_cloner},
  {"space", 1, glwf_space},
  {"onEvent", 2, glwf_onEvent},
  {"navOpen", -1, glwf_navOpen},
  {"targetedEvent", 2, glwf_targetedEvent},
  {"event", 1, glwf_event},
  {"changed", -1, glwf_changed, glwf_changed_ctor, glwf_changed_dtor},
  {"iir", 2, glwf_iir},
  {"float2str", 2, glwf_float2str},
  {"int2str", 1, glwf_int2str},
  {"translate", -1, glwf_translate},
  {"strftime", 2, glwf_strftime},
  {"isSet", 1, glwf_isset},
  {"time", 0, glwf_time},
  {"value2duration", 1, glwf_value2duration},
  {"createChild", 1, glwf_createchild},
  {"delete", 1, glwf_delete},
  {"isFocused", 0, glwf_isFocused},
  {"isHovered", 0, glwf_isHovered},
  {"devoidify", 2, glwf_devoidify},
  {"focusedChild", 0, glwf_focusedChild},
  {"getCaption", 1, glwf_getCaption},
  {"bind", 1, glwf_bind},
  {"delta", 2, glwf_delta, glwf_delta_ctor, glwf_delta_dtor},
  {"isVisible", 0, glwf_isVisible},
  {"select", 3, glwf_select},
  {"trace", 2, glwf_trace},
  {"browse", 1, glwf_browse},
};


/**
 *
 */
int 
glw_model_function_resolve(token_t *t)
{
  int i;

  for(i = 0; i < sizeof(funcvec) / sizeof(funcvec[0]); i++)
    if(!strcmp(funcvec[i].name, t->t_string)) {
      free(t->t_string);
      t->t_func = &funcvec[i];
      t->type = TOKEN_FUNCTION;
      if(t->t_func->ctor != NULL)
	t->t_func->ctor(t);
      return 0;
    }
  return -1;
}
