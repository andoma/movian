#include <assert.h>

#include "showtime.h"
#include "ecmascript.h"


/**
 *
 */
static int
es_console_log(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);

  const char *msg = duk_to_string(ctx, 0);
  TRACE(TRACE_DEBUG, ec->ec_id, "%s", msg);
  return 0;
}


/**
 * Showtime object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_console[] = {
  { "log",                      es_console_log,           1 },
  { NULL, NULL, 0}
};
