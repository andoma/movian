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
#include "misc/string.h"
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


/**
 *
 */
static int
tag_to_code(char *s, uint32_t *output, int olen)
{
  const char *tag;
  int endtag = 0;
  int len;
  int c;
  const char *v;

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

  if(!endtag && !strcmp(tag, "p"))
    c = TR_CODE_START;
  else if(!endtag && !strcmp(tag, "br"))
    c = TR_CODE_NEWLINE;
  else if(!strcmp(tag, "center"))
    c = endtag ? TR_CODE_CENTER_OFF : TR_CODE_CENTER_ON;
  else if(!strcmp(tag, "i"))
    c = endtag ? TR_CODE_ITALIC_OFF : TR_CODE_ITALIC_ON;
  else if(!strcmp(tag, "b"))
    c =  endtag ? TR_CODE_BOLD_OFF : TR_CODE_BOLD_ON;
  else if((v = mystrbegins(tag, "size")) != NULL)
    c = TR_CODE_SIZE_PX | atoi(v);
  else if((v = mystrbegins(tag, "color")) != NULL)
    c = TR_CODE_COLOR | atoi(v);
  else
    return olen;

  return add_one_code(c, output, olen);
}


/**
 *
 */
static int
parse_str(uint32_t *output, const char *str, int flags)
{
  int olen = 0, c, p = -1, d;
  int l = strlen(str);
  char *tmp = NULL;

  while((c = utf8_get(&str)) != 0) {
    if(c == '\r')
      continue;

    if(flags & TEXT_PARSE_TAGS && c == '<') {
      int lp = 0;
      if(tmp == NULL)
	tmp = malloc(l);

      while((d = utf8_get(&str)) != 0) {
	if(d == '>')
	  break;
	tmp[lp++] = d;
      }
      if(d == 0)
	break;
      tmp[lp] = 0;

      olen = tag_to_code(tmp, output, olen);
      continue;
    }

    if(flags & TEXT_PARSE_HTML_ENTETIES && c == '&') {
      int lp = 0;
      if(tmp == NULL)
	tmp = malloc(l);

      while((d = utf8_get(&str)) != 0) {
	if(d == ';')
	  break;
	tmp[lp++] = d;
      }
      if(d == 0)
	break;
      tmp[lp] = 0;

      c = html_entity_lookup(tmp);

      if(c != -1) {
	if(output != NULL)
	  output[olen] = c;
	olen++;
      }
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
text_parse(const char *str, int *lenp, int flags)
{
  uint32_t *buf;

  *lenp = parse_str(NULL, str, flags);
  if(*lenp == 0)
    return NULL;

  buf = malloc(*lenp * sizeof(int));
  parse_str(buf, str, flags);
  return buf;
  
}
