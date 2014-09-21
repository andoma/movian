#include "ecmascript.h"


/**
 *
 */
void
es_callback_register(duk_context *ctx, int obj_idx, void *ptr)
{
  obj_idx = duk_normalize_index(ctx, obj_idx);

  duk_push_global_stash(ctx);

  duk_get_prop_string(ctx, -1, "callbacks");

  char name[64];
  snprintf(name, sizeof(name), "%p", ptr);

  duk_dup(ctx, obj_idx);

  duk_put_prop_string(ctx, -2, name);
  duk_pop_2(ctx);
}


/**
 *
 */
void
es_callback_unregister(duk_context *ctx, void *ptr)
{
  duk_push_global_stash(ctx);

  duk_get_prop_string(ctx, -1, "callbacks");

  char name[64];
  snprintf(name, sizeof(name), "%p", ptr);

  duk_del_prop_string(ctx, -1, name);
  duk_pop_2(ctx);
}


/**
 *
 */
void
es_push_callback(duk_context *ctx, void *ptr)
{
  duk_push_global_stash(ctx);
  duk_get_prop_string(ctx, -1, "callbacks");

  char name[64];
  snprintf(name, sizeof(name), "%p", ptr);

  duk_get_prop_string(ctx, -1, name);
  duk_swap_top(ctx, -3);
  duk_pop_2(ctx);
}
