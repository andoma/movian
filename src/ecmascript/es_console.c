/*
 *  Copyright (C) 2007-2018 Lonelycoder AB
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

#include "main.h"
#include "ecmascript.h"


static duk_ret_t
make_printable(duk_context *ctx)
{
  if(duk_is_object(ctx, -1)) {
    duk_json_encode(ctx, -1);
  }
  return 1;
}


/**
 *
 */
static const char *
log_concat(duk_context *ctx)
{
  int argc = duk_get_top(ctx);
  if(argc == 0)
    return "";

  duk_push_string(ctx, " ");

  for(int i = 0; i < argc; i++) {
    duk_dup(ctx, i);
    duk_safe_call(ctx, make_printable, 1, 1);
  }

  duk_join(ctx, argc);

  return duk_get_string(ctx, -1);
}



/**
 *
 */
static int
es_console_log(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);
  TRACE(TRACE_DEBUG, rstr_get(ec->ec_id), "%s", log_concat(ctx));
  return 0;
}


/**
 *
 */
static int
es_console_error(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);
  TRACE(TRACE_ERROR, rstr_get(ec->ec_id), "%s", log_concat(ctx));
  return 0;
}


/**
 * Showtime object exposed functions
 */
const duk_function_list_entry es_fnlist_console[] = {
  { "log",    es_console_log,    DUK_VARARGS },
  { "error",  es_console_error,  DUK_VARARGS },
  { "warn",   es_console_error,  DUK_VARARGS },
  { NULL, NULL, 0}
};
