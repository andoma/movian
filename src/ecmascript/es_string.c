#include <assert.h>

#include "ecmascript.h"
#include "fileaccess/fileaccess.h"
#include "misc/str.h"
#include "htsmsg/htsbuf.h"


/**
 *
 */
static int
es_is_utf8_duk(duk_context *duk)
{
  if(duk_is_buffer(duk, 0)) {
    duk_size_t size;
    const void *bytes = duk_require_buffer(duk, 0, &size);
    duk_push_lstring(duk, bytes, size);
  }

  const char *str = duk_require_string(duk, -1);
  int v = utf8_verify(str);
  duk_pop(duk);
  duk_push_boolean(duk, v);
  return 1;
}


/**
 *
 */
static int
es_utf8_from_bytes_duk(duk_context *duk)
{
  duk_size_t size;
  const char *csname = duk_safe_to_string(duk, 1);
  const void *bytes = duk_require_buffer(duk, 0, &size);

  if(!strcasecmp(csname, "utf-8") || !strcasecmp(csname, "utf8")) {

    duk_push_lstring(duk, bytes, size);
    const char *str = duk_require_string(duk, -1);

    char *n = utf8_cleanup(str);
    if(n != NULL) {
      duk_pop(duk);
      duk_push_string(duk, n);
      free(n);
    }
    return 1;
  }

  const charset_t *cs = charset_get(csname);
  if(cs == NULL)
    duk_error(duk, DUK_ERR_ERROR, "Unknown character encoding %s", csname);

  if(bytes == NULL) {
    duk_push_undefined(duk);
    return 1;
  }

  buf_t *b = utf8_from_bytes(bytes, size, cs, NULL, 0);
  duk_pop(duk);
  duk_push_string(duk, buf_cstr(b));
  buf_release(b);
  return 1;
}


/**
 * Showtime object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_string[] = {
  { "isUtf8",                   es_is_utf8_duk,           1 },
  { "utf8FromBytes",            es_utf8_from_bytes_duk,   2 },
  { NULL, NULL, 0}
};
