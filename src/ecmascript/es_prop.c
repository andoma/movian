#include <assert.h>

#include "prop/prop_i.h"
#include "ecmascript.h"

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
  prop_t *p = duk_require_pointer(ctx, 0);
  prop_print_tree(p, 0);
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
  prop_t *p = duk_require_pointer(ctx, 0);
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
#define SETPRINTF(fmt...)

/**
 *
 */
static int
es_prop_set_value_duk(duk_context *ctx)
{
  prop_t *p = duk_require_pointer(ctx, 0);
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
  prop_t *p = duk_require_pointer(ctx, 0);
  prop_t *parent = duk_require_pointer(ctx, 1);

  if(prop_set_parent(p, parent))
    duk_error(ctx, DUK_ERR_ERROR, "Parent is not a directory");
  return 0;
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
  { "propSetParent",           es_prop_set_parent_duk,        3 },

  { NULL, NULL, 0}
};
