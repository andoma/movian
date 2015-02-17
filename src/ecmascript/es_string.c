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
 * Autodetect character encoding
 */
static buf_t *
es_utf8_from_bytes_auto(duk_context *ctx, const char *bufstart, int bufsize)
{
  const charset_t *cs = NULL;

  const char *bufend = bufstart + bufsize;
  char *start = find_str((void *)bufstart, bufsize, "<meta http-equiv=\"");

  if(start != NULL) {
    start += strlen("<meta http-equiv=\"");
    char *end = find_str(start + 1, bufend - (start + 1), ">");
    if(end != NULL) {
      unsigned int len = end - start;
      if(len < 1024) {
        char *copy = alloca(len + 1);
        memcpy(copy, start, len);
        copy[len] = 0;
        if(!strncasecmp(copy, "content-type", strlen("content-type"))) {
          const char *charset = strstr(copy, "charset=");
          if(charset != NULL) {
            charset += strlen("charset=");
            char *e = strchr(charset, '"');
            if(e != NULL) {
              *e = 0;

              if(!strcasecmp(charset, "utf-8") ||
                 !strcasecmp(charset, "utf8")) {

                char *x = malloc(bufsize + 1);
                memcpy(x, bufstart, bufsize);
                x[bufsize] = 0;
                char *rbuf = utf8_cleanup(x);

                return buf_create_from_malloced(strlen(rbuf), rbuf);

              } else {
                cs = charset_get(charset);
              }
            }
          }
        }
      }
    }
  }

  return utf8_from_bytes(bufstart, bufsize, cs, NULL, 0);
}


/**
 *
 */
static int
es_utf8_from_bytes_duk(duk_context *duk)
{
  duk_size_t size;
  const void *bytes = duk_require_buffer(duk, 0, &size);

  if(!duk_is_string(duk, 1)) {

    buf_t *b = es_utf8_from_bytes_auto(duk, bytes, size);
    duk_push_lstring(duk, buf_cstr(b), buf_size(b));
    buf_release(b);
    return 1;
  }


  const char *csname = duk_safe_to_string(duk, 1);

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
