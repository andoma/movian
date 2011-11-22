/*
 *  Property trees
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <unistd.h>
#include <math.h>

#include "arch/atomic.h"

#include "showtime.h"
#include "prop_i.h"
#include "misc/string.h"
#include "event.h"

#ifdef PROP_DEBUG
int prop_trace;
#endif

hts_mutex_t prop_mutex;
hts_mutex_t prop_tag_mutex;
static prop_t *prop_global;

static prop_courier_t *global_courier;

static void prop_unlink0(prop_t *p, prop_sub_t *skipme, const char *origin,
			 struct prop_notify_queue *pnq);

static void prop_flood_flag(prop_t *p, int set, int clr);

#define PROPTRACE(fmt...) trace(TRACE_NO_PROP, TRACE_DEBUG, "prop", fmt)


/**
 *
 */
static void
propname0(prop_t *p, char *buf, size_t bufsiz)
{
  int l;

  if(p->hp_parent != NULL)
    propname0(p->hp_parent, buf, bufsiz);

  l = strlen(buf);
  if(l > 0)
    buf[l++] = '.';
  strcpy(buf + l, p->hp_name ?: "<noname>");
}


/**
 *
 */
const char *
propname(prop_t *p)
{
  static char buf[200];

  if(p == NULL)
    return "nil";
  buf[0] = 0;
  propname0(p, buf, sizeof(buf));
  return buf;
}


/**
 *
 */
const char *
prop_get_name(prop_t *p)
{
  return p->hp_name;
}




/**
 *
 */
typedef struct prop_notify {
  TAILQ_ENTRY(prop_notify) hpn_link;
  prop_sub_t *hpn_sub;
  prop_event_t hpn_event;

  union {
    prop_t *p;
    prop_vec_t *pv;
    struct {
      float f;
      int how;
    } f;
    int i;
    struct {
      rstr_t *rstr;
      prop_str_type_t type;
    } rstr;
    struct event *e;
    struct {
      rstr_t *rtitle;
      rstr_t *rurl;
    } link;
    const char *str;

  } u;

#define hpn_prop   u.p
#define hpn_propv  u.pv
#define hpn_float  u.f.f
#define hpn_float_how  u.f.how
#define hpn_int    u.i
#define hpn_rstring u.rstr.rstr
#define hpn_rstrtype u.rstr.type
#define hpn_cstring u.str
#define hpn_ext_event  u.e
#define hpn_link_rtitle u.link.rtitle
#define hpn_link_rurl   u.link.rurl

  prop_t *hpn_prop2;
  int hpn_flags;

} prop_notify_t;


/**
 * Default lockmanager for normal mutexes
 */
static void
proplockmgr(void *ptr, int lock)
{
  hts_mutex_t *mtx = (hts_mutex_t *)ptr;

  if(lock)
    hts_mutex_lock(mtx);
  else
    hts_mutex_unlock(mtx);
}

#ifdef PROP_DEBUG

hts_mutex_t prop_ref_mutex;

struct prop_ref_trace {
  SIMPLEQ_ENTRY(prop_ref_trace) link;
  const char *file;
  int line;
  int value;
  int which;
};


/**
 *
 */
void
prop_ref_dec_traced(prop_t *p, const char *file, int line)
{
  if(p == NULL)
    return;

  if(p->hp_flags & PROP_REF_TRACED) {
    struct prop_ref_trace *prt = malloc(sizeof(struct prop_ref_trace));
    prt->file = file;
    prt->line = line;
    prt->value = p->hp_refcount - 1;
    prt->which = 0;
    hts_mutex_lock(&prop_ref_mutex);
    SIMPLEQ_INSERT_TAIL(&p->hp_ref_trace, prt, link);
    hts_mutex_unlock(&prop_ref_mutex);
  }
  
  if(atomic_add(&p->hp_refcount, -1) > 1)
    return;
  if(p->hp_flags & PROP_REF_TRACED) 
    printf("Prop %p was finalized by %s:%d\n", p, file, line);
  assert(p->hp_type == PROP_ZOMBIE);

  extern void prop_tag_dump(prop_t *p);
  prop_tag_dump(p);

  assert(p->hp_tags == NULL);
  free(p);
}


/**
 *
 */
prop_t *
prop_ref_inc_traced(prop_t *p, const char *file, int line)
{
  if(p == NULL)
    return NULL;

  atomic_add(&p->hp_refcount, 1);
  if(p->hp_flags & PROP_REF_TRACED) {
    struct prop_ref_trace *prt = malloc(sizeof(struct prop_ref_trace));
    prt->file = file;
    prt->line = line;
    prt->value = p->hp_refcount;
    prt->which = 1;
    hts_mutex_lock(&prop_ref_mutex);
    SIMPLEQ_INSERT_TAIL(&p->hp_ref_trace, prt, link);
    hts_mutex_unlock(&prop_ref_mutex);
  }
  return p;
}


/**
 *
 */
void
prop_enable_trace(prop_t *p)
{
  p->hp_flags |= PROP_REF_TRACED;
}

void
prop_print_trace(prop_t *p)
{
  struct prop_ref_trace *prt;
  
  SIMPLEQ_FOREACH(prt, &p->hp_ref_trace, link) {
    printf("Prop %p %s to %d by %s:%d\n",
	   p,
	   prt->which ? "inc" : "dec",
	   prt->value,
	   prt->file,
	   prt->line);
  }

}


#else

/**
 *
 */
void
prop_ref_dec(prop_t *p)
{
  if(p == NULL || atomic_add(&p->hp_refcount, -1) > 1)
    return;
  assert(p->hp_type == PROP_ZOMBIE);
  assert(p->hp_tags == NULL);
#ifdef PROP_DEBUG
  memset(p, 0xdd, sizeof(prop_t));
#endif
  free(p);
}

/**
 *
 */
prop_t *
prop_ref_inc(prop_t *p)
{
  if(p != NULL)
    atomic_add(&p->hp_refcount, 1);
  return p;
}


#endif



/**
 *
 */
prop_t *
prop_xref_addref(prop_t *p)
{
  if(p != NULL) {
    hts_mutex_lock(&prop_mutex);
    assert(p->hp_xref < 255);
    p->hp_xref++;
    hts_mutex_unlock(&prop_mutex);
  }
  return p;
}


/**
 *
 */
static void
prop_sub_ref_dec(prop_sub_t *s)
{
  if(atomic_add(&s->hps_refcount, -1) > 1)
    return;

  free(s);
}


/**
 *
 */
static void
prop_remove_from_originator(prop_t *p)
{
  LIST_REMOVE(p, hp_originator_link);

  if(p->hp_flags & PROP_XREFED_ORIGINATOR)
    prop_destroy0(p->hp_originator);

  p->hp_originator = NULL;
}


/**
 *
 */
static void
prop_notify_free(prop_notify_t *n)
{
  switch(n->hpn_event) {
  case PROP_SET_DIR:
  case PROP_SET_VOID:
    prop_ref_dec(n->hpn_prop2);
    break;

  case PROP_SET_RSTRING:
    rstr_release(n->hpn_rstring);
    prop_ref_dec(n->hpn_prop2);
    break;

  case PROP_SET_CSTRING:
    prop_ref_dec(n->hpn_prop2);
    break;

  case PROP_SET_RLINK:
    rstr_release(n->hpn_link_rtitle);
    rstr_release(n->hpn_link_rurl);
    prop_ref_dec(n->hpn_prop2);
    break;

  case PROP_SET_INT:
    prop_ref_dec(n->hpn_prop2);
    break;

  case PROP_SET_FLOAT:
    prop_ref_dec(n->hpn_prop2);
    break;

  case PROP_ADD_CHILD:
  case PROP_DEL_CHILD:
  case PROP_REQ_NEW_CHILD:
    prop_ref_dec(n->hpn_prop);
    break;

  case PROP_ADD_CHILD_BEFORE:
  case PROP_MOVE_CHILD:
  case PROP_SELECT_CHILD:
    prop_ref_dec(n->hpn_prop);
    prop_ref_dec(n->hpn_prop2);
    break;

  case PROP_DESTROYED:
    prop_ref_dec(n->hpn_prop);
    break;

  case PROP_EXT_EVENT:
    event_release(n->hpn_ext_event);
    break;

  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
  case PROP_WANT_MORE_CHILDS:
  case PROP_HAVE_MORE_CHILDS:
    break;

  case PROP_ADD_CHILD_VECTOR_BEFORE:
    prop_ref_dec(n->hpn_prop2);
    // FALLTHRU
  case PROP_REQ_DELETE_VECTOR:
  case PROP_ADD_CHILD_VECTOR:
    prop_vec_release(n->hpn_propv);
    break;
  }
  prop_sub_ref_dec(n->hpn_sub);
  free(n); 
}


/**
 *
 */
static void 
trampoline_int(prop_sub_t *s, prop_event_t event, ...)
{
  prop_callback_int_t *cb = s->hps_callback;

  va_list ap;
  va_start(ap, event);

  if(event == PROP_SET_INT) {
    cb(s->hps_opaque, va_arg(ap, int));
  } else if(event == PROP_SET_FLOAT) {
    cb(s->hps_opaque, va_arg(ap, double));
  } else if(event == PROP_SET_RSTRING) {
    cb(s->hps_opaque, atoi(rstr_get(va_arg(ap, rstr_t *))));
  } else if(event == PROP_SET_CSTRING) {
    cb(s->hps_opaque, atoi(va_arg(ap, const char *)));
  } else if(!(s->hps_flags & PROP_SUB_IGNORE_VOID)) {
    cb(s->hps_opaque, 0);
  }
}


/**
 *
 */
static void 
trampoline_float(prop_sub_t *s, prop_event_t event, ...)
{
  prop_callback_float_t *cb = s->hps_callback;

  va_list ap;
  va_start(ap, event);

  if(event == PROP_SET_INT) {
    cb(s->hps_opaque, va_arg(ap, int));
  } else if(event == PROP_SET_FLOAT) {
    cb(s->hps_opaque, va_arg(ap, double));
  } else {
    cb(s->hps_opaque, 0);
  }
}


/**
 *
 */
static void 
trampoline_int_set(prop_sub_t *s, prop_event_t event, ...)
{
  int *ptr = s->hps_opaque;
  va_list ap;
  va_start(ap, event);

  if(event == PROP_SET_INT) {
    *ptr = va_arg(ap, int);
  } else if(event == PROP_SET_FLOAT) {
    *ptr = va_arg(ap, double);
  } else if(event == PROP_SET_RSTRING) {
    *ptr = atoi(rstr_get(va_arg(ap, rstr_t *)));
  } else if(event == PROP_SET_CSTRING) {
    *ptr = atoi(va_arg(ap, const char *));
  } else {
    *ptr = 0;
  }
  va_end(ap);
}


/**
 *
 */
static void 
trampoline_float_set(prop_sub_t *s, prop_event_t event, ...)
{
  float *ptr = (float *)s->hps_opaque;
  va_list ap;
  va_start(ap, event);

  if(event == PROP_SET_INT) {
    *ptr = va_arg(ap, int);
  } else if(event == PROP_SET_FLOAT) {
    *ptr = va_arg(ap, double);
  } else {
    *ptr = 0;
  }
  va_end(ap);
}


/**
 *
 */
static void 
trampoline_string(prop_sub_t *s, prop_event_t event, ...)
{
  prop_callback_string_t *cb = s->hps_callback;

  va_list ap;
  va_start(ap, event);

  if(event == PROP_SET_RSTRING) {
    cb(s->hps_opaque, rstr_get(va_arg(ap, const rstr_t *)));
  } else if(event == PROP_SET_CSTRING) {
    cb(s->hps_opaque, va_arg(ap, const char *));
  } else if(event == PROP_SET_RLINK) {
    cb(s->hps_opaque, rstr_get(va_arg(ap, const rstr_t *)));
  } else {
    cb(s->hps_opaque, NULL);
  }
}


/**
 *
 */
static void 
trampoline_rstr(prop_sub_t *s, prop_event_t event, ...)
{
  prop_callback_rstr_t *cb = s->hps_callback;

  va_list ap;
  va_start(ap, event);

  if(event == PROP_SET_RSTRING) {
    cb(s->hps_opaque, va_arg(ap, rstr_t *));
  } else if(event == PROP_SET_RLINK) {
    cb(s->hps_opaque, va_arg(ap, rstr_t *));
  } else {
    cb(s->hps_opaque, NULL);
  }
}


/**
 *
 */
void
prop_notify_dispatch(struct prop_notify_queue *q)
{
  prop_notify_t *n, *next;
  prop_sub_t *s;
  prop_callback_t *cb;
  prop_trampoline_t *pt;

  for(n = TAILQ_FIRST(q); n != NULL; n = next) {
    next = TAILQ_NEXT(n, hpn_link);

    s = n->hpn_sub;

    if(s->hps_lock != NULL)
      s->hps_lockmgr(s->hps_lock, 1);
    
    if(s->hps_zombie) {
      /* Copy pointers to lock and lockmgr since prop_notify_free()
       * may free the subscription (it decreses its refcount)
       */
      prop_lockmgr_t *lockmgr = s->hps_lockmgr;
      void *lock = s->hps_lock;
      
      prop_notify_free(n); // subscription may be free'd here
      
      if(lock)
	lockmgr(lock, 0);
      continue;
    }

    cb = s->hps_callback;
    pt = s->hps_trampoline;

    switch(n->hpn_event) {
    case PROP_SET_DIR:
    case PROP_SET_VOID:
      if(pt != NULL)
	pt(s, n->hpn_event, n->hpn_prop2);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_prop2);
      if(n->hpn_prop2 != NULL)
	prop_ref_dec(n->hpn_prop2);
      break;

    case PROP_SET_RSTRING:
      if(pt != NULL)
	pt(s, n->hpn_event, n->hpn_rstring, n->hpn_prop2, n->hpn_rstrtype);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_rstring, n->hpn_prop2, n->hpn_rstrtype);
      rstr_release(n->hpn_rstring);
      prop_ref_dec(n->hpn_prop2);
      break;

    case PROP_SET_CSTRING:
      if(pt != NULL)
	pt(s, n->hpn_event, n->hpn_cstring, n->hpn_prop2);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_cstring, n->hpn_prop2);
      prop_ref_dec(n->hpn_prop2);
      break;

    case PROP_SET_RLINK:
      if(pt != NULL)
	pt(s, n->hpn_event, n->hpn_link_rtitle, n->hpn_link_rurl, n->hpn_prop2);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_link_rtitle, n->hpn_link_rurl,
	   n->hpn_prop2);
      rstr_release(n->hpn_link_rtitle);
      rstr_release(n->hpn_link_rurl);
      prop_ref_dec(n->hpn_prop2);
      break;


    case PROP_SET_INT:
      if(pt != NULL)
	pt(s, n->hpn_event, n->hpn_int, n->hpn_prop2);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_int, n->hpn_prop2);
      prop_ref_dec(n->hpn_prop2);
      break;

    case PROP_SET_FLOAT:
      if(pt != NULL)
	pt(s, n->hpn_event, n->hpn_float, n->hpn_prop2,
	   n->hpn_float_how);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_float, n->hpn_prop2,
	   n->hpn_float_how);
      prop_ref_dec(n->hpn_prop2);
      break;

    case PROP_ADD_CHILD:
      if(pt != NULL)
	pt(s, n->hpn_event, n->hpn_prop, n->hpn_flags);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_prop, n->hpn_flags);
      prop_ref_dec(n->hpn_prop);
      break;

    case PROP_ADD_CHILD_BEFORE:
    case PROP_MOVE_CHILD:
    case PROP_SELECT_CHILD:
      if(pt != NULL)
	cb(s, n->hpn_event, n->hpn_prop, n->hpn_prop2, n->hpn_flags);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_prop, 
	   n->hpn_prop2, n->hpn_flags);
      if(n->hpn_prop != NULL)
	prop_ref_dec(n->hpn_prop);
      if(n->hpn_prop2 != NULL)
	prop_ref_dec(n->hpn_prop2);
      break;

    case PROP_DEL_CHILD:
    case PROP_REQ_NEW_CHILD:
      if(pt != NULL)
	pt(s, n->hpn_event, n->hpn_prop);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_prop);
      if(n->hpn_prop != NULL)
	prop_ref_dec(n->hpn_prop);
      break;
 
    case PROP_DESTROYED:
      if(pt != NULL)
	pt(s, n->hpn_event, n->hpn_prop, s);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_prop, s);
      prop_ref_dec(n->hpn_prop);
      break;

    case PROP_EXT_EVENT:
      if(pt != NULL)
	pt(s, n->hpn_event, n->hpn_ext_event);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_ext_event);
      event_release(n->hpn_ext_event);
      break;

    case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
    case PROP_WANT_MORE_CHILDS:
    case PROP_HAVE_MORE_CHILDS:
      if(pt != NULL)
	pt(s, n->hpn_event);
      else
	cb(s->hps_opaque, n->hpn_event);
      break;

    case PROP_REQ_DELETE_VECTOR:
    case PROP_ADD_CHILD_VECTOR:
      if(pt != NULL)
	pt(s, n->hpn_event, n->hpn_propv);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_propv);

      prop_vec_release(n->hpn_propv);
      break;

    case PROP_ADD_CHILD_VECTOR_BEFORE:
      if(pt != NULL)
	pt(s, n->hpn_event, n->hpn_propv, n->hpn_prop2);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_propv, n->hpn_prop2);

      prop_vec_release(n->hpn_propv);
      prop_ref_dec(n->hpn_prop2);
      break;
    }

    if(s->hps_lock != NULL)
      s->hps_lockmgr(s->hps_lock, 0);
 
    prop_sub_ref_dec(s);
    free(n);
  }
}



/**
 * Thread for dispatching prop_notify entries
 */
static void *
prop_courier(void *aux)
{
  prop_courier_t *pc = aux;
  struct prop_notify_queue q_exp, q_nor;
  prop_notify_t *n;


  hts_mutex_lock(&prop_mutex);

  while(pc->pc_run) {

    if(TAILQ_FIRST(&pc->pc_queue_exp) == NULL &&
       TAILQ_FIRST(&pc->pc_queue_nor) == NULL) {
      hts_cond_wait(&pc->pc_cond, &prop_mutex);
      continue;
    }

    TAILQ_MOVE(&q_exp, &pc->pc_queue_exp, hpn_link);
    TAILQ_INIT(&pc->pc_queue_exp);

    TAILQ_MOVE(&q_nor, &pc->pc_queue_nor, hpn_link);
    TAILQ_INIT(&pc->pc_queue_nor);

    hts_mutex_unlock(&prop_mutex);
    prop_notify_dispatch(&q_exp);
    prop_notify_dispatch(&q_nor);
    hts_mutex_lock(&prop_mutex);
  }

  while((n = TAILQ_FIRST(&pc->pc_queue_exp)) != NULL) {
    TAILQ_REMOVE(&pc->pc_queue_exp, n, hpn_link);
    prop_notify_free(n);
  }

  while((n = TAILQ_FIRST(&pc->pc_queue_nor)) != NULL) {
    TAILQ_REMOVE(&pc->pc_queue_nor, n, hpn_link);
    prop_notify_free(n);
  }

  if(pc->pc_detached)
    free(pc);

  hts_mutex_unlock(&prop_mutex);
  return NULL;
}

/**
 *
 */
static void
courier_enqueue(prop_sub_t *s, prop_notify_t *n)
{
  prop_courier_t *pc = s->hps_courier;
  
  if(s->hps_flags & PROP_SUB_EXPEDITE)
    TAILQ_INSERT_TAIL(&pc->pc_queue_exp, n, hpn_link);
  else
    TAILQ_INSERT_TAIL(&pc->pc_queue_nor, n, hpn_link);
  if(pc->pc_has_cond)
    hts_cond_signal(&pc->pc_cond);
  else if(pc->pc_notify != NULL)
    pc->pc_notify(pc->pc_opaque);
}


/**
 *
 */
static prop_notify_t *
get_notify(prop_sub_t *s)
{
  prop_notify_t *n = malloc(sizeof(prop_notify_t));
  atomic_add(&s->hps_refcount, 1);
  n->hpn_sub = s;
  return n;
}


/**
 *
 */
static void
prop_build_notify_value(prop_sub_t *s, int direct, const char *origin,
			prop_t *p, struct prop_notify_queue *pnq,
			int how)
{
  prop_notify_t *n;

  if(s->hps_flags & PROP_SUB_DEBUG) {
    switch(p->hp_type) {
    case PROP_RSTRING:
      PROPTRACE("rstr(%s) by %s%s", 
		rstr_get(p->hp_rstring), origin,
		s->hps_flags & PROP_SUB_EXPEDITE ? " (exp)" : "");
      break;
    case PROP_CSTRING:
      PROPTRACE("cstr(%s) by %s%s", 
		p->hp_cstring, origin,
		s->hps_flags & PROP_SUB_EXPEDITE ? " (exp)" : "");
      break;
    case PROP_LINK:
      PROPTRACE("link(%s,%s) by %s%s", 
	    rstr_get(p->hp_link_rtitle), rstr_get(p->hp_link_rurl), origin,
	    s->hps_flags & PROP_SUB_EXPEDITE ? " (exp)" : "");
      break;
    case PROP_FLOAT:
      PROPTRACE("float(%f) by %s %s%s <%d>", p->hp_float, origin,
		s->hps_flags & PROP_SUB_EXPEDITE ? " (exp)" : "",
		how);
      break;
    case PROP_INT:
      PROPTRACE("int(%d) by %s%s", p->hp_int, origin,
	    s->hps_flags & PROP_SUB_EXPEDITE ? " (exp)" : "");
      break;
    case PROP_DIR:
      PROPTRACE("dir by %s%s", origin,
	    s->hps_flags & PROP_SUB_EXPEDITE ? " (exp)" : "");
      break;
    case PROP_VOID:
      PROPTRACE("void by %s%s", origin,
	    s->hps_flags & PROP_SUB_EXPEDITE ? " (exp)" : "");
      break;
    case PROP_ZOMBIE:
      break;
    }
  }
  if(direct || s->hps_flags & PROP_SUB_INTERNAL) {

    assert(pnq == NULL); // Delayed updates are not compatile with direct mode

    /* Direct mode can be requested during subscribe to get
       the current values updated directly without dispatch
       via the courier */

    prop_callback_t *cb = s->hps_callback;
    prop_trampoline_t *pt = s->hps_trampoline;

    switch(p->hp_type) {
    case PROP_RSTRING:
      if(pt != NULL)
	pt(s, PROP_SET_RSTRING, p->hp_rstring, p, p->hp_rstrtype);
      else
	cb(s->hps_opaque, PROP_SET_RSTRING, p->hp_rstring, p, p->hp_rstrtype);
      break;

    case PROP_CSTRING:
      if(pt != NULL)
	pt(s, PROP_SET_CSTRING, p->hp_cstring, p);
      else
	cb(s->hps_opaque, PROP_SET_CSTRING, p->hp_cstring, p);
      break;

    case PROP_LINK:
      if(pt != NULL)
	pt(s, PROP_SET_RLINK, p->hp_link_rtitle,
	   p->hp_link_rurl, p);
      else
	cb(s->hps_opaque, PROP_SET_RLINK, 
	   p->hp_link_rtitle, p->hp_link_rurl, p);
      break;

    case PROP_FLOAT:
      if(pt != NULL)
	pt(s, PROP_SET_FLOAT, p->hp_float, p, how);
      else
	cb(s->hps_opaque, PROP_SET_FLOAT, p->hp_float, p, how);
      break;

    case PROP_INT:
      if(pt != NULL)
	pt(s, PROP_SET_INT, p->hp_int, p);
      else
	cb(s->hps_opaque, PROP_SET_INT, p->hp_int, p);
      break;

    case PROP_DIR:
      if(pt != NULL)
	pt(s, PROP_SET_DIR, p);
      else
	cb(s->hps_opaque, PROP_SET_DIR, p);
      break;

    case PROP_VOID:
      if(pt != NULL)
	pt(s, PROP_SET_VOID, p);
      else
	cb(s->hps_opaque, PROP_SET_VOID, p);
      break;

    case PROP_ZOMBIE:
      abort();

    }
    return;
  }

  n = get_notify(s);

  n->hpn_prop2 = prop_ref_inc(p);

  switch(p->hp_type) {
  case PROP_RSTRING:
    n->hpn_rstring = rstr_dup(p->hp_rstring);
    n->hpn_rstrtype = p->hp_rstrtype;
    n->hpn_event = PROP_SET_RSTRING;
    break;

  case PROP_CSTRING:
    n->hpn_cstring = p->hp_cstring;
    n->hpn_event = PROP_SET_CSTRING;
    break;

  case PROP_LINK:
    n->hpn_link_rtitle = rstr_dup(p->hp_link_rtitle);
    n->hpn_link_rurl   = rstr_dup(p->hp_link_rurl);
    n->hpn_event = PROP_SET_RLINK;
    break;

  case PROP_FLOAT:
    n->hpn_float = p->hp_float;
    n->hpn_float_how = how;
    n->hpn_event = PROP_SET_FLOAT;
    break;

  case PROP_INT:
    n->hpn_float = p->hp_float;
    n->hpn_event = PROP_SET_INT;
    break;

  case PROP_DIR:
    n->hpn_event = PROP_SET_DIR;
    break;

  case PROP_VOID:
    n->hpn_event = PROP_SET_VOID;
    break;

  case PROP_ZOMBIE:
    abort();
  }

  if(pnq) {
    TAILQ_INSERT_TAIL(pnq, n, hpn_link);
  } else {
    courier_enqueue(s, n);
  }
}



/**
 *
 */
static void
prop_notify_void(prop_sub_t *s)
{
  prop_notify_t *n = get_notify(s);

  n->hpn_event = PROP_SET_VOID;
  n->hpn_prop2 = NULL;
  courier_enqueue(s, n);
}


/**
 *
 */
static void
prop_notify_destroyed(prop_sub_t *s, prop_t *p)
{
  if(s->hps_flags & PROP_SUB_INTERNAL) {
    prop_callback_t *cb = s->hps_callback;
    prop_trampoline_t *pt = s->hps_trampoline;

    if(pt != NULL)
      pt(s, PROP_DESTROYED, p, s);
    else
      cb(s->hps_opaque, PROP_DESTROYED, p, s);
    return;
  }

  prop_notify_t *n = get_notify(s);

  n->hpn_event = PROP_DESTROYED;
  n->hpn_prop = p;
  atomic_add(&p->hp_refcount, 1);
  courier_enqueue(s, n);
}


/**
 *
 */
static void
prop_notify_value(prop_t *p, prop_sub_t *skipme, const char *origin,
		  int how)
{
  prop_sub_t *s;

  LIST_FOREACH(s, &p->hp_value_subscriptions, hps_value_prop_link)
    if(s != skipme)
      prop_build_notify_value(s, 0, origin, s->hps_value_prop, NULL,
			      how);

  if(p->hp_flags & PROP_MULTI_NOTIFY)
    while((p = p->hp_parent) != NULL)
      if(p->hp_flags & PROP_MULTI_SUB)
	LIST_FOREACH(s, &p->hp_value_subscriptions, hps_value_prop_link)
	  if(s->hps_flags & PROP_SUB_MULTI)
	    prop_build_notify_value(s, 0, origin, p, NULL, 0);
}


/**
 *
 */
static void
prop_build_notify_child(prop_sub_t *s, prop_t *p, prop_event_t event,
			int direct, int flags)
{
  prop_notify_t *n;

  if(direct || s->hps_flags & PROP_SUB_INTERNAL) {
    prop_callback_t *cb = s->hps_callback;
    prop_trampoline_t *pt = s->hps_trampoline;
    if(pt != NULL)
      pt(s, event, p, flags);
    else
      cb(s->hps_opaque, event, p, flags);
    return;
  }

  n = get_notify(s);

  if(p != NULL)
    atomic_add(&p->hp_refcount, 1);
  n->hpn_flags = flags;
  n->hpn_prop = p;
  n->hpn_event = event;
  courier_enqueue(s, n);
}


/**
 *
 */
static void
prop_notify_child(prop_t *child, prop_t *parent, prop_event_t event,
		  prop_sub_t *skipme, int flags)
{
  prop_sub_t *s;

  LIST_FOREACH(s, &parent->hp_value_subscriptions, hps_value_prop_link)
    if(s != skipme)
      prop_build_notify_child(s, child, event, 0, flags);
}




/**
 *
 */
static void
prop_build_notify_child2(prop_sub_t *s, prop_t *p, prop_t *extra, 
			 prop_event_t event, int direct, int flags)
{
  prop_notify_t *n;

  if(direct || s->hps_flags & PROP_SUB_INTERNAL) {
    prop_callback_t *cb = s->hps_callback;
    prop_trampoline_t *pt = s->hps_trampoline;

    if(pt != NULL)
      pt(s, event, p, extra, flags);
    else
      cb(s->hps_opaque, event, p, extra, flags);
    return;
  }

  n = get_notify(s);

  atomic_add(&p->hp_refcount, 1);
  if(extra != NULL)
    atomic_add(&extra->hp_refcount, 1);

  n->hpn_prop = p;
  n->hpn_prop2 = extra;
  n->hpn_event = event;
  n->hpn_flags = flags;
  courier_enqueue(s, n);
}


/**
 *
 */
static void
prop_notify_child2(prop_t *child, prop_t *parent, prop_t *sibling,
		   prop_event_t event, prop_sub_t *skipme, int flags)
{
  prop_sub_t *s;

  LIST_FOREACH(s, &parent->hp_value_subscriptions, hps_value_prop_link)
    if(s != skipme)
      prop_build_notify_child2(s, child, sibling, event, 0, flags);
}



/**
 *
 */
static void
prop_build_notify_childv(prop_sub_t *s, prop_vec_t *pv, prop_event_t event,
			 prop_t *p2)
{
  prop_notify_t *n = get_notify(s);
  n->hpn_propv = prop_vec_addref(pv);
  n->hpn_flags = 0;
  n->hpn_event = event;
  n->hpn_prop2 = prop_ref_inc(p2);
  courier_enqueue(s, n);
}


/**
 *
 */
void
prop_notify_childv(prop_vec_t *pv, prop_t *parent, prop_event_t event,
		   prop_sub_t *skipme, prop_t *p2)
{
  prop_sub_t *s;

  LIST_FOREACH(s, &parent->hp_value_subscriptions, hps_value_prop_link)
    if(s != skipme)
      prop_build_notify_childv(s, pv, event, p2);
}


/**
 *
 */
static void
prop_send_ext_event0(prop_t *p, event_t *e)
{
  prop_sub_t *s;
  prop_notify_t *n;

  while(p->hp_originator != NULL)
    p = p->hp_originator;

  LIST_FOREACH(s, &p->hp_value_subscriptions, hps_value_prop_link) {
    n = get_notify(s);

    n->hpn_event = PROP_EXT_EVENT;
    atomic_add(&e->e_refcount, 1);
    n->hpn_ext_event = e;
    courier_enqueue(s, n);
  }
}


/**
 *
 */
static void
prop_send_event(prop_t *p, prop_event_t e)
{
  prop_sub_t *s;
  prop_notify_t *n;

  LIST_FOREACH(s, &p->hp_value_subscriptions, hps_value_prop_link) {
    if(s->hps_flags & PROP_SUB_INTERNAL) {
      prop_callback_t *cb = s->hps_callback;
      prop_trampoline_t *pt = s->hps_trampoline;

      if(pt != NULL)
	pt(s, e);
      else
	cb(s->hps_opaque, e);
    } else {
      n = get_notify(s);
      n->hpn_event = e;
      courier_enqueue(s, n);
    }
  }
}


/**
 *
 */
static void
prop_send_subscription_monitor_active(prop_t *p)
{
  prop_sub_t *s;
  prop_notify_t *n;

  LIST_FOREACH(s, &p->hp_value_subscriptions, hps_value_prop_link) {
    if(s->hps_flags & PROP_SUB_SUBSCRIPTION_MONITOR) {
      n = get_notify(s);
      n->hpn_event = PROP_SUBSCRIPTION_MONITOR_ACTIVE;
      courier_enqueue(s, n);
    }
  }
}


/**
 *
 */
void
prop_send_ext_event(prop_t *p, event_t *e)
{
  hts_mutex_lock(&prop_mutex);
  prop_send_ext_event0(p, e);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
static int
prop_clean(prop_t *p)
{
  if(p->hp_flags & PROP_CLIPPED_VALUE) {
    return 1;
  }
  switch(p->hp_type) {
  case PROP_ZOMBIE:
  case PROP_DIR:
    return 1;

  case PROP_VOID:
  case PROP_INT:
  case PROP_FLOAT:
  case PROP_CSTRING:
    break;

  case PROP_RSTRING:
    rstr_release(p->hp_rstring);
    break;

  case PROP_LINK:
    rstr_release(p->hp_link_rtitle);
    rstr_release(p->hp_link_rurl);
    break;
  }
  return 0;
}


/**
 *
 */
static void
prop_make_dir(prop_t *p, prop_sub_t *skipme, const char *origin)
{
  if(p->hp_type == PROP_DIR)
    return;

  if(prop_clean(p))
    abort();
  
  TAILQ_INIT(&p->hp_childs);
  p->hp_selected = NULL;
  p->hp_type = PROP_DIR;
  
  prop_notify_value(p, skipme, origin, 0);
}


/**
 *
 */
static void
prop_insert(prop_t *p, prop_t *parent, prop_t *before, prop_sub_t *skipme)
{
  if(before != NULL) {
    assert(before->hp_parent == parent);
    TAILQ_INSERT_BEFORE(before, p, hp_parent_link);
    prop_notify_child2(p, parent, before, PROP_ADD_CHILD_BEFORE, skipme, 0);
  } else {
    TAILQ_INSERT_TAIL(&parent->hp_childs, p, hp_parent_link);
    prop_notify_child(p, parent, PROP_ADD_CHILD, skipme, 0);
  }
}


/**
 *
 */
prop_t *
prop_make(const char *name, int noalloc, prop_t *parent)
{
  prop_t *hp;
  hp = malloc(sizeof(prop_t));
#ifdef PROP_DEBUG
  SIMPLEQ_INIT(&hp->hp_ref_trace);
#endif
  hp->hp_flags = noalloc ? PROP_NAME_NOT_ALLOCATED : 0;
  hp->hp_originator = NULL;
  hp->hp_refcount = 1;
  hp->hp_xref = 1;
  hp->hp_type = PROP_VOID;
  if(noalloc)
    hp->hp_name = name;
  else
    hp->hp_name = name ? strdup(name) : NULL;

  hp->hp_tags = NULL;
  LIST_INIT(&hp->hp_targets);
  LIST_INIT(&hp->hp_value_subscriptions);
  LIST_INIT(&hp->hp_canonical_subscriptions);

  hp->hp_parent = parent;
  return hp;
}


/**
 *
 */
prop_t *
prop_create0(prop_t *parent, const char *name, prop_sub_t *skipme, int noalloc)
{
  prop_t *hp;

  assert(parent->hp_type != PROP_ZOMBIE);

  prop_make_dir(parent, skipme, "prop_create()");

  if(name != NULL) {
    TAILQ_FOREACH(hp, &parent->hp_childs, hp_parent_link) {
      if(hp->hp_name != NULL && !strcmp(hp->hp_name, name)) {
	return hp;
      }
    }
  }

  hp = prop_make(name, noalloc, parent);

  if(parent->hp_flags & (PROP_MULTI_SUB | PROP_MULTI_NOTIFY))
    prop_flood_flag(hp, PROP_MULTI_NOTIFY, 0);

  prop_insert(hp, parent, NULL, skipme);
  return hp;
}



/**
 *
 */
prop_t *
prop_create_ex(prop_t *parent, const char *name, prop_sub_t *skipme,
	       int noalloc)
{
  prop_t *p;
  hts_mutex_lock(&prop_mutex);
  if(parent != NULL && parent->hp_type != PROP_ZOMBIE) {
    p = prop_create0(parent, name, skipme, noalloc);
  } else {
    p = NULL;
  }
  hts_mutex_unlock(&prop_mutex);
  return p;
}

/**
 *
 */
prop_t *
prop_create_root_ex(const char *name, int noalloc)
{
  return prop_make(name, noalloc, NULL);
}



/**
 *
 */
int
prop_set_parent0(prop_t *p, prop_t *parent, prop_t *before, prop_sub_t *skipme)
{
  if(parent->hp_type == PROP_ZOMBIE)
    return -1;

  prop_make_dir(parent, skipme, "prop_set_parent()");

  if(p->hp_parent != parent) {
    prop_unparent0(p, skipme);

    p->hp_parent = parent;
    if(parent->hp_flags & (PROP_MULTI_SUB | PROP_MULTI_NOTIFY))
      prop_flood_flag(p, PROP_MULTI_NOTIFY, 0);
    prop_insert(p, parent, before, skipme);
  } else {
    prop_move0(p, before, skipme);
  }
  return 0;
}


/**
 *
 */
int
prop_set_parent_ex(prop_t *p, prop_t *parent, prop_t *before, 
		   prop_sub_t *skipme)
{
  int r;
  if(parent == NULL)
    return -1;

  hts_mutex_lock(&prop_mutex);
  r = prop_set_parent0(p, parent, before, skipme);
  hts_mutex_unlock(&prop_mutex);
  return r;
}


/**
 *
 */
void
prop_set_parent_vector(prop_vec_t *pv, prop_t *parent, prop_t *before,
		       prop_sub_t *skipme)
{
  int i;

  hts_mutex_lock(&prop_mutex);

  if(parent == NULL || parent->hp_type == PROP_ZOMBIE) {

  for(i = 0; i < pv->pv_length; i++)
    prop_destroy0(pv->pv_vec[i]);

  } else {

    prop_t *p;

    prop_make_dir(parent, NULL, "prop_set_parent_multi()");

    for(i = 0; i < pv->pv_length; i++) {
      p = pv->pv_vec[i];
      p->hp_parent = parent;
      if(parent->hp_flags & (PROP_MULTI_SUB | PROP_MULTI_NOTIFY))
	prop_flood_flag(p, PROP_MULTI_NOTIFY, 0);
    
      if(before) {
	TAILQ_INSERT_BEFORE(before, p, hp_parent_link);
      } else {
	TAILQ_INSERT_TAIL(&parent->hp_childs, p, hp_parent_link);
      }
    }
    prop_notify_childv(pv, parent, before ? PROP_ADD_CHILD_VECTOR_BEFORE : 
		       PROP_ADD_CHILD_VECTOR, skipme, before);
  }
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_unparent0(prop_t *p, prop_sub_t *skipme)
{
  prop_t *parent = p->hp_parent;
  if(parent == NULL)
    return;

  assert((p->hp_flags & PROP_MULTI_NOTIFY) == 0); // fixme

  prop_notify_child(p, parent, PROP_DEL_CHILD, NULL, 0);
  
  TAILQ_REMOVE(&parent->hp_childs, p, hp_parent_link);
  p->hp_parent = NULL;
  
  if(parent->hp_selected == p)
    parent->hp_selected = NULL;
}

/**
 *
 */
void
prop_unparent_ex(prop_t *p, prop_sub_t *skipme)
{
  hts_mutex_lock(&prop_mutex);
  prop_unparent0(p, skipme);
  hts_mutex_unlock(&prop_mutex);
}

/**
 *
 */
void
prop_unparent_childs(prop_t *p)
{
  hts_mutex_lock(&prop_mutex);
  if(p->hp_type == PROP_DIR) {
    prop_t *c, *next;
    for(c = TAILQ_FIRST(&p->hp_childs); c != NULL; c = next) {
      next = TAILQ_NEXT(c, hp_parent_link);
      prop_unparent0(p, NULL);
    }
  }
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
static void
prop_destroy_child(prop_t *p, prop_t *c)
{
  if(!prop_destroy0(c)) {
    prop_notify_child(c, p, PROP_DEL_CHILD, NULL, 0);
    TAILQ_REMOVE(&p->hp_childs, c, hp_parent_link);
    c->hp_parent = NULL;
  }
}


/**
 *
 */
int
prop_destroy0(prop_t *p)
{
  prop_t *c, *next, *parent;
  prop_sub_t *s;

#ifdef PROP_DEBUG
  if(prop_trace) {
    int csubs = 0, psubs = 0;
    LIST_FOREACH(s, &p->hp_canonical_subscriptions, hps_canonical_prop_link)
      csubs++;
    LIST_FOREACH(s, &p->hp_value_subscriptions, hps_value_prop_link)
      psubs++;

    printf("Entering prop_destroy0(%s) [type=%d, refcnt=%d, xref=%d, csubs=%d, psubs=%d]\n",
	   propname(p), p->hp_type, p->hp_refcount, p->hp_xref,
	   csubs, psubs);
  }
#endif

  if(p->hp_type == PROP_ZOMBIE)
    return 0;

  p->hp_xref--;
  if(p->hp_xref)
    return 0;

  switch(p->hp_type) {
  case PROP_ZOMBIE:
    abort();

  case PROP_DIR:
    for(c = TAILQ_FIRST(&p->hp_childs); c != NULL; c = next) {
      next = TAILQ_NEXT(c, hp_parent_link);
      prop_destroy_child(p, c);
    }
    break;

  case PROP_RSTRING:
    rstr_release(p->hp_rstring);
    break;

  case PROP_LINK:
    rstr_release(p->hp_link_rtitle);
    rstr_release(p->hp_link_rurl);
    break;

  case PROP_FLOAT:
  case PROP_INT:
  case PROP_VOID:
  case PROP_CSTRING:
    break;
  }

  p->hp_type = PROP_ZOMBIE;

  while((s = LIST_FIRST(&p->hp_canonical_subscriptions)) != NULL) {

    LIST_REMOVE(s, hps_canonical_prop_link);
    s->hps_canonical_prop = NULL;

    if(s->hps_flags & PROP_SUB_TRACK_DESTROY)
      prop_notify_destroyed(s, p);
    if(s->hps_flags & PROP_SUB_AUTO_DESTROY)
      prop_unsubscribe0(s);
  }

  while((s = LIST_FIRST(&p->hp_value_subscriptions)) != NULL) {
    prop_notify_void(s);

    LIST_REMOVE(s, hps_value_prop_link);
    s->hps_value_prop = NULL;
  }

  while((c = LIST_FIRST(&p->hp_targets)) != NULL)
    prop_unlink0(c, NULL, "prop_destroy0", NULL);

  if(p->hp_originator != NULL)
    prop_remove_from_originator(p);

#ifdef PROP_DEBUG
  if(prop_trace)
    printf("Leaving prop_destroy0(%s) parent=%p\n", propname(p),
	   p->hp_parent);
#endif

  if(p->hp_parent != NULL) {
    prop_notify_child(p, p->hp_parent, PROP_DEL_CHILD, NULL, 0);
    parent = p->hp_parent;

    TAILQ_REMOVE(&parent->hp_childs, p, hp_parent_link);
    p->hp_parent = NULL;

    if(parent->hp_selected == p)
      parent->hp_selected = NULL;
  }

  if(!(p->hp_flags & PROP_NAME_NOT_ALLOCATED))
    free((void *)p->hp_name);
  p->hp_name = NULL;

  prop_ref_dec(p);
  return 1;
}


/**
 *
 */
void
prop_destroy(prop_t *p)
{
  hts_mutex_lock(&prop_mutex);
  prop_destroy0(p);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_destroy_childs(prop_t *p)
{
  hts_mutex_lock(&prop_mutex);
  if(p->hp_type == PROP_DIR) {
    prop_t *c, *next;
    for(c = TAILQ_FIRST(&p->hp_childs); c != NULL; c = next) {
      next = TAILQ_NEXT(c, hp_parent_link);
      prop_destroy_child(p, c);
    }
  }
  hts_mutex_unlock(&prop_mutex);
}

/**
 *
 */
void
prop_destroy_by_name(prop_t *p, const char *name)
{
  hts_mutex_lock(&prop_mutex);
  if(p->hp_type == PROP_DIR) {
    prop_t *c;
    TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link) {
      if(c->hp_name != NULL && !strcmp(c->hp_name, name)) {
	prop_destroy_child(p, c);
	break;
      }
    }
  }
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
static void
prop_flood_flag(prop_t *p, int set, int clr)
{
  prop_t *c;

  p->hp_flags = (p->hp_flags | set) & ~clr;
  if(p->hp_type == PROP_DIR)
    TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link)
      prop_flood_flag(c, set, clr);
}


/**
 *
 */
static void
prop_flood_flag_on_childs(prop_t *p, int set, int clr)
{
  prop_t *c;

  TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link)
    prop_flood_flag(c, set, clr);
}


/**
 *
 */
static void
prop_set_multi(prop_t *p)
{
  if(p->hp_flags & PROP_MULTI_SUB)
    return;

  p->hp_flags |= PROP_MULTI_SUB;

  if(p->hp_type == PROP_DIR)
    prop_flood_flag_on_childs(p, PROP_MULTI_NOTIFY, 0);
}


/**
 *
 */
static void
prop_clr_multi(prop_t *p)
{
  p->hp_flags &= ~PROP_MULTI_SUB;

  if(p->hp_type == PROP_DIR && !(p->hp_flags & PROP_MULTI_NOTIFY))
    prop_flood_flag_on_childs(p, 0, PROP_MULTI_NOTIFY);
}


/**
 *
 */
void
prop_move0(prop_t *p, prop_t *before, prop_sub_t *skipme)
{
  prop_t *parent;

  assert(p != before);

  if(TAILQ_NEXT(p, hp_parent_link) != before) {

    parent = p->hp_parent;
    TAILQ_REMOVE(&parent->hp_childs, p, hp_parent_link);
  
    if(before != NULL) {
      TAILQ_INSERT_BEFORE(before, p, hp_parent_link);
    } else {
      TAILQ_INSERT_TAIL(&parent->hp_childs, p, hp_parent_link);
    }  
    prop_notify_child2(p, parent, before, PROP_MOVE_CHILD, skipme, 0);
  }
}


/**
 *
 */
void
prop_move(prop_t *p, prop_t *before)
{
  hts_mutex_lock(&prop_mutex);
  prop_move0(p, before, NULL);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
static prop_t *
prop_subfind(prop_t *p, const char **name, int follow_symlinks, int allow_indexing)
{
  prop_t *c;

  while(name[0] != NULL) {
    while(follow_symlinks && p->hp_originator != NULL)
      p = p->hp_originator;

    if(p->hp_type != PROP_DIR) {

      if(p->hp_type != PROP_VOID) {
	/* We don't want subscriptions to overwrite real values */
	return NULL;
      }

      TAILQ_INIT(&p->hp_childs);
      p->hp_selected = NULL;
      p->hp_type = PROP_DIR;

      prop_notify_value(p, NULL, "prop_subfind()", 0);
    }

    if(allow_indexing && name[0][0] == '*') {
      unsigned int i = atoi(name[0]+1);
      TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link) {
	if(i == 0)
	  break;
	i--;
      }
      if(c == NULL)
	return NULL;

    } else {

      TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link) {
	if(c->hp_name != NULL && !strcmp(c->hp_name, name[0]))
	  break;
      }
    }
    p = c ?: prop_create0(p, name[0], NULL, 0);    
    name++;
  }

  while(follow_symlinks && p->hp_originator != NULL)
    p = p->hp_originator;

  return p;
}


LIST_HEAD(prop_root_list, prop_root);

/**
 *
 */
typedef struct prop_root {
  prop_t *p;
  const char *name;
  LIST_ENTRY(prop_root) link;
} prop_root_t;



/**
 *
 */
static prop_t *
prop_resolve_tree(const char *name, struct prop_root_list *prl)
{
  prop_t *p;
  prop_root_t *pr;
  
  if(!strcmp(name, "global")) {
    p = prop_global;
    return p->hp_type == PROP_ZOMBIE ? NULL : p;
  }
  LIST_FOREACH(pr, prl, link) {
    p = pr->p;
    if(p->hp_name != NULL && !strcmp(name, p->hp_name))
      return p->hp_type == PROP_ZOMBIE ? NULL : p;
    if(pr->name != NULL   && !strcmp(name, pr->name))
      return p->hp_type == PROP_ZOMBIE ? NULL : p;
  }
  return NULL;
}

/**
 *
 */
prop_t *
prop_get_by_name(const char **name, int follow_symlinks, ...)
{
  prop_t *p;
  prop_root_t *pr;
  struct prop_root_list proproots;
  int tag;
  va_list ap;

  va_start(ap, follow_symlinks);

  LIST_INIT(&proproots);

  do {
    tag = va_arg(ap, int);
    switch(tag) {

    case PROP_TAG_ROOT:
      pr = alloca(sizeof(prop_root_t));
      pr->p = va_arg(ap, prop_t *);
      pr->name = NULL;
      if(pr->p != NULL)
	LIST_INSERT_HEAD(&proproots, pr, link);
      break;

    case PROP_TAG_NAMED_ROOT:
      pr = alloca(sizeof(prop_root_t));
      pr->p = va_arg(ap, prop_t *);
      pr->name = va_arg(ap, const char *);
      if(pr->p != NULL && pr->name != NULL)
	LIST_INSERT_HEAD(&proproots, pr, link);
      break;

    case PROP_TAG_END:
      break;

    default:
      abort();
    }
  } while(tag);
  
  va_end(ap);

  p = prop_resolve_tree(name[0], &proproots);

  if(p == NULL)
    return NULL;

  name++;
  hts_mutex_lock(&prop_mutex);
  p = prop_subfind(p, name, follow_symlinks, 1);

  p = prop_ref_inc(p);

  hts_mutex_unlock(&prop_mutex);
  return p;
}


/**
 *
 */
static int
gen_add_flags(prop_t *c, prop_t *p)
{
  return c == p->hp_selected ? PROP_ADD_SELECTED : 0;
}


/**
 *
 */
prop_sub_t *
prop_subscribe(int flags, ...)
{
  prop_t *p, *value, *canonical, *c;
  prop_sub_t *s, *t;
  int direct = !!(flags & (PROP_SUB_DIRECT_UPDATE | PROP_SUB_INTERNAL));
  int notify_now = !(flags & PROP_SUB_NO_INITIAL_UPDATE);
  int tag;
  const char **name = NULL;
  void *opaque = NULL;
  prop_courier_t *pc = NULL;
  void *lock = NULL;
  prop_lockmgr_t *lockmgr = proplockmgr;
  prop_root_t *pr;
  struct prop_root_list proproots;
  void *cb = NULL;
  prop_trampoline_t *trampoline = NULL;
  int dolock = !(flags & PROP_SUB_DONTLOCK);
  int activate_on_canonical = 0;
  va_list ap;
  va_start(ap, flags);

  LIST_INIT(&proproots);

  do {
    tag = va_arg(ap, int);
    switch(tag) {
    case PROP_TAG_NAME_VECTOR:
      name = va_arg(ap,  const char **);
      break;

    case PROP_TAG_NAMESTR:
      do {
	const char *s, *s0 = va_arg(ap, const char *);
	int segments = 1, ptr = 0, len;
	char **nv;

	for(s = s0; *s != 0; s++)
	  if(*s == '.')
	    segments++;

	nv = alloca((segments + 1) * sizeof(char *));
	name = (void *)nv;

	for(s = s0; *s != 0; s++) {
	  if(*s == '.') {
	    if(s == s0)
	      return NULL;
	    len = s - s0;
	    nv[ptr] = alloca(len + 1);
	    memcpy(nv[ptr], s0, len);
	    nv[ptr++][len] = 0;
	    s0 = s + 1;
	  }
	}

	len = s - s0;
	nv[ptr] = alloca(len + 1);
	memcpy(nv[ptr], s0, len);
	nv[ptr++][len] = 0;
	assert(ptr == segments);
	nv[segments] = NULL;
      } while(0);
      break;

    case PROP_TAG_CALLBACK:
      cb = va_arg(ap, prop_callback_t *);
      trampoline = NULL;
      opaque = va_arg(ap, void *);
      break;

    case PROP_TAG_CALLBACK_STRING:
      cb = va_arg(ap, void *);
      trampoline = trampoline_string;
      opaque = va_arg(ap, void *);
      break;

    case PROP_TAG_CALLBACK_RSTR:
      cb = va_arg(ap, void *);
      trampoline = trampoline_rstr;
      opaque = va_arg(ap, void *);
      break;

    case PROP_TAG_CALLBACK_INT:
      cb = va_arg(ap, void *);
      trampoline = trampoline_int;
      opaque = va_arg(ap, void *);
      break;

    case PROP_TAG_CALLBACK_FLOAT:
      cb = va_arg(ap, void *);
      trampoline = trampoline_float;
      opaque = va_arg(ap, void *);
      break;

    case PROP_TAG_SET_INT:
      cb = NULL;
      trampoline = trampoline_int_set;
      opaque = va_arg(ap, void *);
      break;

    case PROP_TAG_SET_FLOAT:
      cb = NULL;
      trampoline = trampoline_float_set;
      opaque = va_arg(ap, void *);
      break;

    case PROP_TAG_COURIER:
      pc = va_arg(ap, prop_courier_t *);
      break;

    case PROP_TAG_ROOT:
      pr = alloca(sizeof(prop_root_t));
      pr->p = va_arg(ap, prop_t *);
      pr->name = NULL;
      if(pr->p != NULL)
	LIST_INSERT_HEAD(&proproots, pr, link);
      break;

    case PROP_TAG_NAMED_ROOT:
      pr = alloca(sizeof(prop_root_t));
      pr->p = va_arg(ap, prop_t *);
      pr->name = va_arg(ap, const char *);
      if(pr->p != NULL && pr->name != NULL)
	LIST_INSERT_HEAD(&proproots, pr, link);
      break;

    case PROP_TAG_MUTEX:
      lock = va_arg(ap, void *);
      break;

    case PROP_TAG_EXTERNAL_LOCK:
      lock    = va_arg(ap, void *);
      lockmgr = va_arg(ap, void *);
      break;

    case PROP_TAG_END:
      break;

    default:
      abort();
    }
  } while(tag);
  

  va_end(ap);

  if(name == NULL) {
    /* No name given, just subscribe to the supplied prop */

    if((pr = LIST_FIRST(&proproots)) == NULL)
      return NULL;

    canonical = value = pr->p;
    if(dolock)
      hts_mutex_lock(&prop_mutex);


    if(value->hp_type == PROP_ZOMBIE) {
      hts_mutex_unlock(&prop_mutex);
      return NULL;
    }

  } else {

    if((p = prop_resolve_tree(name[0], &proproots)) == NULL) 
      return NULL;

    name++;

    if(dolock)
      hts_mutex_lock(&prop_mutex);

    /* Canonical name is the resolved props without following symlinks */
    canonical = prop_subfind(p, name, 0, 0);
  
    /* ... and value will follow links */
    value     = prop_subfind(p, name, 1, 0);

    if(canonical == NULL || value == NULL) {
      hts_mutex_unlock(&prop_mutex);
      return NULL;
    }
  }

  s = malloc(sizeof(prop_sub_t));

  s->hps_zombie = 0;
  s->hps_flags = flags;
  if(pc != NULL) {
    s->hps_courier = pc;
    s->hps_lock = pc->pc_entry_mutex;
  } else {
    s->hps_courier = global_courier;
    s->hps_lock = lock;
  }
  s->hps_lockmgr = lockmgr;

  LIST_INSERT_HEAD(&canonical->hp_canonical_subscriptions, s, 
		   hps_canonical_prop_link);
  s->hps_canonical_prop = canonical;

  if(s->hps_flags & PROP_SUB_SUBSCRIPTION_MONITOR &&
     (canonical->hp_flags & PROP_MONITORED) == 0) {
    canonical->hp_flags |= PROP_MONITORED;

    LIST_FOREACH(t, &canonical->hp_value_subscriptions, hps_value_prop_link) {
      if(!(t->hps_flags & PROP_SUB_SUBSCRIPTION_MONITOR))
	break;
    }
    if(t != NULL) {
      // monitor was enabled but there are already subscribers
      activate_on_canonical = 1;
    }
  }

  if(s->hps_flags & PROP_SUB_MULTI)
    prop_set_multi(canonical);

  LIST_INSERT_HEAD(&value->hp_value_subscriptions, s, 
		   hps_value_prop_link);
  s->hps_value_prop = value;

  s->hps_trampoline = trampoline;
  s->hps_callback = cb;
  s->hps_opaque = opaque;
  s->hps_refcount = 1;

  if(notify_now) {

    prop_build_notify_value(s, direct, "prop_subscribe()", 
			    s->hps_value_prop, NULL, 0);

    if(value->hp_type == PROP_DIR && !(s->hps_flags & PROP_SUB_MULTI)) {
      TAILQ_FOREACH(c, &value->hp_childs, hp_parent_link)
	prop_build_notify_child(s, c, PROP_ADD_CHILD, direct,
				gen_add_flags(c, value));
    }
  }

  /* If we have any subscribers monitoring for subscriptions, notify them */
  if(!(s->hps_flags & PROP_SUB_SUBSCRIPTION_MONITOR) && 
     value->hp_flags & PROP_MONITORED)
    prop_send_subscription_monitor_active(value);

  if(activate_on_canonical)
    prop_send_subscription_monitor_active(canonical);

  if(dolock)
    hts_mutex_unlock(&prop_mutex);
  return s;
}



/**
 *
 */
void
prop_unsubscribe0(prop_sub_t *s)
{
  s->hps_zombie = 1;
  
  if(s->hps_value_prop != NULL) {
    LIST_REMOVE(s, hps_value_prop_link);
    s->hps_value_prop = NULL;
  }

  if(s->hps_canonical_prop != NULL) {
    LIST_REMOVE(s, hps_canonical_prop_link);

    if(s->hps_flags & (PROP_SUB_SUBSCRIPTION_MONITOR | PROP_SUB_MULTI)) {

      prop_sub_t *t;

      assert(s->hps_canonical_prop->hp_flags & 
	     (PROP_MONITORED | PROP_MULTI_SUB));
      
      int mon = 0;
      int multi = 0;

      LIST_FOREACH(t, &s->hps_canonical_prop->hp_canonical_subscriptions,
		   hps_canonical_prop_link) {
	if(t->hps_flags & PROP_SUB_SUBSCRIPTION_MONITOR)
	  mon = 1;

	if(t->hps_flags & PROP_SUB_MULTI)
	  multi = 1;
      }

      if(!mon)
	s->hps_canonical_prop->hp_flags &= ~PROP_MONITORED;

      if(!multi)
	prop_clr_multi(s->hps_canonical_prop);
    }
    s->hps_canonical_prop = NULL;
  }
  prop_sub_ref_dec(s);
}




/**
 *
 */
void
prop_unsubscribe(prop_sub_t *s)
{
  hts_mutex_lock(&prop_mutex);
  prop_unsubscribe0(s);
  hts_mutex_unlock(&prop_mutex);
}



/**
 *
 */
void
prop_init(void)
{
  hts_mutex_init(&prop_mutex);
  hts_mutex_init(&prop_tag_mutex);
  prop_global = prop_make("global", 1, NULL);

  global_courier = prop_courier_create_thread(NULL, "global");
}


/**
 *
 */
prop_t *
prop_get_global(void)
{
  return prop_global;
}


/**
 *
 */
static void
prop_set_epilogue(prop_sub_t *skipme, prop_t *p, const char *origin)
{
  prop_notify_value(p, skipme, origin, 0);

  hts_mutex_unlock(&prop_mutex);
}


void
prop_set_string_exl(prop_t *p, prop_sub_t *skipme, const char *str,
		    prop_str_type_t type)
{
  if(p->hp_type == PROP_ZOMBIE)
    return;

  if(p->hp_type != PROP_RSTRING) {

    if(prop_clean(p))
      return;

  } else if(!strcmp(rstr_get(p->hp_rstring), str)) {
    return;
  } else {
    rstr_release(p->hp_rstring);
  }

  p->hp_rstring = rstr_alloc(str);
  p->hp_type = PROP_RSTRING;

  p->hp_rstrtype = type;
  prop_notify_value(p, skipme, "prop_set_string()", 0);
}

/**
 *
 */
void
prop_set_string_ex(prop_t *p, prop_sub_t *skipme, const char *str,
		   prop_str_type_t type)
{
  if(p == NULL)
    return;

  if(str == NULL) {
    prop_set_void_ex(p, skipme);
    return;
  }

  hts_mutex_lock(&prop_mutex);
  prop_set_string_exl(p, skipme, str, type);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_set_rstring_ex(prop_t *p, prop_sub_t *skipme, rstr_t *rstr, int noupdate)
{
  if(p == NULL)
    return;

  if(rstr == NULL) {
    prop_set_void_ex(p, skipme);
    return;
  }

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE || (p->hp_type != PROP_VOID && noupdate)) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  if(p->hp_type != PROP_RSTRING) {

    if(prop_clean(p)) {
      hts_mutex_unlock(&prop_mutex);
      return;
    }

  } else if(!strcmp(rstr_get(p->hp_rstring), rstr_get(rstr))) {
    hts_mutex_unlock(&prop_mutex);
    return;
  } else {
    rstr_release(p->hp_rstring);
  }
  p->hp_rstring = rstr_dup(rstr);
  p->hp_type = PROP_RSTRING;
  p->hp_rstrtype = 0;

  prop_set_epilogue(skipme, p, "prop_set_rstring()");
}


/**
 *
 */
void
prop_set_cstring_ex(prop_t *p, prop_sub_t *skipme, const char *cstr)
{
  if(p == NULL)
    return;

  if(cstr == NULL) {
    prop_set_void_ex(p, skipme);
    return;
  }

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  if(p->hp_type != PROP_CSTRING) {

    if(prop_clean(p)) {
      hts_mutex_unlock(&prop_mutex);
      return;
    }

  } else if(!strcmp(p->hp_cstring, cstr)) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  p->hp_cstring = cstr;
  p->hp_type = PROP_CSTRING;
  p->hp_rstrtype = 0;

  prop_set_epilogue(skipme, p, "prop_set_cstring()");
}

/**
 *
 */
void
prop_set_link_ex(prop_t *p, prop_sub_t *skipme, const char *title, 
		 const char *url)
{
  if(p == NULL)
    return;

  if(title == NULL && url == NULL) {
    prop_set_void_ex(p, skipme);
    return;
  }

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  if(p->hp_type != PROP_LINK) {

    if(prop_clean(p)) {
      hts_mutex_unlock(&prop_mutex);
      return;
    }

  } else if(!strcmp(rstr_get(p->hp_link_rtitle) ?: "", title ?: "") &&
	    !strcmp(rstr_get(p->hp_link_rurl)   ?: "", url   ?: "")) {
    hts_mutex_unlock(&prop_mutex);
    return;
  } else {
    rstr_release(p->hp_link_rtitle);
    rstr_release(p->hp_link_rurl);
  }

  p->hp_link_rtitle = rstr_alloc(title);
  p->hp_link_rurl   = rstr_alloc(url);
  p->hp_type = PROP_LINK;

  prop_set_epilogue(skipme, p, "prop_set_link()");
}


/**
 *
 */
void
prop_set_stringf_ex(prop_t *p, prop_sub_t *skipme, const char *fmt, ...)
{
  char buf[512];

  va_list ap;

  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  prop_set_string_ex(p, skipme, buf, 0);
}

/**
 *
 */
static void 
prop_int_to_float(prop_t *p)
{
  int val, min, max;

  val = p->u.i.val;
  min = p->u.i.min;
  max = p->u.i.max;
  
  p->u.f.val = val;
  p->u.f.min = min;
  p->u.f.max = max;
  
  p->hp_type = PROP_FLOAT;
}

 /**
 *
 */
static void 
prop_float_to_int(prop_t *p)
{
  float val, min, max;

  val = p->u.f.val;
  min = p->u.f.min;
  max = p->u.f.max;
  
  p->u.i.val = val;
  p->u.i.min = min;
  p->u.i.max = max;
  
  p->hp_type = PROP_INT;
}

 

/**
 *
 */
static prop_t *
prop_get_float(prop_t *p, int *forceupdate)
{
  if(p == NULL)
    return NULL;

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return NULL;
  }

  if(p->hp_type == PROP_INT) {
    prop_int_to_float(p);
    if(forceupdate != NULL)
      *forceupdate = 1;
    return p;
  }

  if(p->hp_type != PROP_FLOAT) {

    if(prop_clean(p)) {
      hts_mutex_unlock(&prop_mutex);
      return NULL;
    }
    if(forceupdate != NULL)
      *forceupdate = 1;
    p->hp_float = 0;
    p->hp_type = PROP_FLOAT;
  }
  return p;
}

/**
 *
 */
void
prop_set_float_ex(prop_t *p, prop_sub_t *skipme, float v, int how)
{
  int forceupdate = !!how;

  if((p = prop_get_float(p, &forceupdate)) == NULL)
    return;
  
  if(!forceupdate && p->hp_float == v) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  if(p->hp_flags & PROP_CLIPPED_VALUE) {
    if(v > p->u.f.max)
      v  = p->u.f.max;
    if(v < p->u.f.min)
      v  = p->u.f.min;
  }

  p->hp_float = v;

  prop_notify_value(p, skipme, "prop_set_float_ex()", how);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_add_float_ex(prop_t *p, prop_sub_t *skipme, float v)
{
  float n;
  if((p = prop_get_float(p, NULL)) == NULL)
    return;

  n = p->hp_float + v;

  if(p->hp_flags & PROP_CLIPPED_VALUE) {
    if(n > p->u.f.max)
      n  = p->u.f.max;
    if(n < p->u.f.min)
      n  = p->u.f.min;
  }

  if(p->hp_float != n) {
    p->hp_float = n;
    prop_notify_value(p, skipme, "prop_add_float()", 0);
  }
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_set_float_clipping_range(prop_t *p, float min, float max)
{
  float n;

  if((p = prop_get_float(p, NULL)) == NULL)
    return;

  p->hp_flags |= PROP_CLIPPED_VALUE;

  p->u.f.min = min;
  p->u.f.max = max;

  n = p->hp_float;

  if(n > max)
    n  = max;
  if(n < min)
    n  = min;

  if(n != p->hp_float) {
    p->hp_float = n;
    prop_notify_value(p, NULL, "prop_set_float_clipping_range()", 0);
  }

  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_set_int_ex(prop_t *p, prop_sub_t *skipme, int v)
{
  if(p == NULL)
    return;

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  if(p->hp_type != PROP_INT) {

    if(p->hp_type == PROP_FLOAT) {
      prop_float_to_int(p);
    } else if(prop_clean(p)) {
      hts_mutex_unlock(&prop_mutex);
      return;
    } else {
      p->hp_type = PROP_INT;
    }

  } else if(p->hp_int == v) {
    hts_mutex_unlock(&prop_mutex);
    return;
  } else if(p->hp_flags & PROP_CLIPPED_VALUE) {
    if(v > p->u.i.max)
      v  = p->u.i.max;
    if(v < p->u.i.min)
      v  = p->u.i.min;
  }

  p->hp_int = v;

  prop_set_epilogue(skipme, p, "prop_set_int()");
}


/**
 *
 */
void
prop_add_int_ex(prop_t *p, prop_sub_t *skipme, int v)
{
  int n;
  if(p == NULL)
    return;

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  if(p->hp_type != PROP_INT) {

    if(p->hp_type == PROP_FLOAT) {
      prop_float_to_int(p);
    } else if(prop_clean(p)) {
      hts_mutex_unlock(&prop_mutex);
      return;
    } else {
      p->hp_int = 0;
      p->hp_type = PROP_INT;
    }
  }


  n = p->hp_int + v;

  if(p->hp_flags & PROP_CLIPPED_VALUE) {
    if(n > p->u.i.max)
      n  = p->u.i.max;
    if(n < p->u.i.min)
      n  = p->u.i.min;
  }

  if(n != p->hp_int) {
    p->hp_int = n;
    prop_notify_value(p, skipme, "prop_add_int()", 0);
  }
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_toggle_int_ex(prop_t *p, prop_sub_t *skipme)
{
  if(p == NULL)
    return;

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  if(p->hp_type != PROP_INT) {

    if(p->hp_type == PROP_FLOAT) {
      prop_float_to_int(p);
    } else if(prop_clean(p)) {
      hts_mutex_unlock(&prop_mutex);
      return;
    } else {
      p->hp_int = 0;
      p->hp_type = PROP_INT;
    }
  }

  p->hp_int = !p->hp_int;

  prop_set_epilogue(skipme, p, "prop_toggle_int()");
}

/**
 *
 */
void
prop_set_int_clipping_range(prop_t *p, int min, int max)
{
  int n;

  if(p == NULL)
    return;

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  if(p->hp_type != PROP_INT) {

    if(p->hp_type == PROP_FLOAT) {
      prop_float_to_int(p);
    } else if(prop_clean(p)) {
      hts_mutex_unlock(&prop_mutex);
      return;
    } else {
      p->hp_int = 0;
      p->hp_type = PROP_INT;
    }
  }

  p->hp_flags |= PROP_CLIPPED_VALUE;

  p->u.i.min = min;
  p->u.i.max = max;

  n = p->hp_int;

  if(n > max)
    n  = max;
  if(n < min)
    n  = min;

  if(n != p->hp_int) {
    p->hp_int = n;
    prop_notify_value(p, NULL, "prop_set_int_clipping_range()", 0);
  }

  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_set_void_ex(prop_t *p, prop_sub_t *skipme)
{
  if(p == NULL)
    return;

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  if(p->hp_type != PROP_VOID) {

    if(prop_clean(p)) {
      hts_mutex_unlock(&prop_mutex);
      return;
    }
 
  } else {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  p->hp_type = PROP_VOID;
  prop_set_epilogue(skipme, p, "prop_set_void()");
}


/**
 * Compare the value of two props, return 1 if equal 0 if not equal
 */
static int
prop_value_compare(prop_t *a, prop_t *b)
{
  if(a->hp_type != b->hp_type)
    return 0;

  switch(a->hp_type) {
  case PROP_RSTRING:
    return !strcmp(rstr_get(a->hp_rstring), rstr_get(b->hp_rstring));

  case PROP_CSTRING:
    return !strcmp(a->hp_cstring, b->hp_cstring);

  case PROP_LINK:
    return !strcmp(rstr_get(a->hp_link_rtitle), rstr_get(b->hp_link_rtitle)) &&
      !strcmp(rstr_get(a->hp_link_rurl), rstr_get(b->hp_link_rurl));

  case PROP_FLOAT:
    return a->hp_float == b->hp_float;

  case PROP_INT:
    return a->hp_int == b->hp_int;

  case PROP_VOID:
  case PROP_ZOMBIE:
    return 1;
    
  default:
    return 0;
  }
}


/**
 * Relink subscriptions after a symlink has been changed
 *
 * The canonical prop pointer will stay in the 'dst' tree 
 *
 * The value prop pointer will be moved to originate from the 'src' tree.
 *
 */
static void
relink_subscriptions(prop_t *src, prop_t *dst, prop_sub_t *skipme,
		     const char *origin, struct prop_notify_queue *pnq,
		     prop_t *no_descend)
{
  prop_sub_t *s;
  prop_t *c, *z;
  int equal;

  /* Follow any symlinks should we bump into 'em */
  while(src->hp_originator != NULL)
    src = src->hp_originator;

  LIST_FOREACH(s, &dst->hp_canonical_subscriptions, hps_canonical_prop_link) {

    if(s->hps_value_prop != NULL) {

      if(s->hps_value_prop == src)
	continue;
      /* If we previously was a directory, flush it out */
      if(s->hps_value_prop->hp_type == PROP_DIR) {
	if(s != skipme) 
	  prop_notify_void(s);
      }
      LIST_REMOVE(s, hps_value_prop_link);
      equal = prop_value_compare(s->hps_value_prop, src);
    } else {
      equal = 0;
    }

    LIST_INSERT_HEAD(&src->hp_value_subscriptions, s, hps_value_prop_link);
    s->hps_value_prop = src;

    /* Monitors, activate ! */
    if(src->hp_flags & PROP_MONITORED)
      prop_send_subscription_monitor_active(src);
    
    /* Update with new value */
    if(s == skipme || equal) 
      continue; /* Unless it's to be skipped */

    s->hps_pending_unlink = pnq ? 1 : 0;
    prop_build_notify_value(s, 0, origin, s->hps_value_prop, pnq, 0);

    if(src->hp_type == PROP_DIR) {
      TAILQ_FOREACH(c, &src->hp_childs, hp_parent_link)
	prop_build_notify_child(s, c, PROP_ADD_CHILD, 0,
				gen_add_flags(c, src));
    }
  }

  if(dst->hp_type == PROP_DIR && src->hp_type == PROP_DIR) {
    
    /* Take care of all childs */

    TAILQ_FOREACH(c, &dst->hp_childs, hp_parent_link) {
      
      if(c->hp_name == NULL || c == no_descend)
	continue;

      z = prop_create0(src, c->hp_name, NULL, 0);

      if(c->hp_type == PROP_DIR)
	prop_make_dir(z, skipme, origin);

      relink_subscriptions(z, c, skipme, origin, pnq, NULL);
    }
  }
}

/**
 *
 */
static void
prop_unlink0(prop_t *p, prop_sub_t *skipme, const char *origin,
	     struct prop_notify_queue *pnq)
{
  prop_remove_from_originator(p);
  relink_subscriptions(p, p, skipme, origin, pnq, NULL);
}


/**
 *
 */
void
prop_link0(prop_t *src, prop_t *dst, prop_sub_t *skipme, int hard)
{
  prop_t *t, *no_descend = NULL;
  prop_notify_t *n;
  prop_sub_t *s;
  struct prop_notify_queue pnq;

  assert(src != dst);

  if(src->hp_type == PROP_ZOMBIE || dst->hp_type == PROP_ZOMBIE)
    return;

  TAILQ_INIT(&pnq);

  if(dst->hp_originator != NULL)
    prop_unlink0(dst, skipme, "prop_link()/unlink", &pnq);

  if(hard == PROP_LINK_XREFED ||
     (hard == PROP_LINK_XREFED_IF_ORPHANED && src->hp_parent == NULL)) {
    dst->hp_flags |= PROP_XREFED_ORIGINATOR;
    assert(src->hp_xref < 255);
    src->hp_xref++;
  }

  dst->hp_originator = src;
  LIST_INSERT_HEAD(&src->hp_targets, dst, hp_originator_link);

  /* Follow any aditional symlinks source may point at */
  while(src->hp_originator != NULL)
    src = src->hp_originator;

  relink_subscriptions(src, dst, skipme, "prop_link()/linkchilds", NULL, NULL);

  while((dst = dst->hp_parent) != NULL) {
    LIST_FOREACH(t, &dst->hp_targets, hp_originator_link)
      relink_subscriptions(dst, t, skipme, "prop_link()/linkparents", NULL,
			   no_descend);
    no_descend = dst;
  }

  while((n = TAILQ_FIRST(&pnq)) != NULL) {
    TAILQ_REMOVE(&pnq, n, hpn_link);

    s = n->hpn_sub;

    if(s->hps_pending_unlink) {
      s->hps_pending_unlink = 0;
      courier_enqueue(s, n);
    } else {
      // Already updated by the new linkage
      prop_notify_free(n);
    }
  }
}


/**
 *
 */
void
prop_link_ex(prop_t *src, prop_t *dst, prop_sub_t *skipme, int hard)
{
  hts_mutex_lock(&prop_mutex);
  prop_link0(src, dst, skipme, hard);
  hts_mutex_unlock(&prop_mutex);
}




/**
 *
 */
void
prop_unlink_ex(prop_t *p, prop_sub_t *skipme)
{
  prop_t *t;

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  if(p->hp_originator != NULL)
    prop_unlink0(p, skipme, "prop_unlink()/childs", NULL);

  while((p = p->hp_parent) != NULL) {
    LIST_FOREACH(t, &p->hp_targets, hp_originator_link)
      relink_subscriptions(p, t, skipme, "prop_unlink()/parents", NULL, NULL);
  }

  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
prop_t *
prop_follow(prop_t *p)
{
  hts_mutex_lock(&prop_mutex);

  while(p->hp_originator != NULL)
    p = p->hp_originator;
  
  p = prop_ref_inc(p);
  hts_mutex_unlock(&prop_mutex);
  return p;
}


/**
 *
 */
int
prop_compare(const prop_t *a, const prop_t *b)
{
  hts_mutex_lock(&prop_mutex);

  while(a->hp_originator != NULL)
    a = a->hp_originator;

  while(b->hp_originator != NULL)
    b = b->hp_originator;

  hts_mutex_unlock(&prop_mutex);
  return a == b;
}


/**
 *
 */
void
prop_select_ex(prop_t *p, prop_t *extra, prop_sub_t *skipme)
{
  prop_t *parent;

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  parent = p->hp_parent;

  if(parent != NULL) {
    assert(parent->hp_type == PROP_DIR);
    prop_notify_child2(p, parent, extra, PROP_SELECT_CHILD, skipme, 0);
    parent->hp_selected = p;
  }

  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_unselect_ex(prop_t *parent, prop_sub_t *skipme)
{
  hts_mutex_lock(&prop_mutex);

  if(parent->hp_type == PROP_DIR) {
    prop_notify_child(NULL, parent, PROP_SELECT_CHILD, skipme, 0);
    parent->hp_selected = NULL;
  }

  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
prop_t *
prop_find(prop_t *p, ...)
{
  prop_t *c = NULL;
  const char *n;
  va_list ap;
  va_start(ap, p);

  hts_mutex_lock(&prop_mutex);

  while((n = va_arg(ap, const char *)) != NULL) {

    if(p->hp_type != PROP_DIR) {
      c = NULL;
      break;
    }

    TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link)
      if(c->hp_name != NULL && !strcmp(c->hp_name, n))
	break;
    if(c == NULL)
      break;
    p = c;
  }
  
  c = prop_ref_inc(c);
  hts_mutex_unlock(&prop_mutex);
  return c;
}


/**
 *
 */
void
prop_request_new_child(prop_t *p)
{
  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_DIR || p->hp_type == PROP_VOID)
    prop_notify_child(NULL, p, PROP_REQ_NEW_CHILD, NULL, 0);

  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_request_delete(prop_t *c)
{
  prop_t *p;
  hts_mutex_lock(&prop_mutex);

  if(c->hp_type != PROP_ZOMBIE) {
    p = c->hp_parent;

    if(p->hp_type == PROP_DIR) {
      prop_vec_t *pv = prop_vec_create(1);
      pv = prop_vec_append(pv, c);
      prop_notify_childv(pv, p, PROP_REQ_DELETE_VECTOR, NULL, NULL);
      prop_vec_release(pv);
    }
  }
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_request_delete_multi(prop_vec_t *pv)
{
  hts_mutex_lock(&prop_mutex);
  prop_notify_childv(pv, pv->pv_vec[0]->hp_parent,
		     PROP_REQ_DELETE_VECTOR, NULL, NULL);
  hts_mutex_unlock(&prop_mutex);
}

/**
 *
 */
static prop_courier_t *
prop_courier_create(void)
{
  prop_courier_t *pc = calloc(1, sizeof(prop_courier_t));
  TAILQ_INIT(&pc->pc_queue_nor);
  TAILQ_INIT(&pc->pc_queue_exp);
  return pc;
}


/**
 *
 */
prop_courier_t *
prop_courier_create_thread(hts_mutex_t *entrymutex, const char *name)
{
  prop_courier_t *pc = prop_courier_create();
  char buf[URL_MAX];
  pc->pc_entry_mutex = entrymutex;
  snprintf(buf, sizeof(buf), "PC:%s", name);

  pc->pc_has_cond = 1;
  hts_cond_init(&pc->pc_cond, &prop_mutex);

  pc->pc_run = 1;
  hts_thread_create_joinable(buf, &pc->pc_thread, prop_courier, pc,
			     THREAD_PRIO_LOW);
  return pc;
}


/**
 *
 */
prop_courier_t *
prop_courier_create_passive(void)
{
  return prop_courier_create();
}


/**
 *
 */
prop_courier_t *
prop_courier_create_notify(void (*notify)(void *opaque),
			   void *opaque)
{
  prop_courier_t *pc = prop_courier_create();

  pc->pc_notify = notify;
  pc->pc_opaque = opaque;

  return pc;
}


/**
 *
 */
prop_courier_t *
prop_courier_create_waitable(void)
{
  prop_courier_t *pc = prop_courier_create();
  
  pc->pc_has_cond = 1;
  hts_cond_init(&pc->pc_cond, &prop_mutex);

  return pc;
}


/**
 *
 */
int
prop_courier_wait(prop_courier_t *pc,
		  struct prop_notify_queue *exp,
		  struct prop_notify_queue *nor,
		  int timeout)
{
  int r = 0;
  hts_mutex_lock(&prop_mutex);
  if(TAILQ_FIRST(&pc->pc_queue_exp) == NULL &&
     TAILQ_FIRST(&pc->pc_queue_nor) == NULL) {
    if(timeout)
      r = hts_cond_wait_timeout(&pc->pc_cond, &prop_mutex, timeout);
    else
      hts_cond_wait(&pc->pc_cond, &prop_mutex);
  }

  TAILQ_MOVE(exp, &pc->pc_queue_exp, hpn_link);
  TAILQ_INIT(&pc->pc_queue_exp);
  TAILQ_MOVE(nor, &pc->pc_queue_nor, hpn_link);
  TAILQ_INIT(&pc->pc_queue_nor);
  hts_mutex_unlock(&prop_mutex);
  return r;
}


/**
 *
 */
void
prop_courier_wait_and_dispatch(prop_courier_t *pc)
{
  struct prop_notify_queue exp, nor;
  prop_courier_wait(pc, &nor, &exp, 0);
  prop_notify_dispatch(&exp);
  prop_notify_dispatch(&nor);
}


/**
 *
 */
void
prop_courier_destroy(prop_courier_t *pc)
{
  if(pc->pc_run) {
    hts_mutex_lock(&prop_mutex);
    pc->pc_run = 0;
    hts_cond_signal(&pc->pc_cond);
    hts_mutex_unlock(&prop_mutex);

    hts_thread_join(&pc->pc_thread);
  }

  if(pc->pc_has_cond)
    hts_cond_destroy(&pc->pc_cond);

  free(pc);
}


/**
 *
 */
void
prop_courier_stop(prop_courier_t *pc)
{
  hts_thread_detach(&pc->pc_thread);
  pc->pc_run = 0;
  pc->pc_detached = 1;
}


/**
 *
 */
void
prop_courier_poll(prop_courier_t *pc)
{
  struct prop_notify_queue q_exp, q_nor;
  hts_mutex_lock(&prop_mutex);
  TAILQ_MOVE(&q_exp, &pc->pc_queue_exp, hpn_link);
  TAILQ_INIT(&pc->pc_queue_exp);
  TAILQ_MOVE(&q_nor, &pc->pc_queue_nor, hpn_link);
  TAILQ_INIT(&pc->pc_queue_nor);
  hts_mutex_unlock(&prop_mutex);
  prop_notify_dispatch(&q_exp);
  prop_notify_dispatch(&q_nor);
}


/**
 *
 */
rstr_t *
prop_get_string(prop_t *p)
{
  rstr_t *r;
  char buf[64];

  hts_mutex_lock(&prop_mutex);

  switch(p->hp_type) {
  case PROP_RSTRING:
    r = rstr_dup(p->hp_rstring);
    break;
  case PROP_CSTRING:
    r = rstr_alloc(p->hp_cstring);
    break;
  case PROP_LINK:
    r = rstr_dup(p->hp_link_rtitle);
    break;
  case PROP_FLOAT:
    snprintf(buf, sizeof(buf), "%f", p->hp_float);
    r = rstr_alloc(buf);
    break;
  case PROP_INT:
    snprintf(buf, sizeof(buf), "%d", p->hp_int);
    r = rstr_alloc(buf);
    break;
 default:
   r = NULL;
   break;
  }
  hts_mutex_unlock(&prop_mutex);
  return r;
}


/**
 *
 */
char **
prop_get_name_of_childs(prop_t *p)
{
  prop_t *c;
  char **rval = NULL;
  int i = 0;

  if(p->hp_type != PROP_DIR)
    return NULL;

  hts_mutex_lock(&prop_mutex);

  TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link) {
    if(c->hp_type == PROP_VOID || c->hp_type == PROP_ZOMBIE)
      continue;

    if(c->hp_name != NULL) {
      strvec_addp(&rval, c->hp_name);
    } else {
      char buf[16];
      snprintf(buf, sizeof(buf), "*%d", i);
      strvec_addp(&rval, buf);
    }
    i++;
  }

  hts_mutex_unlock(&prop_mutex);

  return rval;
}



/**
 *
 */
void
prop_want_more_childs0(prop_sub_t *s)
{
  prop_send_event(s->hps_value_prop, PROP_WANT_MORE_CHILDS);
}


/**
 *
 */
void
prop_want_more_childs(prop_sub_t *s)
{
  hts_mutex_lock(&prop_mutex);
  prop_want_more_childs0(s);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_have_more_childs0(prop_t *p)
{
  prop_send_event(p, PROP_HAVE_MORE_CHILDS);
}


/**
 *
 */
void
prop_have_more_childs(prop_t *p)
{
  hts_mutex_lock(&prop_mutex);
  prop_have_more_childs0(p);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_print_tree0(prop_t *p, int indent, int followlinks)
{
  prop_t *c;

  fprintf(stderr, "%*.s%s[%p %d %c%c]: ", indent, "", 
	  p->hp_name, p, p->hp_xref,
	  p->hp_flags & PROP_MULTI_SUB ? 'M' : ' ',
	  p->hp_flags & PROP_MULTI_NOTIFY ? 'N' : ' ');


  if(p->hp_originator != NULL) {
    if(followlinks) {
      fprintf(stderr, "<symlink> => ");
      prop_print_tree0(p->hp_originator, indent, followlinks);
    } else {
      fprintf(stderr, "<symlink> -> %s\n", p->hp_originator->hp_name);
    }
    return;
  }

  switch(p->hp_type) {
  case PROP_RSTRING:
    fprintf(stderr, "\"%s\"\n", rstr_get(p->hp_rstring));
    break;

  case PROP_CSTRING:
    fprintf(stderr, "\"%s\"\n", p->hp_cstring);
    break;

  case PROP_LINK:
    fprintf(stderr, "\"%s\" <%s>\n", rstr_get(p->hpn_link_rtitle),
	    rstr_get(p->hpn_link_rurl));
    break;

  case PROP_FLOAT:
    fprintf(stderr, "%f\n", p->hp_float);
    break;

  case PROP_INT:
    fprintf(stderr, "%d\n", p->hp_int);
    break;

  case PROP_DIR:
    fprintf(stderr, "<directory>\n");
    TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link)
      prop_print_tree0(c, indent + 4, followlinks);
    break;

  case PROP_VOID:
    fprintf(stderr, "<void>\n");
    break;
    
  case PROP_ZOMBIE:
    fprintf(stderr, "<zombie, ref=%d>\n", p->hp_refcount);
    break;
  }
}

/**
 *
 */
void
prop_print_tree(prop_t *p, int followlinks)
{
  hts_mutex_lock(&prop_mutex);
  prop_print_tree0(p, 0, followlinks);
  hts_mutex_unlock(&prop_mutex);
}



/**
 *
 */
static void
prop_tree_to_htsmsg0(prop_t *p, htsmsg_t *m)
{
  prop_t *c;
  htsmsg_t *sub;

  switch(p->hp_type) {
  case PROP_RSTRING:
    htsmsg_add_str(m, p->hp_name, rstr_get(p->hp_rstring));
    break;

  case PROP_CSTRING:
    htsmsg_add_str(m, p->hp_name, p->hp_cstring);
    break;

  case PROP_FLOAT:
    break;

  case PROP_INT:
    htsmsg_add_s32(m, p->hp_name, p->hp_int);
    break;

  case PROP_DIR:

    sub = htsmsg_create_map();
    TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link)
      prop_tree_to_htsmsg0(c, sub);
    htsmsg_add_msg(m, p->hp_name ?: "", sub);
    break;

  case PROP_VOID:
    break;
    
  case PROP_ZOMBIE:
    break;
  }
}


/**
 *
 */
htsmsg_t *
prop_tree_to_htsmsg(prop_t *p)
{
  htsmsg_t *m = htsmsg_create_map();
  hts_mutex_lock(&prop_mutex);
  prop_tree_to_htsmsg0(p, m);
  hts_mutex_unlock(&prop_mutex);
  return m;
}




/**
 *
 */
static void 
prop_test_subscriber(prop_sub_t *s, prop_event_t event, ...)
{
}



#define TEST_COURIERS 100

void
prop_test(void)
{
  int i;

  prop_courier_t *couriers[TEST_COURIERS];
  hts_mutex_t mtx[TEST_COURIERS];

  prop_t *p = prop_create_root(NULL);

  for(i = 0; i < TEST_COURIERS; i++) {
    hts_mutex_init(&mtx[i]);
    couriers[i] = prop_courier_create_thread(&mtx[i], "test");

    prop_subscribe(0,
		   PROP_TAG_CALLBACK, prop_test_subscriber, NULL,
		   PROP_TAG_COURIER, couriers[i],
		   PROP_TAG_ROOT, p,
		   NULL);
  }

  while(1) {
    prop_set_int(p, i++);
    usleep(1);
  }
  sleep(10000);
}
