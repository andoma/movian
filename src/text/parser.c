/*
 *  Text parsing
 *  Copyright (C) 2007 - 2011 Andreas Öman
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

#include <stdio.h>
#include <string.h>

#include "showtime.h"
#include "misc/str.h"
#include "misc/unicode_composition.h"

#include "text.h"


/**
 *
 */
static int 
add_one_code(int c, uint32_t *output, int olen)
{
  if(output != NULL)
    output[olen] = c;
  return olen + 1;
}


static int
is_ws(char c)
{
  return c == ' ' || c == '\t';
}

/**
 *
 */
static int
attrib_parser(char *v, uint32_t *output, int olen,
	      int (*fn)(uint32_t *output, int olen, const char *attrib, 
			const char *value, int context),
	      int context)
{
  const char *attrib, *value;
  char quote;
  char *attrib_stop;

  while(*v) {
    while(is_ws(*v))
      v++;
    if(!*v)
      return olen;
    attrib = v;
    while(1) {
      if(!*v)
	return olen;
      if(is_ws(*v) || *v == '=')
	break;
      v++;
    }
    attrib_stop = v;
    while(is_ws(*v))
      v++;
    if(*v != '=')
      return olen;
    v++;
    *attrib_stop = 0;
    while(is_ws(*v))
      v++;
    quote = *v++;
    if(quote != '"' && quote != '\'')
      return olen;
    value = v;
    while(1) {
      if(!*v)
	return olen;
      if(*v == quote)
	break;
      v++;
    }
    *v++ = 0;
    olen = fn(output, olen, attrib, value, context);
  }
  return olen;
}


/**
 *
 */
static int
font_tag(uint32_t *output, int olen, const char *attrib, const char *value,
	 int context)
{
  if(!strcasecmp(attrib, "size"))
    return add_one_code(TR_CODE_FONT_SIZE |
			MAX(MIN(atoi(value), 7), 1), output, olen);
  if(!strcasecmp(attrib, "face"))
    return add_one_code(TR_CODE_FONT_FAMILY |
			freetype_family_id(value, context), output, olen);
  if(!strcasecmp(attrib, "color"))
    return add_one_code(TR_CODE_COLOR |
			html_makecolor(value), output, olen);
  return olen;
}


/**
 *
 */
static int
outline_tag(uint32_t *output, int olen, const char *attrib, const char *value,
	    int context)
{
  if(!strcasecmp(attrib, "size"))
    return add_one_code(TR_CODE_OUTLINE |
			MIN(atoi(value), 10), output, olen);
  if(!strcasecmp(attrib, "color"))
    return add_one_code(TR_CODE_OUTLINE_COLOR |
			html_makecolor(value), output, olen);
  return olen;
}


/**
 *
 */
static int
shadow_tag(uint32_t *output, int olen, const char *attrib, const char *value,
	   int context)
{
  if(!strcasecmp(attrib, "displacement"))
    return add_one_code(TR_CODE_SHADOW |
			MIN(atoi(value), 10), output, olen);
  if(!strcasecmp(attrib, "color"))
    return add_one_code(TR_CODE_SHADOW_COLOR |
			html_makecolor(value), output, olen);
  return olen;
}



/**
 *
 */
static int
tag_to_code(char *s, uint32_t *output, int olen, int context, int flags)
{
  char *tag;
  int endtag = 0;
  int len;
  int c;

  while(*s == ' ')
    s++;
  if(*s == 0)
    return olen;

  tag = s;

  if(*tag == '/') {
    endtag = 1;
    tag++;
  }
  len = strlen(s);
  while(len > 0 && s[len-1] == ' ')
    s[len--] = 0;

  if(!endtag && !strcasecmp(tag, "p"))
    c = TR_CODE_START;
  else if(!endtag && !strcasecmp(tag, "br"))
    c = TR_CODE_NEWLINE;
  else if(!endtag && !strcasecmp(tag, "hr"))
    c = TR_CODE_HR;
  else if(!endtag && !strcasecmp(tag, "margin"))
    c = TR_CODE_SET_MARGIN;
  else if(!strcasecmp(tag, "center"))
    c = endtag ? TR_CODE_CENTER_OFF : TR_CODE_CENTER_ON;
  else if(!strcasecmp(tag, "i"))
    c = endtag ? TR_CODE_ITALIC_OFF : TR_CODE_ITALIC_ON;
  else if(!strcasecmp(tag, "b"))
    c =  endtag ? TR_CODE_BOLD_OFF : TR_CODE_BOLD_ON;
  else if(!strncasecmp(tag, "font", 4)) {
    if(endtag)
      c = TR_CODE_FONT_RESET;
    else
      return attrib_parser(tag+4, output, olen, font_tag, context);
  } else if(!strncasecmp(tag, "outline", 7)) {
    if(endtag)
      c = TR_CODE_OUTLINE;
    else
      return attrib_parser(tag+7, output, olen, outline_tag, context);
  } else if(!strncasecmp(tag, "shadow", 6))
    if(endtag)
      c = TR_CODE_SHADOW;
    else
      return attrib_parser(tag+6, output, olen, shadow_tag, context);
  else if(flags & TEXT_PARSE_SLOPPY_TAGS)
    return -1;
  else
    return olen;

  return add_one_code(c, output, olen);
}


/**
 *
 */
static int
parse_str(uint32_t *output, const char *str, int flags, int context)
{
  int olen = 0, c, p = -1, d;
  int l = strlen(str);
  char *tmp = NULL;

  while((c = utf8_get(&str)) != 0) {
    if(c == '\r')
      continue;

    if(flags & TEXT_PARSE_TAGS && c == '<') {
      const char *s2 = str;
      int lp = 0;
      if(tmp == NULL)
	tmp = malloc(l);

      while((d = utf8_get(&str)) != 0) {
	if(d == '>')
	  break;
	tmp[lp++] = d;
      }
      if(d == 0) {
	if(output != NULL)
	  output[olen] = '<';
	olen++;
	str = s2;
	continue;
      }
      tmp[lp] = 0;

      int r = tag_to_code(tmp, output, olen, context, flags);
      if(r != -1) {
	olen = r;
	p = -1;
	continue;
      }
      // Failed to parse tag
      str = s2;
    }

    if(flags & TEXT_PARSE_HTML_ENTETIES && c == '&') {
      const char *s2 = str;
      int lp = 0;
      if(tmp == NULL)
	tmp = malloc(l);

      while((d = utf8_get(&str)) != 0) {
	if(d == ';')
	  break;
	tmp[lp++] = d;
      }
      if(d != 0) {
	tmp[lp] = 0;

	c = html_entity_lookup(tmp);

	if(c != -1) {
	  if(output != NULL)
	    output[olen] = c;
	  olen++;
	}
	continue;
      }
      if(output != NULL)
	output[olen] = '&';
      olen++;
      str = s2;
      continue;
    }

    if(p != -1 && (d = unicode_compose(p, c)) != -1) {
      if(output != NULL)
	output[olen-1] = d;
      p = -1;
    } else {
      p = c;
      if(output != NULL)
	output[olen] = c;
      olen++;
    }
  }
  free(tmp);
  return olen;
}


/**
 *
 */
uint32_t *
text_parse(const char *str, int *lenp, int flags,
	   const uint32_t *prefix, int prefixlen, int context)
{
  uint32_t *buf;

  *lenp = parse_str(NULL, str, flags, context);
  if(*lenp == 0)
    return NULL;
  *lenp += prefixlen;
  buf = malloc(*lenp * sizeof(int));
  memcpy(buf, prefix, prefixlen * sizeof(int));
  parse_str(buf+prefixlen, str, flags, context);
  return buf;
  
}
