/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2014 Lonelycoder AB
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

