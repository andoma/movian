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
/**
 * XML parser, written according to this spec:
 *
 * http://www.w3.org/TR/2006/REC-xml-20060816/
 *
 * Parses of UTF-8 and ISO-8859-1 (Latin 1) encoded XML and output as
 * htsmsg's with UTF-8 encoded payloads
 *
 *  Supports:                             Example:
 *  
 *  Comments                              <!--  a comment               -->
 *  Processing Instructions               <?xml                          ?>
 *  CDATA                                 <![CDATA[  <litteraly copied> ]]>
 *  Label references                      &amp;
 *  Character references                  &#65;
 *  Empty tags                            <tagname/>
 *
 *
 *  Not supported:
 *
 *  UTF-16 (mandatory by standard)
 *  Intelligent parsing of <!DOCTYPE>
 *  Entity declarations
 *
 */


#include <assert.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "htsmsg_xml.h"
#include "htsbuf.h"
#include "misc/str.h"

TAILQ_HEAD(cdata_content_queue, cdata_content);

LIST_HEAD(xmlns_list, xmlns);

typedef struct xmlns {
  LIST_ENTRY(xmlns) xmlns_global_link;
  LIST_ENTRY(xmlns) xmlns_scope_link;

  char *xmlns_prefix;
  unsigned int xmlns_prefix_len;

  rstr_t *xmlns_normalized;

} xmlns_t;

typedef struct xmlparser {
  enum {
    XML_ENCODING_UTF8,
    XML_ENCODING_8859_1,
  } xp_encoding;

  char xp_errmsg[128];
  char *xp_errpos;
  int xp_parser_err_line;

  char xp_trim_whitespace;

  struct xmlns_list xp_namespaces;

} xmlparser_t;

#define xmlerr2(xp, pos, fmt, ...) do {                                 \
    snprintf((xp)->xp_errmsg, sizeof((xp)->xp_errmsg), fmt, ##__VA_ARGS__); \
    (xp)->xp_errpos = pos;                                               \
    (xp)->xp_parser_err_line = __LINE__;                                \
  } while(0)


typedef struct cdata_content {
  TAILQ_ENTRY(cdata_content) cc_link;
  char *cc_start, *cc_end; /* end points to byte AFTER last char */
  int cc_encoding;
  char cc_buf[0];
} cdata_content_t;

static char *htsmsg_xml_parse_cd(xmlparser_t *xp, htsmsg_t *parent,
                                 htsmsg_field_t *parent_field,
                                 char *src, buf_t *buf);

static int
xml_is_cc_ws(const cdata_content_t *cc)
{
  const char *c = cc->cc_start;
  while(c != cc->cc_end) {
    if(*c > 32)
      return 0;
    c++;
  }
  return 1;
}


/**
 *
 */
static void
add_unicode(struct cdata_content_queue *ccq, int c)
{
  cdata_content_t *cc;
  char *q;

  cc = malloc(sizeof(cdata_content_t) + 6);
  cc->cc_encoding = XML_ENCODING_UTF8;
  q = cc->cc_buf;

  TAILQ_INSERT_TAIL(ccq, cc, cc_link);
  cc->cc_start = cc->cc_buf;

  q += utf8_put(q, c);
  cc->cc_end = q;
}

/**
 *
 */
static int
decode_character_reference(char **src)
{
  int v = 0;
  char c;

  if(**src == 'x') {
    /* hexadecimal */
    (*src)++;

    /* decimal */
    while(1) {
      c = **src;
      if (c >= '0' && c <= '9')
	v = v * 0x10 + c - '0';
      else if (c >= 'a' && c <= 'f')
        v = v * 0x10 + c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        v = v * 0x10 + c - 'A' + 10;
      else if (c == ';') {
        (*src)++;
        return v;
      } else {
        return 0;
      }
      (*src)++;
    }

  } else {

    /* decimal */
    while(1) {
      c = **src;
      if (c >= '0' && c <= '9')
	v = v * 10 + c - '0';
      else if (c == ';') {
        (*src)++;
        return v;
      } else {
	return 0;
      }
    (*src)++;
    }
  }
}

/**
 *
 */
static __inline int
is_xmlws(char c)
{
  return c > 0 && c <= 32;
  //  return c == 32 || c == 9 || c = 10 || c = 13;
}


/**
 *
 */
static void
xmlns_destroy(xmlns_t *ns)
{
  LIST_REMOVE(ns, xmlns_global_link);
  LIST_REMOVE(ns, xmlns_scope_link);
  free(ns->xmlns_prefix);
  rstr_release(ns->xmlns_normalized);
  free(ns);
}

/**
 *
 */
static htsmsg_field_t *
add_xml_field(xmlparser_t *xp, htsmsg_t *parent, char *tagname, int type,
              int flags)
{
  xmlns_t *ns;
  int i = strcspn(tagname, ":");
  if(tagname[i] && tagname[i + 1]) {
    LIST_FOREACH(ns, &xp->xp_namespaces, xmlns_global_link) {
      if(ns->xmlns_prefix_len == i &&
         !memcmp(ns->xmlns_prefix, tagname, ns->xmlns_prefix_len)) {

        htsmsg_field_t *f = htsmsg_field_add(parent, tagname + i + 1, type,
                                             flags);
        f->hmf_namespace = rstr_dup(ns->xmlns_normalized);
        return f;
      }
    }
  }
  return htsmsg_field_add(parent, tagname, type, flags);
}


/**
 *
 */
static char *
htsmsg_xml_parse_attrib(xmlparser_t *xp, htsmsg_t *msg, char *src,
			struct xmlns_list *xmlns_scope_list, buf_t *buf)
{
  char *attribname, *payload;
  int attriblen, payloadlen;
  char quote;

  attribname = src;
  /* Parse attribute name */
  while(1) {
    if(*src == 0) {
      xmlerr2(xp, src, "Unexpected end of file during attribute name parsing");
      return NULL;
    }

    if(is_xmlws(*src) || *src == '=')
      break;
    src++;
  }

  attriblen = src - attribname;
  if(attriblen < 1 || attriblen > 65535) {
    xmlerr2(xp, attribname, "Invalid attribute name");
    return NULL;
  }

  while(is_xmlws(*src))
    src++;

  if(*src != '=') {
    xmlerr2(xp, src, "Expected '=' in attribute parsing");
    return NULL;
  }
  src++;

  while(is_xmlws(*src))
    src++;

  
  /* Parse attribute payload */
  quote = *src++;
  if(quote != '"' && quote != '\'') {
    xmlerr2(xp, src - 1, "Expected ' or \" before attribute value");
    return NULL;
  }

  payload = src;
  while(1) {
    if(*src == 0) {
      xmlerr2(xp, src, "Unexpected end of file during attribute value parsing");
      return NULL;
    }
    if(*src == quote)
      break;
    src++;
  }

  payloadlen = src - payload;
  if(payloadlen < 0 || payloadlen > 65535) {
    xmlerr2(xp, payload, "Invalid attribute value");
    return NULL;
  }

  src++;
  while(is_xmlws(*src))
    src++;

  if(xmlns_scope_list != NULL &&
     attriblen > 6 && !memcmp(attribname, "xmlns:", 6)) {

    attribname += 6;
    attriblen  -= 6;

    xmlns_t *ns = malloc(sizeof(xmlns_t));

    ns->xmlns_prefix = malloc(attriblen + 1);
    memcpy(ns->xmlns_prefix, attribname, attriblen);
    ns->xmlns_prefix[attriblen] = 0;
    ns->xmlns_prefix_len = attriblen;

    ns->xmlns_normalized = rstr_allocl(payload, payloadlen);

    LIST_INSERT_HEAD(&xp->xp_namespaces, ns, xmlns_global_link);
    LIST_INSERT_HEAD(xmlns_scope_list,   ns, xmlns_scope_link);
    return src;
  }

  if(attribname[attriblen] == '\n' || payload[payloadlen] == '\n') {
    // If we overwrite line endings the line/column computation on error
    // will fail, so for those rare cases, allocate normally

    char *a = mystrndupa(attribname, attriblen);

    htsmsg_field_t *f = add_xml_field(xp, msg, a, HMF_STR,
                                      HMF_XML_ATTRIBUTE | HMF_NAME_ALLOCED |
                                      HMF_ALLOCED);
    f->hmf_str = malloc(payloadlen + 1);
    memcpy(f->hmf_str, payload, payloadlen);
    f->hmf_str[payloadlen] = 0;

  } else {

    attribname[attriblen] = 0;
    payload[payloadlen] = 0;

    htsmsg_set_backing_store(msg, buf);

    htsmsg_field_t *f = add_xml_field(xp, msg, attribname, HMF_STR,
                                      HMF_XML_ATTRIBUTE);
    f->hmf_str = payload;
  }

  return src;
}

/**
 *
 */
static char *
htsmsg_xml_parse_tag(xmlparser_t *xp, htsmsg_t *parent, char *src,
                     buf_t *buf)
{
  struct xmlns_list nslist;
  char *tagname;
  int taglen, empty = 0;

  tagname = src;

  LIST_INIT(&nslist);

  htsmsg_t *m = htsmsg_create_map();

  while(1) {
    if(*src == 0) {
      xmlerr2(xp, src, "Unexpected end of file during tag name parsing");
      return NULL;
    }
    if(is_xmlws(*src) || *src == '>' || *src == '/')
      break;
    src++;
  }

  taglen = src - tagname;
  if(taglen < 1 || taglen > 65535) {
    xmlerr2(xp, tagname, "Invalid tag name");
    return NULL;
  }

  while(1) {

    while(is_xmlws(*src))
      src++;

    if(*src == 0) {
      xmlerr2(xp, src, "Unexpected end of file in tag");
      return NULL;
    }

    if(src[0] == '/' && src[1] == '>') {
      empty = 1;
      src += 2;
      break;
    }

    if(*src == '>') {
      src++;
      break;
    }

    if((src = htsmsg_xml_parse_attrib(xp, m, src, &nslist, buf)) == NULL)
      return NULL;
  }

  htsmsg_field_t *f;

  if(tagname[taglen] == '\n') {
    // If we overwrite line endings the line/column computation on error
    // will fail, so for those rare cases, allocate normally

    char *t = mystrndupa(tagname, taglen);
    f = add_xml_field(xp, parent, t, HMF_MAP, HMF_NAME_ALLOCED);

  } else {
    htsmsg_set_backing_store(parent, buf);

    tagname[taglen] = 0;

    f = add_xml_field(xp, parent, tagname, HMF_MAP, 0);
  }

  if(!empty)
    src = htsmsg_xml_parse_cd(xp, m, f, src, buf);

  if(TAILQ_FIRST(&m->hm_fields) != NULL) {
    f->hmf_childs = m;
  } else {
    htsmsg_release(m);
  }

  xmlns_t *ns;
  while((ns = LIST_FIRST(&nslist)) != NULL)
    xmlns_destroy(ns);
  return src;
}





/**
 *
 */
static char *
htsmsg_xml_parse_pi(xmlparser_t *xp, htsmsg_t *parent, char *src,
                    buf_t *buf)
{
  htsmsg_t *attrs;
  char *s = src;
  char *piname;
  int l;

  while(1) {
    if(*src == 0) {
      xmlerr2(xp, src, "Unexpected end of file during parsing of "
	     "Processing instructions");
      return NULL;
    }

    if(is_xmlws(*src) || *src == '?')
      break;
    src++;
  }

  l = src - s;
  if(l < 1 || l > 1024) {
    xmlerr2(xp, src, "Invalid 'Processing instructions' name");
    return NULL;
  }
  piname = alloca(l + 1);
  memcpy(piname, s, l);
  piname[l] = 0;

  attrs = htsmsg_create_map();

  while(1) {

    while(is_xmlws(*src))
      src++;

    if(*src == 0) {
      htsmsg_release(attrs);
      xmlerr2(xp, src, "Unexpected end of file during parsing of "
	     "Processing instructions");
      return NULL;
    }

    if(src[0] == '?' && src[1] == '>') {
      src += 2;
      break;
    }

    if((src = htsmsg_xml_parse_attrib(xp, attrs, src, NULL, buf)) == NULL) {
      htsmsg_release(attrs);
      return NULL;
    }
  }


  if(TAILQ_FIRST(&attrs->hm_fields) != NULL && parent != NULL) {
    htsmsg_add_msg(parent, piname, attrs);
  } else {
    htsmsg_release(attrs);
  }
  return src;
}


/**
 *
 */
static char *
xml_parse_comment(xmlparser_t *xp, char *src)
{
  char *start = src;
  /* comment */
  while(1) {
    if(*src == 0) { /* EOF inside comment is invalid */
      xmlerr2(xp, start, "Unexpected end of file inside a comment");
      return NULL;
    }

    if(src[0] == '-' && src[1] == '-' && src[2] == '>')
      return src + 3;
    src++;
  }
}

/**
 *
 */
static char *
decode_label_reference(xmlparser_t *xp, 
		       struct cdata_content_queue *ccq, char *src)
{
  char *s = src;
  int l;
  char *label;
  int code;

  char *start = src;
  while(*src != 0 && *src != ';')
    src++;
  if(*src == 0) {
    xmlerr2(xp, start,
            "Unexpected end of file during parsing of label reference");
    return NULL;
  }

  l = src - s;
  if(l < 1) {
    xmlerr2(xp, s, "Too short label reference");
    return NULL;
  }

  if(l > 1024) {
    xmlerr2(xp, s, "Too long label reference");
    return NULL;
  }

  label = alloca(l + 1);
  memcpy(label, s, l);
  label[l] = 0;
  src++;
  
  code = html_entity_lookup(label);
  if(code != -1)
    add_unicode(ccq, code);
  else {
    xmlerr2(xp, start, "Unknown label referense: \"&%s;\"\n", label);
    return NULL;
  }

  return src;
}

/**
 *
 */
static char *
htsmsg_xml_parse_cd0(xmlparser_t *xp,
		     struct cdata_content_queue *ccq, htsmsg_t *tags,
		     htsmsg_t *pis, char *src, int raw, buf_t *buf)
{
  cdata_content_t *cc = NULL;
  int c;

  while(src != NULL && *src != 0) {

    if(raw && src[0] == ']' && src[1] == ']' && src[2] == '>') {
      if(cc != NULL)
	cc->cc_end = src;
      cc = NULL;
      src += 3;
      break;
    }

    if(*src == '<' && !raw) {

      if(cc != NULL)
	cc->cc_end = src;
      cc = NULL;

      src++;
      if(*src == '?') {
	src++;
	src = htsmsg_xml_parse_pi(xp, pis, src, buf);
	continue;
      }

      if(src[0] == '!') {

	src++;

	if(src[0] == '-' && src[1] == '-') {
	  src = xml_parse_comment(xp, src + 2);
	  continue;
	}

	if(!strncmp(src, "[CDATA[", 7)) {
	  src += 7;
	  src = htsmsg_xml_parse_cd0(xp, ccq, tags, pis, src, 1, buf);
	  continue;
	}
	xmlerr2(xp, src, "Unknown syntatic element: <!%.10s", src);
	return NULL;
      }

      if(*src == '/') {
	/* End-tag */
	src++;
	while(*src != '>') {
	  if(*src == 0) { /* EOF inside endtag */
	    xmlerr2(xp, src, "Unexpected end of file inside close tag");
	    return NULL;
	  }
	  src++;
	}
	src++;
	break;
      }

      src = htsmsg_xml_parse_tag(xp, tags, src, buf);
      continue;
    }
    
    if(*src == '&' && !raw) {
      if(cc != NULL)
	cc->cc_end = src;

      src++;

      if(*src == '#') {
        char *start = src;
	src++;
	/* Character reference */
	if((c = decode_character_reference(&src)) != 0)
	  add_unicode(ccq, c);
	else {
	  xmlerr2(xp, start, "Invalid character reference");
	  return NULL;
	}
        cc = NULL;
      } else {
	/* Label references */
	char *x = decode_label_reference(xp, ccq, src);

        if(x != NULL) {
          src = x;
          cc = NULL;
        } else {
          continue;
        }
      }
      continue;
    }

    if(cc == NULL) {
      if(*src < 32) {
	src++;
	continue;
      }
      cc = malloc(sizeof(cdata_content_t));
      cc->cc_encoding = xp->xp_encoding;
      TAILQ_INSERT_TAIL(ccq, cc, cc_link);
      cc->cc_start = src;
    }
    src++;
  }

  if(cc != NULL) {
    assert(src != NULL);
    cc->cc_end = src;
  }
  return src;
}

/**
 *
 */
static char *
htsmsg_xml_parse_cd(xmlparser_t *xp, htsmsg_t *msg, htsmsg_field_t *field,
                    char *src, buf_t *buf)
{
  struct cdata_content_queue ccq;
  cdata_content_t *cc;
  int c = 0, l, y = 0;
  char *body;
  char *x;

  TAILQ_INIT(&ccq);
  src = htsmsg_xml_parse_cd0(xp, &ccq, msg, NULL, src, 0, buf);

  if(xp->xp_trim_whitespace) {
    // Trim whitespaces
    while((cc = TAILQ_FIRST(&ccq)) != NULL && xml_is_cc_ws(cc)) {
      TAILQ_REMOVE(&ccq, cc, cc_link);
      free(cc);
    }

    while((cc = TAILQ_LAST(&ccq, cdata_content_queue)) != NULL &&
          xml_is_cc_ws(cc)) {
      TAILQ_REMOVE(&ccq, cc, cc_link);
      free(cc);
    }
  }

  /* Assemble body */

  TAILQ_FOREACH(cc, &ccq, cc_link) {

    switch(cc->cc_encoding) {
    case XML_ENCODING_UTF8:
      c += cc->cc_end - cc->cc_start;
      y++;
      break;

    case XML_ENCODING_8859_1:
      l = 0;
      for(x = cc->cc_start; x < cc->cc_end; x++)
	l += 1 + ((uint8_t)*x >= 0x80);

      c += l;
      y += 1 + (l != cc->cc_end - cc->cc_start);
      break;
    }
  }

  cc = TAILQ_FIRST(&ccq);

  if(field != NULL && y == 1 && c > 0 && *cc->cc_end != '\n') {
    /* One segment UTF-8 (or 7bit ASCII),
       use data directly from source */

    assert(TAILQ_NEXT(cc, cc_link) == NULL);

    field->hmf_str = cc->cc_start;
    field->hmf_type = HMF_STR;
    *cc->cc_end = 0;
    free(cc);

  } else if(field != NULL && c > 1) {
    body = malloc(c + 1);
    c = 0;

    while((cc = TAILQ_FIRST(&ccq)) != NULL) {

      switch(cc->cc_encoding) {
      case XML_ENCODING_UTF8:
	l = cc->cc_end - cc->cc_start;
	memcpy(body + c, cc->cc_start, l);
	c += l;
	break;

      case XML_ENCODING_8859_1:
	for(x = cc->cc_start; x < cc->cc_end; x++)
	  c += utf8_put(body + c, *x);
	break;
      }

      TAILQ_REMOVE(&ccq, cc, cc_link);
      free(cc);
    }
    body[c] = 0;

    field->hmf_str = body;
    field->hmf_type = HMF_STR;
    field->hmf_flags |= HMF_ALLOCED;

  } else {

    while((cc = TAILQ_FIRST(&ccq)) != NULL) {
      TAILQ_REMOVE(&ccq, cc, cc_link);
      free(cc);
    }
  }
  return src;
}


/**
 *
 */
static char *
htsmsg_parse_prolog(xmlparser_t *xp, char *src, buf_t *buf)
{
  htsmsg_t *pis = htsmsg_create_map();
  htsmsg_t *xmlpi;
  const char *encoding;

  while(1) {
    if(*src == 0)
      break;

    while(is_xmlws(*src))
      src++;
    
    if(!strncmp(src, "<?", 2)) {
      src += 2;
      src = htsmsg_xml_parse_pi(xp, pis, src, buf);
      continue;
    }

    if(!strncmp(src, "<!--", 4)) {
      src = xml_parse_comment(xp, src + 4);
      continue;
    }

    if(!strncmp(src, "<!DOCTYPE", 9)) {
      int depth = 0;

      while(*src != 0) {
	if(*src == '<') {
          depth++;
        } else if(*src == '>') {
	  src++;
          depth--;
          if(depth == 0)
            break;
	}
	src++;
      }
      continue;
    }
    break;
  }

  if((xmlpi = htsmsg_get_map(pis, "xml")) != NULL) {

    if((encoding = htsmsg_get_str(xmlpi, "encoding")) != NULL) {
      if(!strcasecmp(encoding, "iso-8859-1") ||
	 !strcasecmp(encoding, "iso-8859_1") ||
	 !strcasecmp(encoding, "iso_8859-1") ||
	 !strcasecmp(encoding, "iso_8859_1")) {
	xp->xp_encoding = XML_ENCODING_8859_1;
      }
    }
  }

  htsmsg_release(pis);

  return src;
}


/**
 *
 */
static void
get_line_col(const char *str, int len, const char *pos, int *linep, int *colp)
{
  const char *end = str + len;
  int line = 1;
  int column = 0;

  while(str < end) {
    column++;
    if(*str == '\n') {
      column = 0;
      line++;
    } else if(*str == '\r') {
      column = 0;
    }

    if(str == pos)
      break;
    str++;
  }

  *linep = line;
  *colp  = column;
}


/**
 *
 */
htsmsg_t *
htsmsg_xml_deserialize_buf(buf_t *buf, char *errbuf, size_t errbufsize)
{
  htsmsg_t *m;
  xmlparser_t xp;
  int i;
  char *src;
  int line;
  int col;
  buf = buf_make_writable(buf);

  xp.xp_errmsg[0] = 0;
  xp.xp_encoding = XML_ENCODING_UTF8;
  xp.xp_trim_whitespace = 1;
  xp.xp_parser_err_line = 0;

  LIST_INIT(&xp.xp_namespaces);
  src = buf->b_ptr;

  if((src = htsmsg_parse_prolog(&xp, src, buf)) == NULL)
    goto err;

  m = htsmsg_create_map();

  if(htsmsg_xml_parse_cd(&xp, m, NULL, src, buf) == NULL) {
    htsmsg_release(m);
    goto err;
  }
  buf_release(buf);
  return m;

 err:

  get_line_col(buf->b_ptr, buf->b_size, xp.xp_errpos, &line, &col);

  snprintf(errbuf, errbufsize,
           "%s at line %d column %d (XML error %d at byte %d)",
           xp.xp_errmsg, line, col, xp.xp_parser_err_line,
           (int)((void *)xp.xp_errpos - (void *)buf->b_ptr));

  /* Remove any odd chars inside of errmsg */
  for(i = 0; i < errbufsize; i++) {
    if(errbuf[i] < 32) {
      errbuf[i] = 0;
      break;
    }
  }
  buf_release(buf);
  return NULL;
}


/**
 *
 */
htsmsg_t *
htsmsg_xml_deserialize_cstr(const char *str, char *errbuf, size_t errbufsize)
{
  int len = strlen(str);
  buf_t *b = buf_create_and_copy(len, str);
  return htsmsg_xml_deserialize_buf(b, errbuf, errbufsize);
}

