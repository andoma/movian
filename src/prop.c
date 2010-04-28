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

#include <arch/atomic.h>

#include "prop.h"
#include "showtime.h"
#include "misc/pixmap.h"

static hts_mutex_t prop_mutex;
static prop_t *prop_global;

static prop_courier_t *global_courier;

static void prop_unsubscribe0(prop_sub_t *s);

static void prop_unlink0(prop_t *p, prop_sub_t *skipme, const char *origin,
			 struct prop_notify_queue *pnq);

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
typedef struct prop_notify {
  TAILQ_ENTRY(prop_notify) hpn_link;
  prop_sub_t *hpn_sub;
  prop_event_t hpn_event;

  union {
    prop_t *p;
    prop_t **pv;
    float f;
    int i;
    rstr_t *rstr;
    struct pixmap *pp;
    event_t *e;
    struct {
      rstr_t *rtitle;
      rstr_t *rurl;
    } link;

  } u;

#define hpn_prop   u.p
#define hpn_propv  u.pv
#define hpn_float  u.f
#define hpn_int    u.i
#define hpn_rstring u.rstr
#define hpn_pixmap u.pp
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



/**
 *
 */
void
prop_ref_dec(prop_t *p)
{
  if(atomic_add(&p->hp_refcount, -1) > 1)
    return;

  free(p);
}


/**
 *
 */
void
prop_ref_inc(prop_t *p)
{
  atomic_add(&p->hp_refcount, 1);
}


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
void
prop_pvec_free(prop_t **a)
{
  void *A = a;
  for(;*a != NULL; a++)
    prop_ref_dec(*a);
  free(A);
}

/**
 *
 */
int
prop_pvec_len(prop_t **src)
{
  int len = 0;
  while(src[len] != NULL)
    len++;
  return len;
}

/**
 *
 */
prop_t **
prop_pvec_clone(prop_t **src)
{
  prop_t **r;
  int i, len = prop_pvec_len(src);

  r = malloc(sizeof(prop_t *) * (1 + len));
  for(i = 0; i < len; i++) {
    r[i] = src[i];
    prop_ref_inc(r[i]);
  }
  r[i] = NULL;
  return r;
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
    if(n->hpn_prop2 != NULL)
      prop_ref_dec(n->hpn_prop2);
    break;

  case PROP_SET_RSTRING:
    rstr_release(n->hpn_rstring);
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

  case PROP_SET_PIXMAP:
    pixmap_release(n->hpn_pixmap);
    prop_ref_dec(n->hpn_prop2);
    break;

  case PROP_ADD_CHILD:
  case PROP_DEL_CHILD:
  case PROP_SELECT_CHILD:
  case PROP_REQ_NEW_CHILD:
    if(n->hpn_prop != NULL)
      prop_ref_dec(n->hpn_prop);
    break;

  case PROP_ADD_CHILD_BEFORE:
  case PROP_MOVE_CHILD:
    prop_ref_dec(n->hpn_prop);
    if(n->hpn_prop2 != NULL)
      prop_ref_dec(n->hpn_prop2);
    break;

  case PROP_DESTROYED:
    prop_ref_dec(n->hpn_prop);
    break;

  case PROP_EXT_EVENT:
    event_unref(n->hpn_ext_event);
    break;

  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
    break;

  case PROP_REQ_DELETE_MULTI:
    prop_pvec_free(n->hpn_propv);
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
  } else {
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
trampoline_string(prop_sub_t *s, prop_event_t event, ...)
{
  prop_callback_string_t *cb = s->hps_callback;

  va_list ap;
  va_start(ap, event);

  if(event == PROP_SET_RSTRING) {
    cb(s->hps_opaque, rstr_get(va_arg(ap, const rstr_t *)));
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
	pt(s, n->hpn_event, n->hpn_rstring, n->hpn_prop2);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_rstring, n->hpn_prop2);
      rstr_release(n->hpn_rstring);
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
	pt(s, n->hpn_event, n->hpn_float, n->hpn_prop2);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_float, n->hpn_prop2);
      prop_ref_dec(n->hpn_prop2);
      break;

    case PROP_SET_PIXMAP:
      if(pt != NULL)
	pt(s, n->hpn_event, n->hpn_pixmap, n->hpn_prop2);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_pixmap, n->hpn_prop2);
      pixmap_release(n->hpn_pixmap);
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
      if(pt != NULL)
	cb(s, n->hpn_event, n->hpn_prop, n->hpn_prop2, n->hpn_flags);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_prop, 
	   n->hpn_prop2, n->hpn_flags);
      prop_ref_dec(n->hpn_prop);
      if(n->hpn_prop2 != NULL)
	prop_ref_dec(n->hpn_prop2);
      break;

    case PROP_DEL_CHILD:
    case PROP_SELECT_CHILD:
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
      event_unref(n->hpn_ext_event);
      break;

    case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
      if(pt != NULL)
	pt(s, n->hpn_event);
      else
	cb(s->hps_opaque, n->hpn_event);
      break;

    case PROP_REQ_DELETE_MULTI:
      if(pt != NULL)
	pt(s, n->hpn_event, n->hpn_propv);
      else
	cb(s->hps_opaque, n->hpn_event, n->hpn_propv);

      prop_pvec_free(n->hpn_propv);
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
  struct prop_notify_queue q;
  prop_notify_t *n;


  hts_mutex_lock(&prop_mutex);

  while(pc->pc_run) {

    if((n = TAILQ_FIRST(&pc->pc_queue)) == NULL) {
      hts_cond_wait(&pc->pc_cond, &prop_mutex);
      continue;
    }

    TAILQ_MOVE(&q, &pc->pc_queue, hpn_link);
    TAILQ_INIT(&pc->pc_queue);
    hts_mutex_unlock(&prop_mutex);
    prop_notify_dispatch(&q);
    hts_mutex_lock(&prop_mutex);
  }

  while((n = TAILQ_FIRST(&pc->pc_queue)) != NULL) {
    TAILQ_REMOVE(&pc->pc_queue, n, hpn_link);
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
courier_enqueue(prop_courier_t *pc, prop_notify_t *n)
{
  TAILQ_INSERT_TAIL(&pc->pc_queue, n, hpn_link);
  if(pc->pc_run)
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
			struct prop_notify_queue *pnq)
{
  prop_t *p = s->hps_value_prop;
  prop_notify_t *n;

  if(s->hps_flags & PROP_SUB_DEBUG) {
    switch(p->hp_type) {
    case PROP_STRING:
      TRACE(TRACE_DEBUG, "prop", "str(%s) by %s", 
	    rstr_get(p->hp_rstring), origin);
      break;
    case PROP_LINK:
      TRACE(TRACE_DEBUG, "prop", "link(%s,%s) by %s", 
	    rstr_get(p->hp_link_rtitle), rstr_get(p->hp_link_rurl), origin);
      break;
    case PROP_FLOAT:
      TRACE(TRACE_DEBUG, "prop", "float(%f) by %s", p->hp_float, origin);
      break;
    case PROP_INT:
      TRACE(TRACE_DEBUG, "prop", "int(%d) by %s", p->hp_int, origin);
      break;
    case PROP_DIR:
      TRACE(TRACE_DEBUG, "prop", "dir by %s", origin);
      break;
    case PROP_VOID:
      TRACE(TRACE_DEBUG, "prop", "void by %s", origin);
      break;
    case PROP_PIXMAP:
      TRACE(TRACE_DEBUG, "prop", "pixmap by %s", origin);
      break;
    case PROP_ZOMBIE:
      break;
    }
  }
  if(direct) {

    assert(pnq == NULL); // Delayed updates are not compatile with direct mode

    /* Direct mode can be requested during subscribe to get
       the current values updated directly without dispatch
       via the courier */

    prop_callback_t *cb = s->hps_callback;
    prop_trampoline_t *pt = s->hps_trampoline;

    switch(p->hp_type) {
    case PROP_STRING:
      if(pt != NULL)
	pt(s, PROP_SET_RSTRING, p->hp_rstring, p);
      else
	cb(s->hps_opaque, PROP_SET_RSTRING, p->hp_rstring, p);
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
	pt(s, PROP_SET_FLOAT, p->hp_float, p);
      else
	cb(s->hps_opaque, PROP_SET_FLOAT, p->hp_float, p);
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

    case PROP_PIXMAP:
      if(pt != NULL)
	pt(s, PROP_SET_PIXMAP, p->hp_pixmap, p);
      else
	cb(s->hps_opaque, PROP_SET_PIXMAP, p->hp_pixmap, p);
      break;

    case PROP_ZOMBIE:
      abort();

    }
    return;
  }

  n = get_notify(s);

  n->hpn_prop2 = p;
  prop_ref_inc(p);

  switch(p->hp_type) {
  case PROP_STRING:
    n->hpn_rstring = rstr_dup(p->hp_rstring);
    n->hpn_event = PROP_SET_RSTRING;
    break;

  case PROP_LINK:
    n->hpn_link_rtitle = rstr_dup(p->hp_link_rtitle);
    n->hpn_link_rurl   = rstr_dup(p->hp_link_rurl);
    n->hpn_event = PROP_SET_RLINK;
    break;

  case PROP_FLOAT:
    n->hpn_float = p->hp_float;
    n->hpn_event = PROP_SET_FLOAT;
    break;

  case PROP_INT:
    n->hpn_float = p->hp_float;
    n->hpn_event = PROP_SET_INT;
    break;

  case PROP_DIR:
    n->hpn_event = PROP_SET_DIR;
    break;

  case PROP_PIXMAP:
    n->hpn_pixmap = pixmap_dup(p->hp_pixmap);
    n->hpn_event = PROP_SET_PIXMAP;
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
    courier_enqueue(s->hps_courier, n);
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
  courier_enqueue(s->hps_courier, n);
}


/**
 *
 */
static void
prop_notify_destroyed(prop_sub_t *s, prop_t *p)
{
  prop_notify_t *n = get_notify(s);

  n->hpn_event = PROP_DESTROYED;
  n->hpn_prop = p;
  atomic_add(&p->hp_refcount, 1);
  courier_enqueue(s->hps_courier, n);
}


/**
 *
 */
static void
prop_notify_value(prop_t *p, prop_sub_t *skipme, const char *origin)
{
  prop_sub_t *s;

  LIST_FOREACH(s, &p->hp_value_subscriptions, hps_value_prop_link)
    if(s != skipme)
      prop_build_notify_value(s, 0, origin, NULL);
}


/**
 *
 */
static void
prop_build_notify_child(prop_sub_t *s, prop_t *p, prop_event_t event,
			int direct, int flags)
{
  prop_notify_t *n;

  if(direct) {
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
  courier_enqueue(s->hps_courier, n);
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
prop_build_notify_child2(prop_sub_t *s, prop_t *p, prop_t *sibling, 
			 prop_event_t event, int flags)
{
  prop_notify_t *n;

  n = get_notify(s);

  atomic_add(&p->hp_refcount, 1);
  if(sibling != NULL)
    atomic_add(&sibling->hp_refcount, 1);

  n->hpn_prop = p;
  n->hpn_prop2 = sibling;
  n->hpn_event = event;
  n->hpn_flags = flags;
  courier_enqueue(s->hps_courier, n);
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
      prop_build_notify_child2(s, child, sibling, event, flags);
}



/**
 *
 */
static void
prop_build_notify_childv(prop_sub_t *s, prop_t **childv, prop_event_t event)
{
  prop_notify_t *n;
  int len = 0;

  while(childv[len] != NULL) {
    atomic_add(&childv[len]->hp_refcount, 1);
    len++;
  }

  n = get_notify(s);


  n->hpn_propv = malloc(sizeof(prop_t *) * (len + 1));
  memcpy(n->hpn_propv, childv, sizeof(prop_t *) * (len + 1));
  n->hpn_flags = 0;
  n->hpn_event = event;
  courier_enqueue(s->hps_courier, n);
}


/**
 *
 */
static void
prop_notify_childv(prop_t **childv, prop_t *parent, prop_event_t event,
		   prop_sub_t *skipme)
{
  prop_sub_t *s;

  LIST_FOREACH(s, &parent->hp_value_subscriptions, hps_value_prop_link)
    if(s != skipme)
      prop_build_notify_childv(s, childv, event);
}


/**
 *
 */
static void
prop_send_ext_event0(prop_t *p, event_t *e)
{
  prop_sub_t *s;
  prop_notify_t *n;

  LIST_FOREACH(s, &p->hp_value_subscriptions, hps_value_prop_link) {
    n = get_notify(s);

    n->hpn_event = PROP_EXT_EVENT;
    atomic_add(&e->e_refcount, 1);
    n->hpn_ext_event = e;
    courier_enqueue(s->hps_courier, n);
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
      courier_enqueue(s->hps_courier, n);
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
    break;

  case PROP_STRING:
    rstr_release(p->hp_rstring);
    break;

  case PROP_LINK:
    rstr_release(p->hp_link_rtitle);
    rstr_release(p->hp_link_rurl);
    break;

  case PROP_PIXMAP:
    pixmap_release(p->hp_pixmap);
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
  
  prop_notify_value(p, skipme, origin);
}


/**
 *
 */
static int 
prop_compar(prop_t *a, prop_t *b)
{
  return strcmp(a->hp_name ?: "", b->hp_name ?: "");
}


/**
 *
 */
static int 
prop_compar2(prop_t *a, prop_t *b)
{
  return strcasecmp(a->hp_name ?: "", b->hp_name ?: "");
}


/**
 *
 */
static void
prop_insert(prop_t *p, prop_t *parent, prop_t *before, prop_sub_t *skipme)
{
  prop_t *n;

  if(parent->hp_flags & PROP_SORTED_CHILDS) {
    if(parent->hp_flags & PROP_SORT_CASE_INSENSITIVE)
      TAILQ_INSERT_SORTED(&parent->hp_childs, p, hp_parent_link, prop_compar2);
    else
      TAILQ_INSERT_SORTED(&parent->hp_childs, p, hp_parent_link, prop_compar);

    n = TAILQ_NEXT(p, hp_parent_link);

    if(n == NULL) {
      prop_notify_child(p, parent, PROP_ADD_CHILD, skipme, 0);
    } else {
      prop_notify_child2(p, parent, n, PROP_ADD_CHILD_BEFORE, skipme, 0);
    }
  } else if(before != NULL) {
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
static prop_t *
prop_create0(prop_t *parent, const char *name, prop_sub_t *skipme, int flags)
{
  prop_t *hp;

  if(parent != NULL) {

    prop_make_dir(parent, skipme, "prop_create()");

    if(name != NULL) {
      TAILQ_FOREACH(hp, &parent->hp_childs, hp_parent_link) {
	if(hp->hp_name != NULL && !strcmp(hp->hp_name, name)) {
	  return hp;
	}
      }
    }
  }

  hp = malloc(sizeof(prop_t));
  hp->hp_flags = flags;
  hp->hp_originator = NULL;
  hp->hp_refcount = 1;
  hp->hp_monitors = 0;
  hp->hp_xref = 1;
  hp->hp_type = PROP_VOID;
  if(flags & PROP_NAME_NOT_ALLOCATED)
    hp->hp_name = name;
  else
    hp->hp_name = name ? strdup(name) : NULL;

  LIST_INIT(&hp->hp_targets);
  LIST_INIT(&hp->hp_value_subscriptions);
  LIST_INIT(&hp->hp_canonical_subscriptions);

  hp->hp_parent = parent;

  if(parent != NULL)
    prop_insert(hp, parent, NULL, skipme);

  return hp;
}



/**
 *
 */
prop_t *
prop_create_ex(prop_t *parent, const char *name, prop_sub_t *skipme, int flags)
{
  prop_t *p;

  if(parent == NULL)
    return prop_create0(NULL, name, skipme, flags);

  hts_mutex_lock(&prop_mutex);
  
  if(parent->hp_type != PROP_ZOMBIE)
    p = prop_create0(parent, name, skipme, flags);
  else
    p = NULL;

  hts_mutex_unlock(&prop_mutex);

  return p;
}

/**
 *
 */
void
prop_rename_ex(prop_t *p, const char *name, prop_sub_t *skipme)
{
  hts_mutex_lock(&prop_mutex);

  if(!(p->hp_flags & PROP_NAME_NOT_ALLOCATED))
    free((void *)p->hp_name);

  p->hp_name = strdup(name);

  if(p->hp_parent != NULL && p->hp_parent->hp_flags & PROP_SORTED_CHILDS) {

    prop_t *parent = p->hp_parent;
    
    TAILQ_REMOVE(&parent->hp_childs, p, hp_parent_link);

    if(parent->hp_flags & PROP_SORT_CASE_INSENSITIVE)
      TAILQ_INSERT_SORTED(&parent->hp_childs, p, hp_parent_link, prop_compar2);
    else
      TAILQ_INSERT_SORTED(&parent->hp_childs, p, hp_parent_link, prop_compar);

    prop_notify_child2(p, parent, TAILQ_NEXT(p, hp_parent_link),
		       PROP_MOVE_CHILD, NULL, 0);
  }

  hts_mutex_unlock(&prop_mutex);
}

/**
 *
 */
int
prop_set_parent_ex(prop_t *p, prop_t *parent, prop_t *before, 
		   prop_sub_t *skipme)
{
  assert(p->hp_parent == NULL);

  hts_mutex_lock(&prop_mutex);

  if(parent->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return -1;
  }

  prop_make_dir(parent, skipme, "prop_set_parent()");

  p->hp_parent = parent;

  prop_insert(p, parent, before, skipme);
  hts_mutex_unlock(&prop_mutex);
  return 0;
}


/**
 *
 */
void
prop_unparent_ex(prop_t *p, prop_sub_t *skipme)
{
  prop_t *parent;

  hts_mutex_lock(&prop_mutex);

  parent = p->hp_parent;
  if(parent != NULL) {

    prop_notify_child(p, parent, PROP_DEL_CHILD, NULL, 0);

    TAILQ_REMOVE(&parent->hp_childs, p, hp_parent_link);
    p->hp_parent = NULL;

    if(parent->hp_selected == p)
      parent->hp_selected = NULL;
  }

  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
static int
prop_destroy0(prop_t *p)
{
  prop_t *c, *next, *parent;
  prop_sub_t *s;

  p->hp_xref--;
  if(p->hp_xref)
    return 0;

  switch(p->hp_type) {
  case PROP_ZOMBIE:
    abort();

  case PROP_DIR:
    for(c = TAILQ_FIRST(&p->hp_childs); c != NULL; c = next) {
      next = TAILQ_NEXT(c, hp_parent_link);
      if(!prop_destroy0(c)) {
	TAILQ_REMOVE(&p->hp_childs, c, hp_parent_link);
	c->hp_parent = NULL;
      }
    }
    break;

  case PROP_STRING:
    rstr_release(p->hp_rstring);
    break;

  case PROP_LINK:
    rstr_release(p->hp_link_rtitle);
    rstr_release(p->hp_link_rurl);
    break;

  case PROP_PIXMAP:
    pixmap_release(p->hp_pixmap);
    break;

  case PROP_FLOAT:
  case PROP_INT:
  case PROP_VOID:
    break;
  }

  p->hp_type = PROP_ZOMBIE;

  while((s = LIST_FIRST(&p->hp_canonical_subscriptions)) != NULL) {

    if(s->hps_flags & PROP_SUB_TRACK_DESTROY)
      prop_notify_destroyed(s, p);

    LIST_REMOVE(s, hps_canonical_prop_link);
    s->hps_canonical_prop = NULL;
  }

  while((s = LIST_FIRST(&p->hp_value_subscriptions)) != NULL) {
    prop_notify_void(s);

    LIST_REMOVE(s, hps_value_prop_link);
    s->hps_value_prop = NULL;
  }

  while((c = LIST_FIRST(&p->hp_targets)) != NULL)
    prop_unlink0(c, NULL, "prop_destroy0", NULL);

  if(p->hp_originator != NULL) {
    p->hp_originator = NULL;
    LIST_REMOVE(p, hp_originator_link);
  }

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
      if(!prop_destroy0(c)) {
	TAILQ_REMOVE(&p->hp_childs, c, hp_parent_link);
	c->hp_parent = NULL;
      }
    }
  }
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_move(prop_t *p, prop_t *before)
{
  prop_t *parent;

  hts_mutex_lock(&prop_mutex);
  
  assert(p != before);

  parent = p->hp_parent;
 
  TAILQ_REMOVE(&parent->hp_childs, p, hp_parent_link);
  
  if(before != NULL) {
    TAILQ_INSERT_BEFORE(before, p, hp_parent_link);
  } else {
    TAILQ_INSERT_TAIL(&parent->hp_childs, p, hp_parent_link);
  }  
  prop_notify_child2(p, parent, before, PROP_MOVE_CHILD, NULL, 0);

  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
static prop_t *
prop_subfind(prop_t *p, const char **name, int follow_symlinks)
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

      prop_notify_value(p, NULL, "prop_subfind()");
    }

    TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link) {
      if(c->hp_name != NULL && !strcmp(c->hp_name, name[0]))
	break;
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
  p = prop_subfind(p, name, follow_symlinks);

  if(p != NULL)
    prop_ref_inc(p);

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
  prop_sub_t *s;
  int direct = !!(flags & PROP_SUB_DIRECT_UPDATE);
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
  
  va_list ap;
  va_start(ap, flags);

  LIST_INIT(&proproots);

  do {
    tag = va_arg(ap, int);
    switch(tag) {
    case PROP_TAG_NAME_VECTOR:
      name = va_arg(ap, const char **);
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
    hts_mutex_lock(&prop_mutex);

  } else {

    if((p = prop_resolve_tree(name[0], &proproots)) == NULL) 
      return NULL;

    name++;

    hts_mutex_lock(&prop_mutex);

    /* Canonical name is the resolved props without following symlinks */
    canonical = prop_subfind(p, name, 0);
  
    /* ... and value will follow links */
    value     = prop_subfind(p, name, 1);

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

  if(s->hps_flags & PROP_SUB_SUBSCRIPTION_MONITOR) {
    assert(canonical->hp_monitors < 255); // avoid wrap
    canonical->hp_monitors++;
  }


  LIST_INSERT_HEAD(&value->hp_value_subscriptions, s, 
		   hps_value_prop_link);
  s->hps_value_prop = value;

  s->hps_trampoline = trampoline;
  s->hps_callback = cb;
  s->hps_opaque = opaque;
  s->hps_refcount = 1;

  if(notify_now) {

    prop_build_notify_value(s, direct, "prop_subscribe()", NULL);

    if(value->hp_type == PROP_DIR) {
      TAILQ_FOREACH(c, &value->hp_childs, hp_parent_link)
	prop_build_notify_child(s, c, PROP_ADD_CHILD, direct,
				gen_add_flags(c, value));
    }
  }

  /* If we have any subscribers monitoring for subscriptions, notify them */
  if(!(s->hps_flags & PROP_SUB_SUBSCRIPTION_MONITOR) && value->hp_monitors > 0)
    prop_send_subscription_monitor_active(value);

  hts_mutex_unlock(&prop_mutex);
  return s;
}



/**
 *
 */
static void
prop_unsubscribe0(prop_sub_t *s)
{
  s->hps_zombie = 1;

  if(s->hps_value_prop != NULL) {
    LIST_REMOVE(s, hps_value_prop_link);
    s->hps_value_prop = NULL;
  }

  if(s->hps_canonical_prop != NULL) {

    if(s->hps_flags & PROP_SUB_SUBSCRIPTION_MONITOR) {
      assert(s->hps_canonical_prop->hp_monitors > 0);
      s->hps_canonical_prop->hp_monitors--;
    }


    LIST_REMOVE(s, hps_canonical_prop_link);
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
  prop_global = prop_create0(NULL, "global", NULL, 0);

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
  prop_notify_value(p, skipme, origin);

  hts_mutex_unlock(&prop_mutex);
}



/**
 *
 */
void
prop_set_string_ex(prop_t *p, prop_sub_t *skipme, const char *str)
{
  if(p == NULL)
    return;

  if(str == NULL) {
    prop_set_void_ex(p, skipme);
    return;
  }

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  if(p->hp_type != PROP_STRING) {

    if(prop_clean(p)) {
      hts_mutex_unlock(&prop_mutex);
      return;
    }

  } else if(!strcmp(rstr_get(p->hp_rstring), str)) {
    hts_mutex_unlock(&prop_mutex);
    return;
  } else {
    rstr_release(p->hp_rstring);
  }

  p->hp_rstring = rstr_alloc(str);
  p->hp_type = PROP_STRING;

  prop_set_epilogue(skipme, p, "prop_set_string()");
}


/**
 *
 */
void
prop_set_rstring_ex(prop_t *p, prop_sub_t *skipme, rstr_t *rstr)
{
  if(p == NULL)
    return;

  if(rstr == NULL) {
    prop_set_void_ex(p, skipme);
    return;
  }

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  if(p->hp_type != PROP_STRING) {

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
  p->hp_type = PROP_STRING;

  prop_set_epilogue(skipme, p, "prop_set_string()");
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

  if(title == NULL && link == NULL) {
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

  prop_set_string_ex(p, skipme, buf);
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
prop_set_float_ex(prop_t *p, prop_sub_t *skipme, float v)
{
  int forceupdate = 0;

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

  prop_set_epilogue(skipme, p, "prop_set_float()");
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
    prop_notify_value(p, skipme, "prop_add_float()");
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
    prop_notify_value(p, NULL, "prop_set_float_clipping_range()");
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
    prop_notify_value(p, skipme, "prop_add_int()");
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
    prop_notify_value(p, NULL, "prop_set_int_clipping_range()");
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
 *
 */
void
prop_set_pixmap_ex(prop_t *p, prop_sub_t *skipme, struct pixmap *pm)
{
  if(p == NULL)
    return;

  if(pm == NULL) {
    prop_set_void_ex(p, skipme);
    return;
  }

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  if(p->hp_type != PROP_PIXMAP) {

    if(prop_clean(p)) {
      hts_mutex_unlock(&prop_mutex);
      return;
    }

  } else {
    pixmap_release(p->hp_pixmap);
  }

  p->hp_pixmap = pixmap_dup(pm);
  p->hp_type = PROP_PIXMAP;

  prop_set_epilogue(skipme, p, "prop_set_pixmap()");
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
  case PROP_STRING:
    return !strcmp(rstr_get(a->hp_rstring), rstr_get(b->hp_rstring));

  case PROP_LINK:
    return !strcmp(rstr_get(a->hp_link_rtitle), rstr_get(b->hp_link_rtitle)) &&
      !strcmp(rstr_get(a->hp_link_rurl), rstr_get(b->hp_link_rurl));

  case PROP_FLOAT:
    return a->hp_float == b->hp_float;

  case PROP_INT:
    return a->hp_int == b->hp_int;

  case PROP_PIXMAP:
    return a->hp_pixmap == b->hp_pixmap;

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
    if(src->hp_monitors > 0)
      prop_send_subscription_monitor_active(src);
    
    /* Update with new value */
    if(s == skipme || equal) 
      continue; /* Unless it's to be skipped */

    s->hps_pending_unlink = pnq ? 1 : 0;
    prop_build_notify_value(s, 0, origin, pnq);

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

      /* Search for a matching source */
      TAILQ_FOREACH(z, &src->hp_childs, hp_parent_link) {
	if(z->hp_name != NULL && !strcmp(c->hp_name, z->hp_name))
	  break;
      }
      
      if(z != NULL) {
	/* Found! Recurse */

	relink_subscriptions(z, c, skipme, origin, pnq, NULL);

      } else {
	/* Nothing, blast the value */

	LIST_FOREACH(s, &c->hp_canonical_subscriptions,
		     hps_canonical_prop_link) {

	  if(s != skipme)
	    prop_notify_void(s);

	  LIST_REMOVE(s, hps_value_prop_link);
	  s->hps_value_prop = NULL;
	}
      }
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
  LIST_REMOVE(p, hp_originator_link);
  p->hp_originator = NULL;

  relink_subscriptions(p, p, skipme, origin, pnq, NULL);
}


/**
 *
 */
void
prop_link_ex(prop_t *src, prop_t *dst, prop_sub_t *skipme)
{
  prop_t *t, *no_descend = NULL;
  prop_notify_t *n;
  prop_sub_t *s;
  struct prop_notify_queue pnq;

  hts_mutex_lock(&prop_mutex);

  if(src->hp_type == PROP_ZOMBIE || dst->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  TAILQ_INIT(&pnq);

  if(dst->hp_originator != NULL)
    prop_unlink0(dst, skipme, "prop_link()/unlink", &pnq);

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
      courier_enqueue(s->hps_courier, n);
    } else {
      // Already updated by the new linkage
      prop_notify_free(n);
    }
  }
  
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
void
prop_select_ex(prop_t *p, int advisory, prop_sub_t *skipme)
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

    /* If in advisory mode and something is already selected,
       don't do anything */
    if(!advisory || parent->hp_selected == NULL) {
      prop_notify_child(p, parent, PROP_SELECT_CHILD, skipme, 0);
      parent->hp_selected = p;
    }
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
prop_t **
prop_get_ancestors(prop_t *p)
{
  prop_t *a = p, **r;
  int l = 2; /* one for current, one for terminating NULL */

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return NULL;
  }

  while(a->hp_parent != NULL) {
    l++;
    a = a->hp_parent;
  }

  r = malloc(l * sizeof(prop_t *));
  
  l = 0;
  while(p != NULL) {
    prop_ref_inc(p);
    r[l++] = p;
    p = p->hp_parent;
  }
  r[l] = NULL;

  hts_mutex_unlock(&prop_mutex);
  return r;
}


/**
 * XXX: Remove this
 */
void
prop_ancestors_unref(prop_t **r)
{
  prop_pvec_free(r);
}


/**
 *
 */
prop_t *
prop_get_by_names(prop_t *p, ...)
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
  
  if(c != NULL)
    prop_ref_inc(c);
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
      prop_t *vec[2];
      vec[0] = c;
      vec[1] = NULL;
      prop_notify_childv(vec, p, PROP_REQ_DELETE_MULTI, NULL);
    }
  }
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_request_delete_multi(prop_t **vec)
{
  hts_mutex_lock(&prop_mutex);
  prop_notify_childv(vec, vec[0]->hp_parent, PROP_REQ_DELETE_MULTI, NULL);
  hts_mutex_unlock(&prop_mutex);
}

/**
 *
 */
static prop_courier_t *
prop_courier_create(void)
{
  prop_courier_t *pc = calloc(1, sizeof(prop_courier_t));
  TAILQ_INIT(&pc->pc_queue);
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
  hts_cond_init(&pc->pc_cond);
  pc->pc_run = 1;
  hts_thread_create_joinable(buf, &pc->pc_thread, prop_courier, pc);
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
void
prop_courier_destroy(prop_courier_t *pc)
{
  if(pc->pc_run) {
    hts_mutex_lock(&prop_mutex);
    pc->pc_run = 0;
    hts_cond_signal(&pc->pc_cond);
    hts_mutex_unlock(&prop_mutex);

    hts_thread_join(&pc->pc_thread);
    hts_cond_destroy(&pc->pc_cond);
  }
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
  struct prop_notify_queue q;
  hts_mutex_lock(&prop_mutex);
  TAILQ_MOVE(&q, &pc->pc_queue, hpn_link);
  TAILQ_INIT(&pc->pc_queue);
  hts_mutex_unlock(&prop_mutex);
  prop_notify_dispatch(&q);
}


/**
 *
 */
int
prop_get_string(prop_t *p, char *buf, size_t bufsize)
{
  int r;

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_STRING) {
    snprintf(buf, bufsize, "%s", rstr_get(p->hp_rstring));
    r = 0;
  } else {
    r = -1;
  }
  hts_mutex_unlock(&prop_mutex);
  return r;
}



/**
 *
 */
static void
prop_print_tree0(prop_t *p, int indent, int followlinks)
{
  prop_t *c;

  fprintf(stderr, "%*.s%s[%p %d]: ", indent, "", p->hp_name, p, p->hp_xref);

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
  case PROP_STRING:
    fprintf(stderr, "\"%s\"\n", rstr_get(p->hp_rstring));
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

  case PROP_PIXMAP:
    fprintf(stderr, "<pixmap>\n");
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
  case PROP_STRING:
    htsmsg_add_str(m, p->hp_name, rstr_get(p->hp_rstring));
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

  case PROP_PIXMAP:
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

  prop_t *p = prop_create(NULL, NULL);

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
