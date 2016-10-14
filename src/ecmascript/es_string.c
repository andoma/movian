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

  return utf8_from_bytes((const uint8_t *)bufstart, bufsize, cs, NULL, 0);
}


/**
 *
 */
static int
es_utf8_from_bytes_duk(duk_context *duk)
{
  duk_size_t size;
  const void *bytes = duk_require_buffer_data(duk, 0, &size);

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
 *
 */
static int
es_entitydecode(duk_context *ctx)
{
  char *out = strdup(duk_safe_to_string(ctx, 0));
  html_entities_decode(out);
  duk_push_string(ctx, out);
  free(out);
  return 1;
}


/**
 *
 */
static void
es_queryStringSplit_internal(duk_context *ctx, const char *str)
{
  char *s0, *s;
  duk_push_object(ctx);

  s0 = s = strdup(str);

  while(s) {

    char *k = s;
    char *v = strchr(s, '=');
    if(v == NULL)
      break;

    *v++ = 0;

    if((s = strchr(v, '&')) != NULL)
      *s++ = 0;

    k = strdup(k);
    v = strdup(v);

    url_deescape(k);
    url_deescape(v);

    duk_push_string(ctx, v);
    duk_put_prop_string(ctx, -2, k);
    free(k);
    free(v);
  }
  free(s0);
}


/**
 *
 */
static int
es_queryStringSplit(duk_context *ctx)
{
  es_queryStringSplit_internal(ctx, duk_safe_to_string(ctx, 0));
  return 1;
}


/**
 *
 */
static int
es_escape(duk_context *ctx, int how)
{
  const char *str = duk_safe_to_string(ctx, 0);

  size_t len = url_escape(NULL, 0, str, how);
  char *r = malloc(len);
  url_escape(r, len, str, how);

  duk_push_lstring(ctx, r, len - 1);
  free(r);
  return 1;
}

/**
 *
 */
static int
es_pathEscape(duk_context *ctx)
{
  return es_escape(ctx, URL_ESCAPE_PATH);
}


/**
 *
 */
static int
es_paramEscape(duk_context *ctx)
{
  return es_escape(ctx, URL_ESCAPE_PARAM);
}

/**
 *
 */
static int
es_durationtostring(duk_context *ctx)
{
  int s = duk_to_uint(ctx, 0);
  char tmp[32];
  int m = s / 60;
  int h = s / 3600;
  if(h > 0) {
    snprintf(tmp, sizeof(tmp), "%d:%02d:%02d", h, m % 60, s % 60);
  } else {
    snprintf(tmp, sizeof(tmp), "%d:%02d", m % 60, s % 60);
  }
  duk_push_string(ctx, tmp);
  return 1;
}


/**
 *
 */
static int
es_parseTime(duk_context *ctx)
{
  time_t t;
  const char *str = duk_require_string(ctx, 0);
  if(http_ctime(&t, str))
    duk_error(ctx, DUK_ERR_ERROR, "Invalid time: %s", str);
  duk_push_number(ctx, t * 1000ULL); // Convert to ms
  return 1;
}


/**
 *
 */
static int
es_parseURL(duk_context *ctx)
{
  const char *str = duk_require_string(ctx, 0);
  int parseq = duk_get_boolean(ctx, 1);

  char proto[64];
  char auth[512];
  char hostname[512];
  int port = -1;
  char path[8192];
  url_split(proto, sizeof(proto)-1,
            auth, sizeof(auth),
            hostname, sizeof(hostname),
            &port,
            path, sizeof(path), str);

  duk_push_object(ctx);

  strcat(proto, ":");
  duk_push_string(ctx, proto);
  duk_put_prop_string(ctx, -2, "protocol");

  duk_push_string(ctx, hostname);
  duk_put_prop_string(ctx, -2, "hostname");

  if(*auth) {
    duk_push_string(ctx, auth);
    duk_put_prop_string(ctx, -2, "auth");
  }

  if(port != -1) {
    duk_push_int(ctx, port);
    duk_put_prop_string(ctx, -2, "port");
  }

  char *hash = strrchr(path, '#');
  if(hash != NULL) {
    *hash = 0;
    duk_push_string(ctx, hash + 1);
    duk_put_prop_string(ctx, -2, "hash");
  }

  duk_push_string(ctx, path);
  duk_put_prop_string(ctx, -2, "path");

  char *query = strchr(path, '?');
  if(query != NULL) {
    duk_push_string(ctx, query);
    duk_put_prop_string(ctx, -2, "search");
    *query++ = 0;

    if(parseq) {
      es_queryStringSplit_internal(ctx, query);
    } else {
      duk_push_object(ctx);
    }
    duk_put_prop_string(ctx, -2, "query");
  } else {

    if(parseq) {
      duk_push_object(ctx);
      duk_put_prop_string(ctx, -2, "query");
      duk_push_string(ctx, "");
      duk_put_prop_string(ctx, -2, "search");
    }
  }

  duk_push_string(ctx, path);
  duk_put_prop_string(ctx, -2, "pathname");

  return 1;
}



static const duk_function_list_entry fnlist_string[] = {
  { "isUtf8",                es_is_utf8_duk,           1 },
  { "utf8FromBytes",         es_utf8_from_bytes_duk,   2 },
  { "entityDecode",          es_entitydecode,          1 },
  { "queryStringSplit",      es_queryStringSplit,      1 },
  { "pathEscape",            es_pathEscape,            1 },
  { "paramEscape",           es_paramEscape,           1 },
  { "durationToString",      es_durationtostring,      1 },
  { "parseTime",             es_parseTime,             1 },
  { "parseURL",              es_parseURL,              2 },
  { NULL, NULL, 0}
};

ES_MODULE("string", fnlist_string);
