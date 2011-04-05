/*
 *  JSON helpers
 *  Copyright (C) 2011 Andreas Öman
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
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "json.h"
#include "string.h"
#include "dbl.h"

static const char *json_parse_value(const char *s, void *parent, 
				    const char *name,
				    const json_deserializer_t *jd,
				    void *opaque);

/**
 * Returns a newly allocated string
 */
static char *
json_parse_string(const char *s, const char **endp)
{
  const char *start;
  char *r, *a, *b;
  int l, esc = 0;

  while(*s > 0 && *s < 33)
    s++;

  if(*s != '"')
    return NULL;

  s++;
  start = s;

  while(1) {
    if(*s == 0)
      return NULL;

    if(*s == '\\') {
      esc = 1;
    } else if(*s == '"' && s[-1] != '\\') {

      *endp = s + 1;

      /* End */
      l = s - start;
      r = malloc(l + 1);
      memcpy(r, start, l);
      r[l] = 0;

      if(esc) {
	/* Do deescaping inplace */

	a = b = r;

	while(*a) {
	  if(*a == '\\') {
	    a++;
	    if(*a == 'b')
	      *b++ = '\b';
	    else if(*a == 'f')
	      *b++ = '\f';
	    else if(*a == 'n')
	      *b++ = '\n';
	    else if(*a == 'r')
	      *b++ = '\r';
	    else if(*a == 't')
	      *b++ = '\t';
	    else if(*a == 'u') {
	      // Unicode character
	      int i, v = 0;

	      a++;
	      for(i = 0; i < 4; i++) {
		v = v << 4;
		switch(a[i]) {
		case '0' ... '9':
		  v |= a[i] - '0';
		  break;
		case 'a' ... 'f':
		  v |= a[i] - 'a' + 10;
		  break;
		case 'A' ... 'F':
		  v |= a[i] - 'F' + 10;
		  break;
		default:
		  free(r);
		  return NULL;
		}
	      }
	      a+=3;
	      b += utf8_put(b, v);
	    } else {
	      *b++ = *a;
	    }
	    a++;
	  } else {
	    *b++ = *a++;
	  }
	}
	*b = 0;
      }
      return r;
    }
    s++;
  }
}


/**
 *
 */
static void *
json_parse_map(const char *s, const char **endp, const json_deserializer_t *jd,
	       void *opaque)
{
  char *name;
  const char *s2;
  void *r;

  while(*s > 0 && *s < 33)
    s++;

  if(*s != '{')
    return NULL;

  s++;

  r = jd->jd_create_map(opaque);
  
  while(1) {

    if((name = json_parse_string(s, &s2)) == NULL) {
      jd->jd_destroy_obj(opaque, r);
      return NULL;
    }

    s = s2;
    
    while(*s > 0 && *s < 33)
      s++;

    if(*s != ':') {
      jd->jd_destroy_obj(opaque, r);
      free(name);
      return NULL;
    }
    s++;

    s2 = json_parse_value(s, r, name, jd, opaque);
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
      return NULL;
    }
    s++;
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
		void *opaque)
{
  const char *s2;
  void *r;

  while(*s > 0 && *s < 33)
    s++;

  if(*s != '[')
    return NULL;

  s++;

  r = jd->jd_create_list(opaque);
  
  while(*s > 0 && *s < 33)
    s++;

  if(*s != ']') {

    while(1) {

      s2 = json_parse_value(s, r, NULL, jd, opaque);

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
  const char *s2 = s;
  while(*s2 >= '0' && *s2 <= '9')
    s2++;

  if(*s2 == 0)
    return NULL;
  if(s2[1] == '.' || s2[1] == 'e' || s2[1] == 'E')
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
		 const json_deserializer_t *jd, void *opaque)
{
  const char *s2;
  char *str;
  double d = 0;
  long l;
  void *c;

  if((c = json_parse_map(s, &s2, jd, opaque)) != NULL) {
    jd->jd_add_obj(opaque, parent, name, c);
    return s2;
  } else if((c = json_parse_list(s, &s2, jd, opaque)) != NULL) {
    jd->jd_add_obj(opaque, parent, name, c);
    return s2;
  } else if((str = json_parse_string(s, &s2)) != NULL) {
    jd->jd_add_string(opaque, parent, name, str);
    return s2;
  } else if((s2 = json_parse_integer(s, &l)) != NULL) {
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
  return NULL;
}


/**
 *
 */
void *
json_deserialize(const char *src, const json_deserializer_t *jd, void *opaque)
{
  const char *end;
  void *c;

  if((c = json_parse_map(src, &end, jd, opaque)) != NULL)
    return c;

  if((c = json_parse_list(src, &end, jd, opaque)) != NULL)
    return c;
  return NULL;
}
