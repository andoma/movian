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
#include "prop/prop.h"
#include "ecmascript.h"


typedef struct searcher_aux {
  prop_t *model;
  const char *query;
  prop_t *loading;
} searcher_aux_t;


/**
 *
 */
static int
searcher_push_args(duk_context *duk, void *opaque)
{
  searcher_aux_t *sa = opaque;
  es_stprop_push(duk, sa->model);
  duk_push_string(duk, sa->query);
  es_stprop_push(duk, sa->loading);
  return 3;
}


/**
 *
 */
void
ecmascript_search(struct prop *model, const char *query, prop_t *loading)
{
  searcher_aux_t sa = { model, query, loading };
  es_hook_invoke("searcher", searcher_push_args, &sa);
}
