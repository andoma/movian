#include <assert.h>

#include "prop/prop_i.h"
#include "ecmascript.h"
#include "backend/backend_prop.h"

/**
 *
 */
prop_t *
es_stprop_get(duk_context *ctx, int val_index)
{
  if(!duk_is_pointer(ctx, val_index)) {

    if(!duk_is_object(ctx, val_index))
      return NULL;

    duk_get_prop_string(ctx, val_index, "__rawptr__");
    if(!duk_is_pointer(ctx, -1)) {
      duk_pop(ctx);
      return NULL;
    }
    duk_replace(ctx, val_index);
  }
  return duk_to_pointer(ctx, val_index);
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
  prop_print_tree(p, 7);
  return 0;
}


/**
 *
 */
static int
es_prop_create_duk(duk_context *ctx)
{
  prop_t *p =  prop_create_root(NULL);
  duk_push_pointer(ctx, prop_ref_inc(p));
  return 1;
}


/**
 *
 */
static int
es_prop_get_value_duk(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  const char *str = duk_require_string(ctx, 1);

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return 0;
  }

  p = prop_create0(p, str, NULL, 0);

  switch(p->hp_type) {
  case PROP_CSTRING:
    duk_push_string(ctx, p->hp_cstring);
    break;
  case PROP_RSTRING:
    duk_push_string(ctx, rstr_get(p->hp_rstring));
    break;
  case PROP_LINK:
    duk_push_string(ctx, rstr_get(p->hp_link_rtitle));
    break;
  case PROP_FLOAT:
    duk_push_number(ctx, p->hp_float);
    break;
  case PROP_INT:
    duk_push_int(ctx, p->hp_int);
    break;
  default:
    duk_push_pointer(ctx, prop_ref_inc(p));
    break;
  }
  hts_mutex_unlock(&prop_mutex);
  return 1;
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
    SETPRINTF("%f", duk_get_number(ctx, 2));
    prop_set(p, str, PROP_SET_FLOAT, duk_get_number(ctx, 2));
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
  es_context_t *ec = opaque;
  duk_context *ctx = ec->ec_duk;
  va_list ap;
  const rstr_t *r, *r2;
  const char *c;
  double d;
  int i;

  duk_push_global_object(ctx);
  duk_get_prop_string(ctx, -1, "subscriptionInvoke");

  if(!duk_is_function(ctx, -1)) {
    duk_pop_2(ctx);
    return;
  }

  va_start(ap, event);

  int nargs;

  switch(event) {
  case PROP_SET_DIR:
    (void)va_arg(ap, prop_t *);
    duk_push_int(ctx, va_arg(ap, int));
    duk_push_string(ctx, "dir");
    nargs = 2;
    break;

  case PROP_SET_VOID:
    (void)va_arg(ap, prop_t *);
    duk_push_int(ctx, va_arg(ap, int));
    duk_push_string(ctx, "set");
    duk_push_null(ctx);
    nargs = 3;
    break;

  case PROP_SET_RSTRING:
    r = va_arg(ap, const rstr_t *);
    (void)va_arg(ap, prop_t *);
    duk_push_int(ctx, va_arg(ap, int));
    duk_push_string(ctx, "set");
    duk_push_string(ctx, rstr_get(r));
    nargs = 3;
    break;

  case PROP_SET_CSTRING:
    c = va_arg(ap, const char *);
    (void)va_arg(ap, prop_t *);
    duk_push_int(ctx, va_arg(ap, int));
    duk_push_string(ctx, "set");
    duk_push_string(ctx, c);
    nargs = 3;
    break;

  case PROP_SET_RLINK:
    r = va_arg(ap, const rstr_t *);
    r2 = va_arg(ap, const rstr_t *);
    (void)va_arg(ap, prop_t *);
    duk_push_int(ctx, va_arg(ap, int));
    duk_push_string(ctx, "link");
    duk_push_string(ctx, rstr_get(r));
    duk_push_string(ctx, rstr_get(r2));
    nargs = 4;
    break;

  case PROP_SET_INT:
    i = va_arg(ap, int);
    (void)va_arg(ap, prop_t *);
    duk_push_int(ctx, va_arg(ap, int));
    duk_push_string(ctx, "set");
    duk_push_int(ctx, i);
    nargs = 3;
    break;

  case PROP_SET_FLOAT:
    d = va_arg(ap, double);
    (void)va_arg(ap, prop_t *);
    duk_push_int(ctx, va_arg(ap, int));
    duk_push_string(ctx, "set");
    duk_push_number(ctx, d);
    nargs = 3;
    break;

  case PROP_WANT_MORE_CHILDS:
    duk_push_int(ctx, va_arg(ap, int));
    duk_push_string(ctx, "wantmorechilds");
    nargs = 2;
    break;

  case PROP_DESTROYED:
    (void)va_arg(ap, prop_sub_t *);
    duk_push_int(ctx, va_arg(ap, int));
    duk_push_string(ctx, "destroyed");
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
    duk_pop(ctx);
  }
  duk_pop(ctx);
}


/**
 *
 */
static int
es_prop_subscribe(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);
  prop_t *p = es_stprop_get(ctx, 0);
  int idx = duk_get_number(ctx, 1);

  prop_sub_t *s =
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
                   PROP_TAG_ROOT, p,
                   PROP_TAG_MUTEX, &ec->ec_mutex,
                   PROP_TAG_CALLBACK_USER_INT, es_sub_cb, ec, idx,
                   NULL);

  duk_push_pointer(ctx, s);
  return 1;
}


/**
 *
 */
static int
es_prop_unsubscribe(duk_context *ctx)
{
  prop_sub_t *s = duk_require_pointer(ctx, 0);
  prop_unsubscribe(s);
  return 0;
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
 * Showtime object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_prop[] = {

  { "propPrint",               es_prop_print_duk,             1 },
  { "propRelease",             es_prop_release_duk,           1 },
  { "propCreate",              es_prop_create_duk,            0 },
  { "propGet",                 es_prop_get_value_duk,         2 },
  { "propSet",                 es_prop_set_value_duk,         3 },
  { "propSetParent",           es_prop_set_parent_duk,        2 },
  { "propSubscribe",           es_prop_subscribe,             2 },
  { "propUnsubscribe",         es_prop_unsubscribe,           1 },
  { "propHaveMore",            es_prop_have_more,             2 },
  { "propMakeUrl",             es_prop_make_url,              1 },

  { NULL, NULL, 0}
};
