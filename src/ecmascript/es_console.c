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

#include "main.h"
#include "ecmascript.h"


/**
 *
 */
static int
es_console_log(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);

  const char *msg = duk_to_string(ctx, 0);
  TRACE(TRACE_DEBUG, rstr_get(ec->ec_id), "%s", msg);
  return 0;
}

/**
 *
 */
static int
es_console_error(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);

  const char *msg = duk_to_string(ctx, 0);
  TRACE(TRACE_ERROR, rstr_get(ec->ec_id), "%s", msg);
  return 0;
}


/**
 * Showtime object exposed functions
 */
const duk_function_list_entry es_fnlist_console[] = {
  { "log",                      es_console_log,           1 },
  { "error",                    es_console_error,         1 },
  { NULL, NULL, 0}
};
