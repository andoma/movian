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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <unistd.h>
#include <math.h>

#include "arch/atomic.h"

#include "main.h"
#include "prop_i.h"
#include "misc/str.h"
#include "event.h"

#include "prop_proxy.h"

#ifdef PROP_DEBUG
int prop_trace;
static prop_sub_t *track_sub;
#endif

hts_mutex_t prop_mutex;
hts_mutex_t prop_tag_mutex;
static prop_t *prop_global;


pool_t *prop_pool;
pool_t *notify_pool;
pool_t *sub_pool;
pool_t *pot_pool;
pool_t *psd_pool;


// Global dispatch


#define PROP_GLOBAL_DISPATCH_IDLE_THREADS 4
#define PROP_GLOBAL_DISPATCH_MAX_THREADS  8

static hts_cond_t prop_global_dispatch_cond;
static struct prop_sub_dispatch_queue prop_global_dispatch_queue;
static struct prop_sub_dispatch_queue prop_global_dispatch_dispatching_queue;
static int prop_global_dispatch_running;
static int prop_global_dispatch_avail;


// Some forward decl.
static void prop_unlink0(prop_t *p, prop_sub_t *skipme, const char *origin,
			 struct prop_notify_queue *pnq);

static void prop_flood_flag(prop_t *p, int set, int clr);

static void prop_destroy_childs0(prop_t *p);

#define PROPTRACE(fmt, ...) \
  tracelog(TRACE_NO_PROP, TRACE_DEBUG, "prop", fmt, ##__VA_ARGS__)

#ifdef PROP_SUB_STATS
static LIST_HEAD(, prop_sub) all_subs;
#endif

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
rstr_t *
prop_get_name0(prop_t *p)
{
  return p->hp_name ? rstr_alloc(p->hp_name) : NULL;
}


/**
 *
 */
rstr_t *
prop_get_name(prop_t *p)
{
  hts_mutex_lock(&prop_mutex);
  rstr_t *r = prop_get_name0(p);
  hts_mutex_unlock(&prop_mutex);
  return r;
}


/**
 *
 */
const char *
prop_get_DN(prop_t *p, int compact)
{
  static int idx;
  static char buf[4][256];
  
  const int maxlen = 256;
  prop_t *revvec[32];
  char *s = buf[idx];
  int len = 0;
  int pfx = 0;

  if(p == NULL)
    return "(null)";

  int d;
  for(d = 0; d < 32; d++) {
    if(p == NULL)
      break;
    revvec[d] = p;
    p = p->hp_parent;
  }


  *s = 0;
  d--;
  for(;d >= 0;d--) {
    p = revvec[d];
#ifdef PROP_DEBUG
    assert(p->hp_magic == PROP_MAGIC);
#endif
    if(compact) {
      len += snprintf(s + len, maxlen - len, "%s%s", 
		      pfx ? "." : "", p->hp_name ?: "(NULL)");

      if(d == 0)
	len += snprintf(s + len, maxlen - len, "<%p>", p);

    } else {
      len += snprintf(s + len, maxlen - len, "%s%s<%p>%s%s", 
		      pfx ? ", " : "", p->hp_name ?: "(NULL)", p,
		      LIST_FIRST(&p->hp_canonical_subscriptions) ? "Cs" : "",
		      LIST_FIRST(&p->hp_value_subscriptions) ? "Vs" : "");
    }
    pfx = 1;
  }

  idx = (idx + 1) & 3;
  return s;
}


/**
 * Default lockmanager for normal mutexes
 */
static int
proplockmgr(void *ptr, lockmgr_op_t op)
{
  hts_mutex_t *mtx = (hts_mutex_t *)ptr;

  switch(op) {
  case LOCKMGR_UNLOCK:
    hts_mutex_unlock(mtx);
    break;
  case LOCKMGR_LOCK:
    hts_mutex_lock(mtx);
    break;
  case LOCKMGR_TRY:
    return hts_mutex_trylock(mtx);
  case LOCKMGR_RETAIN:
  case LOCKMGR_RELEASE:
    break;
  }
  return 0;
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

  assert(p->hp_magic == PROP_MAGIC);

  if(p->hp_flags & PROP_REF_TRACED) {
    struct prop_ref_trace *prt = malloc(sizeof(struct prop_ref_trace));
    prt->file = file;
    prt->line = line;
    prt->value = atomic_get(&p->hp_refcount) - 1;
    prt->which = 0;
    hts_mutex_lock(&prop_ref_mutex);
    SIMPLEQ_INSERT_TAIL(&p->hp_ref_trace, prt, link);
    hts_mutex_unlock(&prop_ref_mutex);
  }
  
  if(atomic_dec(&p->hp_refcount))
    return;
  if(p->hp_flags & PROP_REF_TRACED) 
    printf("Prop %p was finalized by %s:%d\n", p, file, line);
  assert(p->hp_type == PROP_ZOMBIE);

  extern void prop_tag_dump(prop_t *p);
  prop_tag_dump(p);

  hts_mutex_lock(&prop_mutex);
  assert(p->hp_tags == NULL);
  memset(p, 0xdd, sizeof(prop_t));
  pool_put(prop_pool, p);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_ref_dec_traced_locked(prop_t *p, const char *file, int line)
{
  if(p == NULL)
    return;

  assert(p->hp_magic == PROP_MAGIC);

  if(p->hp_flags & PROP_REF_TRACED) {
    struct prop_ref_trace *prt = malloc(sizeof(struct prop_ref_trace));
    prt->file = file;
    prt->line = line;
    prt->value = atomic_get(&p->hp_refcount) - 1;
    prt->which = 0;
    hts_mutex_lock(&prop_ref_mutex);
    SIMPLEQ_INSERT_TAIL(&p->hp_ref_trace, prt, link);
    hts_mutex_unlock(&prop_ref_mutex);
  }
  
  if(atomic_dec(&p->hp_refcount))
    return;
  if(p->hp_flags & PROP_REF_TRACED) 
    printf("Prop %p was finalized by %s:%d\n", p, file, line);
  assert(p->hp_type == PROP_ZOMBIE);

  extern void prop_tag_dump(prop_t *p);
  prop_tag_dump(p);

  assert(p->hp_tags == NULL);
  memset(p, 0xdd, sizeof(prop_t));
  pool_put(prop_pool, p);
}



/**
 *
 */
prop_t *
prop_ref_inc_traced(prop_t *p, const char *file, int line)
{
  if(p == NULL)
    return NULL;

  atomic_inc(&p->hp_refcount);
  if(p->hp_flags & PROP_REF_TRACED) {
    struct prop_ref_trace *prt = malloc(sizeof(struct prop_ref_trace));
    prt->file = file;
    prt->line = line;
    prt->value = atomic_get(&p->hp_refcount);
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
  if(p == NULL || atomic_dec(&p->hp_refcount))
    return;
  assert(p->hp_type == PROP_ZOMBIE);
  assert(p->hp_tags == NULL);
#ifdef PROP_DEBUG
  assert(p->hp_magic == PROP_MAGIC);
  memset(p, 0xdd, sizeof(prop_t));
#endif
  hts_mutex_lock(&prop_mutex);
  pool_put(prop_pool, p);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_ref_dec_locked(prop_t *p)
{
  if(p == NULL || atomic_dec(&p->hp_refcount))
    return;
  assert(p->hp_type == PROP_ZOMBIE);
  assert(p->hp_tags == NULL);
#ifdef PROP_DEBUG
  assert(p->hp_magic == PROP_MAGIC);
  memset(p, 0xdd, sizeof(prop_t));
#endif
  pool_put(prop_pool, p);
}

/**
 *
 */
prop_t *
prop_ref_inc(prop_t *p)
{
  if(p != NULL)
    atomic_inc(&p->hp_refcount);
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
void
prop_sub_ref_dec_locked(prop_sub_t *s)
{
  if(atomic_dec(&s->hps_refcount))
    return;
  s->hps_lockmgr(s->hps_lock, LOCKMGR_RELEASE);
  pool_put(sub_pool, s);
}


/**
 *
 */
static void
prop_remove_from_originator(prop_t *p)
{
  LIST_REMOVE(p, hp_originator_link);

  if(p->hp_flags & PROP_XREFED_ORIGINATOR) {
    prop_destroy0(p->hp_originator);
    p->hp_flags &= ~PROP_XREFED_ORIGINATOR;
  }

  p->hp_originator = NULL;
}


/**
 *
 */
static void
prop_notify_free_payload(prop_notify_t *n)
{
  switch(n->hpn_event) {
  case PROP_SET_DIR:
  case PROP_SET_VOID:
  case PROP_SET_CSTRING:
  case PROP_SET_INT:
  case PROP_SET_FLOAT:
    break;

  case PROP_SET_RSTRING:
    rstr_release(n->hpn_rstring);
    break;

  case PROP_SET_URI:
    rstr_release(n->hpn_uri_title);
    rstr_release(n->hpn_uri);
    break;

  case PROP_ADD_CHILD_BEFORE:
  case PROP_MOVE_CHILD:
  case PROP_REQ_MOVE_CHILD:
  case PROP_SELECT_CHILD:
    prop_ref_dec_locked(n->hpn_prop_extra);
  case PROP_ADD_CHILD:
  case PROP_DEL_CHILD:
  case PROP_REQ_NEW_CHILD:
  case PROP_SUGGEST_FOCUS:
  case PROP_SET_PROP:
    prop_ref_dec_locked(n->hpn_prop);
    break;

  case PROP_EXT_EVENT:
    event_release(n->hpn_ext_event);
    break;


  case PROP_ADD_CHILD_VECTOR_BEFORE:
    prop_ref_dec_locked(n->hpn_prop_extra);
    // FALLTHRU
  case PROP_REQ_DELETE_VECTOR:
  case PROP_ADD_CHILD_VECTOR:
  case PROP_ADD_CHILD_VECTOR_DIRECT:
    prop_vec_release(n->hpn_propv);
    break;

  case PROP_INVALID_EVENTS:
  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
  case PROP_WANT_MORE_CHILDS:
  case PROP_HAVE_MORE_CHILDS_YES:
  case PROP_HAVE_MORE_CHILDS_NO:
  case PROP_DESTROYED:
  case PROP_VALUE_PROP:
    break;
  }
}


/**
 *
 */
static void
prop_notify_free(prop_notify_t *n)
{
  prop_notify_free_payload(n);
  prop_sub_ref_dec_locked(n->hpn_sub);
  pool_put(notify_pool, n);
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
  } else if(event == PROP_SET_URI) {
    cb(s->hps_opaque, rstr_get(va_arg(ap, const rstr_t *)));
  } else if(!(s->hps_flags & PROP_SUB_IGNORE_VOID)) {
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
  } else if(event == PROP_SET_CSTRING) {
    const char *str = va_arg(ap, const char *);
    rstr_t *t = rstr_alloc(str);
    cb(s->hps_opaque, t);
    rstr_release(t);
  } else if(event == PROP_SET_URI) {
    cb(s->hps_opaque, va_arg(ap, rstr_t *));
  } else if(!(s->hps_flags & PROP_SUB_IGNORE_VOID)) {
    cb(s->hps_opaque, NULL);
  }
}


/**
 *
 */
static void 
trampoline_event(prop_sub_t *s, prop_event_t event, ...)
{
  prop_callback_event_t *cb = s->hps_callback;

  va_list ap;
  va_start(ap, event);

  if(event == PROP_EXT_EVENT)
    cb(s->hps_opaque, va_arg(ap, event_t *));
  va_end(ap);
}


/**
 *
 */
static void 
trampoline_destroyed(prop_sub_t *s, prop_event_t event, ...)
{
  prop_callback_destroyed_t *cb = s->hps_callback;

  va_list ap;
  va_start(ap, event);

  if(event == PROP_DESTROYED)
    cb(s->hps_opaque, va_arg(ap, prop_sub_t *));
  va_end(ap);
}

/**
 *
 */
static void
notify_invoke(prop_sub_t *s, prop_notify_t *n)
{
  prop_callback_t *cb = s->hps_callback;
  prop_trampoline_t *pt = s->hps_trampoline;

  switch(n->hpn_event) {
  case PROP_SET_DIR:
  case PROP_SET_VOID:
    if(pt != NULL)
      pt(s, n->hpn_event);
    else
      cb(s->hps_opaque, n->hpn_event, s->hps_user_int);
    break;

  case PROP_SET_RSTRING:
    if(pt != NULL)
      pt(s, n->hpn_event, n->hpn_rstring, n->hpn_rstrtype);
    else
      cb(s->hps_opaque, n->hpn_event, n->hpn_rstring,
         n->hpn_rstrtype, s->hps_user_int);
    rstr_release(n->hpn_rstring);
    break;

  case PROP_SET_CSTRING:
    if(pt != NULL)
      pt(s, n->hpn_event, n->hpn_cstring);
    else
      cb(s->hps_opaque, n->hpn_event, n->hpn_cstring, s->hps_user_int);
    break;

  case PROP_SET_URI:
    if(pt != NULL)
      pt(s, n->hpn_event, n->hpn_uri_title, n->hpn_uri);
    else
      cb(s->hps_opaque, n->hpn_event, n->hpn_uri_title, n->hpn_uri,
	 s->hps_user_int);
    rstr_release(n->hpn_uri_title);
    rstr_release(n->hpn_uri);
    break;


  case PROP_SET_INT:
    if(pt != NULL)
      pt(s, n->hpn_event, n->hpn_int, s->hps_user_int);
    else
      cb(s->hps_opaque, n->hpn_event, n->hpn_int, s->hps_user_int);
    break;

  case PROP_SET_FLOAT:
    if(pt != NULL)
      pt(s, n->hpn_event, n->hpn_float, s->hps_user_int);
    else
      cb(s->hps_opaque, n->hpn_event, n->hpn_float, s->hps_user_int);
    break;

  case PROP_ADD_CHILD:
    if(pt != NULL)
      pt(s, n->hpn_event, n->hpn_prop, n->hpn_flags, s->hps_user_int);
    else
      cb(s->hps_opaque, n->hpn_event, n->hpn_prop, n->hpn_flags, s->hps_user_int);
    prop_ref_dec(n->hpn_prop);
    break;

  case PROP_ADD_CHILD_BEFORE:
  case PROP_MOVE_CHILD:
  case PROP_SELECT_CHILD:
  case PROP_REQ_MOVE_CHILD:
    if(pt != NULL)
      pt(s, n->hpn_event, n->hpn_prop, n->hpn_prop_extra, n->hpn_flags, s->hps_user_int);
    else
      cb(s->hps_opaque, n->hpn_event, n->hpn_prop,
	 n->hpn_prop_extra, n->hpn_flags, s->hps_user_int);
    prop_ref_dec(n->hpn_prop);
    prop_ref_dec(n->hpn_prop_extra);
    break;

  case PROP_DEL_CHILD:
  case PROP_REQ_NEW_CHILD:
  case PROP_SUGGEST_FOCUS:
  case PROP_SET_PROP:
    if(pt != NULL)
      pt(s, n->hpn_event, n->hpn_prop, s->hps_user_int);
    else
      cb(s->hps_opaque, n->hpn_event, n->hpn_prop, s->hps_user_int);
    if(n->hpn_prop != NULL)
      prop_ref_dec(n->hpn_prop);
    break;
 
  case PROP_DESTROYED:
    if(pt != NULL)
      pt(s, n->hpn_event, s, s->hps_user_int);
    else
      cb(s->hps_opaque, n->hpn_event, s, s->hps_user_int);
    break;

  case PROP_EXT_EVENT:
    if(pt != NULL)
      pt(s, n->hpn_event, n->hpn_ext_event, s->hps_user_int);
    else
      cb(s->hps_opaque, n->hpn_event, n->hpn_ext_event, s->hps_user_int);
    event_release(n->hpn_ext_event);
    break;

  case PROP_SUBSCRIPTION_MONITOR_ACTIVE:
  case PROP_WANT_MORE_CHILDS:
  case PROP_HAVE_MORE_CHILDS_YES:
  case PROP_HAVE_MORE_CHILDS_NO:
    if(pt != NULL)
      pt(s, n->hpn_event, s->hps_user_int);
    else
      cb(s->hps_opaque, n->hpn_event, s->hps_user_int);
    break;

  case PROP_REQ_DELETE_VECTOR:
  case PROP_ADD_CHILD_VECTOR:
  case PROP_ADD_CHILD_VECTOR_DIRECT:
    if(pt != NULL)
      pt(s, n->hpn_event, n->hpn_propv, s->hps_user_int);
    else
      cb(s->hps_opaque, n->hpn_event, n->hpn_propv, s->hps_user_int);

    prop_vec_release(n->hpn_propv);
    break;

  case PROP_ADD_CHILD_VECTOR_BEFORE:
    if(pt != NULL)
      pt(s, n->hpn_event, n->hpn_propv, n->hpn_prop_extra, s->hps_user_int);
    else
      cb(s->hps_opaque, n->hpn_event, n->hpn_propv, n->hpn_prop_extra,
         s->hps_user_int);

    prop_vec_release(n->hpn_propv);
    prop_ref_dec(n->hpn_prop_extra);
    break;
  case PROP_VALUE_PROP:
    assert(pt == NULL);
    cb(s->hps_opaque, n->hpn_event, n->hpn_prop);
    prop_ref_dec(n->hpn_prop);
    break;

  case PROP_INVALID_EVENTS:
    abort();
    break;
  }
}


/**
 *
 */
int
prop_dispatch_one(prop_notify_t *n, int lockmode)
{
  assert(lockmode != 0);
  prop_sub_t *s = n->hpn_sub;

  assert((s->hps_flags & PROP_SUB_INTERNAL) == 0);

  if(s->hps_lock != NULL) {
    if(s->hps_lockmgr(s->hps_lock, lockmode)) {
      assert(lockmode == LOCKMGR_TRY);
      return 1;
    }
  }

  if(s->hps_zombie) {

    if(s->hps_lock != NULL)
      s->hps_lockmgr(s->hps_lock, 0);

    prop_notify_free_payload(n);
    return 0;
  }

  notify_invoke(s, n);

  if(s->hps_lock != NULL)
    s->hps_lockmgr(s->hps_lock, 0);

  return 0;
}

/**
 *
 */
void
prop_notify_dispatch(struct prop_notify_queue *q, const char *trace_name)
{
  prop_notify_t *n, *next;

  if(trace_name) {
    TAILQ_FOREACH(n, q, hpn_link) {
      char info[128];
#ifdef PROP_SUB_RECORD_SOURCE
      snprintf(info, sizeof(info), "%p (%s:%d)", n->hpn_sub,
               n->hpn_sub->hps_file, n->hpn_sub->hps_line);
#else
      snprintf(info, sizeof(info), "%p", n->hpn_sub);
#endif
      int64_t ts = arch_get_ts();
      prop_dispatch_one(n, LOCKMGR_LOCK);
      ts = arch_get_ts() - ts;
      if(ts > 10000) {
        TRACE(ts > 100000 ? TRACE_INFO : TRACE_DEBUG,
              "PROP", "%s: Dispatch of [%s] took %d us",
              trace_name, info, (int)ts);
      }
    }

  } else {
    TAILQ_FOREACH(n, q, hpn_link)
      prop_dispatch_one(n, LOCKMGR_LOCK);
  }

  hts_mutex_lock(&prop_mutex);

  for(n = TAILQ_FIRST(q); n != NULL; n = next) {
    next = TAILQ_NEXT(n, hpn_link);

    prop_sub_ref_dec_locked(n->hpn_sub);
    pool_put(notify_pool, n);
  }
  hts_mutex_unlock(&prop_mutex);
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

  if(pc->pc_prologue)
    pc->pc_prologue();
  
  hts_mutex_lock(&prop_mutex);

  while(pc->pc_run) {

    if(TAILQ_FIRST(&pc->pc_queue_exp) == NULL &&
       TAILQ_FIRST(&pc->pc_queue_nor) == NULL) {
      hts_cond_wait(&pc->pc_cond, &prop_mutex);
      continue;
    }

    TAILQ_MOVE(&q_exp, &pc->pc_queue_exp, hpn_link);
    TAILQ_INIT(&pc->pc_queue_exp);

    TAILQ_INIT(&q_nor);
    if((n = TAILQ_FIRST(&pc->pc_queue_nor)) != NULL) {
      TAILQ_REMOVE(&pc->pc_queue_nor, n, hpn_link);
      TAILQ_INSERT_TAIL(&q_nor, n, hpn_link);
    }

    const char *tt = pc->pc_flags & PROP_COURIER_TRACE_TIMES ?
      pc->pc_name : NULL;
    hts_mutex_unlock(&prop_mutex);
    prop_notify_dispatch(&q_exp, tt);
    prop_notify_dispatch(&q_nor, tt);
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

  if(pc->pc_epilogue)
    pc->pc_epilogue();

  return NULL;
}

/**
 *
 */
static void *
prop_global_dispatch_thread(void *aux)
{
  prop_sub_dispatch_t *psd, *s;
  prop_notify_t *n;

  hts_mutex_lock(&prop_mutex);
  while(1) {
    psd = TAILQ_FIRST(&prop_global_dispatch_queue);
    if(psd == NULL) {
      if(prop_global_dispatch_avail == PROP_GLOBAL_DISPATCH_IDLE_THREADS)
        break;

      prop_global_dispatch_avail++;
      hts_cond_wait(&prop_global_dispatch_cond, &prop_mutex);
      prop_global_dispatch_avail--;
      continue;
    }

    TAILQ_REMOVE(&prop_global_dispatch_queue, psd, psd_link);

    TAILQ_INSERT_TAIL(&prop_global_dispatch_dispatching_queue, psd, psd_link);

    n = TAILQ_FIRST(&psd->psd_notifications);
    assert(n != NULL);

    hts_mutex_unlock(&prop_mutex);
    int r = prop_dispatch_one(n, LOCKMGR_TRY);
    hts_mutex_lock(&prop_mutex);

    TAILQ_REMOVE(&prop_global_dispatch_dispatching_queue, psd, psd_link);


    if(r) {
      // Failed to acquire lock
      // Check if any other 'psd' is holding the lock

      TAILQ_FOREACH(s, &prop_global_dispatch_dispatching_queue, psd_link) {
        assert(s != psd); // We should not find ourself in here

        prop_notify_t *n2 = TAILQ_FIRST(&s->psd_notifications);
        assert(n2 != NULL);

        if(n2->hpn_sub->hps_zombie == 0 && n->hpn_sub->hps_zombie == 0 &&
           n2->hpn_sub->hps_lockmgr == n->hpn_sub->hps_lockmgr &&
           n2->hpn_sub->hps_lock    == n->hpn_sub->hps_lock) {
          // Found another subscription, piggy back to that
          TAILQ_INSERT_TAIL(&s->psd_wait_queue, psd, psd_link);
          break;
        }
      }

      if(s == NULL) {

        // Didn't find a contending psd, something else must be locking it.
        // Wait

        TAILQ_INSERT_TAIL(&prop_global_dispatch_dispatching_queue, psd,
                          psd_link);
        hts_mutex_unlock(&prop_mutex);
        prop_dispatch_one(n, LOCKMGR_LOCK);
        hts_mutex_lock(&prop_mutex);
        TAILQ_REMOVE(&prop_global_dispatch_dispatching_queue, psd, psd_link);

      } else {
        // Ok, we're now tagged onto another psd, find something else to do
        continue;
      }
    }

    TAILQ_REMOVE(&psd->psd_notifications, n, hpn_link);

    s = TAILQ_FIRST(&psd->psd_wait_queue);
    if(s != NULL) {
      TAILQ_REMOVE(&psd->psd_wait_queue, s, psd_link);
      TAILQ_INSERT_TAIL(&prop_global_dispatch_queue, s, psd_link);
      TAILQ_MERGE(&s->psd_wait_queue, &psd->psd_wait_queue, psd_link);
    }

    if(TAILQ_FIRST(&psd->psd_notifications) == NULL) {
      n->hpn_sub->hps_dispatch = NULL;
      pool_put(psd_pool, psd);
    } else {
      // Insert at end of queue to make sure we round robin between
      // all subscriptions
      TAILQ_INSERT_TAIL(&prop_global_dispatch_queue, psd, psd_link);
    }

    prop_sub_ref_dec_locked(n->hpn_sub);
    pool_put(notify_pool, n);
  }
  prop_global_dispatch_running--;
  hts_mutex_unlock(&prop_mutex);
  return NULL;
}


/**
 *
 */
static void
prop_global_dispatch_wakeup(void)
{
  if(prop_global_dispatch_avail > 0) {
    hts_cond_signal(&prop_global_dispatch_cond);
  } else {
    if(prop_global_dispatch_running < PROP_GLOBAL_DISPATCH_MAX_THREADS) {
      prop_global_dispatch_running++;
      hts_thread_create_detached("propdispatch",
                                 prop_global_dispatch_thread, NULL,
                                 THREAD_PRIO_BGTASK);
    }
  }
}

/**
 *
 */
static void
courier_notify(prop_courier_t *pc)
{
  if(pc->pc_has_cond)
    hts_cond_signal(&pc->pc_cond);
  else if(pc->pc_notify != NULL)
    pc->pc_notify(pc->pc_opaque);
}


/**
 *
 */
static void
courier_enqueue0(prop_sub_t *s, prop_notify_t *n, int expedite)
{
  if(s->hps_global_dispatch) {

    prop_sub_dispatch_t *psd = s->hps_dispatch;

    if(psd == NULL) {
      psd = s->hps_dispatch = pool_get(psd_pool);
      TAILQ_INIT(&psd->psd_notifications);
      TAILQ_INIT(&psd->psd_wait_queue);
      TAILQ_INSERT_TAIL(&prop_global_dispatch_queue, psd, psd_link);
      prop_global_dispatch_wakeup();
    }

    TAILQ_INSERT_TAIL(&psd->psd_notifications, n, hpn_link);

  } else {

    prop_courier_t *pc = s->hps_dispatch;

    if(expedite)
      TAILQ_INSERT_TAIL(&pc->pc_queue_exp, n, hpn_link);
    else
      TAILQ_INSERT_TAIL(&pc->pc_queue_nor, n, hpn_link);

    courier_notify(pc);
  }
}


/**
 *
 */
void
prop_courier_enqueue(prop_sub_t *s, prop_notify_t *n)
{
  courier_enqueue0(s, n, s->hps_flags & PROP_SUB_EXPEDITE);
}


/**
 *
 */
prop_notify_t *
prop_get_notify(prop_sub_t *s)
{
  prop_notify_t *n = pool_get(notify_pool);
  atomic_inc(&s->hps_refcount);
  n->hpn_sub = s;
  return n;
}


/**
 *
 */
static void
prop_build_notify_value(prop_sub_t *s, int direct, const char *origin,
			prop_t *p, struct prop_notify_queue *pnq)
{
  prop_notify_t *n;

  if(unlikely(s->hps_flags & PROP_SUB_IGNORE_VOID) && p->hp_type == PROP_VOID)
    return;

  if(s->hps_flags & PROP_SUB_DEBUG) {

    char trail[64];
    snprintf(trail, sizeof(trail), "%s%s",
             s->hps_flags & PROP_SUB_EXPEDITE ? " (exp)" : "",
             pnq ? " (deferred)" : "");

    switch(p->hp_type) {
    case PROP_RSTRING:
      PROPTRACE("rstr(%s) by %s%s", rstr_get(p->hp_rstring), origin, trail);
      break;
    case PROP_CSTRING:
      PROPTRACE("cstr(%s) by %s%s", p->hp_cstring, origin, trail);
      break;
    case PROP_URI:
      PROPTRACE("uri(%s,%s) by %s%s",
                rstr_get(p->hp_uri_title), rstr_get(p->hp_uri), origin,
                trail);
      break;
    case PROP_FLOAT:
      PROPTRACE("float(%f) by %s%s", p->hp_float, origin, trail);
      break;
    case PROP_INT:
      PROPTRACE("int(%d) by %s%s", p->hp_int, origin, trail);
      break;
    case PROP_DIR:
      PROPTRACE("dir by %s%s", origin, trail);
      break;
    case PROP_VOID:
      PROPTRACE("void by %s%s", origin, trail);
      break;
    case PROP_ZOMBIE:
      break;
    case PROP_PROP:
      PROPTRACE("prop by %s%s", origin, trail);
      break;
    case PROP_PROXY:
      abort();
    }
  }
  if((direct || s->hps_flags & PROP_SUB_INTERNAL) && pnq == NULL) {

    /* Direct mode can be requested during subscribe to get
       the current values updated directly without dispatch
       via the courier */

    prop_callback_t *cb = s->hps_callback;
    prop_trampoline_t *pt = s->hps_trampoline;


    if(s->hps_flags & PROP_SUB_SEND_VALUE_PROP) {
      assert(pt == NULL);
      cb(s->hps_opaque, PROP_VALUE_PROP, p);
    }

    switch(p->hp_type) {
    case PROP_RSTRING:
      if(pt != NULL)
	pt(s, PROP_SET_RSTRING, p->hp_rstring, p->hp_rstrtype, s->hps_user_int);
      else
	cb(s->hps_opaque, PROP_SET_RSTRING,
           p->hp_rstring, p->hp_rstrtype, s->hps_user_int);
      break;

    case PROP_CSTRING:
      if(pt != NULL)
	pt(s, PROP_SET_CSTRING, p->hp_cstring, s->hps_user_int);
      else
	cb(s->hps_opaque, PROP_SET_CSTRING, p->hp_cstring, s->hps_user_int);
      break;

    case PROP_URI:
      if(pt != NULL)
	pt(s, PROP_SET_URI, p->hp_uri_title, p->hp_uri, s->hps_user_int);
      else
	cb(s->hps_opaque, PROP_SET_URI,
	   p->hp_uri_title, p->hp_uri, s->hps_user_int);
      break;

    case PROP_FLOAT:
      if(pt != NULL)
	pt(s, PROP_SET_FLOAT, p->hp_float, s->hps_user_int);
      else
	cb(s->hps_opaque, PROP_SET_FLOAT, p->hp_float, s->hps_user_int);
      break;

    case PROP_INT:
      if(pt != NULL)
	pt(s, PROP_SET_INT, p->hp_int, s->hps_user_int);
      else
	cb(s->hps_opaque, PROP_SET_INT, p->hp_int, s->hps_user_int);
      break;

    case PROP_DIR:
      if(pt != NULL)
	pt(s, PROP_SET_DIR, s->hps_user_int);
      else
	cb(s->hps_opaque, PROP_SET_DIR, s->hps_user_int);
      break;

    case PROP_VOID:
      if(pt != NULL)
	pt(s, PROP_SET_VOID, s->hps_user_int);
      else
	cb(s->hps_opaque, PROP_SET_VOID, s->hps_user_int);
      break;

    case PROP_PROP:
      if(pt != NULL)
	pt(s, PROP_SET_DIR, p->hp_prop, s->hps_user_int);
      else
	cb(s->hps_opaque, PROP_SET_DIR, p->hp_prop, s->hps_user_int);
      break;

    case PROP_ZOMBIE:
    case PROP_PROXY:
      abort();

    }
    return;
  }

  if(s->hps_flags & PROP_SUB_SEND_VALUE_PROP) {
    n = prop_get_notify(s);
    n->hpn_prop = prop_ref_inc(p);
    n->hpn_event = PROP_VALUE_PROP;
    if(pnq) {
      TAILQ_INSERT_TAIL(pnq, n, hpn_link);
    } else {
      prop_courier_enqueue(s, n);
    }
  }

  n = prop_get_notify(s);

  switch(p->hp_type) {
  case PROP_RSTRING:
    assert(p->hp_rstring != NULL);
    n->hpn_rstring = rstr_dup(p->hp_rstring);
    n->hpn_rstrtype = p->hp_rstrtype;
    n->hpn_event = PROP_SET_RSTRING;
    break;

  case PROP_CSTRING:
    n->hpn_cstring = p->hp_cstring;
    n->hpn_event = PROP_SET_CSTRING;
    break;

  case PROP_URI:
    n->hpn_uri_title = rstr_dup(p->hp_uri_title);
    n->hpn_uri       = rstr_dup(p->hp_uri);
    n->hpn_event = PROP_SET_URI;
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

  case PROP_VOID:
    n->hpn_event = PROP_SET_VOID;
    break;

  case PROP_PROP:
    n->hpn_prop = prop_ref_inc(p->hp_prop);
    n->hpn_event = PROP_SET_PROP;
    break;

  case PROP_ZOMBIE:
  case PROP_PROXY:
    abort();
  }

  if(pnq) {
    TAILQ_INSERT_TAIL(pnq, n, hpn_link);
  } else {
    prop_courier_enqueue(s, n);
  }
}



/**
 *
 */
static void
prop_notify_void(prop_sub_t *s)
{
  if(unlikely(s->hps_flags & PROP_SUB_IGNORE_VOID))
    return;

  if(s->hps_flags & PROP_SUB_INTERNAL) {
    prop_callback_t *cb = s->hps_callback;
    prop_trampoline_t *pt = s->hps_trampoline;

    if(pt != NULL)
      pt(s, PROP_SET_VOID, s->hps_value_prop, s->hps_user_int);
    else
      cb(s->hps_opaque, PROP_SET_VOID, s->hps_value_prop, s->hps_user_int);
    return;
  }

  prop_notify_t *n = prop_get_notify(s);

  n->hpn_event = PROP_SET_VOID;
  n->hpn_prop_extra = NULL;
  prop_courier_enqueue(s, n);
}


/**
 *
 */
static void
prop_notify_destroyed(prop_sub_t *s)
{
  if(s->hps_flags & PROP_SUB_INTERNAL) {
    prop_callback_t *cb = s->hps_callback;
    prop_trampoline_t *pt = s->hps_trampoline;

    if(pt != NULL)
      pt(s, PROP_DESTROYED, s, s->hps_user_int);
    else
      cb(s->hps_opaque, PROP_DESTROYED, s, s->hps_user_int);
    return;
  }

  prop_notify_t *n = prop_get_notify(s);

  n->hpn_event = PROP_DESTROYED;

  courier_enqueue0(s, n, s->hps_flags & (PROP_SUB_EXPEDITE |
                                         PROP_SUB_TRACK_DESTROY_EXP));
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
      prop_build_notify_value(s, 0, origin, s->hps_value_prop, NULL);

  if(p->hp_flags & PROP_MULTI_NOTIFY)
    while((p = p->hp_parent) != NULL)
      if(p->hp_flags & PROP_MULTI_SUB)
	LIST_FOREACH(s, &p->hp_value_subscriptions, hps_value_prop_link)
	  if(s->hps_flags & PROP_SUB_MULTI)
	    prop_build_notify_value(s, 0, origin, p, NULL);
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
      pt(s, event, p, flags, s->hps_user_int);
    else
      cb(s->hps_opaque, event, p, flags, s->hps_user_int);
    return;
  }

  n = prop_get_notify(s);

  if(p != NULL)
    atomic_inc(&p->hp_refcount);
  n->hpn_flags = flags;
  n->hpn_prop = p;
  n->hpn_event = event;
  prop_courier_enqueue(s, n);
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
      pt(s, event, p, extra, flags, s->hps_user_int);
    else
      cb(s->hps_opaque, event, p, extra, flags, s->hps_user_int);
    return;
  }

  n = prop_get_notify(s);

  atomic_inc(&p->hp_refcount);
  if(extra != NULL)
    atomic_inc(&extra->hp_refcount);

  n->hpn_prop = p;
  n->hpn_prop_extra = extra;
  n->hpn_event = event;
  n->hpn_flags = flags;
  prop_courier_enqueue(s, n);
}


/**
 *
 */
void
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
			 prop_t *p2, int direct)
{
  if(direct || s->hps_flags & PROP_SUB_INTERNAL) {
    prop_callback_t *cb = s->hps_callback;
    prop_trampoline_t *pt = s->hps_trampoline;

    if(pt != NULL)
      pt(s, event, pv, p2, s->hps_user_int);
    else
      cb(s->hps_opaque, event, pv, p2, s->hps_user_int);
    return;
  }


  prop_notify_t *n = prop_get_notify(s);
  n->hpn_propv = prop_vec_addref(pv);
  n->hpn_flags = 0;
  n->hpn_event = event;
  n->hpn_prop_extra = prop_ref_inc(p2);
  prop_courier_enqueue(s, n);
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
      prop_build_notify_childv(s, pv, event, p2, 0);
}


/**
 *
 */
static void
prop_send_ext_event0(prop_t *p, event_t *e)
{
  prop_sub_t *s;
  prop_notify_t *n;

  if(p->hp_type == PROP_PROXY) {
    prop_proxy_send_event(p, e);
    return;
  }


  while(p->hp_originator != NULL)
    p = p->hp_originator;

  LIST_FOREACH(s, &p->hp_value_subscriptions, hps_value_prop_link) {
    n = prop_get_notify(s);

    n->hpn_event = PROP_EXT_EVENT;
    atomic_inc(&e->e_refcount);
    n->hpn_ext_event = e;
    prop_courier_enqueue(s, n);
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
	pt(s, e, s->hps_user_int);
      else
	cb(s->hps_opaque, e, s->hps_user_int);
    } else {
      n = prop_get_notify(s);
      n->hpn_event = e;
      prop_courier_enqueue(s, n);
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
      n = prop_get_notify(s);
      n->hpn_event = PROP_SUBSCRIPTION_MONITOR_ACTIVE;
      prop_courier_enqueue(s, n);
    }
  }
}


/**
 *
 */
void
prop_send_ext_event(prop_t *p, event_t *e)
{
  if(p == NULL)
    return;
  hts_mutex_lock(&prop_mutex);
  prop_send_ext_event0(p, e);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
static prop_sub_t *
prop_check_canonical_subs_descending(prop_t *p)
{
  prop_t *c;
  prop_sub_t *r;

  if(p->hp_type != PROP_DIR)
    return NULL;

  TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link) {
    prop_sub_t *s = LIST_FIRST(&c->hp_canonical_subscriptions);

    if(s != NULL)
      return s;

    if((r = prop_check_canonical_subs_descending(c)) != NULL)
      return r;
  }

  return NULL;
}


/**
 *
 */
static int attribute_unused_result
prop_clean(prop_t *p)
{
  prop_sub_t *s;
  if(p->hp_flags & PROP_CLIPPED_VALUE) {
    return 1;
  }
  switch(p->hp_type) {
  case PROP_DIR:
    s = prop_check_canonical_subs_descending(p);
    if(s != NULL) {
#ifdef PROP_SUB_RECORD_SOURCE
      tracelog(TRACE_NO_PROP, TRACE_ERROR, "prop",
            "Refusing to clean prop %s because a decendant (%s) have "
            "canonical sub %s:%d",
            prop_get_DN(p, 1),
            prop_get_DN(s->hps_canonical_prop, 1),
            s->hps_file, s->hps_line);
#endif
      return 1;
    }
    prop_destroy_childs0(p);
    break;

  case PROP_ZOMBIE:
  case PROP_PROXY:
    return 1;

  case PROP_VOID:
  case PROP_INT:
  case PROP_FLOAT:
  case PROP_CSTRING:
    break;

  case PROP_RSTRING:
    rstr_release(p->hp_rstring);
    break;

  case PROP_PROP:
    prop_ref_dec_locked(p->hp_prop);
    break;

  case PROP_URI:
    rstr_release(p->hp_uri_title);
    rstr_release(p->hp_uri);
    break;
  }
  return 0;
}


/**
 *
 */
void
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
  prop_t *hp = pool_get(prop_pool);
#ifdef PROP_DEBUG
  hp->hp_magic = PROP_MAGIC;
  SIMPLEQ_INIT(&hp->hp_ref_trace);
#endif
  hp->hp_flags = noalloc ? PROP_NAME_NOT_ALLOCATED : 0;
  hp->hp_originator = NULL;
  atomic_set(&hp->hp_refcount, 1);
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

  if(parent->hp_type == PROP_PROXY)
    return prop_proxy_create(parent, name);

  prop_make_dir(parent, skipme, "prop_create()");

  if(name != NULL) {
    TAILQ_FOREACH(hp, &parent->hp_childs, hp_parent_link) {
      if(hp->hp_name != NULL && !strcmp(hp->hp_name, name)) {

	if(!(hp->hp_flags & PROP_NAME_NOT_ALLOCATED) && noalloc) {
	  // Trick: We have a pointer to a compile time constant string
	  // and the current prop does not have that, we could switch to
	  // it and thus save some memory allocation
	  free((void *)hp->hp_name);
	  hp->hp_name = name;
	  hp->hp_flags |= PROP_NAME_NOT_ALLOCATED;
	}
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
	       int noalloc, int incref)
{
  prop_t *p;
  hts_mutex_lock(&prop_mutex);
  if(parent != NULL && parent->hp_type != PROP_ZOMBIE) {
    p = prop_create0(parent, name, skipme, noalloc);
  } else {
    p = NULL;
  }
  if(incref)
    p = prop_ref_inc(p);
  hts_mutex_unlock(&prop_mutex);
  return p;
}


/**
 *
 */
prop_t *
prop_create_root_ex(const char *name, int noalloc)
{
  hts_mutex_lock(&prop_mutex);
  prop_t *p = prop_make(name, noalloc, NULL);
  hts_mutex_unlock(&prop_mutex);
  return p;
}


/**
 *
 */
prop_t *
prop_create_multi(prop_t *p, ...)
{
  va_list ap;
  const char *name;
  va_start(ap, p);

  if(p == NULL)
    return NULL;

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    p = prop_ref_inc(p);
    hts_mutex_unlock(&prop_mutex);
    return p;
  }

  while((name = va_arg(ap, const char *)) != NULL)
    p = prop_create0(p, name, NULL, 0);

  p = prop_ref_inc(p);
  hts_mutex_unlock(&prop_mutex);
  return p;
}


/**
 *
 */
prop_t *
prop_create_after(prop_t *parent, const char *name, prop_t *after,
		  prop_sub_t *skipme)
{
  prop_t *p;
  hts_mutex_lock(&prop_mutex);

  if(parent != NULL && parent->hp_type != PROP_ZOMBIE) {

    prop_make_dir(parent, skipme, "prop_create_after()");

    TAILQ_FOREACH(p, &parent->hp_childs, hp_parent_link)
      if(p->hp_name != NULL && !strcmp(p->hp_name, name))
	break;

    if(p == NULL) {

      p = prop_make(name, 0, parent);
  
      if(after == NULL) {
	TAILQ_INSERT_HEAD(&parent->hp_childs, p, hp_parent_link);
      } else {
	TAILQ_INSERT_AFTER(&parent->hp_childs, after, p, hp_parent_link);
      }

      prop_t *next = TAILQ_NEXT(p, hp_parent_link);
      if(next == NULL) {
	prop_notify_child2(p, parent, next, PROP_ADD_CHILD_BEFORE, skipme, 0);
      } else {
	prop_notify_child(p, parent, PROP_ADD_CHILD, skipme, 0);
      }

    } else {

      prop_t *prev = TAILQ_PREV(p, prop_queue, hp_parent_link);

      if(prev != after) {
	
	TAILQ_REMOVE(&parent->hp_childs, p, hp_parent_link);

	if(after == NULL) {
	  TAILQ_INSERT_HEAD(&parent->hp_childs, p, hp_parent_link);
	} else {
	  TAILQ_INSERT_AFTER(&parent->hp_childs, after, p, hp_parent_link);
	}
	
	prop_t *next = TAILQ_NEXT(p, hp_parent_link);
	prop_notify_child2(p, parent, next, PROP_MOVE_CHILD, skipme, 0);
      }
    }


  } else {
    p = NULL;
  }

  hts_mutex_unlock(&prop_mutex);
  return p;
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
static void
recursive_unlink(prop_t *p)
{
  prop_t *c;
  while((c = LIST_FIRST(&p->hp_targets)) != NULL) {
    recursive_unlink(c);
    prop_unlink0(c, NULL, "prop_destroy0", NULL);
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
	   propname(p), p->hp_type, atomic_get(&p->hp_refcount), p->hp_xref,
	   csubs, psubs);
  }
#endif

  if(p->hp_type == PROP_ZOMBIE)
    return 0;

  p->hp_xref--;
  if(p->hp_xref)
    return 0;

  recursive_unlink(p);

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

  case PROP_PROP:
    prop_ref_dec_locked(p->hp_prop);
    break;

  case PROP_URI:
    rstr_release(p->hp_uri_title);
    rstr_release(p->hp_uri);
    break;

  case PROP_FLOAT:
  case PROP_INT:
  case PROP_VOID:
  case PROP_CSTRING:
    break;
  case PROP_PROXY:
    assert(p->hp_originator == NULL);
    prop_proxy_destroy(p);
    p->hp_type = PROP_ZOMBIE;
    goto finale;
  }

  p->hp_type = PROP_ZOMBIE;

  while((s = LIST_FIRST(&p->hp_canonical_subscriptions)) != NULL) {

    LIST_REMOVE(s, hps_canonical_prop_link);
    s->hps_canonical_prop = NULL;

    if(s->hps_flags & (PROP_SUB_TRACK_DESTROY | PROP_SUB_TRACK_DESTROY_EXP))
      prop_notify_destroyed(s);
  }

  while((s = LIST_FIRST(&p->hp_value_subscriptions)) != NULL) {

    if(!(s->hps_flags & (PROP_SUB_TRACK_DESTROY |
                         PROP_SUB_TRACK_DESTROY_EXP))) {
#ifdef PROP_SUB_RECORD_SOURCE
      if(s->hps_flags & PROP_SUB_DEBUG) {
        PROPTRACE("Sub %s:%d lost value prop",
                  s->hps_file, s->hps_line);
      }
#endif
      prop_notify_void(s);
    }

    LIST_REMOVE(s, hps_value_prop_link);
    s->hps_value_prop = NULL;
  }

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

 finale:
  if(!(p->hp_flags & PROP_NAME_NOT_ALLOCATED))
    free((void *)p->hp_name);
  p->hp_name = NULL;

  prop_ref_dec_locked(p);
  return 1;
}


/**
 *
 */
void
prop_destroy(prop_t *p)
{
  if(p == NULL)
    return;
  hts_mutex_lock(&prop_mutex);
  prop_destroy0(p);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
static void
prop_destroy_childs0(prop_t *p)
{
  prop_t *c, *next;

  struct prop_queue childs;
  TAILQ_MOVE(&childs, &p->hp_childs, hp_parent_link);
  TAILQ_INIT(&p->hp_childs);

  p->hp_type = PROP_VOID;
  p->hp_selected = NULL;
  prop_notify_value(p, NULL, "prop_destroy_childs0()");

  for(c = TAILQ_FIRST(&childs); c != NULL; c = next) {
    next = TAILQ_NEXT(c, hp_parent_link);
    c->hp_parent = NULL;
    prop_destroy0(c);
  }

}


/**
 *
 */
void
prop_destroy_childs(prop_t *p)
{
  if(p == NULL)
    return;
  hts_mutex_lock(&prop_mutex);
  if(p->hp_type == PROP_DIR)
    prop_destroy_childs0(p);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
static void
prop_void_childs0(prop_t *p)
{
  if(p->hp_type == PROP_ZOMBIE)
    return;
  if(p->hp_type == PROP_DIR) {
    prop_t *c;
    TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link)
      prop_void_childs0(c);
  } else {
    if(prop_clean(p))
      return;
    p->hp_type = PROP_VOID;
    prop_notify_value(p, NULL, "prop_void_childs()");
  }
}


/**
 *
 */
void
prop_void_childs(prop_t *p)
{
  if(p == NULL)
    return;
  hts_mutex_lock(&prop_mutex);
  prop_void_childs0(p);
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
    if(name == NULL) {
      prop_t *n;
      for(c = TAILQ_FIRST(&p->hp_childs); c != NULL; c = n) {
	n = TAILQ_NEXT(c, hp_parent_link);
	if(c->hp_name == NULL)
	  prop_destroy_child(p, c);
      }
    } else {
      TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link) {
	if(c->hp_name != NULL && !strcmp(c->hp_name, name)) {
	  prop_destroy_child(p, c);
	  break;
	}
      }
    }
  }
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_destroy_first(prop_t *p)
{
  hts_mutex_lock(&prop_mutex);
  if(p->hp_type == PROP_DIR) {
    prop_t *c = TAILQ_FIRST(&p->hp_childs);
    if(c != NULL)
      prop_destroy_child(p, c);
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

  if(before && p->hp_parent != before->hp_parent)
    return;

  if(TAILQ_NEXT(p, hp_parent_link) != before) {

    parent = p->hp_parent;
    if(parent == NULL)
      return;

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
void
prop_req_move0(prop_t *p, prop_t *before, prop_sub_t *skipme)
{
  prop_t *parent;

  if(p == before)
    return;

  if(p->hp_type == PROP_PROXY) {
    assert(skipme == NULL);
    prop_proxy_req_move(p, before);
    return;
  }

  if(TAILQ_NEXT(p, hp_parent_link) != before) {
    parent = p->hp_parent;
    prop_notify_child2(p, parent, before, PROP_REQ_MOVE_CHILD, skipme, 0);
  }
}


/**
 *
 */
void
prop_req_move(prop_t *p, prop_t *before)
{
  hts_mutex_lock(&prop_mutex);
  prop_req_move0(p, before, NULL);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
static prop_t *
prop_subfind(prop_t *p, const char **name, int follow_symlinks,
             int allow_indexing, prop_t **origin_chain)
{
  prop_t *c;
  int ocnum = 0;

  while(name[0] != NULL) {
    while(follow_symlinks && p->hp_originator != NULL) {
      if(origin_chain)
	origin_chain[ocnum++] = p;
      p = p->hp_originator;
    }

    if(p->hp_type != PROP_DIR) {

      if(p->hp_type != PROP_VOID) {
	/* We don't want subscriptions to overwrite real values */
        if(origin_chain)
          origin_chain[0] = NULL;
	return NULL;
      }

      TAILQ_INIT(&p->hp_childs);
      p->hp_selected = NULL;
      p->hp_type = PROP_DIR;

      prop_notify_value(p, NULL, "prop_subfind()");
    }

    if(allow_indexing && name[0][0] == '*') {
      unsigned int i = atoi(name[0]+1);
      TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link) {
	if(i == 0)
	  break;
	i--;
      }
      if(c == NULL) {
        if(origin_chain)
          origin_chain[0] = NULL;
	return NULL;
      }
    } else {

      TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link) {
	if(c->hp_name != NULL && !strcmp(c->hp_name, name[0]))
	  break;
      }
    }
    p = c ?: prop_create0(p, name[0], NULL, 0);    
    name++;
  }

  while(follow_symlinks && p->hp_originator != NULL) {
    if(origin_chain)
      origin_chain[ocnum++] = p;
    p = p->hp_originator;
  }

  if(origin_chain)
    origin_chain[ocnum] = NULL;

  return p;
}


LIST_HEAD(prop_root_node_list, prop_root_node);

/**
 *
 */
typedef struct prop_root_node {
  prop_t *p;
  const char *name;
  LIST_ENTRY(prop_root_node) link;
} prop_root_node_t;



/**
 *
 */
static prop_t *
prop_resolve_tree(const char *name, struct prop_root_node_list *prl,
                  struct prop_root *prv, int prvlen)
{
  prop_root_node_t *pr;

  if(!strcmp(name, "global"))
    return prop_global;

  for(int i = 0; i < prvlen; i++, prv++) {
    if(prv->p != NULL && !strcmp(prv->name, name))
      return prv->p;
  }

  LIST_FOREACH(pr, prl, link) {
    prop_t *p = pr->p;
    if(p->hp_name != NULL && !strcmp(name, p->hp_name))
      return p;
    if(pr->name != NULL && !strcmp(name, pr->name))
      return p;
  }
  return NULL;
}

/**
 *
 */
#ifdef PROP_DEBUG
prop_t *
prop_get_by_name0(const char *file, int line,
                  const char **name, int follow_symlinks, ...)
#else
prop_t *
prop_get_by_name(const char **name, int follow_symlinks, ...)
#endif
{
  prop_t *p;
  prop_root_node_t *pr;
  struct prop_root_node_list proproots;
  int tag;
  va_list ap;
  prop_root_t *prv = NULL;
  int prvlen = 0;

  va_start(ap, follow_symlinks);

  LIST_INIT(&proproots);

  do {
    tag = va_arg(ap, int);
    switch(tag) {

    case PROP_TAG_ROOT:
      pr = alloca(sizeof(prop_root_node_t));
      pr->p = va_arg(ap, prop_t *);
      pr->name = NULL;
      if(pr->p != NULL)
	LIST_INSERT_HEAD(&proproots, pr, link);
      break;

    case PROP_TAG_NAMED_ROOT:
      pr = alloca(sizeof(prop_root_node_t));
      pr->p = va_arg(ap, prop_t *);
      pr->name = va_arg(ap, const char *);
      if(pr->p != NULL && pr->name != NULL)
	LIST_INSERT_HEAD(&proproots, pr, link);
      break;

    case PROP_TAG_ROOT_VECTOR:
      prv = va_arg(ap, prop_root_t *);
      prvlen = va_arg(ap, int);
      break;

    case PROP_TAG_END:
      break;

    default:
      abort();
    }
  } while(tag);

  va_end(ap);

  p = prop_resolve_tree(name[0], &proproots, prv, prvlen);

  if(p == NULL || p->hp_type == PROP_ZOMBIE)
    return NULL;

  name++;
  hts_mutex_lock(&prop_mutex);
  if(p->hp_type == PROP_PROXY) {
    int len = 0;
    if(p->hp_proxy_pfx != NULL)
      while(p->hp_proxy_pfx[len] != NULL)
        len++;

    while(name[len] != NULL)
      len++;
    char **vec = malloc((1 + len) * sizeof(char *));

    len = 0;
    if(p->hp_proxy_pfx != NULL) {
      while(p->hp_proxy_pfx[len] != NULL) {
        vec[len] = strdup(p->hp_proxy_pfx[len]);
        len++;
      }
    }

    for(int i = 0 ; name[i] != NULL; i++)
      vec[len++] = strdup(name[i]);
    vec[len] = NULL;

#ifdef PROP_DEBUG
    p = prop_proxy_make0(p->hp_proxy_ppc, p->hp_proxy_id, NULL, p, vec,
                         file, line);
#else
    p = prop_proxy_make(p->hp_proxy_ppc, p->hp_proxy_id, NULL, p, vec);
#endif
    if(follow_symlinks)
      p->hp_flags |= PROP_PROXY_FOLLOW_SYMLINK;

  } else {

    p = prop_subfind(p, name, follow_symlinks, 1, NULL);

  }

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
#ifdef PROP_SUB_RECORD_SOURCE
prop_sub_t *
prop_subscribe_ex(const char *file, int line, int flags, ...)
#else
  prop_sub_t *
  prop_subscribe(int flags, ...)
#endif
{
  prop_t *value, *canonical, *c;
  prop_sub_t *s, *t;
  int direct = !!(flags & (PROP_SUB_DIRECT_UPDATE | PROP_SUB_INTERNAL));
  int notify_now = !(flags & PROP_SUB_NO_INITIAL_UPDATE);
  int tag;
  const char **name = NULL;
  void *opaque = NULL;
  prop_courier_t *pc = NULL;
  void *lock = NULL;
  lockmgr_fn_t *lockmgr = NULL;
  prop_root_node_t *pr;
  struct prop_root_node_list proproots;
  void *cb = NULL;
  prop_trampoline_t *trampoline = NULL;
  int dolock = !(flags & PROP_SUB_DONTLOCK);
  int activate_on_canonical = 0;
  int user_int = 0;
  va_list ap;
  va_start(ap, flags);

  prop_t *origin_chain[16];
  origin_chain[0] = NULL;

  struct prop_proxy_connection *ppc = NULL;

  prop_root_t *prv = NULL;
  int prvlen = 0;

  LIST_INIT(&proproots);

  do {
    tag = va_arg(ap, int);
    switch(tag) {
    case PROP_TAG_NAME_VECTOR:
      if(name != NULL)
        (void)va_arg(ap, const char **);
      else
        name = va_arg(ap,  const char **);
      break;

    case PROP_TAG_NAMESTR:
      if(name != NULL) {
        (void)va_arg(ap, const char *);
      } else {
	const char *s, *s0 = va_arg(ap, const char *);
        int segments = 1, ptr = 0, len;
	char **nv;

        if(s0 == NULL)
          break;

	for(s = s0; *s != 0; s++)
	  if(*s == '.')
	    segments++;

	nv = alloca((segments + 1) * sizeof(char *));
	name = (void *)nv;

	for(s = s0; *s != 0; s++) {
	  if(*s == '.') {
	    len = s - s0;
	    if(len > 0) {
	      nv[ptr] = alloca(len + 1);
	      memcpy(nv[ptr], s0, len);
	      nv[ptr++][len] = 0;
	    }
	    s0 = s + 1;
	  }
	}

	len = s - s0;
	nv[ptr] = alloca(len + 1);
	memcpy(nv[ptr], s0, len);
	nv[ptr++][len] = 0;
	nv[ptr] = NULL;
      }
      break;

    case PROP_TAG_CALLBACK:
      cb = va_arg(ap, prop_callback_t *);
      trampoline = NULL;
      opaque = va_arg(ap, void *);
      break;

    case PROP_TAG_CALLBACK_USER_INT:
      cb = va_arg(ap, prop_callback_t *);
      trampoline = NULL;
      opaque = va_arg(ap, void *);
      user_int = va_arg(ap, int);
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

    case PROP_TAG_CALLBACK_EVENT:
      cb = va_arg(ap, void *);
      trampoline = trampoline_event;
      opaque = va_arg(ap, void *);
      break;

    case PROP_TAG_CALLBACK_DESTROYED:
      cb = va_arg(ap, void *);
      trampoline = trampoline_destroyed;
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
      pr = alloca(sizeof(prop_root_node_t));
      pr->p = va_arg(ap, prop_t *);
      pr->name = NULL;
      if(pr->p != NULL)
	LIST_INSERT_HEAD(&proproots, pr, link);
      break;

    case PROP_TAG_NAMED_ROOT:
      pr = alloca(sizeof(prop_root_node_t));
      pr->p = va_arg(ap, prop_t *);
      pr->name = va_arg(ap, const char *);
      if(pr->p != NULL && pr->name != NULL)
	LIST_INSERT_HEAD(&proproots, pr, link);
      break;

    case PROP_TAG_ROOT_VECTOR:
      prv = va_arg(ap, prop_root_t *);
      prvlen = va_arg(ap, int);
      break;

    case PROP_TAG_MUTEX:
      lock = va_arg(ap, void *);
      break;

    case PROP_TAG_LOCKMGR:
      lockmgr = va_arg(ap, lockmgr_fn_t *);
      break;

#ifdef PROP_SUB_RECORD_SOURCE
    case PROP_TAG_SOURCE:
      file = va_arg(ap, const char *);
      line = va_arg(ap, int);
#endif
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

    pr = LIST_FIRST(&proproots);

    canonical = value = pr ? pr->p : NULL;

    if(dolock)
      hts_mutex_lock(&prop_mutex);

    if(value != NULL && value->hp_type == PROP_PROXY) {
      ppc = value->hp_proxy_ppc;
    }

  } else {
    prop_t *p;
    if(flags & PROP_SUB_ALT_PATH) {
      p = LIST_FIRST(&proproots) ? LIST_FIRST(&proproots)->p : NULL;
    } else {
      p = prop_resolve_tree(name[0], &proproots, prv, prvlen);
      name++;
    }

    if(dolock)
      hts_mutex_lock(&prop_mutex);

    if(p == NULL || p->hp_type == PROP_ZOMBIE) {
      canonical = value = NULL;

    } else if(p->hp_type == PROP_PROXY) {
      ppc = p->hp_proxy_ppc;
      canonical = value = p;

    } else {
      /* Canonical name is the resolved props without following symlinks */
      canonical = prop_subfind(p, name, 0, 0, NULL);

      /* ... and value will follow links */
      value     = prop_subfind(p, name, 1, 0, origin_chain);
    }
  }

  if(flags & PROP_SUB_SINGLETON) {
    LIST_FOREACH(s, &value->hp_value_subscriptions, hps_value_prop_link) {
      if(s->hps_callback == cb && s->hps_opaque == opaque) {
	hts_mutex_unlock(&prop_mutex);
	return NULL;
      }
    }
  }

  if(value != NULL && value->hp_type == PROP_ZOMBIE)
    value = NULL;

  if(canonical != NULL && canonical->hp_type == PROP_ZOMBIE)
    canonical = NULL;


  s = pool_get(sub_pool);

#ifdef PROP_SUB_RECORD_SOURCE
  s->hps_file = file;
  s->hps_line = line;
#endif

#ifdef PROP_SUB_STATS
  LIST_INSERT_HEAD(&all_subs, s, hps_all_sub_link);
#endif
  s->hps_multiple_origins = 0;
  s->hps_origin = NULL;
  s->hps_zombie = 0;
  s->hps_flags = flags;
  s->hps_trampoline = trampoline;
  s->hps_callback = cb;
  s->hps_opaque = opaque;
  atomic_set(&s->hps_refcount, 1);
  s->hps_user_int = user_int;

  if(origin_chain[0] != NULL) {
    
    if(origin_chain[1] != NULL) {
      /* We passed multiple originators in the search for our value prop
	 need to build using an external structure
      */

      s->hps_multiple_origins = 1;
      int num = 0;
      while(origin_chain[num] != NULL)
	num++;
      assert(num >= 2);

      s->hps_pots = NULL;

      while(--num >= 0) {
	prop_originator_tracking_t *pot = pool_get(pot_pool);
	pot->pot_p = prop_ref_inc(origin_chain[num]);
	pot->pot_next = s->hps_pots;
	s->hps_pots = pot;
      }

    } else {
      s->hps_origin = prop_ref_inc(origin_chain[0]);
    }
  }

  if(lockmgr == NULL)
    lockmgr = proplockmgr;

  if(pc != NULL) {
    s->hps_global_dispatch = 0;
    s->hps_dispatch = pc;
    s->hps_lock = pc->pc_entry_lock;
    s->hps_lockmgr = pc->pc_lockmgr ?: lockmgr;
    pc->pc_refcount++;
  } else {
    s->hps_global_dispatch = 1;
    s->hps_dispatch = NULL;
    s->hps_lock = lock;
    s->hps_lockmgr = lockmgr;
  }

  s->hps_lockmgr(s->hps_lock, LOCKMGR_RETAIN);

  s->hps_canonical_prop = canonical;
  s->hps_value_prop = value;

  if(ppc != NULL) {

    // Subscribe via external proxy
    s->hps_proxy = 1;
    prop_proxy_subscribe(ppc, s, value, name);

  } else {
    s->hps_proxy = 0;

    if(canonical != NULL) {
      LIST_INSERT_HEAD(&canonical->hp_canonical_subscriptions, s,
                       hps_canonical_prop_link);

      if(s->hps_flags & PROP_SUB_SUBSCRIPTION_MONITOR &&
         (canonical->hp_flags & PROP_MONITORED) == 0) {
        canonical->hp_flags |= PROP_MONITORED;

        LIST_FOREACH(t, &canonical->hp_value_subscriptions,
                     hps_value_prop_link) {
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
    }

    if(value != NULL) {

      LIST_INSERT_HEAD(&value->hp_value_subscriptions, s, 
                       hps_value_prop_link);


      if(notify_now) {

        prop_build_notify_value(s, direct, "prop_subscribe()",
                                s->hps_value_prop, NULL);

        if(value->hp_type == PROP_DIR && !(s->hps_flags & PROP_SUB_MULTI)) {

          if(value->hp_selected == NULL && direct) {

            int cnt = 0;
            TAILQ_FOREACH(c, &value->hp_childs, hp_parent_link)
              cnt++;
	
            prop_vec_t *pv = prop_vec_create(cnt);
            TAILQ_FOREACH(c, &value->hp_childs, hp_parent_link)
              pv = prop_vec_append(pv, c);

            prop_build_notify_childv(s, pv, PROP_ADD_CHILD_VECTOR_DIRECT,
                                     NULL, 1);
            prop_vec_release(pv);

          } else {
            TAILQ_FOREACH(c, &value->hp_childs, hp_parent_link)
              prop_build_notify_child(s, c, PROP_ADD_CHILD, direct,
                                      gen_add_flags(c, value));
          }
        }
      }

      /* If we have any subscribers monitoring for subscriptions, notify them */
      if(!(s->hps_flags & PROP_SUB_SUBSCRIPTION_MONITOR) &&
         value->hp_flags & PROP_MONITORED)
        prop_send_subscription_monitor_active(value);
    }

    if(activate_on_canonical)
      prop_send_subscription_monitor_active(canonical);

    if(canonical == NULL &&
       s->hps_flags & (PROP_SUB_TRACK_DESTROY | PROP_SUB_TRACK_DESTROY_EXP)) {

      if(direct) {
        prop_callback_t *cb = s->hps_callback;
        prop_trampoline_t *pt = s->hps_trampoline;
        if(pt != NULL)
          pt(s, PROP_DESTROYED, s, s->hps_user_int);
        else
          cb(s->hps_opaque, PROP_DESTROYED, s, s->hps_user_int);
        s = NULL;

      } else {
        prop_notify_destroyed(s);
      }
    }
  }
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
  assert(s->hps_zombie == 0);

#ifdef PROP_SUB_STATS
  LIST_REMOVE(s, hps_all_sub_link);
#endif

  s->hps_zombie = 1;
  if(!s->hps_global_dispatch) {
    prop_courier_t *pc = s->hps_dispatch;
    pc->pc_refcount--;
  }

#ifdef PROP_DEBUG
  s->hps_dispatch = NULL;
#endif

  if(s->hps_proxy) {
    prop_proxy_unsubscribe(s);
  } else {

    if(s->hps_multiple_origins) {
      prop_originator_tracking_t *pot, *next;
      for(pot = s->hps_pots; pot != NULL; pot = next) {
        next = pot->pot_next;
        prop_ref_dec_locked(pot->pot_p);
        pool_put(pot_pool, pot);
      }
    } else {
      prop_ref_dec_locked(s->hps_origin);
    }

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
  }
  prop_sub_ref_dec_locked(s);
}




/**
 *
 */
void
prop_unsubscribe(prop_sub_t *s)
{
  if(s == NULL)
    return;

  hts_mutex_lock(&prop_mutex);
  prop_unsubscribe0(s);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_sub_reemit(prop_sub_t *s)
{
  if(s == NULL)
    return;

  hts_mutex_lock(&prop_mutex);
  prop_build_notify_value(s, 0, "reemit", s->hps_value_prop, NULL);
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
  hts_cond_init(&prop_global_dispatch_cond, &prop_mutex);

  TAILQ_INIT(&prop_global_dispatch_queue);
  TAILQ_INIT(&prop_global_dispatch_dispatching_queue);


  prop_pool   = pool_create("prop", sizeof(prop_t), 0);
  notify_pool = pool_create("notify", sizeof(prop_notify_t), 0);
  sub_pool    = pool_create("subs", sizeof(prop_sub_t), 0);
  pot_pool    = pool_create("pots", sizeof(prop_originator_tracking_t), 0);
  psd_pool    = pool_create("psds", sizeof(prop_sub_dispatch_t), 0);

  hts_mutex_lock(&prop_mutex);
  prop_global = prop_make("global", 1, NULL);
  hts_mutex_unlock(&prop_mutex);
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


void
prop_set_string_exl(prop_t *p, prop_sub_t *skipme, const char *str,
		    prop_str_type_t type)
{
  if(p->hp_type == PROP_ZOMBIE)
    return;

  if(p->hp_type == PROP_PROXY) {
    prop_proxy_set_string(p, str, type);
    return;
  }

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
  prop_notify_value(p, skipme, "prop_set_string()");
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
static void
prop_set_rstring_exl(prop_t *p, prop_sub_t *skipme, rstr_t *rstr,
                     prop_str_type_t type)
{
  if(p->hp_type == PROP_ZOMBIE)
    return;

  if(p->hp_type == PROP_PROXY) {
    prop_proxy_set_string(p, rstr_get(rstr), type);
    return;
  }

  if(p->hp_type != PROP_RSTRING) {

    if(prop_clean(p))
      return;

  } else if(!strcmp(rstr_get(p->hp_rstring), rstr_get(rstr))) {
    return;
  } else {
    rstr_release(p->hp_rstring);
  }
  p->hp_rstring = rstr_dup(rstr);
  p->hp_type = PROP_RSTRING;
  p->hp_rstrtype = type;
  prop_notify_value(p, skipme, "prop_set_rstring()");
}


/**
 *
 */
void
prop_set_rstring_ex(prop_t *p, prop_sub_t *skipme, rstr_t *rstr,
                    prop_str_type_t type)
{
  if(p == NULL)
    return;

  if(rstr == NULL) {
    prop_set_void_ex(p, skipme);
    return;
  }

  hts_mutex_lock(&prop_mutex);
  prop_set_rstring_exl(p, skipme, rstr, type);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_adopt_rstring_ex(prop_t *p, prop_sub_t *skipme, rstr_t *rstr,
                      prop_str_type_t type)
{
  if(p != NULL) {
    if(rstr == NULL) {
      prop_set_void_ex(p, skipme);
    } else {
      hts_mutex_lock(&prop_mutex);
      prop_set_rstring_exl(p, skipme, rstr, type);
      hts_mutex_unlock(&prop_mutex);
    }
  }
  rstr_release(rstr);
}


/**
 *
 */
static void
prop_set_cstring_exl(prop_t *p, prop_sub_t *skipme, const char *cstr)
{
  if(p->hp_type == PROP_ZOMBIE)
    return;

  if(p->hp_type == PROP_PROXY) {
    prop_proxy_set_string(p, cstr, PROP_STR_UTF8);
    return;
  }

  if(p->hp_type != PROP_CSTRING) {

    if(prop_clean(p)) {
      return;
    }

  } else if(!strcmp(p->hp_cstring, cstr)) {
    return;
  }

  p->hp_cstring = cstr;
  p->hp_type = PROP_CSTRING;
  prop_notify_value(p, skipme, "prop_set_cstring()");
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
  prop_set_cstring_exl(p, skipme, cstr);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_set_uri_ex(prop_t *p, prop_sub_t *skipme, const char *title,
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

  if(p->hp_type != PROP_URI) {

    if(prop_clean(p)) {
      hts_mutex_unlock(&prop_mutex);
      return;
    }

  } else if(!strcmp(rstr_get(p->hp_uri_title) ?: "", title ?: "") &&
	    !strcmp(rstr_get(p->hp_uri)   ?: "", url   ?: "")) {
    hts_mutex_unlock(&prop_mutex);
    return;
  } else {
    rstr_release(p->hp_uri_title);
    rstr_release(p->hp_uri);
  }

  p->hp_uri_title = rstr_alloc(title);
  p->hp_uri   = rstr_alloc(url);
  p->hp_type = PROP_URI;

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
prop_get_float_locked(prop_t *p)
{
  if(p == NULL || p->hp_type == PROP_ZOMBIE)
    return NULL;

  if(p->hp_type == PROP_INT) {
    prop_int_to_float(p);
    return p;
  }

  if(p->hp_type != PROP_FLOAT) {

    if(prop_clean(p))
      return NULL;

    p->hp_float = 0;
    p->hp_type = PROP_FLOAT;
  }
  return p;
}


/**
 *
 */
static void
prop_set_float_exl(prop_t *p, prop_sub_t *skipme, float v)
{
  if(p != NULL && p->hp_type == PROP_PROXY) {
    prop_proxy_set_float(p, v);
    return;
  }

  if((p = prop_get_float_locked(p)) != NULL) {

    if(p->hp_float != v) {

      if(p->hp_flags & PROP_CLIPPED_VALUE) {
	if(v > p->u.f.max)
	  v  = p->u.f.max;
	if(v < p->u.f.min)
	  v  = p->u.f.min;
      }

      p->hp_float = v;
      
      prop_notify_value(p, skipme, "prop_set_float_ex()");
    }
  }
}


/**
 *
 */
void
prop_set_float_ex(prop_t *p, prop_sub_t *skipme, float v)
{
  hts_mutex_lock(&prop_mutex);
  prop_set_float_exl(p, skipme, v);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_add_float_ex(prop_t *p, prop_sub_t *skipme, float v)
{
  hts_mutex_lock(&prop_mutex);

  if((p = prop_get_float_locked(p)) != NULL) {
    float n = p->hp_float + v;

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
  }
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_set_float_clipping_range(prop_t *p, float min, float max)
{
  hts_mutex_lock(&prop_mutex);

  if((p = prop_get_float_locked(p)) != NULL) {

    p->hp_flags |= PROP_CLIPPED_VALUE;

    p->u.f.min = min;
    p->u.f.max = max;

    float n = p->hp_float;

    if(n > max)
      n  = max;
    if(n < min)
      n  = min;

    if(n != p->hp_float) {
      p->hp_float = n;
      prop_notify_value(p, NULL, "prop_set_float_clipping_range()");
    }
  }

  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
static void
prop_set_int_exl(prop_t *p, prop_sub_t *skipme, int v)
{
  if(p->hp_type == PROP_ZOMBIE)
    return;

  if(p->hp_type == PROP_PROXY) {
    prop_proxy_set_int(p, v);
    return;
  }

  if(p->hp_type != PROP_INT) {

    if(p->hp_type == PROP_FLOAT) {
      prop_float_to_int(p);
    } else if(prop_clean(p)) {
      return;
    } else {
      p->hp_type = PROP_INT;
    }

  } else if(p->hp_int == v) {
    return;
  } else if(p->hp_flags & PROP_CLIPPED_VALUE) {
    if(v > p->u.i.max)
      v  = p->u.i.max;
    if(v < p->u.i.min)
      v  = p->u.i.min;
  }

  p->hp_int = v;

  prop_notify_value(p, skipme, "prop_set_int_exl()");
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
  prop_set_int_exl(p, skipme, v);
  hts_mutex_unlock(&prop_mutex);
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

  if(p->hp_type == PROP_PROXY) {
    prop_proxy_add_int(p, v);
    hts_mutex_unlock(&prop_mutex);
    return;
  }

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

  if(p->hp_type == PROP_PROXY) {
    prop_proxy_toggle_int(p);
    hts_mutex_unlock(&prop_mutex);
    return;
  }

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
static void
prop_set_void_exl(prop_t *p, prop_sub_t *skipme)
{
  if(p->hp_type == PROP_ZOMBIE)
    return;

  if(p->hp_type == PROP_PROXY) {
    prop_proxy_set_void(p);
    return;
  }

  if(p->hp_type != PROP_VOID) {

    if(prop_clean(p))
      return;
    
    p->hp_type = PROP_VOID;
    prop_notify_value(p, skipme, "prop_set_void()");
  }
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
  prop_set_void_exl(p, skipme);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
static void
prop_set_prop_exl(prop_t *p, prop_sub_t *skipme, prop_t *target)
{
  if(p->hp_type == PROP_ZOMBIE)
    return;

  if(p->hp_type != PROP_PROP) {

    if(prop_clean(p))
      return;

  } else if(p->hp_prop == target) {
    return;
  } else {
    prop_ref_dec_locked(p->hp_prop);
  }

  p->hp_prop = prop_ref_inc(target);
  p->hp_type = PROP_PROP;

  prop_notify_value(p, skipme, "prop_set_prop()");
}

/**
 *
 */
void
prop_set_prop_ex(prop_t *p, prop_sub_t *skipme, prop_t *x)
{
  if(p == NULL)
    return;

  hts_mutex_lock(&prop_mutex);
  prop_set_prop_exl(p, skipme, x);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_copy_ex(prop_t *dst, prop_sub_t *skipme, prop_t *src)
{
  if(dst == NULL)
    return;

  hts_mutex_lock(&prop_mutex);

  if(src == NULL) {
    prop_set_void_exl(dst, skipme);
  } else {
    switch(src->hp_type) {
    case PROP_INT:
      prop_set_int_exl(dst, skipme, src->hp_int);
      break;
    case PROP_FLOAT:
      prop_set_float_exl(dst, skipme, src->hp_float);
      break;
    case PROP_RSTRING:
      prop_set_rstring_exl(dst, skipme, src->hp_rstring, 0);
      break;
    case PROP_CSTRING:
      prop_set_cstring_exl(dst, skipme, src->hp_cstring);
      break;
    default:
      prop_set_void_exl(dst, skipme);
      break;

    }
  }

  hts_mutex_unlock(&prop_mutex);
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

  case PROP_URI:
    return !strcmp(rstr_get(a->hp_uri_title), rstr_get(b->hp_uri_title)) &&
      !strcmp(rstr_get(a->hp_uri), rstr_get(b->hp_uri));

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



#ifdef PROP_DEBUG
/**
 *
 */
static void
show_origins(prop_sub_t *s, const char *prefix, prop_t *broken_link)
{
  printf("Tracked sub %p @ '%s' ", s, prefix);
  if(s->hps_origin == NULL) {
    printf("No origins\n");
    return;
  }

  if(s->hps_multiple_origins) {
    printf("Multiple origins\n");
    prop_originator_tracking_t *pot = s->hps_pots;
    while(pot != NULL) {
      printf("  %10s %s\n", pot->pot_p == broken_link ? "BREAK" : "",
             prop_get_DN(pot->pot_p, 1));
      pot = pot->pot_next;
    }

  } else {
    printf("Single origin\n");
    prop_t *p = s->hps_origin;
    printf("  %10s %s\n", p == broken_link ? "BREAK" : "",
           prop_get_DN(p, 1));
  }
}
#endif


/**
 *
 */
static void
retarget_subscription(prop_t *p, prop_sub_t *s, prop_sub_t *skipme,
		      const char *origin, struct prop_notify_queue *pnq)
{
  int equal;


#ifdef PROP_DEBUG
  if(s == track_sub) {
    printf("Tracked sub %p got %s to %s%s previously at %s%s\n",
           s, pnq ? "restored" : "attached",
           prop_get_DN(p, 7), p == s->hps_canonical_prop ? " (CANONICAL)" : "",
           prop_get_DN(s->hps_value_prop, 7),
           s->hps_value_prop == s->hps_canonical_prop ? " (CANONICAL)" : "");
  }

#endif

  if(s->hps_value_prop != NULL) {

    if(s->hps_value_prop == p)
      return;

    /* If we previously was a directory, flush it out */
    if(s->hps_value_prop->hp_type == PROP_DIR) {
      if(s != skipme) 
	prop_notify_void(s);
    }
    LIST_REMOVE(s, hps_value_prop_link);
    equal = prop_value_compare(s->hps_value_prop, p);
  } else {
    equal = 0;
  }

  LIST_INSERT_HEAD(&p->hp_value_subscriptions, s, hps_value_prop_link);
  s->hps_value_prop = p;

  /* Monitors, activate ! */
  if(p->hp_flags & PROP_MONITORED)
    prop_send_subscription_monitor_active(p);
    
  /* Update with new value */
  if(s == skipme || equal) 
    return; /* Unless it's to be skipped */

  s->hps_pending_unlink = pnq ? 1 : 0;
  prop_build_notify_value(s, 0, origin, s->hps_value_prop, pnq);

  if(p->hp_type != PROP_DIR)
    return;

  prop_t *c;

  TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link)
    prop_build_notify_child(s, c, PROP_ADD_CHILD, 0, gen_add_flags(c, p));
}


/**
 * Prepend a new origin (where we divert search due to a symlink)
 */
static void
prepend_origin(prop_sub_t *s, prop_t *prepend)
{
  if(s->hps_origin == NULL) {
    s->hps_origin = prop_ref_inc(prepend);
    return;
  }

  prop_originator_tracking_t *pot;
  if(!s->hps_multiple_origins) {

    if(prepend == s->hps_origin)
      return;

    pot = pool_get(pot_pool);
    pot->pot_p = s->hps_origin;
    pot->pot_next = NULL;
    s->hps_pots = pot;
    s->hps_multiple_origins = 1;

  } else {

    for(pot = s->hps_pots; pot != NULL; pot = pot->pot_next) {
      if(pot->pot_p == prepend)
        return;
    }
  }

  pot = pool_get(pot_pool);
  pot->pot_p = prop_ref_inc(prepend);
  pot->pot_next = s->hps_pots;
  s->hps_pots = pot;
}


/**
 *
 */
static void
prepend_origins(prop_sub_t *s, prop_t **prependvec, int prependveclen)
{
  for(int i = 0; i < prependveclen; i++)
    prepend_origin(s, prependvec[i]);
}


/**
 *
 */
static void
relink_subscriptions(prop_t *src, prop_t *dst, prop_sub_t *skipme,
		     const char *origin, prop_t **prependvec,
		     int prependveclen)
{
  prop_sub_t *s;

  if(src->hp_originator != NULL) {

    prop_t **newpv = alloca((prependveclen + 1) * sizeof(prop_t *));
    memcpy(newpv, prependvec, prependveclen * sizeof(prop_t *));
    newpv[prependveclen] = src;

    relink_subscriptions(src->hp_originator, dst, skipme,
			 origin, newpv, prependveclen + 1);
    return;
  }

  assert(src != dst);

  while((s = LIST_FIRST(&dst->hp_value_subscriptions)) != NULL) {
    LIST_REMOVE(s, hps_value_prop_link);
#ifdef PROP_DEBUG
    if(s == track_sub)
      show_origins(s, "relink pre", NULL);
#endif
    retarget_subscription(src, s, skipme, origin, NULL);
    prepend_origins(s, prependvec, prependveclen);
#ifdef PROP_DEBUG
    if(s == track_sub)
      show_origins(s, "relink post", NULL);
#endif
  }

  if(dst->hp_type != PROP_DIR)
    return;

  switch(src->hp_type) {
  case PROP_DIR:
    break;
  case PROP_VOID:
    prop_make_dir(src, skipme, origin);
    break;
  default:
    return;
  }

  prop_t *c;

  /**
   * Take care of childs,
   * We iterate over the destination tree, since that's where
   * any _currently_ active subscriptions are located that we
   * must reattach.
   */

  TAILQ_FOREACH(c, &dst->hp_childs, hp_parent_link) {
    if(c->hp_name == NULL)
      continue;

    prop_t *z = prop_create0(src, c->hp_name, NULL,
                             c->hp_flags & PROP_NAME_NOT_ALLOCATED);

    if(c->hp_type == PROP_DIR)
      prop_make_dir(z, skipme, origin);

    relink_subscriptions(z, c, skipme, origin, prependvec, prependveclen);
  }
}


/**
 *
 */
static int
search_for_linkage(prop_t *src, prop_t *link)
{
  prop_sub_t *s;

  while(src->hp_originator != NULL)
    src = src->hp_originator;

  LIST_FOREACH(s, &src->hp_value_subscriptions, hps_value_prop_link) {
    if(s->hps_origin == NULL)
      continue;

    if(!s->hps_multiple_origins) {
      if(s->hps_origin == link)
	return 1;
    } else {
      prop_originator_tracking_t *pot;

      for(pot = s->hps_pots; pot != NULL; pot = pot->pot_next)
        if(pot->pot_p == link)
	  return 1;
    }
  }

  if(src->hp_type != PROP_DIR)
    return 0;

  prop_t *c;
  TAILQ_FOREACH(c, &src->hp_childs, hp_parent_link) {

    if(c->hp_name == NULL)
      continue;

    if(search_for_linkage(c, link))
      return 1;
  }
  return 0;
}



/**
 *
 */
static void
restore_and_descend(prop_t *dst, prop_t *src, prop_sub_t *skipme,
		    const char *origin, struct prop_notify_queue *pnq,
		    prop_t *broken_link,
                    prop_t **prependvec, int prependveclen)
{
  prop_sub_t *s, *next;

  for(s = LIST_FIRST(&src->hp_value_subscriptions); s != NULL; s = next) {
    next = LIST_NEXT(s, hps_value_prop_link);

#ifdef PROP_DEBUG
    if(s == track_sub)
      show_origins(s, "restore_and_descend pre", broken_link);
#endif
     if(s->hps_origin == NULL)
      continue;

    if(!s->hps_multiple_origins) {

      if(s->hps_origin != broken_link)
	continue;
      
      prop_ref_dec_locked(s->hps_origin);
      s->hps_origin = NULL;

    } else {

      prop_originator_tracking_t *pot, **prev = &s->hps_pots;

      while((pot = *prev) != NULL) {
	if(pot->pot_p == broken_link)
	  break;
	prev = &pot->pot_next;
      }

      if(pot == NULL)
	continue;

      // Delete pot

      *prev = pot->pot_next;
      prop_ref_dec_locked(pot->pot_p);
      pool_put(pot_pool, pot);

      // If only one pot remains, transform into single ref

      if(s->hps_pots->pot_next == NULL) {
	pot = s->hps_pots;
	s->hps_multiple_origins = 0;
	s->hps_origin = pot->pot_p;
	pool_put(pot_pool, pot);
      }
    }
    retarget_subscription(dst, s, skipme, origin, pnq);
    prepend_origins(s, prependvec, prependveclen);
#ifdef PROP_DEBUG
    if(s == track_sub)
      show_origins(s, "restore_and_descend post", broken_link);
#endif
  }

  if(src->hp_type != PROP_DIR)
    return;

  prop_t *c;
  TAILQ_FOREACH(c, &src->hp_childs, hp_parent_link) {

    if(c->hp_name == NULL)
      continue;

    prop_t *s = c;

    while(s->hp_originator != NULL)
      s = s->hp_originator;

    if(!search_for_linkage(s, broken_link))
      continue;

    prop_t *z = prop_create0(dst, c->hp_name, NULL,
                             c->hp_flags & PROP_NAME_NOT_ALLOCATED);

    if(c->hp_type == PROP_DIR)
      prop_make_dir(z, skipme, origin);

    restore_and_descend(z, s, skipme, origin, pnq, broken_link,
                        prependvec, prependveclen);
  }
}


/**
 *
 */
static void
prop_follow_and_unlink(prop_t *dst, prop_t *src, prop_sub_t *skipme,
                       const char *origin, struct prop_notify_queue *pnq,
                       prop_t **prependvec, int prependveclen)
{
  if(src->hp_originator != NULL) {

    prop_t **newpv = alloca((prependveclen + 1) * sizeof(prop_t *));
    memcpy(newpv, prependvec, prependveclen * sizeof(prop_t *));
    newpv[prependveclen] = src;

    prop_follow_and_unlink(dst, src->hp_originator, skipme, origin, pnq,
                           newpv, prependveclen + 1);
    return;
  }

  if(search_for_linkage(src, dst))
    restore_and_descend(dst, src, skipme, origin, pnq, dst, prependvec,
                        prependveclen);
}


/**
 *
 */
static void
prop_unlink0(prop_t *dst, prop_sub_t *skipme, const char *origin,
	     struct prop_notify_queue *pnq)
{
  prop_t *src = dst->hp_originator;

  dst->hp_originator = NULL;
  LIST_REMOVE(dst, hp_originator_link);

  prop_follow_and_unlink(dst, src, skipme, origin, pnq, NULL, 0);

  if(dst->hp_flags & PROP_XREFED_ORIGINATOR) {
    prop_destroy0(src);
    dst->hp_flags &= ~PROP_XREFED_ORIGINATOR;
  }
}


/**
 *
 */
void
prop_link0(prop_t *src, prop_t *dst, prop_sub_t *skipme, int hard, int debug)
{
  prop_notify_t *n;
  prop_sub_t *s;
  struct prop_notify_queue pnq;

  assert(src != dst);

  if(src->hp_type == PROP_ZOMBIE || dst->hp_type == PROP_ZOMBIE)
    return;

  if(debug) {
    printf("Link %s -> %s\n", prop_get_DN(src, 1), prop_get_DN(dst, 1));
  }

  TAILQ_INIT(&pnq);

  if(dst->hp_originator != NULL) {

    if(dst->hp_originator == src) {
      // Linking against itself again, this is a NOP
      if(debug)
        printf("Duplicate link is a NOP\n");
      return;
    }

    if(debug) {
      printf("--- Destination [%s] before unlink ---\n", prop_get_DN(dst, 1));
      prop_print_tree0(dst, 0, 7);
      printf("\n\n\n");

      printf("--- Previous origin [%s] before unlink ---\n",
             prop_get_DN(dst->hp_originator, 1));
      prop_print_tree0(dst->hp_originator, 0, 7);
      printf("\n\n\n");
    }


    prop_unlink0(dst, skipme, "prop_link()/unlink", &pnq);
    if(debug)
      printf("\tUnlink done (destination had previous linkage)\n");
  }

  if(debug) {
    printf("--- Destination [%s] before link ---\n", prop_get_DN(dst, 1));
    prop_print_tree0(dst, 0, 7);
    printf("\n\n\n");

    printf("--- Source [%s] before link ---\n", prop_get_DN(src, 1));
    prop_print_tree0(src, 0, 7);
    printf("\n\n\n");
  }

  if(hard == PROP_LINK_XREFED ||
     (hard == PROP_LINK_XREFED_IF_ORPHANED && src->hp_parent == NULL)) {
    dst->hp_flags |= PROP_XREFED_ORIGINATOR;
    assert(src->hp_xref < 255);
    src->hp_xref++;
  }

  dst->hp_originator = src;
  LIST_INSERT_HEAD(&src->hp_targets, dst, hp_originator_link);

  relink_subscriptions(src, dst, skipme, "relink_tree()", &dst, 1);

  while((n = TAILQ_FIRST(&pnq)) != NULL) {
    TAILQ_REMOVE(&pnq, n, hpn_link);

    s = n->hpn_sub;

    if(s->hps_pending_unlink) {
      s->hps_pending_unlink = 0;

      if(s->hps_flags & PROP_SUB_INTERNAL) {
        notify_invoke(s, n);
        prop_sub_ref_dec_locked(s);
        pool_put(notify_pool, n);
      } else {
        prop_courier_enqueue(s, n);
      }
    } else {
      // Already updated by the new linkage
      prop_notify_free(n);
    }
  }

  if(debug) {
    printf("--- Destination [%s] after link ---\n", prop_get_DN(dst, 1));
    prop_print_tree0(dst, 0, 7);
    printf("\n\n\n");

    printf("--- Source [%s] after link ---\n", prop_get_DN(src, 1));
    prop_print_tree0(src, 0, 7);
    printf("\n\n\n");
  }
}


/**
 *
 */
static void
prop_unlink_exl(prop_t *p, prop_sub_t *skipme)
{
  assert(p->hp_type != PROP_PROXY);

  if(p->hp_type != PROP_ZOMBIE && p->hp_originator != NULL)
    prop_unlink0(p, skipme, "prop_unlink()/childs", NULL);
}


/**
 *
 */
static void
prop_link_exl(prop_t *src, prop_t *dst, prop_sub_t *skipme,
              int hard, int debug)
{
  if(src == NULL) {
    prop_unlink_exl(dst, skipme);
    return;
  }


  if(src->hp_type == PROP_PROXY && dst->hp_type == PROP_PROXY) {
    assert(skipme == NULL);
    prop_proxy_link(src, dst);
  } else if(src->hp_type == PROP_PROXY || dst->hp_type == PROP_PROXY) {
    printf("Linking a proxied property with a non-proxied one is mind-boggling difficult, giving up\n");
    printf("SRC\n");
    prop_print_tree0(src, 0, 1);
    printf("DST\n");
    prop_print_tree0(dst, 0, 1);

    abort();
  } else {
    prop_link0(src, dst, skipme, hard, debug);
  }
}


/**
 *
 */
void
prop_link_ex(prop_t *src, prop_t *dst, prop_sub_t *skipme,
             int hard, int debug)
{
  if(dst == NULL)
    return;

  hts_mutex_lock(&prop_mutex);
  prop_link_exl(src, dst, skipme, hard, debug);
  hts_mutex_unlock(&prop_mutex);
}




/**
 *
 */
void
prop_unlink_ex(prop_t *p, prop_sub_t *skipme)
{
  if(p == NULL)
    return;

  hts_mutex_lock(&prop_mutex);
  prop_unlink_exl(p, skipme);
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
prop_t *
prop_get_prop(prop_t *p)
{
  hts_mutex_lock(&prop_mutex);

  if(p != NULL && p->hp_type == PROP_PROP) {
    p = prop_ref_inc(p->hp_prop);
  } else {
    p = prop_ref_inc(p);
  }
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

  if(p->hp_type == PROP_PROXY) {
    prop_proxy_select(p);
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
void
prop_select_by_value_ex(prop_t *p, const char *name, prop_sub_t *skipme)
{
  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_DIR) {
    prop_t *c;
    TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link)
      if(c->hp_name != NULL && !strcmp(c->hp_name, name))
        break;

    prop_notify_child(c, p, PROP_SELECT_CHILD, skipme, 0);
    p->hp_selected = c;
  }
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_suggest_focus0(prop_t *p)
{
  prop_t *parent;

  if(p->hp_type == PROP_ZOMBIE)
    return;

  parent = p->hp_parent;

  if(parent != NULL) {
    assert(parent->hp_type == PROP_DIR);
    prop_notify_child(p, parent, PROP_SUGGEST_FOCUS, NULL, 0);
  }
}


/**
 *
 */
void
prop_suggest_focus(prop_t *p)
{
  hts_mutex_lock(&prop_mutex);
  prop_suggest_focus0(p);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
prop_t *
prop_findv(prop_t *p, char **names)
{
  hts_mutex_lock(&prop_mutex);

  while(p->hp_originator != NULL)
    p = p->hp_originator;

  prop_t *c = p;

  for(int i = 0; names[i] != NULL; i++) {
    const char *n = names[i];

    if(p->hp_type != PROP_DIR) {
      c = NULL;
      break;
    }

    TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link)
      if(c->hp_name != NULL && !strcmp(c->hp_name, n))
	break;
    if(c == NULL)
      break;

    while(c->hp_originator != NULL)
      c = c->hp_originator;
    p = c;
  }
  c = prop_ref_inc(c);
  hts_mutex_unlock(&prop_mutex);
  return c;
}


/**
 *
 */
static prop_t *
prop_find0(prop_t *p, va_list ap)
{
  prop_t *c = p;
  const char *n;

  while((n = va_arg(ap, const char *)) != NULL) {

    if(p->hp_type != PROP_DIR) {
      c = NULL;
      break;
    }

    TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link)
      if(c->hp_name != NULL && !strcmp(c->hp_name, n))
	break;
    if(c == NULL)
	return NULL;
    p = c;
  }
  return c;
}


/**
 *
 */
prop_t *
prop_find(prop_t *p, ...)
{
  va_list ap;
  va_start(ap, p);

  hts_mutex_lock(&prop_mutex);
  prop_t *c = prop_ref_inc(prop_find0(p, ap));
  hts_mutex_unlock(&prop_mutex);
  va_end(ap);
  return c;
}


/**
 *
 */
prop_t *
prop_first_child(prop_t *p)
{
  hts_mutex_lock(&prop_mutex);
  prop_t *c = p && p->hp_type == PROP_DIR ? TAILQ_FIRST(&p->hp_childs) : NULL;
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
  TAILQ_INIT(&pc->pc_dispatch_queue);
  TAILQ_INIT(&pc->pc_free_queue);
  return pc;
}


/**
 *
 */
prop_courier_t *
prop_courier_create_thread(hts_mutex_t *entrymutex, const char *name,
                           int flags)
{
  prop_courier_t *pc = prop_courier_create();
  char buf[URL_MAX];
  pc->pc_entry_lock = entrymutex;
  snprintf(buf, sizeof(buf), "PC:%s", name);
  pc->pc_flags = flags;
  pc->pc_has_cond = 1;
  hts_cond_init(&pc->pc_cond, &prop_mutex);

  pc->pc_name = strdup(name);
  pc->pc_run = 1;
  hts_thread_create_joinable(buf, &pc->pc_thread, prop_courier, pc,
			     THREAD_PRIO_MODEL);
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
prop_courier_wait(prop_courier_t *pc, struct prop_notify_queue *q, int timeout)
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

  TAILQ_MOVE(q, &pc->pc_queue_exp, hpn_link);
  TAILQ_MERGE(q, &pc->pc_queue_nor, hpn_link);
  hts_mutex_unlock(&prop_mutex);
  return r;
}


/**
 *
 */
void
prop_courier_wait_and_dispatch(prop_courier_t *pc)
{
  struct prop_notify_queue q;
  prop_courier_wait(pc, &q, 0);
  prop_notify_dispatch(&q, 0);
}



#ifdef POOL_DEBUG
static void
debug_check_courier(void *ptr, void *pc)
{
  prop_sub_t *hps = ptr;
  if(hps->hps_dispatch == pc) {
#ifdef PROP_SUB_RECORD_SOURCE
    tracelog(TRACE_NO_PROP, TRACE_ERROR, "prop",
             "Subscription at %s:%d lingering", hps->hps_file, hps->hps_line);
#else
    tracelog(TRACE_NO_PROP, TRACE_ERROR, "prop",
          "Subscription at %p lingering", hps);
#endif
  }
}

#endif

/**
 *
 */
void
prop_courier_destroy(prop_courier_t *pc)
{
  if(pc->pc_refcount != 0) {
    tracelog(TRACE_NO_PROP, TRACE_ERROR, "prop",
	  "Refcnt is %d on courier '%s' destroy", pc->pc_refcount,
          pc->pc_name);

#ifdef POOL_DEBUG
    hts_mutex_lock(&prop_mutex);
    pool_foreach(sub_pool, debug_check_courier, pc);
    hts_mutex_unlock(&prop_mutex);
#endif
  }

  if(pc->pc_run) {
    hts_mutex_lock(&prop_mutex);
    pc->pc_run = 0;
    hts_cond_signal(&pc->pc_cond);
    hts_mutex_unlock(&prop_mutex);

    hts_thread_join(&pc->pc_thread);
  }

  if(pc->pc_has_cond)
    hts_cond_destroy(&pc->pc_cond);

  free(pc->pc_name);

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
  TAILQ_MOVE(&q, &pc->pc_queue_exp, hpn_link);
  TAILQ_MERGE(&q, &pc->pc_queue_nor, hpn_link);
  hts_mutex_unlock(&prop_mutex);
  prop_notify_dispatch(&q, 0);
}


/**
 *
 */
void
prop_courier_poll_timed(prop_courier_t *pc, int maxtime)
{
  if(maxtime == -1)
    return prop_courier_poll(pc);

  prop_notify_t *n, *next;

  if(!hts_mutex_trylock(&prop_mutex)) {
    TAILQ_MERGE(&pc->pc_dispatch_queue, &pc->pc_queue_exp, hpn_link);
    TAILQ_MERGE(&pc->pc_dispatch_queue, &pc->pc_queue_nor, hpn_link);

    for(n = TAILQ_FIRST(&pc->pc_free_queue); n != NULL; n = next) {
      next = TAILQ_NEXT(n, hpn_link);

      prop_sub_ref_dec_locked(n->hpn_sub);
      pool_put(notify_pool, n);
    }
    TAILQ_INIT(&pc->pc_free_queue);

    hts_mutex_unlock(&prop_mutex);
  }

  int64_t ts = arch_get_ts();

  while((n = TAILQ_FIRST(&pc->pc_dispatch_queue)) != NULL) {
    prop_dispatch_one(n, LOCKMGR_LOCK);
    TAILQ_REMOVE(&pc->pc_dispatch_queue, n, hpn_link);
    TAILQ_INSERT_TAIL(&pc->pc_free_queue, n, hpn_link);
    if(arch_get_ts() > ts + maxtime)
      break;
  }
}


/**
 *
 */
int
prop_courier_check(prop_courier_t *pc)
{
  hts_mutex_lock(&prop_mutex);
  int r = TAILQ_FIRST(&pc->pc_queue_exp) || TAILQ_FIRST(&pc->pc_queue_nor);
  hts_mutex_unlock(&prop_mutex);
  return r;

}


/**
 *
 */
rstr_t *
prop_get_string(prop_t *p, ...)
{
  rstr_t *r = NULL;
  char buf[64];
  va_list ap;

  va_start(ap, p);

  hts_mutex_lock(&prop_mutex);
  p = prop_find0(p, ap);

  if(p != NULL) {

    switch(p->hp_type) {
    case PROP_RSTRING:
      r = rstr_dup(p->hp_rstring);
      break;
    case PROP_CSTRING:
      r = rstr_alloc(p->hp_cstring);
      break;
    case PROP_URI:
      r = rstr_dup(p->hp_uri_title);
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
      break;
    }
  }
  hts_mutex_unlock(&prop_mutex);
  va_end(ap);
  return r;
}


/**
 *
 */
int
prop_get_int(prop_t *p, ...)
{
  int r = 0;
  va_list ap;

  va_start(ap, p);

  hts_mutex_lock(&prop_mutex);
  p = prop_find0(p, ap);

  if(p != NULL) {

    switch(p->hp_type) {
    case PROP_RSTRING:
      r = atoi(rstr_get(p->hp_rstring));
      break;
    case PROP_CSTRING:
      r = atoi(p->hp_cstring);
      break;
    case PROP_URI:
      r = atoi(rstr_get(p->hp_uri_title));
      break;
    case PROP_FLOAT:
      r = p->hp_float;
      break;
    case PROP_INT:
      r = p->hp_int;
      break;
    default:
      break;
    }
  }
  hts_mutex_unlock(&prop_mutex);
  va_end(ap);
  return r;
}

/**
 *
 */
static void
prop_seti(prop_sub_t *skipme, prop_t *p, va_list ap)
{
  rstr_t *rstr;
  const char *str;
  int ev = va_arg(ap, prop_event_t);

  switch(ev) {
  case PROP_SET_STRING:
    str = va_arg(ap, const char *);
    if(str == NULL)
      prop_set_void_exl(p, skipme);
    else
      prop_set_string_exl(p, skipme, str, PROP_STR_UTF8);
    break;
  case PROP_SET_RSTRING:
  case PROP_ADOPT_RSTRING:
    rstr = va_arg(ap, rstr_t *);
    if(rstr == NULL)
      prop_set_void_exl(p, skipme);
    else {
      prop_set_rstring_exl(p, skipme, rstr, 0);
      if(ev == PROP_ADOPT_RSTRING)
        rstr_release(rstr);
    }
    break;
  case PROP_SET_INT:
    prop_set_int_exl(p, skipme, va_arg(ap, int));
    break;
  case PROP_SET_FLOAT:
    prop_set_float_exl(p, skipme, va_arg(ap, double));
    break;
  case PROP_SET_VOID:
    prop_set_void_exl(p, skipme);
    break;
  case PROP_SET_PROP:
    prop_set_prop_exl(p, skipme, va_arg(ap, prop_t *));
    break;
  case PROP_SET_LINK:
    prop_link_exl(va_arg(ap, prop_t *), p, skipme, 0, 0);
    break;
 default:
   fprintf(stderr, "Unable to handle event: %d\n", ev);
   assert(0);
   break;
  }
}


/**
 *
 */
void
prop_setv_ex(prop_sub_t *skipme, prop_t *p, ...)
{
  va_list ap;
  prop_t *c = p;
  const char *n;

  if(p == NULL)
    return;

  va_start(ap, p);

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE)
    goto bad;

  while((n = va_arg(ap, const char *)) != NULL) {
    if(p->hp_type == PROP_ZOMBIE)
      goto bad;
    if(p->hp_type == PROP_DIR) {
      TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link)
	if(c->hp_name != NULL && !strcmp(c->hp_name, n))
	  break;
    } else 
      c = NULL;
    if(c == NULL)
      c = prop_create0(p, n, skipme, 0);
    p = c;
  }

  prop_seti(skipme, p, ap);

 bad:
  hts_mutex_unlock(&prop_mutex);
  va_end(ap);
}


/**
 *
 */
void
prop_setdn(prop_sub_t *skipme, prop_t *p, const char *str, ...)
{
  va_list ap;
  prop_t *c = p;

  if(p == NULL)
    return;

  va_start(ap, str);

  hts_mutex_lock(&prop_mutex);

  while(1) {
    if(p->hp_type == PROP_ZOMBIE)
      goto bad;

    if(str == NULL || !*str)
      break;

    const char *s2 = strchr(str, '.');
    if(s2 != NULL) {
      int l = s2 - str;
      s2++;
      char *s3 = alloca(l+1);
      s3[l] = 0;
      str = memcpy(s3, str, l);
    }


    if(p->hp_type == PROP_DIR) {
      TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link)
	if(c->hp_name != NULL && !strcmp(c->hp_name, str))
	  break;
    } else 
      c = NULL;
    if(c == NULL)
      c = prop_create0(p, str, skipme, 0);
    p = c;
    str = s2;
  }

  prop_seti(skipme, p, ap);

 bad:
  hts_mutex_unlock(&prop_mutex);
  va_end(ap);
}



/**
 *
 */
void
prop_set_ex(prop_t *p, const char *name, int noalloc, ...)
{
  va_list ap;

  if(p == NULL)
    return;

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type != PROP_ZOMBIE) {
    p = prop_create0(p, name, NULL, noalloc);
    va_start(ap, noalloc);
    prop_seti(NULL, p, ap);
    va_end(ap);
  }
  hts_mutex_unlock(&prop_mutex);
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
  if(s->hps_proxy) {
    prop_proxy_want_more_childs(s);
  } else {
    prop_send_event(s->hps_value_prop, PROP_WANT_MORE_CHILDS);
  }
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
prop_have_more_childs0(prop_t *p, int yes)
{
  prop_send_event(p,
                  yes ? PROP_HAVE_MORE_CHILDS_YES : PROP_HAVE_MORE_CHILDS_NO);
}


/**
 *
 */
void
prop_have_more_childs(prop_t *p, int yes)
{
  hts_mutex_lock(&prop_mutex);
  prop_have_more_childs0(p, yes);
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_mark_childs(prop_t *p)
{
  prop_t *c;
  hts_mutex_lock(&prop_mutex);
  if(p->hp_type == PROP_DIR) {
    TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link)
      c->hp_flags |= PROP_MARKED;
  }
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
void
prop_unmark(prop_t *p)
{
  if(p == NULL)
    return;
  hts_mutex_lock(&prop_mutex);
  p->hp_flags &= ~PROP_MARKED;
  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
int
prop_is_marked(prop_t *p)
{
  return p->hp_flags & PROP_MARKED ? 1 : 0;
}


/**
 *
 */
void
prop_destroy_marked_childs(prop_t *p)
{
  hts_mutex_lock(&prop_mutex);
  if(p->hp_type == PROP_DIR) {
    prop_t *c, *next;
    for(c = TAILQ_FIRST(&p->hp_childs); c != NULL; c = next) {
      next = TAILQ_NEXT(c, hp_parent_link);
      if(c->hp_flags & PROP_MARKED)
        prop_destroy0(c);
    }
  }
  hts_mutex_unlock(&prop_mutex);
}


#ifdef PROP_SUB_RECORD_SOURCE

static void
printorigins(prop_sub_t *s)
{
  if(!s->hps_origin)
    return;
  fprintf(stderr, "{");
  if(!s->hps_multiple_origins) {
    fprintf(stderr, "%p", s->hps_origin);
  } else {
    prop_originator_tracking_t *pot;

    for(pot = s->hps_pots; pot != NULL; pot = pot->pot_next)
      fprintf(stderr, "%p%s", pot->pot_p, pot->pot_next ? " " : "");
  }
  fprintf(stderr, "} ");
}
#endif


/**
 *
 */
void
prop_print_tree0(prop_t *p, int indent, int flags)
{
  prop_t *c;

  fprintf(stderr, "%*.s%s[%p %d %d %c%c]: ", indent, "", 
	  p->hp_name, p, atomic_get(&p->hp_refcount), p->hp_xref,
	  p->hp_flags & PROP_MULTI_SUB ? 'M' : ' ',
	  p->hp_flags & PROP_MULTI_NOTIFY ? 'N' : ' ');

  int targets = 0;
  LIST_FOREACH(c, &p->hp_targets, hp_originator_link)
    targets++;
  if(targets)
    fprintf(stderr, "(%d targets) ", targets);


  if(p->hp_originator != NULL) {
    if(flags & 1) {
      fprintf(stderr, "<symlink> => ");
      prop_print_tree0(p->hp_originator, indent, flags);
      return;
    }
    fprintf(stderr, "<symlink> -> %s (Not followed) -> ",
            p->hp_originator->hp_name);
  }

  switch(p->hp_type) {
  case PROP_RSTRING:
    if(p->hp_rstrtype == PROP_STR_RICH)
      fprintf(stderr, "'%s'\n", rstr_get(p->hp_rstring));
    else
      fprintf(stderr, "\"%s\"\n", rstr_get(p->hp_rstring));
    break;

  case PROP_CSTRING:
    fprintf(stderr, "\"%s\"\n", p->hp_cstring);
    break;

  case PROP_PROP:
    fprintf(stderr, "\{%s}\n", p->hp_prop->hp_name ?: "NONAME");
    break;

  case PROP_URI:
    fprintf(stderr, "\"%s\" <%s>\n", rstr_get(p->hpn_uri_title),
	    rstr_get(p->hpn_uri));
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
      prop_print_tree0(c, indent + 4, flags);
    break;

  case PROP_VOID:
    fprintf(stderr, "<void>\n");
    break;

  case PROP_PROXY:
    fprintf(stderr, "<proxy>\n");
    break;

  case PROP_ZOMBIE:
    fprintf(stderr, "<zombie, ref=%d>\n", atomic_get(&p->hp_refcount));
    break;
  }

  if(flags & 2) {
    prop_sub_t *s;
    LIST_FOREACH(s, &p->hp_value_subscriptions, hps_value_prop_link) {
      fprintf(stderr, "%*.s \033[1mV-Subscriber: ", indent, "");
#ifdef PROP_SUB_RECORD_SOURCE
      fprintf(stderr, "%s:%d ", s->hps_file, s->hps_line);
      printorigins(s);
#endif
      fprintf(stderr, "[%s] @ %p p=%p value=%s\033[0m\n",
              prop_get_DN(s->hps_canonical_prop, 1), s, s->hps_canonical_prop,
              prop_get_DN(s->hps_value_prop, 1));
    }
  }
  if(flags & 4) {
    prop_sub_t *s;
    LIST_FOREACH(s, &p->hp_canonical_subscriptions, hps_canonical_prop_link) {
      fprintf(stderr, "%*.s \033[1mC-Subscriber: ", indent, "");
#ifdef PROP_SUB_RECORD_SOURCE
      fprintf(stderr, "%s:%d ", s->hps_file, s->hps_line);
#endif
      fprintf(stderr, "[%s] @ %p p=%p value=%s\033[0m\n",
              prop_get_DN(s->hps_canonical_prop, 1), s, s->hps_canonical_prop,
              prop_get_DN(s->hps_value_prop, 1));
    }
  }
}

/**
 *
 */
void
prop_print_tree(prop_t *p, int followlinks)
{
  hts_mutex_lock(&prop_mutex);
  fprintf(stderr, "Print tree form %s\n",
          prop_get_DN(p, 1));
  prop_print_tree0(p, 0, followlinks);
  hts_mutex_unlock(&prop_mutex);
}



#ifdef PROP_SUB_STATS
#include "misc/callout.h"

static callout_t prop_stats_callout;

static void
prop_report_stats(callout_t *c, void *aux)
{
  prop_sub_t *s;
  int num_subs = 0;
  int origin_link_hist[4] = {};

  hts_mutex_lock(&prop_mutex);
  LIST_FOREACH(s, &all_subs, hps_all_sub_link) {
    num_subs++;

    prop_originator_tracking_t *pot;
    int num_pot = 0;

    if(s->hps_origin != NULL) {
      if(!s->hps_multiple_origins) {
	num_pot = 1;
      } else {
	for(pot = s->hps_pots; pot != NULL; pot = pot->pot_next)
	  num_pot++;
      }
    }
    if(num_pot > 3)
      num_pot = 3;
    origin_link_hist[num_pot]++;
  }


  hts_mutex_unlock(&prop_mutex);
  printf("%d subs: %d %d %d %d\n",
	 num_subs, 
	 origin_link_hist[0],
	 origin_link_hist[1],
	 origin_link_hist[2],
	 origin_link_hist[3]);
  callout_arm(&prop_stats_callout, prop_report_stats, NULL, 1);

}

#endif

extern void prop_test(void);

void
prop_init_late(void)
{
#ifdef PROP_SUB_STATS
  callout_arm(&prop_stats_callout, prop_report_stats, NULL, 1);
#endif

#if 0
  prop_test();
  exit(0);
#endif
}


#ifdef PROP_DEBUG
void
prop_track_sub(prop_sub_t *s)
{
  track_sub = s;
}

#endif
