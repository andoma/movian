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
#include "ecmascript.h"


/**
 *
 */
void
es_root_register(duk_context *ctx, int obj_idx, void *ptr)
{
  obj_idx = duk_normalize_index(ctx, obj_idx);

  duk_push_global_stash(ctx);

  duk_get_prop_string(ctx, -1, "roots");

  char name[64];
  snprintf(name, sizeof(name), "%p", ptr);

  duk_dup(ctx, obj_idx);

  duk_put_prop_string(ctx, -2, name);
  duk_pop_2(ctx);

  es_context_t *ec = es_get(ctx);
  ec->ec_rooted_objects++;
}


/**
 *
 */
void
es_root_unregister(duk_context *ctx, void *ptr)
{
  duk_push_global_stash(ctx);

  duk_get_prop_string(ctx, -1, "roots");

  char name[64];
  snprintf(name, sizeof(name), "%p", ptr);

  duk_del_prop_string(ctx, -1, name);
  duk_pop_2(ctx);

  es_context_t *ec = es_get(ctx);
  ec->ec_rooted_objects--;
}


/**
 *
 */
void
es_push_root(duk_context *ctx, void *ptr)
{
  duk_push_global_stash(ctx);
  duk_get_prop_string(ctx, -1, "roots");

  char name[64];
  snprintf(name, sizeof(name), "%p", ptr);

  duk_get_prop_string(ctx, -1, name);
  duk_swap_top(ctx, -3);
  duk_pop_2(ctx);
}
