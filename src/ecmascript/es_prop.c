#include <assert.h>

#include "prop/prop_i.h"
#include "ecmascript.h"
#include "backend/backend_prop.h"


/**
 *
 */
typedef struct es_prop_sub {
  es_resource_t eps_super;
  prop_sub_t *eps_sub;
  char eps_autodestry;
} es_prop_sub_t;



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
  return es_get_native_obj(ctx, val_index, ES_NATIVE_PROP);
}


/**
 *
 */
void
es_stprop_push(duk_context *ctx, prop_t *p)
{
  es_push_native_obj(ctx, ES_NATIVE_PROP, prop_ref_inc(p));
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
  es_stprop_push(ctx, prop_create_root(NULL));
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
es_prop_get_value_duk(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  char tmp[64];
  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return 0;
  }

  switch(p->hp_type) {
  case PROP_CSTRING:
    duk_push_string(ctx, p->hp_cstring);
    break;
  case PROP_RSTRING:
    duk_push_string(ctx, rstr_get(p->hp_rstring));
    break;
  case PROP_URI:
    duk_push_string(ctx, rstr_get(p->hp_uri_title));
    break;
  case PROP_FLOAT:
    duk_push_number(ctx, p->hp_float);
    break;
  case PROP_INT:
    duk_push_int(ctx, p->hp_int);
    break;
  case PROP_VOID:
    duk_push_null(ctx);
    break;
  default:
    snprintf(tmp, sizeof(tmp), "[prop internal type %d]", p->hp_type);
    duk_push_string(ctx, tmp);
    break;
  }
  hts_mutex_unlock(&prop_mutex);
  return 1;
}


/**
 *
 */
static int
es_prop_get_child_duk(duk_context *ctx)
{
  prop_t *p = es_stprop_get(ctx, 0);
  const char *str = duk_require_string(ctx, 1);

  hts_mutex_lock(&prop_mutex);

  if(p->hp_type == PROP_ZOMBIE) {
    hts_mutex_unlock(&prop_mutex);
    return 0;
  }

  p = prop_create0(p, str, NULL, 0);
  es_push_native_obj(ctx, ES_NATIVE_PROP, prop_ref_inc(p));
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
  es_prop_sub_t *eps = opaque;
  es_context_t *ec = eps->eps_super.er_ctx;
  va_list ap;
  const rstr_t *r, *r2;
  const char *c;
  double d;
  int i;
  int destroy = 0;
  const  event_t *e;
  duk_context *ctx = ec->ec_duk;

  es_push_root(ctx, eps);

  va_start(ap, event);

  int nargs;

  switch(event) {
  case PROP_SET_DIR:
    (void)va_arg(ap, prop_t *);
    duk_push_string(ctx, "dir");
    nargs = 1;
    break;

  case PROP_SET_VOID:
    (void)va_arg(ap, prop_t *);
    duk_push_string(ctx, "set");
    duk_push_null(ctx);
    nargs = 2;
    break;

  case PROP_SET_RSTRING:
    r = va_arg(ap, const rstr_t *);
    (void)va_arg(ap, prop_t *);
    duk_push_string(ctx, "set");
    duk_push_string(ctx, rstr_get(r));
    nargs = 2;
    break;

  case PROP_SET_CSTRING:
    c = va_arg(ap, const char *);
    (void)va_arg(ap, prop_t *);
    duk_push_string(ctx, "set");
    duk_push_string(ctx, c);
    nargs = 2;
    break;

  case PROP_SET_URI:
    r = va_arg(ap, const rstr_t *);
    r2 = va_arg(ap, const rstr_t *);
    (void)va_arg(ap, prop_t *);
    duk_push_string(ctx, "uri");
    duk_push_string(ctx, rstr_get(r));
    duk_push_string(ctx, rstr_get(r2));
    nargs = 3;
    break;

  case PROP_SET_INT:
    i = va_arg(ap, int);
    (void)va_arg(ap, prop_t *);
    duk_push_string(ctx, "set");
    duk_push_int(ctx, i);
    nargs = 2;
    break;

  case PROP_SET_FLOAT:
    d = va_arg(ap, double);
    (void)va_arg(ap, prop_t *);
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

  case PROP_EXT_EVENT:
    nargs = 2;
    duk_push_string(ctx, "event");
    e = va_arg(ap, const event_t *);

    if(event_is_type(e, EVENT_DYNAMIC_ACTION)) {
      const event_payload_t *ep = (const event_payload_t *)e;
      duk_push_string(ctx, ep->payload);

    } else if(e->e_type_x == EVENT_ACTION_VECTOR) {
      const event_action_vector_t *eav = (const event_action_vector_t *)e;
      assert(eav->num > 0);
      duk_push_string(ctx, action_code2str(eav->actions[0]));

    } else if(e->e_type_x == EVENT_UNICODE) {
      const event_int_t *eu = (const event_int_t *)e;
      char tmp[8];
      snprintf(tmp, sizeof(tmp), "%C", eu->val);
      duk_push_string(ctx, tmp);

    } else {
      duk_pop(ctx);
      nargs = 0;
    }
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
  es_prop_sub_t *eps = es_resource_create(ec, &es_resource_prop_sub);

  es_root_register(ctx, 1, eps);

  eps->eps_autodestry = es_prop_is_true(ctx, 2, "autoDestroy");

  eps->eps_sub =
    prop_subscribe(PROP_SUB_TRACK_DESTROY,
                   PROP_TAG_ROOT, p,
                   PROP_TAG_MUTEX, &ec->ec_mutex,
                   PROP_TAG_CALLBACK, es_sub_cb, eps,
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
 * Showtime object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_prop[] = {

  { "propPrint",               es_prop_print_duk,             1 },
  { "propRelease",             es_prop_release_duk,           1 },
  { "propCreate",              es_prop_create_duk,            0 },
  { "propGetValue",            es_prop_get_value_duk,         2 },
  { "propGetChild",            es_prop_get_child_duk,         2 },
  { "propSet",                 es_prop_set_value_duk,         3 },
  { "propSetParent",           es_prop_set_parent_duk,        2 },
  { "propSubscribe",           es_prop_subscribe,             3 },
  { "propHaveMore",            es_prop_have_more,             2 },
  { "propMakeUrl",             es_prop_make_url,              1 },
  { "propGlobal",              es_prop_get_global,            0 },

  { NULL, NULL, 0}
};
