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
#include <stdio.h>
#include <string.h>

#include "misc/str.h"
#include "misc/unicode_composition.h"
#include "misc/minmax.h"

#include "text.h"

#ifdef USE_FRIBIDI
#include <fribidi.h>
#endif

typedef int Bool;
#define True 1
#define False 0

#ifdef USE_FRIBIDI
static char *bidi_convert( const char *logical_str, const char *charset, int *out_len )
{
  char *visual_str=NULL;
  FriBidiCharSet fribidi_charset;
  FriBidiChar *logical_unicode_str;
  FriBidiChar *visual_unicode_str;
  FriBidiParType pbase_dir = FRIBIDI_TYPE_ON;
  if (logical_str == NULL || charset == NULL)
  {
    return NULL;
  }
  int str_len = (int)strlen(logical_str);
  fribidi_charset = fribidi_parse_charset((char *)charset);
  if (fribidi_charset == FRIBIDI_CHAR_SET_NOT_FOUND)
  {
    return NULL;
  }
  logical_unicode_str = (FriBidiChar *)malloc((str_len + 1) * sizeof(FriBidiChar));
  str_len = fribidi_charset_to_unicode(
      fribidi_charset, (char *)logical_str, str_len,
      logical_unicode_str);
  visual_unicode_str = (FriBidiChar *)malloc((str_len + 1) * sizeof(FriBidiChar));
  if (!fribidi_log2vis(
          logical_unicode_str, str_len, &pbase_dir,
          visual_unicode_str, NULL, NULL, NULL))
  {
    return NULL;
  }

  visual_str = malloc((4 * str_len + 1) * sizeof(char));
  *out_len = fribidi_unicode_to_charset(
      fribidi_charset, visual_unicode_str, str_len, visual_str);

  free(logical_unicode_str);
  free(visual_unicode_str);
  return visual_str;
}
#endif //USE_FRIBIDI

/**
 *
 */
typedef struct parse_ctx {
  char eol_reset_bold;
  char eol_reset_italic;
  char eol_reset_color;
} parse_ctx_t;

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
html_tag_to_code(char *s, uint32_t *output, int olen, int context, int flags)
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

  while(*tag == ' ')
    tag++;

  len = strlen(s);
  while(len > 0 && s[len-1] == ' ')
    s[len--] = 0;

  if(!strcasecmp(tag, "ruby")) {
    if(endtag)
      c = TR_CODE_FONT_RESET;
    else
      c = TR_CODE_ITALIC_ON;
  } else if(!strcasecmp(tag, "rt")) {
    olen = add_one_code(TR_CODE_ITALIC_OFF, output, olen);
    c = TR_CODE_START;
  } else if(!endtag && !strcasecmp(tag, "p")) {
    c = TR_CODE_START;
  } else if(!endtag && !strcasecmp(tag, "br")) {
    c = TR_CODE_NEWLINE;
  } else if(!endtag && !strcasecmp(tag, "hr")) {
    c = TR_CODE_HR;
  } else if(!endtag && !strcasecmp(tag, "margin")) {
    c = TR_CODE_SET_MARGIN;
  } else if(!strcasecmp(tag, "center")) {
    c = endtag ? TR_CODE_CENTER_OFF : TR_CODE_CENTER_ON;
  } else if(!strcasecmp(tag, "i")) {
    c = endtag ? TR_CODE_ITALIC_OFF : TR_CODE_ITALIC_ON;
  } else if(!strcasecmp(tag, "b")) {
    c =  endtag ? TR_CODE_BOLD_OFF : TR_CODE_BOLD_ON;
  } else if(!strncasecmp(tag, "font", 4)) {
    if(endtag)
      c = TR_CODE_FONT_RESET;
    else
      return attrib_parser(tag+4, output, olen, font_tag, context);
  } else if(!strncasecmp(tag, "outline", 7)) {
    if(endtag)
      c = TR_CODE_OUTLINE;
    else
      return attrib_parser(tag+7, output, olen, outline_tag, context);
  } else if(!strncasecmp(tag, "shadow", 6)) {
    if(endtag)
      c = TR_CODE_SHADOW;
    else
      return attrib_parser(tag+6, output, olen, shadow_tag, context);
  } else if(flags & TEXT_PARSE_SLOPPY_TAGS) {
    return -1;
  } else {
    return olen;
  }

  return add_one_code(c, output, olen);
}


/**
 *
 */
static int
sub_tag_to_code(char *s, uint32_t *output, int olen, int context, int flags,
                parse_ctx_t *pc)
{
  while(*s == ' ')
    s++;
  if(*s == 0)
    return olen;

  int doreset = 0;

  switch(*s) {
  default:
  bad:
    if(flags & TEXT_PARSE_SLOPPY_TAGS) {
      return -1;
    } else {
      return olen;
    }

  case 'y':
    doreset = 1;
    // FALLTHRU
  case 'Y':
    if(s[1] != ':')
      goto bad;

    while(*s) {
      if(*s == 'b') {
        olen = add_one_code(TR_CODE_BOLD_ON, output, olen);
        pc->eol_reset_bold = doreset;
      } else if(*s == 'i') {
        olen = add_one_code(TR_CODE_ITALIC_ON, output, olen);
        pc->eol_reset_italic = doreset;
      }
      s++;
    }
    break;

  case 'c':
    pc->eol_reset_color = 1;
    // FALLTHRU
  case 'C':
    if(s[1] != ':')
      goto bad;
    if(s[2] != '$')
      goto bad;
    if(strlen(s) < 9)
      break;
    s+= 3;
    olen = add_one_code(TR_CODE_COLOR |
                        (hexnibble(s[0]) << 20) |
                        (hexnibble(s[1]) << 16) |
                        (hexnibble(s[2]) << 12) |
                        (hexnibble(s[3]) <<  8) |
                        (hexnibble(s[4]) <<  4) |
                        (hexnibble(s[5])      ),
                        output, olen);
    break;
  }
  return olen;
}


/**
 *
 */
static int
parse_str(uint32_t *output, const char *str, int flags, int context,
          int default_color)
{
  parse_ctx_t pc = {0};
  int olen = 0, c, p = -1, d;
  int l = strlen(str);
  char *tmp = NULL;
  int sol = 1; // Start of line

  while((c = utf8_get(&str)) != 0) {
    if(c == '\r')
      continue;

    if(c == '\n') {
      sol = 1;
      if(pc.eol_reset_color) {
        olen = add_one_code(TR_CODE_COLOR | default_color,
                            output, olen);
        pc.eol_reset_color = 0;
      }

      if(pc.eol_reset_bold) {
        olen = add_one_code(TR_CODE_BOLD_OFF, output, olen);
        pc.eol_reset_bold = 0;
      }

      if(pc.eol_reset_italic) {
        olen = add_one_code(TR_CODE_ITALIC_OFF, output, olen);
        pc.eol_reset_italic = 0;
      }
    }


    if(flags & TEXT_PARSE_SLASH_PREFIX && sol && c == '/') {
      olen = add_one_code(TR_CODE_ITALIC_ON, output, olen);
      sol = 0;
      continue;
    }

    if(flags & TEXT_PARSE_HTML_TAGS && c == '<') {
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

      int r = html_tag_to_code(tmp, output, olen, context, flags);
      if(r != -1) {
	olen = r;
	p = -1;
	continue;
      }
      // Failed to parse tag
      str = s2;
    }

    if(flags & TEXT_PARSE_SUB_TAGS && c == '{') {
      const char *s2 = str;
      int lp = 0;
      if(tmp == NULL)
	tmp = malloc(l);

      while((d = utf8_get(&str)) != 0) {
	if(d == '}')
	  break;
	tmp[lp++] = d;
      }
      if(d == 0) {
	if(output != NULL)
	  output[olen] = '{';
	olen++;
	str = s2;
	continue;
      }
      tmp[lp] = 0;
      int r = sub_tag_to_code(tmp, output, olen, context, flags, &pc);
      if(r != -1) {
	olen = r;
	p = -1;
	continue;
      }
      // Failed to parse tag
      str = s2;
    }

    if(flags & TEXT_PARSE_HTML_ENTITIES && c == '&') {
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
      olen = add_one_code(c, output, olen);
      sol = 0;
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
  int default_color = 0xffffff;
  for(int i = 0; i < prefixlen; i++) {
    if((prefix[i] & 0xff000000) == TR_CODE_COLOR)
      default_color = prefix[i] & 0xffffff;
  }
# ifdef USE_FRIBIDI
  int out_len =0;
  char *str_final = bidi_convert(str, "UTF-8", &out_len);
  if (str_final)
    *lenp = parse_str(NULL, str_final, flags, context, default_color);
  else
#endif //USE_FRIBIDI
    *lenp = parse_str(NULL, str, flags, context, default_color);
  if (*lenp == 0)
    return NULL;
  *lenp += prefixlen;
  buf = malloc(*lenp * sizeof(int));
  memcpy(buf, prefix, prefixlen * sizeof(int));
# ifdef USE_FRIBIDI
  if (str_final) {
    parse_str(buf + prefixlen, str_final, flags, context, default_color);
    free(str_final);
  } else
# endif //USE_FRIBIDI
    parse_str(buf + prefixlen, str, flags, context, default_color);
  return buf;
  
}
