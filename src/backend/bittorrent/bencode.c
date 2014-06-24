/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include "misc/str.h"
#include "htsmsg/htsmsg.h"
#include "bencode.h"

#define NOT_THIS_TYPE ((void *)-1)

static const char *bencode_parse_value(const char *s, const char *stop,
                                       htsmsg_t *msg, const char *name,
                                       const char **failp,
                                       const char **failmsg);


/**
 * Returns a newly allocated string
 */
static void *
bencode_parse_binary(const char *start, const char *stop,
                     const char **endp, size_t *lenp,
                     const char **failp, const char **failmsg)
{
  int len = 0;

  while(1) {
    if(start == stop) {
      *failmsg = "Unexpected end of BENCODE message";
      *failp = start;
      return NULL;
    }

    char c = *start;
    if(c == ':')
      break;

    if(c < '0' || c > '9')
      return NOT_THIS_TYPE;

    len = (len * 10) + c - '0';
    start++;
  }

  start++;

  if(start + len >= stop) {
    *failmsg = "Excessive binary length";
    *failp = start;
    return NULL;
  }

  *lenp = len;
  char *x = malloc(len + 1);
  x[len] = 0;
  memcpy(x, start, len);
  *endp = start + len;
  return x;
}


/**
 * Returns a newly allocated string
 */
static char *
bencode_parse_string(const char *start, const char *stop,
                     const char **endp,
                     const char **failp, const char **failmsg)
{
  size_t len;

  char *x = bencode_parse_binary(start, stop, endp, &len, failp, failmsg);
  if(x == NULL || x == NOT_THIS_TYPE)
    return x;

  for(int i = 0; i < len; i++) {
    if(x[i] == 0) {
      *failmsg = "Unexpected NUL byte";
      *failp = start + i;
      return NULL;
    }
  }
  return x;
}


/**
 *
 */
static void *
bencode_parse_map(const char *s, const char *stop,
                  const char **endp,
                  const char **failp, const char **failmsg,
                  bencode_pase_cb_t *cb, void *opaque)
{
  char *name;
  const char *s2;

  if(*s != 'd')
    return NOT_THIS_TYPE;

  s++;

  htsmsg_t *r = htsmsg_create_map();

  if(s != stop && *s != 'e') {

    while(1) {
      name = bencode_parse_string(s, stop, &s2, failp, failmsg);
      if(name == NOT_THIS_TYPE) {
	*failmsg = "Expected string";
	*failp = s;
        htsmsg_release(r);
	return NULL;
      }

      if(name == NULL) {
        htsmsg_release(r);
	return NULL;
      }

      s = s2;

      s2 = bencode_parse_value(s, stop, r, name, failp, failmsg);

      if(s2 == NULL) {
        htsmsg_release(r);
        free(name);
	return NULL;
      }

      if(cb != NULL)
        cb(opaque, name, s, s2 - s);

      free(name);

      s = s2;

      if(*s == 'e')
	break;
    }
  }

  s++;
  *endp = s;
  return r;
}


/**
 *
 */
static void *
bencode_parse_list(const char *s, const char *stop, const char **endp,
                   const char **failp, const char **failmsg)
{
  const char *s2;

  if(*s != 'l')
    return NOT_THIS_TYPE;

  s++;

  htsmsg_t *r = htsmsg_create_list();

  if(s != stop && *s != 'e') {

    while(1) {

      s2 = bencode_parse_value(s, stop, r, NULL, failp, failmsg);

      if(s2 == NULL) {
        htsmsg_release(r);
	return NULL;
      }

      s = s2;

      if(*s == 'e')
	break;
    }
  }
  s++;
  *endp = s;
  return r;
}


/**
 *
 */
static const char *
bencode_parse_integer(const char *s, const char *stop, int64_t *lp)
{
  if(*s != 'i')
    return NULL;

  int neg = 1;
  int64_t val = 0;

  s++;
  if(s != stop && *s == '-') {
    neg = -1;
    s++;
  }

  while(1) {
    if(s == stop)
      break;
    char c = *s;
    if(c == 'e') {
      s++;
      break;
    }
    val = val * 10 + c - '0';
    s++;
  }

  *lp = val * neg;
  return s;
}


/**
 *
 */
static const char *
bencode_parse_value(const char *s, const char *stop,
                    htsmsg_t *parent, const char *name,
                    const char **failp, const char **failmsg)
{
  const char *s2;
  char *str;
  int64_t l = 0;
  htsmsg_t *c;

  if(s == stop)
    return NULL;

  if((c = bencode_parse_map(s, stop, &s2, failp, failmsg, NULL, NULL)) == NULL)
    return NULL;

  if(c != NOT_THIS_TYPE) {
    htsmsg_add_msg(parent, name, c);
    return s2;
  }

  if((c = bencode_parse_list(s, stop, &s2, failp, failmsg)) == NULL)
    return NULL;
  
  if(c != NOT_THIS_TYPE) {
    htsmsg_add_msg(parent, name, c);
    return s2;
  }

  if((s2 = bencode_parse_integer(s, stop, &l)) != NULL) {
    htsmsg_add_s64(parent, name, l);
    return s2;
  }

  size_t blen;
  if((str = bencode_parse_binary(s, stop, &s2, &blen, failp, failmsg)) == NULL)
    return NULL;

  if(str != NOT_THIS_TYPE) {

    int cleanstr = 1;
    for(int i = 0; i < blen; i++) {
      if(str[i] < 0x20) {
        cleanstr = 0;
        break;
      }
    }

    if(cleanstr)
      htsmsg_add_str(parent, name, str);
    else
      htsmsg_add_bin(parent, name, str, blen);
    free(str);
    return s2;
  }

  *failmsg = "Unknown token";
  *failp = s;
  return NULL;
}


/**
 *
 */
htsmsg_t *
bencode_deserialize(const char *src, const char *stop,
                    char *errbuf, size_t errlen,
                    bencode_pase_cb_t *cb, void *opaque)
{
  const char *end;
  void *c;
  const char *errmsg;
  const char *errp;

  c = bencode_parse_map(src, stop, &end, &errp, &errmsg, cb, opaque);
  if(c == NOT_THIS_TYPE)
    c = bencode_parse_list(src, stop, &end, &errp, &errmsg);

  if(c == NOT_THIS_TYPE) {
    snprintf(errbuf, errlen, "Invalid BENCODE, expected 'd' or 'l'");
    return NULL;
  }

  if(c == NULL) {
    size_t len = strlen(src);
    ssize_t offset = errp - src;
    if(offset > len || offset < 0) {
      snprintf(errbuf, errlen, "%s at (bad) offset %d", errmsg, (int)offset);
    } else {
      offset -= 10;
      if(offset < 0)
	offset = 0;
      snprintf(errbuf, errlen, "%s at offset %d : '%.20s'", errmsg, (int)offset,
	       src + offset);
    }
  }
  return c;
}


/**
 *
 */
static int
serialize_bytes(void *data, int len, char *ptr)
{
  char buf[32];
  int l = snprintf(buf, sizeof(buf), "%d:", len);
  if(ptr != NULL) {
    memcpy(ptr, buf, l);
    memcpy(ptr + l, data, len);
  }
  return l + len;
}


/**
 *
 */
static int
bencode_serialize_r(htsmsg_t *msg, char *ptr)
{
  char buf[32];
  int len = 0;
  int sublen;
  htsmsg_field_t *f;
  const int isarray = msg->hm_islist;

  if(ptr != NULL)
    *ptr++ = isarray ? 'l' : 'd';
  len++;

  TAILQ_FOREACH(f, &msg->hm_fields, hmf_link) {

    if(!isarray) {
      sublen = serialize_bytes(f->hmf_name, strlen(f->hmf_name), ptr);
      if(ptr)
        ptr += sublen;
      len += sublen;
    }

    switch(f->hmf_type) {
    case HMF_LIST:
    case HMF_MAP:
      sublen = bencode_serialize_r(f->hmf_childs, ptr);
      break;

    case HMF_STR:
      sublen = serialize_bytes(f->hmf_str, strlen(f->hmf_str), ptr);
      break;

    case HMF_BIN:
      sublen = serialize_bytes(f->hmf_bin, f->hmf_binsize, ptr);
      break;

    case HMF_DBL:
      // questionable
      snprintf(buf, sizeof(buf), "i%" PRId64"e", (int64_t)f->hmf_dbl);
      sublen = serialize_bytes(buf, strlen(buf), ptr);
      break;

    case HMF_S64:
      snprintf(buf, sizeof(buf), "i%" PRId64"e", f->hmf_s64);
      sublen = serialize_bytes(buf, strlen(buf), ptr);
      break;
    default:
      abort();
    }

    if(ptr)
      ptr += sublen;
    len += sublen;
  }

  if(ptr != NULL)
    *ptr++ = 'e';
  len++;
  return len;
}



/**
 *
 */
buf_t *
bencode_serialize(htsmsg_t *src)
{
  // get size first
  int len = bencode_serialize_r(src, NULL);
  buf_t *b = buf_create(len);
  bencode_serialize_r(src, buf_str(b));
  return b;
}
