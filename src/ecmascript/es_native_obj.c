#include <assert.h>

#include "prop/prop.h"

#include "ecmascript.h"

#define PTRNAME "\xff""ptr"

static const char *
native_type_to_str(es_native_type_t type)
{
  switch(type) {
  case ES_NATIVE_PROP:     return "prop";
  case ES_NATIVE_RESOURCE: return "resource";
  default: return "???";
  }
}


/**
 *
 */
void *
es_get_native_obj(duk_context *ctx, int obj_idx, es_native_type_t wanted_type)
{
  duk_get_finalizer(ctx, obj_idx);

  if(!duk_is_function(ctx, -1)) {
    duk_error(ctx, DUK_ERR_ERROR, "Object is not of type %s (no finalizer)",
              native_type_to_str(wanted_type));
  }

  es_native_type_t current_type = duk_get_magic(ctx, -1);

  duk_pop(ctx);

  if(current_type != wanted_type)
    duk_error(ctx, DUK_ERR_ERROR, "Object is %s, expected %s",
              native_type_to_str(current_type),
              native_type_to_str(wanted_type));

  duk_get_prop_string(ctx, obj_idx, PTRNAME);
  if(!duk_is_pointer(ctx, -1))
    duk_error(ctx, DUK_ERR_ERROR, "Object missing ptr");

  void *r = duk_get_pointer(ctx, -1);
  duk_pop(ctx);
  return r;
}


/**
 *
 */
static void
call_finalizer(int type, void *ptr)
{
  switch(type) {
  case ES_NATIVE_PROP:
    prop_ref_dec(ptr);
    break;
  case ES_NATIVE_RESOURCE:
    es_resource_release(ptr);
    break;
  default:
    abort();
  }

}


/**
 *
 */
static int
es_native_finalizer(duk_context *ctx)
{
  int type = duk_get_current_magic(ctx);

  duk_get_prop_string(ctx, 0, PTRNAME);

  if(duk_is_pointer(ctx, -1)) {
    void *ptr = duk_get_pointer(ctx, -1);
    duk_del_prop_string(ctx, 0, PTRNAME);
    call_finalizer(type, ptr);
    duk_pop(ctx);
  }
  return 0;
}


/**
 *
 */
int
es_push_native_obj(duk_context *ctx, es_native_type_t type, void *ptr)
{
  int obj_idx = duk_push_object(ctx);

  duk_push_pointer(ctx, ptr);
  duk_put_prop_string(ctx, obj_idx, PTRNAME);

  duk_push_c_function(ctx, es_native_finalizer, 1);
  duk_set_magic(ctx, -1, type);
  duk_set_finalizer(ctx, obj_idx);

  return obj_idx;
}

