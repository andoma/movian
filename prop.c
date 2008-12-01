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

#include <libhts/htsthreads.h>
#include <libhts/htsatomic.h>

#include "prop.h"

static hts_mutex_t prop_mutex;
static prop_t *prop_global;

static prop_courier_t *global_courier;

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
    float f;
    int i;
    char *s;
  } u;

#define hpn_prop   u.p
#define hpn_float  u.f
#define hpn_int    u.i
#define hpn_string u.s

} prop_notify_t;


/**
 *
 */
void
prop_ref_dec(prop_t *p)
{
  if(atomic_add(&p->hp_refcount, -1) > 1)
    return;

  free(p->hp_name);
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
static void
prop_sub_ref_dec(prop_sub_t *s)
{
  if(atomic_add(&s->hps_refcount, -1) > 1)
    return;

  free(s);
}



/**
 * Thread for dispatching prop_notify entries
 */
static void *
prop_courier(void *aux)
{
  prop_courier_t *pc = aux;

  prop_notify_t *n;
  prop_sub_t *s;

  hts_mutex_lock(&prop_mutex);

  while(pc->pc_run) {

    if((n = TAILQ_FIRST(&pc->pc_queue)) == NULL) {
      hts_cond_wait(&pc->pc_cond, &prop_mutex);
      continue;
    }

    TAILQ_REMOVE(&pc->pc_queue, n, hpn_link);
    
    hts_mutex_unlock(&prop_mutex);

    s = n->hpn_sub;

    if(pc->pc_entry_mutex != NULL)
      hts_mutex_lock(pc->pc_entry_mutex);
    
    switch(n->hpn_event) {
    case PROP_SET_VOID:
    case PROP_SET_DIR:
      s->hps_callback(s, n->hpn_event);
      break;

    case PROP_SET_STRING:
      s->hps_callback(s, n->hpn_event, n->hpn_string);
      free(n->hpn_string);
      break;

    case PROP_SET_INT:
      s->hps_callback(s, n->hpn_event, n->hpn_int);
      break;

    case PROP_SET_FLOAT:
      s->hps_callback(s, n->hpn_event, n->hpn_float);
      break;

    case PROP_ADD_CHILD:
    case PROP_ADD_SELECTED_CHILD:
    case PROP_DEL_CHILD:
    case PROP_SEL_CHILD:
      s->hps_callback(s, n->hpn_event, n->hpn_prop);
      if(n->hpn_prop != NULL)
	prop_ref_dec(n->hpn_prop);
      break;
    }

    if(pc->pc_entry_mutex != NULL)
      hts_mutex_unlock(pc->pc_entry_mutex);
 
    prop_sub_ref_dec(s);
    free(n);
  }
  return NULL;
}

/**
 *
 */
static void
courier_enqueue(prop_courier_t *pc, prop_notify_t *n)
{
  TAILQ_INSERT_TAIL(&pc->pc_queue, n, hpn_link);
  hts_cond_signal(&pc->pc_cond);
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
prop_build_notify_value(prop_sub_t *s)
{
  prop_notify_t *n = get_notify(s);
  prop_t *p = s->hps_value_prop;

  switch(p->hp_type) {
  case PROP_STRING:
    n->hpn_string = strdup(p->hp_string);
    n->hpn_event = PROP_SET_STRING;
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
  }
  courier_enqueue(s->hps_courier, n);
}



/**
 *
 */
static void
prop_notify_void(prop_sub_t *s)
{
  prop_notify_t *n = get_notify(s);

  n->hpn_event = PROP_SET_VOID;
  courier_enqueue(s->hps_courier, n);
}


/**
 *
 */
static void
prop_notify_value(prop_t *p, prop_sub_t *skipme)
{
  prop_sub_t *s;

  LIST_FOREACH(s, &p->hp_value_subscriptions, hps_value_prop_link)
    if(s != skipme)
      prop_build_notify_value(s);
}


/**
 *
 */
static void
prop_build_notify_child(prop_sub_t *s, prop_t *p, prop_event_t event)
{
  prop_notify_t *n = get_notify(s);

  if(p != NULL)
    atomic_add(&p->hp_refcount, 1);

  n->hpn_prop = p;
  n->hpn_event = event;
  courier_enqueue(s->hps_courier, n);
}


/**
 *
 */
static void
prop_notify_child(prop_t *child, prop_t *parent, prop_event_t event,
		  prop_sub_t *skipme)
{
  prop_sub_t *s;

  LIST_FOREACH(s, &parent->hp_value_subscriptions, hps_value_prop_link)
    if(s != skipme)
      prop_build_notify_child(s, child, event);
}


/**
 *
 */
static void
prop_clean(prop_t *p)
{
  switch(p->hp_type) {
  case PROP_VOID:
  case PROP_INT:
  case PROP_FLOAT:
    break;

  case PROP_STRING:
    free(p->hp_string);
    break;

  case PROP_DIR:
    abort();
  }
}


/**
 *
 */
static void
prop_make_dir(prop_t *p)
{
  if(p->hp_type == PROP_DIR)
    return;

  prop_clean(p);
  
  TAILQ_INIT(&p->hp_childs);
  p->hp_selected = NULL;
  p->hp_type = PROP_DIR;
  
  prop_notify_value(p, NULL);
}




/**
 *
 */
static prop_t *
prop_create0(prop_t *parent, const char *name, prop_sub_t *skipme)
{
  prop_t *hp;

  if(parent != NULL) {

    prop_make_dir(parent);

    if(name != NULL) {
      TAILQ_FOREACH(hp, &parent->hp_childs, hp_parent_link) {
	if(hp->hp_name != NULL && !strcmp(hp->hp_name, name)) {
	  return hp;
	}
      }
    }
  }

  hp = malloc(sizeof(prop_t));
  hp->hp_originator = NULL;
  hp->hp_refcount = 1;
  hp->hp_type = PROP_VOID;
  hp->hp_name = name ? strdup(name) : NULL;

  LIST_INIT(&hp->hp_targets);
  LIST_INIT(&hp->hp_value_subscriptions);
  LIST_INIT(&hp->hp_canonical_subscriptions);

  hp->hp_parent = parent;

  if(parent != NULL) {
    TAILQ_INSERT_TAIL(&parent->hp_childs, hp, hp_parent_link);
    prop_notify_child(hp, parent, PROP_ADD_CHILD, skipme);
  }

  return hp;
}



/**
 *
 */
prop_t *
prop_create_ex(prop_t *parent, const char *name, prop_sub_t *skipme)
{
  prop_t *p;

  hts_mutex_lock(&prop_mutex);
  p = prop_create0(parent, name, skipme);
  hts_mutex_unlock(&prop_mutex);

  return p;
}


/**
 *
 */
void
prop_set_parent_ex(prop_t *p, prop_t *parent, prop_sub_t *skipme)
{
  assert(p->hp_parent == NULL);

  hts_mutex_lock(&prop_mutex);

  prop_make_dir(parent);

  p->hp_parent = parent;
  TAILQ_INSERT_TAIL(&parent->hp_childs, p, hp_parent_link);

  prop_notify_child(p, parent, PROP_ADD_CHILD, skipme);

  hts_mutex_unlock(&prop_mutex);
}


/**
 *
 */
static void
prop_destroy0(prop_t *p)
{
  prop_t *c, *parent;
  prop_sub_t *s;

  switch(p->hp_type) {
  case PROP_DIR:
    while((c = TAILQ_FIRST(&p->hp_childs)) != NULL)
      prop_destroy0(c);
    break;

  case PROP_STRING:
    free(p->hp_string);
    break;

  case PROP_FLOAT:
  case PROP_INT:
  case PROP_VOID:
    break;
  }


  while((s = LIST_FIRST(&p->hp_canonical_subscriptions)) != NULL) {
    LIST_REMOVE(s, hps_canonical_prop_link);
    s->hps_canonical_prop = NULL;
  }

  while((s = LIST_FIRST(&p->hp_value_subscriptions)) != NULL) {
    prop_notify_void(s);

    LIST_REMOVE(s, hps_value_prop_link);
    s->hps_value_prop = NULL;
  }

  if(p->hp_originator != NULL) {
    p->hp_originator = NULL;
    LIST_REMOVE(p, hp_originator_link);
  }

  if(p->hp_parent != NULL) {
    prop_notify_child(p, p->hp_parent, PROP_DEL_CHILD, NULL);
    parent = p->hp_parent;

    TAILQ_REMOVE(&parent->hp_childs, p, hp_parent_link);
    p->hp_parent = NULL;

    if(parent->hp_selected == p) {

      /* It's pointless for us to try to choose a different child
	 to be selected. Why? .. All subscribers sort the props
	 in their own order. Instead, notify them that nothing
	 is selected. This should trig them to reselect somthing. 
	 This will echo back to us as an advisory prop_select()
      */
      parent->hp_selected = NULL;
      prop_notify_child(NULL, parent, PROP_SEL_CHILD, NULL);
    }

  }

  prop_ref_dec(p);
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

      prop_notify_value(p, NULL);
    }

    TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link) {
      if(c->hp_name != NULL && !strcmp(c->hp_name, name[0]))
	break;
    }
    p = c ?: prop_create0(p, name[0], NULL);
    name++;
  }

  while(follow_symlinks && p->hp_originator != NULL)
    p = p->hp_originator;

  return p;
}


/**
 *
 */
prop_sub_t *
prop_subscribe(struct prop *prop, const char **name,
	       prop_callback_t *cb, void *opaque, prop_courier_t *pc)
{
  prop_t *p, *value, *canonical, *c;
  prop_sub_t *s;

  if(name == NULL) {
    /* No name given, just subscribe to the supplied prop */
    canonical = value = prop;
    hts_mutex_lock(&prop_mutex);

  } else {

    if(!strcmp(name[0], "global"))
      p = prop_global;
    else if(!strcmp(name[0], "self"))
      p = prop;
    else if(prop->hp_name != NULL && !strcmp(name[0], prop->hp_name))
      p = prop;
    else {
      return NULL;
    }

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

  s->hps_courier = pc ?: global_courier;

  LIST_INSERT_HEAD(&canonical->hp_canonical_subscriptions, s, 
		   hps_canonical_prop_link);
  s->hps_canonical_prop = canonical;


  LIST_INSERT_HEAD(&value->hp_value_subscriptions, s, 
		   hps_value_prop_link);
  s->hps_value_prop = value;

  s->hps_callback = cb;
  s->hps_opaque = opaque;
  s->hps_refcount = 1;

  prop_build_notify_value(s);

  if(value->hp_type == PROP_DIR) {
    TAILQ_FOREACH(c, &value->hp_childs, hp_parent_link)
      prop_build_notify_child(s, c, 
			      value->hp_selected == c ? 
			      PROP_ADD_SELECTED_CHILD : PROP_ADD_CHILD);
  }

  hts_mutex_unlock(&prop_mutex);
  return s;
}


/**
 *
 */
void
prop_unsubscribe(prop_sub_t *s)
{
  hts_mutex_lock(&prop_mutex);

  if(s->hps_value_prop != NULL) {
    LIST_REMOVE(s, hps_value_prop_link);
    s->hps_value_prop = NULL;
  }

  if(s->hps_canonical_prop != NULL) {
    LIST_REMOVE(s, hps_canonical_prop_link);
    s->hps_canonical_prop = NULL;
  }

  hts_mutex_unlock(&prop_mutex);

  prop_sub_ref_dec(s);
}



/**
 *
 */
void
prop_init(void)
{
  hts_mutex_init(&prop_mutex);
  prop_global = prop_create0(NULL, "global", NULL);

  global_courier = prop_courier_create(NULL);
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
prop_set_epilogue(prop_sub_t *skipme, prop_t *p)
{
  prop_notify_value(p, skipme);

  hts_mutex_unlock(&prop_mutex);
}



/**
 *
 */
void
prop_set_string_ex(prop_t *p, prop_sub_t *skipme, const char *str)
{
  hts_mutex_lock(&prop_mutex);

  if(p->hp_type != PROP_STRING) {

    prop_clean(p);

  } else if(!strcmp(p->hp_string, str)) {
    hts_mutex_unlock(&prop_mutex);
    return;
  } else {
    free(p->hp_string);
  }

  p->hp_string = strdup(str);
  p->hp_type = PROP_STRING;

  prop_set_epilogue(skipme, p);
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
void
prop_set_float_ex(prop_t *p, prop_sub_t *skipme, float v)
{
  hts_mutex_lock(&prop_mutex);

  if(p->hp_type != PROP_FLOAT) {

    prop_clean(p);

  } else if(p->hp_float == v) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  p->hp_float = v;
  p->hp_type = PROP_FLOAT;

  prop_set_epilogue(skipme, p);
}

/**
 *
 */
void
prop_set_int_ex(prop_t *p, prop_sub_t *skipme, int v)
{
  hts_mutex_lock(&prop_mutex);

  if(p->hp_type != PROP_INT) {

    prop_clean(p);

  } else if(p->hp_int == v) {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  p->hp_int = v;
  p->hp_type = PROP_INT;

  prop_set_epilogue(skipme, p);
}


/**
 *
 */
void
prop_set_void_ex(prop_t *p, prop_sub_t *skipme)
{
  hts_mutex_lock(&prop_mutex);

  if(p->hp_type != PROP_VOID) {

    prop_clean(p);

  } else {
    hts_mutex_unlock(&prop_mutex);
    return;
  }

  p->hp_type = PROP_VOID;
  prop_set_epilogue(skipme, p);
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
relink_subscriptions(prop_t *src, prop_t *dst)
{
  prop_sub_t *s;
  prop_t *c, *z;

  /* Follow any symlinks should we bump into 'em */
  while(src->hp_originator != NULL)
    src = src->hp_originator;

  LIST_FOREACH(s, &dst->hp_canonical_subscriptions, hps_canonical_prop_link) {

    if(s->hps_value_prop != NULL) {
      /* If we previously was a directory, flush it out */
      if(s->hps_value_prop->hp_type == PROP_DIR) {
	prop_notify_void(s);
      }
      LIST_REMOVE(s, hps_value_prop_link);
    }

    LIST_INSERT_HEAD(&src->hp_value_subscriptions, s, hps_value_prop_link);
    s->hps_value_prop = src;

    /* Update with new value */
    prop_build_notify_value(s);

    if(src->hp_type == PROP_DIR) {
      TAILQ_FOREACH(c, &src->hp_childs, hp_parent_link)
	prop_build_notify_child(s, c,
				src->hp_selected == c ? 
				PROP_ADD_SELECTED_CHILD : PROP_ADD_CHILD);
    }
  }

  if(dst->hp_type == PROP_DIR) {
    
    /* Take care of all childs */

    TAILQ_FOREACH(c, &dst->hp_childs, hp_parent_link) {
      
      if(c->hp_name == NULL)
	continue;

      /* Search for a matching source */
      TAILQ_FOREACH(z, &src->hp_childs, hp_parent_link) {
	if(z->hp_name != NULL && !strcmp(c->hp_name, z->hp_name))
	  break;
      }
      
      if(z != NULL) {
	/* Found! Recurse */

	relink_subscriptions(z, c);

      } else {
	/* Nothing, blast the value */

	LIST_FOREACH(s, &c->hp_canonical_subscriptions,
		     hps_canonical_prop_link) {

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
prop_unlink0(prop_t *p)
{
  LIST_REMOVE(p, hp_originator_link);
  p->hp_originator = NULL;

  relink_subscriptions(p, p);
}


/**
 *
 */
void
prop_link(prop_t *src, prop_t *dst)
{
  prop_t *t;

  hts_mutex_lock(&prop_mutex);

  if(dst->hp_originator != NULL)
    prop_unlink0(dst);

  dst->hp_originator = src;
  LIST_INSERT_HEAD(&src->hp_targets, dst, hp_originator_link);

  /* Follow any aditional symlinks source may point at */
  while(src->hp_originator != NULL)
    src = src->hp_originator;

  relink_subscriptions(src, dst);

  while((dst = dst->hp_parent) != NULL) {
    LIST_FOREACH(t, &dst->hp_targets, hp_originator_link)
      relink_subscriptions(dst, t);
  }

  hts_mutex_unlock(&prop_mutex);
}




/**
 *
 */
void
prop_unlink(prop_t *p)
{
  prop_t *t;

  hts_mutex_lock(&prop_mutex);

  if(p->hp_originator != NULL)
    prop_unlink0(p);

  while((p = p->hp_parent) != NULL) {
    LIST_FOREACH(t, &p->hp_targets, hp_originator_link)
      relink_subscriptions(p, t);
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

  parent = p->hp_parent;

  if(parent != NULL) {
    assert(parent->hp_type == PROP_DIR);

    /* If in advisory mode and something is already selected,
       don't do anything */
    if(!advisory || parent->hp_selected == NULL) {
      prop_notify_child(p, parent, PROP_SEL_CHILD, skipme);
      parent->hp_selected = p;
    }
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
 *
 */
void
prop_ancestors_unref(prop_t **r)
{
  prop_t **a = r;

  while(*a != NULL) {
    prop_ref_dec(*a);
    a++;
  }
  free(r);
}


/**
 *
 */
prop_t *
prop_get_by_subscription(prop_sub_t *s)
{
  prop_t *p;

  hts_mutex_lock(&prop_mutex);

  p = s->hps_value_prop;
  if(p != NULL)
    prop_ref_inc(p);

  hts_mutex_unlock(&prop_mutex);

  return p;
}


/**
 *
 */
prop_courier_t *
prop_courier_create(hts_mutex_t *entrymutex)
{
  prop_courier_t *pc = calloc(1, sizeof(prop_courier_t));

  hts_cond_init(&pc->pc_cond);

  TAILQ_INIT(&pc->pc_queue);
  pc->pc_run = 1;

  hts_thread_create(&pc->pc_thread, prop_courier, pc);
  return pc;
}
