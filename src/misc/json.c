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
#include "json.h"
#include "str.h"
#include "dbl.h"

#define NOT_THIS_TYPE ((void *)-1)

static const char *json_parse_value(const char *s, void *parent, 
				    const char *name,
				    const json_deserializer_t *jd,
				    void *opaque,
				    const char **failp, const char **failmsg);


static int
json_str_read_char(const char **ptr)
{
  const char *s = *ptr;
  if(*s == 0)
    return -1;

  if(*s != '\\')
    return utf8_get(ptr);

  s++;
  *ptr = s + 1;

  int v = 0, i;

  switch(*s) {
  default:    return *s;
  case 0:     return -1;
  case 'b':   return '\b';
  case 'f':   return '\f';
  case 'n':   return '\n';
  case 'r':   return '\r';
  case 't':   return '\t';
  case 'u':

    s++;
    for(i = 0; i < 4; i++) {
      v = v << 4;
      switch(*s) {
      case '0' ... '9':
        v |= *s - '0';
        break;
      case 'a' ... 'f':
        v |= *s - 'a' + 10;
        break;
      case 'A' ... 'F':
        v |= *s - 'F' + 10;
        break;
      default:
        return -2;
      }
      s++;
    }
    *ptr = s;
    return v;
  }
}






/**
 * Returns a newly allocated string
 */
static char *
json_parse_string(const char *start, const char **endp,
		  const char **failp, const char **failmsg)
{
  const char *s;
  while(*start > 0 && *start < 33)
    start++;

  if(*start != '"')
    return NOT_THIS_TYPE;

  start++;

  int len = 0;
  for(s = start; *s != '"';) {

    int v = json_str_read_char(&s);
    if(v == -1) {
      *failmsg = "Unexpected end of JSON message";
      *failp = s;
      return NULL;
    }
    if(v == -2) {
      *failmsg = "Incorrect escape sequence";
      *failp = s;
      return NULL;
    }
    len += utf8_put(NULL, v);
  }

  char *r = malloc(len + 1);
  char *dst = r;
  r[len] = 0;

  for(s = start; *s != '"';) {
    int v = json_str_read_char(&s);
    assert(v > 0);
    dst += utf8_put(dst, v);
  }
  *endp = s + 1;
  return r;
}


/**
 *
 */
static void *
json_parse_map(const char *s, const char **endp, const json_deserializer_t *jd,
	       void *opaque, const char **failp, const char **failmsg)

{
  char *name;
  const char *s2;
  void *r;

  while(*s > 0 && *s < 33)
    s++;

  if(*s != '{')
    return NOT_THIS_TYPE;

  s++;

  r = jd->jd_create_map(opaque);
  
  while(*s > 0 && *s < 33)
    s++;

  if(*s != '}') {

    while(1) {
      name = json_parse_string(s, &s2, failp, failmsg);
      if(name == NOT_THIS_TYPE) {
	*failmsg = "Expected string";
	*failp = s;
	return NULL;
      }

      if(name == NULL)
	return NULL;

      s = s2;
    
      while(*s > 0 && *s < 33)
	s++;

      if(*s != ':') {
	jd->jd_destroy_obj(opaque, r);
	free(name);
	*failmsg = "Expected ':'";
	*failp = s;
	return NULL;
      }
      s++;

      s2 = json_parse_value(s, r, name, jd, opaque, failp, failmsg);
      free(name);

      if(s2 == NULL) {
	jd->jd_destroy_obj(opaque, r);
	return NULL;
      }

      s = s2;

      while(*s > 0 && *s < 33)
	s++;

      if(*s == '}')
	break;

      if(*s != ',') {
	jd->jd_destroy_obj(opaque, r);
	*failmsg = "Expected ','";
	*failp = s;
	return NULL;
      }
      s++;
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
json_parse_list(const char *s, const char **endp, const json_deserializer_t *jd,
		void *opaque, const char **failp, const char **failmsg)
{
  const char *s2;
  void *r;

  while(*s > 0 && *s < 33)
    s++;

  if(*s != '[')
    return NOT_THIS_TYPE;

  s++;

  r = jd->jd_create_list(opaque);
  
  while(*s > 0 && *s < 33)
    s++;

  if(*s != ']') {

    while(1) {

      s2 = json_parse_value(s, r, NULL, jd, opaque, failp, failmsg);

      if(s2 == NULL) {
	jd->jd_destroy_obj(opaque, r);
	return NULL;
      }

      s = s2;

      while(*s > 0 && *s < 33)
	s++;

      if(*s == ']')
	break;

      if(*s != ',') {
	jd->jd_destroy_obj(opaque, r);
	*failmsg = "Expected ','";
	*failp = s;
	return NULL;
      }
      s++;
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
json_parse_double(const char *s, double *dp)
{
  const char *ep;
  while(*s > 0 && *s < 33)
    s++;

  double d = my_str2double(s, &ep);

  if(ep == s)
    return NULL;

  *dp = d;
  return ep;
}


/**
 *
 */
static char *
json_parse_integer(const char *s, long *lp)
{
  char *ep;
  while(*s > 0 && *s < 33)
    s++;
  const char *s2 = s;
  if(*s2 == '-')
    s2++;
  while(*s2 >= '0' && *s2 <= '9')
    s2++;

  if(*s2 == 0)
    return NULL;
  if(s2[0] == '.' || s2[0] == 'e' || s2[0] == 'E')
    return NULL; // Is floating point

  long v = strtol(s, &ep, 10);
  if(v == LONG_MIN || v == LONG_MAX)
    return NULL;

  if(ep == s)
    return NULL;

  *lp = v;
  return ep;
}

/**
 *
 */
static const char *
json_parse_value(const char *s, void *parent, const char *name,
		 const json_deserializer_t *jd, void *opaque,
		 const char **failp, const char **failmsg)
{
  const char *s2;
  char *str;
  double d = 0;
  long l = 0;
  void *c;

  if((c = json_parse_map(s, &s2, jd, opaque, failp, failmsg)) == NULL)
    return NULL;

  if(c != NOT_THIS_TYPE) {
    jd->jd_add_obj(opaque, parent, name, c);
    return s2;
  }

  if((c = json_parse_list(s, &s2, jd, opaque, failp, failmsg)) == NULL)
    return NULL;
  
  if(c != NOT_THIS_TYPE) {
    jd->jd_add_obj(opaque, parent, name, c);
    return s2;
  }

  if((str = json_parse_string(s, &s2, failp, failmsg)) == NULL)
    return NULL;

  if(str != NOT_THIS_TYPE) {
    jd->jd_add_string(opaque, parent, name, str);
    return s2;
  }

  if((s2 = json_parse_integer(s, &l)) != NULL) {
    jd->jd_add_long(opaque, parent, name, l);
    return s2;
  } else if((s2 = json_parse_double(s, &d)) != NULL) {
    jd->jd_add_double(opaque, parent, name, d);
    return s2;
  }

  while(*s > 0 && *s < 33)
    s++;

  if(!strncmp(s, "true", 4)) {
    jd->jd_add_bool(opaque, parent, name, 1);
    return s + 4;
  }

  if(!strncmp(s, "false", 5)) {
    jd->jd_add_bool(opaque, parent, name, 0);
    return s + 5;
  }

  if(!strncmp(s, "null", 4)) {
    jd->jd_add_null(opaque, parent, name);
    return s + 4;
  }

  *failmsg = "Unknown token";
  *failp = s;
  return NULL;
}


/**
 *
 */
void *
json_deserialize(const char *src, const json_deserializer_t *jd, void *opaque,
		 char *errbuf, size_t errlen)
{
  const char *end;
  void *c;
  const char *errmsg;
  const char *errp;

  c = json_parse_map(src, &end, jd, opaque, &errp, &errmsg);
  if(c == NOT_THIS_TYPE)
    c = json_parse_list(src, &end, jd, opaque, &errp, &errmsg);

  if(c == NOT_THIS_TYPE) {
    snprintf(errbuf, errlen, "Invalid JSON, expected '{' or '['");
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
