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
#include <assert.h>

#include "prop/prop_i.h"
#include "prop/prop_nodefilter.h"
#include "ecmascript.h"
#include "backend/backend_prop.h"


/**
 *
 */
typedef struct es_prop_sub {
  es_resource_t eps_super;
  prop_sub_t *eps_sub;
  char eps_autodestry;
  char eps_action_as_array;
} es_prop_sub_t;

static void
es_prop_ref_dec(prop_t *p)
{
  prop_ref_dec(p);
}

ES_NATIVE_CLASS(prop, es_prop_ref_dec);
ES_NATIVE_CLASS(propnf, prop_nf_release);

/**
 *
 */
static void
es_prop_sub_destroy(es_resource_t *eres)
{
  es_prop_sub_t *eps = (es_prop_sub_t *)eres;
  if(eps->eps_sub == NULL)
    return;

  es_root_unregister(eres->er_ctx->ec_duk, eps);
  prop_unsubscribe(eps->eps_sub);
  eps->eps_sub = NULL;
  es_resource_unlink(eres);
}


/**
 *
 */
static const es_resource_class_t es_resource_prop_sub = {
  .erc_name = "propsub",
  .erc_size = sizeof(es_prop_sub_t),
  .erc_destroy = es_prop_sub_destroy,
};


/**
 *
 */
prop_t *
es_stprop_get(duk_context *ctx, int val_index)
{
  return es_get_native_obj(ctx, val_index, &es_native_prop);
}


/**
 *
 */
void
es_stprop_push(duk_context *ctx, prop_t *p)
{
  es_push_native_obj(ctx, &es_native_prop, prop_ref_inc(p));
}

/**
 *
 */
static int
es_prop_release_duk(duk_context *ctx)
{
  prop_t *p = duk_require_pointer(ctx, 0);

  hts_mutex_lock(&prop_mutex);

  if(p->hp_parent == NULL)
    prop_destroy0(p);

  prop_ref_dec_locked(p);

  hts_mutex_unlock(&prop_mutex);
  return 0;
}


/**
 *
 */
static int
es_prop_print_duk(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  prop_print_tree(p, 1);
  return 0;
}


/**
 *
 */
static int
es_prop_create_duk(duk_context *ctx)
{
  const char *str = duk_get_string(ctx, 0);
  es_stprop_push(ctx, prop_create_root(str));
  return 1;
}


/**
 *
 */
static int
es_prop_get_global(duk_context *ctx)
{
  es_stprop_push(ctx, prop_get_global());
  return 1;
}


/**
 *
 */
static int
es_prop_get_name_duk(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);

  rstr_t *r = prop_get_name(p);
  duk_push_string(ctx, rstr_get(r));
  rstr_release(r);
  return 1;
}


/**
 *
 */
static int
es_prop_get_value_duk(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  char tmp[64];
  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    duk_error(ctx, ST_ERROR_PROP_ZOMBIE, NULL);
  }

  switch(p->hp_type) {
  case PROP_CSTRING:
    {
      const char *s = p->hp_cstring;
      hts_mutex_unlock(&prop_mutex);
      duk_push_string(ctx, s);
    }
    break;

  case PROP_RSTRING:
    {
      rstr_t *r = rstr_dup(p->hp_rstring);
      hts_mutex_unlock(&prop_mutex);
      duk_push_string(ctx, rstr_get(r));
      rstr_release(r);
    }
    break;
  case PROP_URI:
    {
      rstr_t *r = rstr_dup(p->hp_uri_title);
      hts_mutex_unlock(&prop_mutex);
      duk_push_string(ctx, rstr_get(r));
      rstr_release(r);
    }
    break;
  case PROP_FLOAT:
    {
      const float v = p->hp_float;
      hts_mutex_unlock(&prop_mutex);
      duk_push_number(ctx, v);
    }
    break;
  case PROP_INT:
    {
      const int v = p->hp_int;
      hts_mutex_unlock(&prop_mutex);
      duk_push_int(ctx, v);
    }
    break;
  case PROP_VOID:
    hts_mutex_unlock(&prop_mutex);
    duk_push_null(ctx);
    break;
  case PROP_DIR:
    snprintf(tmp, sizeof(tmp), "[prop directory '%s']", p->hp_name);
    hts_mutex_unlock(&prop_mutex);
    duk_push_string(ctx, tmp);
    break;
  default:
    snprintf(tmp, sizeof(tmp), "[prop internal type %d]", p->hp_type);
    hts_mutex_unlock(&prop_mutex);
    duk_push_string(ctx, tmp);
    break;
  }
  return 1;
}


/**
 *
 */
static int
es_prop_get_child_duk(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  const char *str = NULL;
  int idx = 0;
  if(duk_is_number(ctx, 1)) {
    idx = duk_to_int(ctx, 1);
  } else {
    str = duk_require_string(ctx, 1);
  }

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    duk_error(ctx, ST_ERROR_PROP_ZOMBIE, NULL);
  }

  if(str != NULL) {
    p = prop_create0(p, str, NULL, 0);
  } else {
    prop_t *c;
    TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link) {
      if(idx == 0)
        break;
      idx--;
    }

    p = c;
  }

  if(p != NULL) {
    p = prop_ref_inc(p);
    hts_mutex_unlock(&prop_mutex);
    es_push_native_obj(ctx, &es_native_prop, p);
    return 1;
  }
  hts_mutex_unlock(&prop_mutex);
  return 0;
}


/**
 *
 */
static int
es_prop_enum_duk(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);

  duk_push_array(ctx);

  hts_mutex_lock(&prop_mutex);


  if(p->hp_type != PROP_DIR) {
    hts_mutex_unlock(&prop_mutex);
    return 1;
  }

  prop_t *c;
  int i;
  int cnt = 0;

  TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link)
    cnt++;
  char **names = malloc(sizeof(char *) * cnt);

  i = 0;
  TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link)
    names[i++] = c->hp_name ? strdup(c->hp_name) : NULL;

  hts_mutex_unlock(&prop_mutex);

  for(int i = 0; i < cnt; i++) {
    if(names[i])
      duk_push_string(ctx, names[i]);
    else
      duk_push_int(ctx, i);
    free(names[i]);
    duk_put_prop_index(ctx, -2, i);
  }
  free(names);
  return 1;
}


/**
 *
 */
static int
es_prop_has_duk(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  const char *name = duk_get_string(ctx, 1);
  int yes = 0;

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_DIR) {
    prop_t *c;
    TAILQ_FOREACH(c, &p->hp_childs, hp_parent_link) {
      if(c->hp_name != NULL && !strcmp(c->hp_name, name)) {
        yes = 1;
        break;
      }
    }
  }
  hts_mutex_unlock(&prop_mutex);
  duk_push_boolean(ctx, yes);
  return 1;
}


/**
 *
 */
static int
es_prop_delete_child_duk(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  const char *name = duk_require_string(ctx, 1);
  prop_destroy_by_name(p, name);
  duk_push_boolean(ctx, 1);
  return 1;
}


/**
 *
 */
static int
es_prop_delete_childs_duk(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  prop_destroy_childs(p);
  return 0;
}


/**
 *
 */
static int
es_prop_destroy_duk(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  prop_destroy(p);
  return 0;
}


//#define SETPRINTF(fmt...) printf(fmt);
#define SETPRINTF(fmt, ...)

/**
 *
 */
static int
es_prop_set_value_duk(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  const char *str = duk_require_string(ctx, 1);

  SETPRINTF("Set %s.%s to ", p->hp_name, str);

  if(duk_is_boolean(ctx, 2)) {
    SETPRINTF("%s", duk_get_boolean(ctx, 2) ? "true" : "false");
    prop_set(p, str, PROP_SET_INT, duk_get_boolean(ctx, 2));
  } else if(duk_is_number(ctx, 2)) {
    double dbl = duk_get_number(ctx, 2);

    if(ceil(dbl) == dbl && dbl <= INT32_MAX && dbl >= INT32_MIN) {
      SETPRINTF("%d", (int)dbl);
      prop_set(p, str, PROP_SET_INT, (int)dbl);
    } else {
      SETPRINTF("%f", dbl);
      prop_set(p, str, PROP_SET_FLOAT, dbl);
    }
  } else if(duk_is_string(ctx, 2)) {
    SETPRINTF("\"%s\"", duk_get_string(ctx, 2));
    prop_set(p, str, PROP_SET_STRING, duk_get_string(ctx, 2));
  } else {
    SETPRINTF("(void)");
    prop_set(p, str, PROP_SET_VOID);
  }
  SETPRINTF("\n");
  return 0;
}


/**
 *
 */
static int
es_prop_set_rich_str_duk(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  const char *key = duk_require_string(ctx, 1);
  const char *richstr = duk_require_string(ctx, 2);

  prop_t *c = prop_create_r(p, key);
  prop_set_string_ex(c, NULL, richstr, PROP_STR_RICH);
  prop_ref_dec(c);
  return 0;
}


/**
 *
 */
static int
es_prop_set_parent_duk(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  prop_t *parent = es_stprop_get(ctx, 1);

  if(prop_set_parent(p, parent))
    prop_destroy(p);

  return 0;
}


/**
 *
 */
static void
es_sub_cb(void *opaque, prop_event_t event, ...)
{
  es_prop_sub_t *eps = opaque;
  es_context_t *ec = eps->eps_super.er_ctx;
  va_list ap;
  const rstr_t *r, *r2;
  const char *c;
  double d;
  int i;
  int destroy = 0;
  const  event_t *e;
  prop_t *p1, *p2;
  prop_vec_t *pv;
  duk_context *ctx = ec->ec_duk;

  es_push_root(ctx, eps);

  va_start(ap, event);

  int nargs;

  switch(event) {
  case PROP_SET_DIR:
    duk_push_string(ctx, "dir");
    nargs = 1;
    break;

  case PROP_SET_VOID:
    duk_push_string(ctx, "set");
    duk_push_null(ctx);
    nargs = 2;
    break;

  case PROP_SET_RSTRING:
    r = va_arg(ap, const rstr_t *);
    duk_push_string(ctx, "set");
    duk_push_string(ctx, rstr_get(r));
    nargs = 2;
    break;

  case PROP_SET_CSTRING:
    c = va_arg(ap, const char *);
    duk_push_string(ctx, "set");
    duk_push_string(ctx, c);
    nargs = 2;
    break;

  case PROP_SET_URI:
    r = va_arg(ap, const rstr_t *);
    r2 = va_arg(ap, const rstr_t *);
    duk_push_string(ctx, "uri");
    duk_push_string(ctx, rstr_get(r));
    duk_push_string(ctx, rstr_get(r2));
    nargs = 3;
    break;

  case PROP_SET_INT:
    i = va_arg(ap, int);
    duk_push_string(ctx, "set");
    duk_push_int(ctx, i);
    nargs = 2;
    break;

  case PROP_SET_FLOAT:
    d = va_arg(ap, double);
    duk_push_string(ctx, "set");
    duk_push_number(ctx, d);
    nargs = 2;
    break;

  case PROP_WANT_MORE_CHILDS:
    duk_push_string(ctx, "wantmorechilds");
    nargs = 1;
    break;

  case PROP_DESTROYED:
    (void)va_arg(ap, prop_sub_t *);
    duk_push_string(ctx, "destroyed");
    nargs = 1;
    if(eps->eps_autodestry)
      destroy = 1;
    break;

  case PROP_REQ_MOVE_CHILD:
    duk_push_string(ctx, "reqmove");
    p1 = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    nargs = 3;
    es_stprop_push(ctx, p1);

    if(p2 != NULL) {
      es_stprop_push(ctx, p2);
    } else {
      duk_push_null(ctx);
    }
    break;

  case PROP_ADD_CHILD:
    duk_push_string(ctx, "addchild");
    p1 = va_arg(ap, prop_t *);
    nargs = 2;
    es_stprop_push(ctx, p1);
    break;

  case PROP_ADD_CHILD_BEFORE:
    duk_push_string(ctx, "addchildbefore");
    p1 = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    nargs = 3;
    es_stprop_push(ctx, p1);
    es_stprop_push(ctx, p2);
    break;

  case PROP_ADD_CHILD_VECTOR:
  case PROP_ADD_CHILD_VECTOR_DIRECT:
    duk_push_string(ctx, "addchilds");

    pv = va_arg(ap, prop_vec_t *);
    duk_push_array(ctx);

    for(int i = 0; i < prop_vec_len(pv); i++) {
      es_stprop_push(ctx, prop_vec_get(pv, i));
      duk_put_prop_index(ctx, -2, i);
    }
    nargs = 2;
    break;

  case PROP_ADD_CHILD_VECTOR_BEFORE:
    duk_push_string(ctx, "addchildsbefore");

    pv = va_arg(ap, prop_vec_t *);
    p2 = va_arg(ap, prop_t *);
    duk_push_array(ctx);
    for(int i = 0; i < prop_vec_len(pv); i++) {
      es_stprop_push(ctx, prop_vec_get(pv, i));
      duk_put_prop_index(ctx, -2, i);
    }
    es_stprop_push(ctx, p2);
    nargs = 3;
    break;

  case PROP_DEL_CHILD:
    duk_push_string(ctx, "delchild");
    p1 = va_arg(ap, prop_t *);
    es_stprop_push(ctx, p1);
    nargs = 2;
    break;

  case PROP_MOVE_CHILD:
    duk_push_string(ctx, "movechild");
    p1 = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    es_stprop_push(ctx, p1);
    es_stprop_push(ctx, p2);
    nargs = 3;
    break;


  case PROP_EXT_EVENT:
    e = va_arg(ap, const event_t *);

    if(event_is_type(e, EVENT_DYNAMIC_ACTION)) {
      const event_payload_t *ep = (const event_payload_t *)e;
      nargs = 2;
      duk_push_string(ctx, "action");

      if(eps->eps_action_as_array) {
        duk_push_array(ctx);
        duk_push_string(ctx, ep->payload);
        duk_put_prop_index(ctx, -2, 0);
      } else {
        duk_push_string(ctx, ep->payload);
      }

    } else if(e->e_type == EVENT_ACTION_VECTOR) {
      const event_action_vector_t *eav = (const event_action_vector_t *)e;
      assert(eav->num > 0);
      nargs = 2;
      duk_push_string(ctx, "action");

      if(eps->eps_action_as_array) {
        duk_push_array(ctx);
        for(int i = 0; i < eav->num; i++) {
          duk_push_string(ctx, action_code2str(eav->actions[i]));
          duk_put_prop_index(ctx, -2, i);
        }
      } else {
        duk_push_string(ctx, action_code2str(eav->actions[0]));
      }

    } else if(e->e_type == EVENT_UNICODE) {
      const event_int_t *eu = (const event_int_t *)e;
      nargs = 2;
      duk_push_string(ctx, "unicode");
      duk_push_int(ctx, eu->val);

    } else if(e->e_type == EVENT_PROPREF) {
      event_prop_t *ep = (event_prop_t *)e;
      nargs = 2;
      duk_push_string(ctx, "propref");
      es_stprop_push(ctx, ep->p);
    } else {
      nargs = 0;
    }

    if(nargs > 0 && e->e_nav != NULL) {
      es_stprop_push(ctx, e->e_nav);
      nargs++;
    }
    break;

  case PROP_SELECT_CHILD:
    duk_push_string(ctx, "selectchild");
    es_stprop_push(ctx, va_arg(ap, prop_t *));
    nargs = 2;
    break;

  default:
    nargs = 0;
    break;
  }

  va_end(ap);

  if(nargs > 0) {
    int rc = duk_pcall(ctx, nargs);
    if(rc)
      es_dump_err(ctx);
  }
  duk_pop(ctx);

  if(destroy)
    es_resource_destroy(&eps->eps_super);
}


/**
 *
 */
static int
es_prop_subscribe(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);
  prop_t *p = es_stprop_get(ctx, 0);
  es_prop_sub_t *eps = es_resource_create(ec, &es_resource_prop_sub, 1);

  es_root_register(ctx, 1, eps);

  eps->eps_autodestry = es_prop_is_true(ctx, 2, "autoDestroy");

  int flags = PROP_SUB_TRACK_DESTROY;

  if(es_prop_is_true(ctx, 2, "ignoreVoid"))
    flags |= PROP_SUB_IGNORE_VOID;

  if(es_prop_is_true(ctx, 2, "debug"))
    flags |= PROP_SUB_DEBUG;

  if(es_prop_is_true(ctx, 2, "noInitialUpdate"))
    flags |= PROP_SUB_NO_INITIAL_UPDATE;

  if(es_prop_is_true(ctx, 2, "earlyChildDelete"))
    flags |= PROP_SUB_EARLY_DEL_CHILD;

  if(es_prop_is_true(ctx, 2, "actionAsArray"))
    eps->eps_action_as_array = 1;

  eps->eps_sub =
      prop_subscribe(flags,
                     PROP_TAG_ROOT, p,
                     PROP_TAG_LOCKMGR, ecmascript_context_lockmgr,
                     PROP_TAG_MUTEX, ec,
                     PROP_TAG_CALLBACK, es_sub_cb, eps,
                     PROP_TAG_DISPATCH_GROUP, ec->ec_prop_dispatch_group,
                     NULL);

  es_resource_push(ctx, &eps->eps_super);
  return 1;
}


/**
 *
 */
static int
es_prop_have_more(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  int yes = duk_require_boolean(ctx, 1);
  prop_have_more_childs(p, yes);
  return 0;
}


/**
 *
 */
static int
es_prop_make_url(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  rstr_t *r = backend_prop_make(p, NULL);
  duk_push_string(ctx, rstr_get(r));
  rstr_release(r);
  return 1;
}


/**
 *
 */
static int
es_prop_select(duk_context *ctx)
{
  prop_select(es_stprop_get(ctx, 0));
  return 0;
}


/**
 *
 */
static int
es_prop_link(duk_context *ctx)
{
  prop_link(es_stprop_get(ctx, 0), es_stprop_get(ctx, 1));
  return 0;
}


/**
 *
 */
static int
es_prop_unlink(duk_context *ctx)
{
  prop_unlink(es_stprop_get(ctx, 0));
  return 0;
}


/**
 *
 */
static int
es_prop_send_event(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  const char *type = duk_require_string(ctx, 1);
  event_t *e;

  if(!strcmp(type, "redirect")) {
    e = event_create_str(EVENT_REDIRECT, duk_require_string(ctx, 2));
  } else if(!strcmp(type, "openurl")) {

    event_openurl_args_t args = {};

    rstr_t *url        = es_prop_to_rstr(ctx, 2, "url");
    rstr_t *view       = es_prop_to_rstr(ctx, 2, "view");
    rstr_t *how        = es_prop_to_rstr(ctx, 2, "how");
    rstr_t *parent_url = es_prop_to_rstr(ctx, 2, "parenturl");

    args.url        = rstr_get(url);
    args.view       = rstr_get(view);
    args.how        = rstr_get(how);
    args.parent_url = rstr_get(parent_url);

    e = event_create_openurl_args(&args);

    rstr_release(url);
    rstr_release(view);
    rstr_release(how);
    rstr_release(parent_url);

  } else {
    duk_error(ctx, DUK_ERR_ERROR, "Event type %s not understood", type);
  }

  prop_send_ext_event(p, e);
  event_release(e);
  return 0;
}


/**
 *
 */
static int
es_prop_is_value(duk_context *ctx)
{
  prop_t *p = es_get_native_obj_nothrow(ctx, 0, &es_native_prop);
  if(p == NULL) {
    duk_push_false(ctx);

  } else {
    int v;

    hts_mutex_lock(&prop_mutex);

    switch(p->hp_type) {
    case PROP_CSTRING:
    case PROP_RSTRING:
    case PROP_URI:
    case PROP_FLOAT:
    case PROP_INT:
    case PROP_VOID:
      v = 1;
      break;
    default:
      v = 0;
      break;
    }
    hts_mutex_unlock(&prop_mutex);
    duk_push_boolean(ctx, v);
  }
  return 1;
}


/**
 *
 */
static int
es_prop_atomic_add(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  int num = duk_require_number(ctx, 1);

  prop_add_int(p, num);
  return 0;
}


/**
 *
 */
static int
es_prop_is_same(duk_context *ctx)
{
  prop_t *a = es_stprop_get(ctx, 0);
  prop_t *b = es_stprop_get(ctx, 1);
  duk_push_boolean(ctx, a == b);
  return 1;
}


/**
 *
 */
static int
es_prop_move_before(duk_context *ctx)
{
  prop_t *a = es_stprop_get(ctx, 0);
  prop_t *b = es_get_native_obj_nothrow(ctx, 1, &es_native_prop);
  prop_move(a, b);
  return 0;
}


/**
 *
 */
static int
es_prop_unload_destroy(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);
  prop_t *a = es_stprop_get(ctx, 0);
  ec->ec_prop_unload_destroy = prop_vec_append(ec->ec_prop_unload_destroy, a);
  return 0;
}


/**
 *
 */
static int
es_prop_is_zombie(duk_context *ctx)
{
  prop_t *a = es_stprop_get(ctx, 0);
  duk_push_boolean(ctx, a->hp_type == PROP_ZOMBIE);
  return 1;
}


/**
 *
 */
static int
es_prop_set_clip_range(duk_context *ctx)
{
  prop_t *a = es_stprop_get(ctx, 0);
  prop_set_int_clipping_range(a, duk_to_int(ctx, 1), duk_to_int(ctx, 2));
  return 0;
}


/**
 *
 */
static int
es_prop_tag_set(duk_context *ctx)
{
  es_prop_sub_t *eps = es_resource_get(ctx, 0, &es_resource_prop_sub);
  prop_t *a = es_stprop_get(ctx, 1);
  void *v = malloc(1);
  prop_tag_set(a, eps, v);
  es_root_register(ctx, 2, v);
  return 0;
}

/**
 *
 */
static int
es_prop_tag_clear(duk_context *ctx)
{
  es_prop_sub_t *eps = es_resource_get(ctx, 0, &es_resource_prop_sub);
  prop_t *a = es_stprop_get(ctx, 1);
  void *v = prop_tag_clear(a, eps);
  es_push_root(ctx, v);
  es_root_unregister(ctx, v);
  free(v);
  return 1;
}

/**
 *
 */
static int
es_prop_tag_get(duk_context *ctx)
{
  es_prop_sub_t *eps = es_resource_get(ctx, 0, &es_resource_prop_sub);
  prop_t *a = es_stprop_get(ctx, 1);
  void *v = prop_tag_get(a, eps);
  es_push_root(ctx, v);
  return 1;
}


/**
 *
 */
static int
es_prop_node_filter_create(duk_context *ctx)
{
  es_push_native_obj(ctx, &es_native_propnf,
                     prop_nf_create(es_stprop_get(ctx, 0),
                                    es_stprop_get(ctx, 1),
                                    NULL, 0));
  return 1;
}


static int
es_prop_node_filter_add_pred(duk_context *ctx)
{
  struct prop_nf *pnf = es_get_native_obj(ctx, 0, &es_native_propnf);
  const char *path = duk_require_string(ctx, 1);
  prop_t *enable = es_get_native_obj_nothrow(ctx, 4, &es_native_prop);

  const char *cfstr = duk_require_string(ctx, 2);
  prop_nf_cmp_t cf;
  if(!strcmp(cfstr, "eq")) {
    cf = PROP_NF_CMP_EQ;
  } else if(!strcmp(cfstr, "neq")) {
    cf = PROP_NF_CMP_NEQ;
  } else {
    duk_error(ctx, DUK_ERR_ERROR, "Bad comparison function");
  }

  const char *modestr = duk_require_string(ctx, 5);
  prop_nf_cmp_t mode;
  if(!strcmp(modestr, "include")) {
    mode = PROP_NF_MODE_INCLUDE;
  } else if(!strcmp(modestr, "exclude")) {
    mode = PROP_NF_MODE_EXCLUDE;
  } else {
    duk_error(ctx, DUK_ERR_ERROR, "Bad filter mode");
  }

  int r;
  if(duk_is_string(ctx, 3)) {
    r = prop_nf_pred_str_add(pnf, path, cf, duk_to_string(ctx, 3),
                             enable, mode);
  } else if(duk_is_number(ctx, 3)) {
    r = prop_nf_pred_int_add(pnf, path, cf, duk_to_number(ctx, 3),
                             enable, mode);
  } else {
    duk_error(ctx, DUK_ERR_ERROR, "Predicate is not a string or number");
  }
  duk_push_int(ctx, r);
  return 1;
}



static int
es_prop_node_filter_del_pred(duk_context *ctx)
{
  struct prop_nf *pnf = es_get_native_obj(ctx, 0, &es_native_propnf);
  int predid = duk_require_number(ctx, 1);
  prop_nf_pred_remove(pnf, predid);
  return 0;
}


static const duk_function_list_entry fnlist_prop[] = {

  { "print",               es_prop_print_duk,             1 },
  { "release",             es_prop_release_duk,           1 },
  { "create",              es_prop_create_duk,            1 },
  { "getValue",            es_prop_get_value_duk,         1 },
  { "getName",             es_prop_get_name_duk,          1 },
  { "getChild",            es_prop_get_child_duk,         2 },
  { "set",                 es_prop_set_value_duk,         3 },
  { "setRichStr",          es_prop_set_rich_str_duk,      3 },
  { "setParent",           es_prop_set_parent_duk,        2 },
  { "subscribe",           es_prop_subscribe,             3 },
  { "haveMore",            es_prop_have_more,             2 },
  { "makeUrl",             es_prop_make_url,              1 },
  { "global",              es_prop_get_global,            0 },
  { "enumerate",           es_prop_enum_duk,              1 },
  { "has",                 es_prop_has_duk,               2 },
  { "deleteChild",         es_prop_delete_child_duk,      2 },
  { "deleteChilds",        es_prop_delete_childs_duk,     1 },
  { "destroy",             es_prop_destroy_duk,           1 },
  { "select",              es_prop_select,                1 },
  { "link",                es_prop_link,                  2 },
  { "unlink",              es_prop_unlink,                1 },
  { "sendEvent",           es_prop_send_event,            3 },
  { "isValue",             es_prop_is_value,              1 },
  { "atomicAdd",           es_prop_atomic_add,            2 },
  { "isSame",              es_prop_is_same,               2 },
  { "moveBefore",          es_prop_move_before,           2 },
  { "unloadDestroy",       es_prop_unload_destroy,        1 },
  { "isZombie",            es_prop_is_zombie,             1 },
  { "setClipRange",        es_prop_set_clip_range,        3 },
  { "tagSet",              es_prop_tag_set,               3 },
  { "tagClear",            es_prop_tag_clear,             2 },
  { "tagGet",              es_prop_tag_get,               2 },

  { "nodeFilterCreate",    es_prop_node_filter_create,    2 },
  { "nodeFilterAddPred",   es_prop_node_filter_add_pred,  6 },
  { "nodeFilterDelPred",   es_prop_node_filter_del_pred,  2 },



  { NULL, NULL, 0}
};

ES_MODULE("prop", fnlist_prop);
