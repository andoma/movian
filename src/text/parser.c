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

#include <string.h>

#include "misc/string.h"
#include "misc/unicode_composition.h"

#include "text.h"



/**
 *
 */
static int
tag_to_code(char *s)
{
  const char *tag;
  int endtag = 0;

  while(*s == ' ')
    s++;
  if(*s == 0)
    return 0;

  tag = s;

  if(*tag == '/') {
    endtag = 1;
    tag++;
  }
    
  while(*s != ' ' && *s != '/' && *s != 0)
    s++;
  *s = 0;

  if(!endtag && !strcmp(tag, "p")) 
    return TR_CODE_START;

  if(!endtag && !strcmp(tag, "br")) 
    return TR_CODE_NEWLINE;

  if(!strcmp(tag, "center")) 
    return endtag ? TR_CODE_CENTER_OFF : TR_CODE_CENTER_ON;

  if(!strcmp(tag, "i")) 
    return endtag ? TR_CODE_ITALIC_OFF : TR_CODE_ITALIC_ON;

  if(!strcmp(tag, "b")) 
    return endtag ? TR_CODE_BOLD_OFF : TR_CODE_BOLD_ON;

  return 0;
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

      c = tag_to_code(tmp);

      if(c) {
	if(output != NULL)
	  output[olen] = c;
	olen++;
      }
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
