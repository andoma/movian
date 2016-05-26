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

#include "prop/prop.h"
#include "ecmascript.h"

#define PTRNAME "\xff""ptr"


static ecmascript_native_class_t *native_classes[ECMASCRIPT_MAX_NATIVE_CLASSES];

void
ecmascript_register_native_class(ecmascript_native_class_t *c)
{
  static int idgen;

  assert(idgen < ARRAYSIZE(native_classes));
  c->id = idgen;
  native_classes[idgen] = c;
  idgen++;
}


/**
 *
 */
void *
es_get_native_obj(duk_context *ctx, int obj_idx,
                  ecmascript_native_class_t *wanted_type)
{
  duk_get_finalizer(ctx, obj_idx);

  if(!duk_is_function(ctx, -1)) {
    duk_error(ctx, DUK_ERR_ERROR, "Object is not of type %s (no finalizer)",
              wanted_type->name);
  }

  int current_type = duk_get_magic(ctx, -1);

  duk_pop(ctx);

  if(current_type != wanted_type->id)
    duk_error(ctx, DUK_ERR_ERROR, "Object is not of typ %s",
              wanted_type->name);

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
void *
es_get_native_obj_nothrow(duk_context *ctx, int obj_idx,
                          ecmascript_native_class_t *wanted_type)
{
  if(!duk_is_object(ctx, obj_idx))
    return NULL;

  if(duk_is_null(ctx, obj_idx))
    return NULL;

  duk_get_finalizer(ctx, obj_idx);

  if(!duk_is_function(ctx, -1))
    return NULL;

  int current_type = duk_get_magic(ctx, -1);

  duk_pop(ctx);

  if(current_type != wanted_type->id)
    return NULL;

  duk_get_prop_string(ctx, obj_idx, PTRNAME);
  if(!duk_is_pointer(ctx, -1))
    return NULL;

  void *r = duk_get_pointer(ctx, -1);
  duk_pop(ctx);
  return r;
}


/**
 *
 */
static int
es_native_finalizer(duk_context *ctx)
{
  int type = duk_get_current_magic(ctx);
  assert(type < ECMASCRIPT_MAX_NATIVE_CLASSES);

  duk_get_prop_string(ctx, 0, PTRNAME);

  if(duk_is_pointer(ctx, -1)) {
    void *ptr = duk_get_pointer(ctx, -1);
    duk_del_prop_string(ctx, 0, PTRNAME);
    native_classes[type]->release(ptr);

    duk_pop(ctx);
    es_context_t *ec = es_get(ctx);
    ec->ec_native_instances[type]--;
  }
  return 0;
}


/**
 *
 */
int
es_push_native_obj(duk_context *ctx, ecmascript_native_class_t *class,
                   void *ptr)
{
  int obj_idx = duk_push_object(ctx);
  
  duk_push_pointer(ctx, ptr);
  duk_put_prop_string(ctx, obj_idx, PTRNAME);

  duk_push_c_function(ctx, es_native_finalizer, 1);
  duk_set_magic(ctx, -1, class->id);
  duk_set_finalizer(ctx, obj_idx);

  es_context_t *ec = es_get(ctx);
  ec->ec_native_instances[class->id]++;

  return obj_idx;
}



const char *
ecmascript_native_class_name(int id)
{
  assert(id < ECMASCRIPT_MAX_NATIVE_CLASSES);
  if(native_classes[id] == NULL)
    return NULL;
  return native_classes[id]->name;
}
