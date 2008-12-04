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
#include <libhts/hts_strtab.h>

#include "glw_model.h"
#include "glw.h"
#include "glw_event.h"


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

} glw_prop_sub_t;


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

  if(a == NULL || b == NULL)
    return glw_model_seterr(ec->ei, self, "Missing operands");
  

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
  case TOKEN_INT:
    return !!t->t_int;
  case TOKEN_FLOAT:
    return t->t_float > 0.5;
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

  if(a == NULL || b == NULL)
    return glw_model_seterr(ec->ei, self, "Missing operands");
  

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
eval_array(glw_model_eval_context_t *pec, token_t *t0)
{
  token_t *t, *out = NULL;
  int nelem = 0, n = 0;
  token_type_t vt;

  glw_model_eval_context_t ec;

  for(t = t0->child; t != NULL; t = t->next)
    nelem++;
  

  memset(&ec, 0, sizeof(ec));
  ec.ei = pec->ei;
  ec.prop = pec->prop;
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
    } else {
      out->t_float_vector[n] = ec.stack->t_float;
    }
    n++;
    glw_model_free_chain(ec.alloc);

    pec->persistence = GLW_MAX(pec->persistence, ec.persistence);
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
  token_t *b = eval_pop(ec), *a = eval_pop(ec);
  prop_t *p;
  int r;

  if(a == NULL || b == NULL)
    return glw_model_seterr(ec->ei, self, "Missing operands");
  
  if(a->propsubr != NULL) {
    
    p = prop_get_by_subscription(a->propsubr->gps_sub);
    switch(b->type) {
    case TOKEN_STRING:
      prop_set_string(p, b->t_string);
      break;
    case TOKEN_INT:
      prop_set_int(p, b->t_int);
      break;
    case TOKEN_FLOAT:
      prop_set_float(p, b->t_float);
      break;
    default:
      prop_set_void(p);
      break;
    }
    prop_ref_dec(p);
    eval_push(ec, b);
    return 0;
  }


  if(a->type != TOKEN_OBJECT_ATTRIBUTE)
    return glw_model_seterr(ec->ei, self, "Invalid assignment");

  r = a->t_attrib->set(ec, a->t_attrib, b);

  eval_push(ec, b);
  return r;
}



/**
 *
 */
static int
eval_prop(glw_model_eval_context_t *ec, struct token *self)
{
  glw_prop_sub_t *gps = self->propsubr;
  token_t *r;

  ec->persistence = GLW_MAX(ec->persistence, GLW_MODEL_EVAL_PROP);

  if((r = gps->gps_token) == NULL)
    r = eval_alloc(self, ec, TOKEN_VOID);

  eval_push(ec, r);
  return 0;
}



/**
 *
 */

static int
eval_dynamic_signal_handler(glw_t *w, void *opaque, 
			    glw_signal_t signal, void *extra)
{
  if(signal == GLW_SIGNAL_LAYOUT)
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

  assert(ec.persistence != 0);

  if(ec.persistence == GLW_MODEL_EVAL_EVERY_FRAME)
    glw_signal_handler_register(w, eval_dynamic_signal_handler, rpn, 1000);
  else
    glw_signal_handler_unregister(w, eval_dynamic_signal_handler, rpn);
}


/**
 *
 */
static int
cloner_child_signal_handler(glw_t *w, void *opaque, 
			    glw_signal_t signal, void *extra)
{
  prop_t *p = opaque;

  switch(signal) {
  case GLW_SIGNAL_DESTROY:
    prop_ref_dec(p);
    break;
#if 0
  case GLW_SIGNAL_SELECTED_UPDATE:
    prop_select(p, 0);
    break;

  case GLW_SIGNAL_SELECTED_UPDATE_ADVISORY:
    prop_select(p, 1);
    break;
#endif

  default:
    break;
  }
  return 0;
}

/**
 *
 */
static void
cloner_add_child0(glw_prop_sub_t *gps, prop_t *p,
		  glw_t *parent, errorinfo_t *ei, int selected)
{
  token_t *body;
  glw_model_eval_context_t n;

  body = glw_model_clone_chain(gps->gps_cloner_body);

  memset(&n, 0, sizeof(n));
  n.prop = p;
  n.ei = ei;
  n.gr = parent->glw_root;

  n.w = glw_create_i(parent->glw_root,
		     gps->gps_cloner_class,
		     GLW_ATTRIB_SIGNAL_HANDLER, cloner_child_signal_handler,
		     p, 500,
		     GLW_ATTRIB_PARENT, parent,
		     GLW_ATTRIB_PROPROOT, p,
		     NULL);

  if(selected)
    glw_signal0(parent, GLW_SIGNAL_SELECT, n.w);

  n.sublist = &n.w->glw_prop_subscriptions;

  glw_model_eval_block(body, &n);
  glw_model_free_chain(body);

}


/**
 *
 */
static void
cloner_add_child(glw_prop_sub_t *gps, prop_t *p,
		 glw_t *parent, errorinfo_t *ei, int selected)
{
  glw_prop_sub_pending_t *gpsp;

  prop_ref_inc(p); /* Decreased upon destroy in signal handler or
			  if it is removed from the pending list */

  if(gps->gps_cloner_body == NULL) {

    /*
     * The cloner body has not been evaluated yet so we can not
     * create the child. This happens when we subscribe initially.
     * Put it on a pending list and add it once the cloner has been
     * setup.
     */

    gpsp = malloc(sizeof(glw_prop_sub_pending_t));
    gpsp->gpsp_prop = p;
    TAILQ_INSERT_TAIL(&gps->gps_pending, gpsp, gpsp_link);

    if(selected)
      gps->gps_pending_select = p;
    return;
  }
  cloner_add_child0(gps, p, parent, ei, selected);
}


/**
 *
 */
static glw_t *
cloner_find_child(prop_t *p, glw_t *parent)
{
  glw_t *w;
  glw_signal_handler_t *gsh;

  TAILQ_FOREACH(w, &parent->glw_childs, glw_parent_link)
    LIST_FOREACH(gsh, &w->glw_signal_handlers, gsh_link)
      if(gsh->gsh_func == cloner_child_signal_handler && 
	 gsh->gsh_opaque == p)
	return w;
  return NULL;
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

  /* It may reside in the pending list */
  TAILQ_FOREACH(gpsp, &gps->gps_pending, gpsp_link) {
    if(gpsp->gpsp_prop == p) {

      if(gps->gps_pending_select == p)
	gps->gps_pending_select = NULL;

      prop_ref_dec(p);
      TAILQ_REMOVE(&gps->gps_pending, gpsp, gpsp_link);
      free(gpsp);
      return;
    }
  }
  
  fprintf(stderr, "glw: warning, cloner deletes unknown child\n");
  fprintf(stderr, "glw: This is a programming error, bailing out\n");
  abort();
}

/**
 *
 */
static void
cloner_select_child(glw_prop_sub_t *gps, prop_t *p, glw_t *parent)
{
  glw_t *w;

  if(p == NULL)
    return;

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
prop_callback(prop_sub_t *s, prop_event_t event, ...)
{
  glw_prop_sub_t *gps;
  prop_t *p;
  token_t *rpn = NULL, *t = NULL;

  va_list ap;
  va_start(ap, event);

  gps = s->hps_opaque;

  /* The opaque value may be NULL if we have free'd the gps-struct.
     Thus, if gps is NULL, we've no longer any interest in this
     subscription, so just skip it all */

  if(gps != NULL) {

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

    case PROP_ADD_CHILD:
      p = va_arg(ap, prop_t *);
      cloner_add_child(gps, p, gps->gps_widget, NULL, 0);
      break;

    case PROP_ADD_SELECTED_CHILD:
      p = va_arg(ap, prop_t *);
      cloner_add_child(gps, p, gps->gps_widget, NULL, 1);
      break;

    case PROP_DEL_CHILD:
      p = va_arg(ap, prop_t *);
      cloner_del_child(gps, p, gps->gps_widget);
      break;

    case PROP_SELECT_CHILD:
      p = va_arg(ap, prop_t *);
      cloner_select_child(gps, p, gps->gps_widget);
      break;

    case PROP_REQ_NEW_CHILD:
    case PROP_REQ_DELETE:
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

  if(i == 1 && !strcmp(propname[0], "event") && ec->event != NULL &&
     ec->event->e_type == EVENT_KEYDESC) {

    event_keydesc_t *ek = (event_keydesc_t *)ec->event;

    /* Hack, if user ask for $event, try to translate the event 
     * into something clever.
     * I don't like this very much, but it'll have to do for now 
     */

    t = eval_alloc(self, ec, TOKEN_STRING);
    t->t_string = strdup(ek->desc);
    eval_push(ec, t);
    return 0;
  }

  gps = calloc(1, sizeof(glw_prop_sub_t));

  TAILQ_INIT(&gps->gps_pending);

#ifdef GLW_MODEL_ERRORINFO
  gps->gps_file = refstr_dup(self->file);
  gps->gps_line = self->line;
#endif


  s = prop_subscribe(ec->prop, propname, prop_callback, gps,
		     w->glw_root->gr_courier, PROP_SUB_DIRECT_UPDATE);

  if(s == NULL) {
    refstr_unref(gps->gps_file);
    free(gps);
    return glw_model_seterr(ec->ei, self, "Property does not exist");
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


  return eval_prop(ec, self);
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

    case TOKEN_PROPERTY_SUBSCRIPTION:
      if(eval_prop(ec, t))
	return -1;
      break;

    case TOKEN_PROPERTY:
      if(subscribe_prop(ec, t))
	return -1;
      break;

    case TOKEN_BLOCK:
    case TOKEN_STRING:
    case TOKEN_FLOAT:
    case TOKEN_IDENTIFIER:
    case TOKEN_OBJECT_ATTRIBUTE:
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
	
    case TOKEN_FUNCTION:
      if(t->t_func->cb(ec, t))
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
  ec.ei = pec->ei;
  ec.prop = pec->prop;
  ec.w = pec->w;
  ec.rpn = t;
  ec.gr = pec->gr;
  ec.passive_subscriptions = pec->passive_subscriptions;
  ec.sublist = pec->sublist;
  ec.event = pec->event;

  r = glw_model_eval_rpn0(t, &ec);

  *copyp = ec.persistence;
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

  assert(ec->persistence == 0);

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

      if(copy == GLW_MODEL_EVAL_EVERY_FRAME)
	glw_signal_handler_register(w, eval_dynamic_signal_handler, t, 1000);
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
static token_t *
get_float_op(glw_model_eval_context_t *ec, token_t *self, const char *fname)
{
  token_t *a = eval_pop(ec);
  if(a == NULL || a->type != TOKEN_FLOAT) {
    glw_model_seterr(ec->ei, self, 
		     "%s operand to function %s",
		     a ? "Invalid type of" : "Missing", fname);
    return NULL;
  }
  return a;
}


/**
 *
 */
static int 
glwf_sin(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *a = get_float_op(ec, self, "sin");
  token_t *r = eval_alloc(self, ec, TOKEN_FLOAT);

  if(a == NULL)
    return -1;

  r->t_float = sin(a->t_float);
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static int 
glwf_cos(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *a = get_float_op(ec, self, "cos");
  token_t *r = eval_alloc(self, ec, TOKEN_FLOAT);

  if(a == NULL)
    return -1;

  r->t_float = cos(a->t_float);
  eval_push(ec, r);
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
  { "bitmap",        GLW_BITMAP},
  { "label",         GLW_LABEL},
  { "text",          GLW_TEXT},
  { "integer",       GLW_INTEGER},
  //  { "array",         GLW_ARRAY},
  { "list_x",        GLW_LIST_X},
  { "list_y",        GLW_LIST_Y},
  { "deck",          GLW_DECK},
  { "expander",      GLW_EXPANDER},
  //  { "slideshow",     GLW_SLIDESHOW},
  { "cursor",        GLW_CURSOR},
  { "mirror",        GLW_MIRROR},
  { "rotator",       GLW_ROTATOR},
  { "animator",      GLW_ANIMATOR},
  { "fx_texrot",     GLW_FX_TEXROT},
};

/**
 *
 */
static int 
glwf_widget(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *b = eval_pop(ec);
  token_t *a = eval_pop(ec);
  int c;
  glw_model_eval_context_t n;

  if(ec->w == NULL) 
    return glw_model_seterr(ec->ei, self, 
			    "Widget can not be created in this scope");

  if(a == NULL || b == NULL)
    return glw_model_seterr(ec->ei, self, "Missing operands");


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
  n.prop = ec->prop;
  n.ei = ec->ei;
  n.gr = ec->gr;
  n.w = glw_create_i(ec->gr,
		     c,
		     GLW_ATTRIB_PARENT, ec->w,
		     GLW_ATTRIB_PROPROOT, ec->prop,
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
glwf_cloner(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *c = eval_pop(ec); /* Block argument */
  token_t *b = eval_pop(ec); /* Widget class */
  token_t *a = eval_pop(ec); /* Prop source */
  glw_prop_sub_t *gps;
  glw_prop_sub_pending_t *gpsp;
  int class;
  glw_t *w;

  if(ec->w == NULL) 
    return glw_model_seterr(ec->ei, self, 
			    "Cloner can not be created in this scope");

  if(a == NULL || b == NULL || c == NULL)
    return glw_model_seterr(ec->ei, self, "Missing operands");

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

  while((w = TAILQ_FIRST(&ec->w->glw_childs)) != NULL)
    glw_destroy0(w);

  if(a->type == TOKEN_DIRECTORY) {
    gps = a->propsubr;

    if(gps->gps_cloner_body != NULL)
      glw_model_free_chain(gps->gps_cloner_body);

    gps->gps_cloner_body = glw_model_clone_chain(c);
    gps->gps_cloner_class = class;

    /* Create pending childs */
    while((gpsp = TAILQ_FIRST(&gps->gps_pending)) != NULL) {
      TAILQ_REMOVE(&gps->gps_pending, gpsp, gpsp_link);

      cloner_add_child0(gps, gpsp->gpsp_prop, ec->w, ec->ei,
			gpsp->gpsp_prop == gps->gps_pending_select);
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
glwf_space(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *a = eval_pop(ec);

  if(ec->w == NULL) 
    return glw_model_seterr(ec->ei, self, 
			    "Widget can not be created in this scope");

  if(a == NULL)
    return glw_model_seterr(ec->ei, self, "Missing operands");


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
  n.prop = b->prop;
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
  b->prop = ec->prop;

  if(b->prop)
    prop_ref_inc(b->prop);

  b->map.gem_dtor = glw_event_map_eval_block_dtor;
  b->map.gem_fire = glw_event_map_eval_block_fire;
  return &b->map;
}



/**
 *
 */
static int 
glwf_onEvent(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *b = eval_pop(ec);  /* Target */
  token_t *a = eval_pop(ec);  /* Source */
  int srcevent;
  glw_t *w = ec->w;
  glw_event_map_t *gem;

  if(w == NULL) 
    return glw_model_seterr(ec->ei, self, 
			    "Events can not be mapped in this scope");

  if(a == NULL || b == NULL)
    return glw_model_seterr(ec->ei, self, "Missing operands");

  if(a->type != TOKEN_IDENTIFIER || 
     (srcevent = event_str2code(a->t_string)) < 0)
    return glw_model_seterr(ec->ei, a, "Invalid source event type");


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

  gem->gem_srcevent = srcevent;
  glw_event_map_add(w, gem);
  return 0;
}


/**
 *
 */
static int 
glwf_genericEvent(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *c = eval_pop(ec);  /* Argument */
  token_t *b = eval_pop(ec);  /* Method */
  token_t *a = eval_pop(ec);  /* Target name */
  token_t *r;
  const char *s;

  if(a == NULL || b == NULL || c == NULL)
    return glw_model_seterr(ec->ei, self, "Missing operands");

  if(a->type != TOKEN_IDENTIFIER)
    return glw_model_seterr(ec->ei, a, "genericEvent: "
			    "First argument is not an identifier");
  
  if(b->type != TOKEN_IDENTIFIER)
    return glw_model_seterr(ec->ei, b, "genericEvent: "
			    "Second argument is not an identifier");
  
  switch(c->type) {
  case TOKEN_STRING:
    s = c->t_string;
    break;

  case TOKEN_VOID:
    s = "";
    break;

  default:
    return glw_model_seterr(ec->ei, c, "genericEvent: "
			    "Third argument is not a string");
  }

  r = eval_alloc(self, ec, TOKEN_EVENT);
  r->t_gem = glw_event_map_generic_create(a->t_string, b->t_string, s);
  eval_push(ec, r);
  return 0;
}


/**
 *
 */
static int 
glwf_internalEvent(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *b = eval_pop(ec);  /* Event */
  token_t *a = eval_pop(ec);  /* Target name */
  token_t *r;
  int dstevent;

  if(a == NULL || b == NULL)
    return glw_model_seterr(ec->ei, self, "Missing operands");

  if(a->type != TOKEN_STRING)
    return glw_model_seterr(ec->ei, a, "internalEvent: "
			    "First argument is not a string");
  
  if(b->type != TOKEN_IDENTIFIER ||
     (dstevent = event_str2code(b->t_string )) < 0)
    return glw_model_seterr(ec->ei, b, "internalEvent: "
			    "Invalid target event");
  
  r = eval_alloc(self, ec, TOKEN_EVENT);
  r->t_gem = glw_event_map_internal_create(a->t_string, dstevent);
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

} glwf_changed_extra_t;


/**
 *
 */
static int 
glwf_changed(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *b = eval_pop(ec);
  token_t *a = eval_pop(ec);
  token_t *r;
  glwf_changed_extra_t *e = self->t_extra;
  int change = 0;

  if(a == NULL || b == NULL)
    return glw_model_seterr(ec->ei, self, "Missing operands");

  if(a->type != TOKEN_FLOAT && a->type != TOKEN_STRING)
    return glw_model_seterr(ec->ei, self, "Invalid first operand to changed()");

  if(b->type != TOKEN_FLOAT)
    return glw_model_seterr(ec->ei, self, 
			    "Invalid second operand to changed(), "
			    "expected scalar");

  if(a->type != e->type) {
    if(e->type == TOKEN_STRING)
      free(e->u.str);

    e->type = a->type;
    if(a->type == TOKEN_STRING)
      e->u.str = strdup(a->t_string);
    else
      e->u.value = a->t_float;

    change = 1;

  } else if(e->type == TOKEN_STRING) {
    if((change = strcmp(e->u.str, a->t_string))) {
      free(e->u.str);
      e->u.str = strdup(a->t_string);
    }
  } else {
    /* must be float */
    if(e->u.value != a->t_float) {
      e->u.value = a->t_float;
      change = 1;
    }
  }

  if(change == 1)
    e->threshold = b->t_float * ec->gr->gr_framerate;

  if(e->threshold > 0)
    e->threshold--;

  r = eval_alloc(self, ec, TOKEN_FLOAT);
  if(e->threshold > 0) {
    r->t_float = 1;
    ec->persistence = GLW_MAX(ec->persistence, GLW_MODEL_EVAL_EVERY_FRAME);
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
glwf_iir(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *b = eval_pop(ec);
  token_t *a = eval_pop(ec);
  token_t *r;
  float f;

  int x, y;

  if(a == NULL || (a->type != TOKEN_FLOAT && a->type != TOKEN_INT &&
		   a->type != TOKEN_STRING))
    return glw_model_seterr(ec->ei, self, "Invalid first operand to iir()");

  if(a->type == TOKEN_STRING)
    f = 0;
  else
    f = a->type == TOKEN_INT ? a->t_int : a->t_float;

  if(b == NULL || b->type != TOKEN_FLOAT)
    return glw_model_seterr(ec->ei, self, "Invalid second operand to iir()");

  x = self->t_extra_float * 100.;

  self->t_extra_float =  GLW_LP(b->t_float, self->t_extra_float, f);

  y = self->t_extra_float * 100.;
  r = eval_alloc(self, ec, TOKEN_FLOAT);

  if(x != y) {
    r->t_float = self->t_extra_float;
    ec->persistence = GLW_MAX(ec->persistence, GLW_MODEL_EVAL_EVERY_FRAME);
  } else {
    r->t_float = f;
  }

  eval_push(ec, r);
  return 0;
}

/**
 * Float to string
 */
static int 
glwf_float2str(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *b = eval_pop(ec);
  token_t *a = eval_pop(ec);
  token_t *r;
  char buf[30];

  if(a == NULL || a->type != TOKEN_FLOAT) {

    if(a != NULL && a->type == TOKEN_STRING) {
      r = eval_alloc(self, ec, TOKEN_STRING);
      r->t_string = strdup(a->t_string);
      eval_push(ec, r);
      return 0;
    }
    return glw_model_seterr(ec->ei, self, 
			    "Invalid first operand to float2str()");
  }

  if(b == NULL || b->type != TOKEN_FLOAT)
    return glw_model_seterr(ec->ei, self, 
			    "Invalid second operand to float2str()");

  snprintf(buf, sizeof(buf), "%.*f", (int)b->t_float, a->t_float);

  r = eval_alloc(self, ec, TOKEN_STRING);
  r->t_string = strdup(buf);
  eval_push(ec, r);
  return 0;
}


/**
 * Int to string
 */
static int 
glwf_int2str(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *a = eval_pop(ec);
  token_t *r;
  char buf[30];

  if(a == NULL) {
    return glw_model_seterr(ec->ei, self, 
			    "Missing operant to int2str()");
  }


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
 * Translate a string to another by using an array as a dictionary
 */
static int 
glwf_translate(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *c = eval_pop(ec);  // default is no match
  token_t *b = eval_pop(ec);  // dictionary
  token_t *a = eval_pop(ec);  // original string
  token_t *r;
  int i;
  const char *s;

  if(a == NULL)
    return glw_model_seterr(ec->ei, self, 
			    "Invalid first operand to translate()");

  if(b == NULL || b->type != TOKEN_VECTOR_STRING)
    return glw_model_seterr(ec->ei, self, 
			    "Invalid second operand to translate()");

  if(c == NULL || c->type != TOKEN_STRING)
    return glw_model_seterr(ec->ei, self, 
			    "Invalid third operand to translate()");

  s = c->t_string;
  if(a->type == TOKEN_STRING) {
    for(i = 0; i < b->t_elements; i += 2) {
      if(!strcmp(a->t_string, b->t_string_vector[i])) {
	s = b->t_string_vector[i + 1];
	break;
      }
    }
  }

  r = eval_alloc(self, ec, TOKEN_STRING);
  r->t_string = strdup(s);
  eval_push(ec, r);
  return 0;
}


/**
 * strftime support (only localtime)
 */
static int 
glwf_strftime(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *b = eval_pop(ec);  // format
  token_t *a = eval_pop(ec);  // unixtime
  token_t *r;
  char buf[50];
  struct tm tm;
  time_t t;

  if(a == NULL || a->type != TOKEN_INT)
    return glw_model_seterr(ec->ei, self, 
			    "Invalid first operand to strftime()");

  if(b == NULL || b->type != TOKEN_STRING)
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
glwf_isset(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *a = eval_pop(ec);  // format
  token_t *r;
  int rv;

  if(a == NULL)
    return glw_model_seterr(ec->ei, self, 
			    "Missing operand to isset()");

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
  }

  r = eval_alloc(self, ec, TOKEN_FLOAT);
  r->t_float = rv;
  eval_push(ec, r);
  return 0;
}



/**
 * Return current time
 */
static int 
glwf_time(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *r;
  time_t now;

  time(&now);

  r = eval_alloc(self, ec, TOKEN_INT);
  r->t_int = now;
  eval_push(ec, r);

  ec->persistence = GLW_MAX(ec->persistence, GLW_MODEL_EVAL_EVERY_FRAME);

  return 0;
}

/**
 * Int to string
 */
static int 
glwf_int2duration(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *a = eval_pop(ec);
  token_t *r;
  char tmp[30];
  const char *str = NULL;
  int s = 0;


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
glwf_createchild(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *a = eval_pop(ec);

  if(a == NULL || a->propsubr == NULL) {
    return glw_model_seterr(ec->ei, self, 
			    "Invalid operand to createChild()");
  }

  prop_request_new_child_by_subscription(a->propsubr->gps_sub);
  return 0;
}



/**
 * Delete given property
 */
static int 
glwf_delete(glw_model_eval_context_t *ec, struct token *self)
{
  token_t *a = eval_pop(ec);

  if(a == NULL || a->propsubr == NULL) {
    return glw_model_seterr(ec->ei, self, 
			    "Invalid operand to delete()");
  }

  prop_request_delete_child_by_subscription(a->propsubr->gps_sub);
  return 0;
}


/**
 *
 */
static const token_func_t funcvec[] = {
  {"widget", glwf_widget},
  {"cloner", glwf_cloner},
  {"space", glwf_space},
  {"onEvent", glwf_onEvent},
  {"genericEvent", glwf_genericEvent},
  {"internalEvent", glwf_internalEvent},
  {"changed", glwf_changed, glwf_changed_ctor, glwf_changed_dtor},
  {"iir", glwf_iir},
  {"float2str", glwf_float2str},
  {"int2str", glwf_int2str},
  {"translate", glwf_translate},
  {"sin", glwf_sin},
  {"cos", glwf_cos},
  {"strftime", glwf_strftime},
  {"isset", glwf_isset},
  {"time", glwf_time},
  {"int2duration", glwf_int2duration},
  {"createChild", glwf_createchild},
  {"delete", glwf_delete},
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
