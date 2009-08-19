/*
 *  Functions converting HTSMSGs to/from XML
 *  Copyright (C) 2008 Andreas Öman
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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <libavutil/common.h>

#include "htsmsg_xml.h"
#include "htsbuf.h"

TAILQ_HEAD(cdata_content_queue, cdata_content);

LIST_HEAD(xmlns_list, xmlns);

typedef struct xmlns {
  LIST_ENTRY(xmlns) xmlns_global_link;
  LIST_ENTRY(xmlns) xmlns_scope_link;

  char *xmlns_prefix;
  unsigned int xmlns_prefix_len;

  char *xmlns_norm;
  unsigned int xmlns_norm_len;

} xmlns_t;

typedef struct xmlparser {
  enum {
    XML_ENCODING_UTF8,
    XML_ENCODING_8859_1,
  } xp_encoding;

  char xp_errmsg[128];

  int xp_srcdataused;

  struct xmlns_list xp_namespaces;

} xmlparser_t;

#define xmlerr(xp, fmt...) \
 snprintf((xp)->xp_errmsg, sizeof((xp)->xp_errmsg), fmt)


typedef struct cdata_content {
  TAILQ_ENTRY(cdata_content) cc_link;
  char *cc_start, *cc_end; /* end points to byte AFTER last char */
  int cc_encoding;
  char cc_buf[0];
} cdata_content_t;

static char *htsmsg_xml_parse_cd(xmlparser_t *xp, 
				 htsmsg_t *parent, char *src);

/* table from w3 tidy entities.c */
static struct html_entity
{
  const char *name;
  int code;
} html_entities[] = {
  {"quot",    34},
  {"amp",     38},
  {"lt",      60},
  {"gt",      62},
  {"nbsp",   160},
  {"iexcl",  161},
  {"cent",   162},
  {"pound",  163},
  {"curren", 164},
  {"yen",    165},
  {"brvbar", 166},
  {"sect",   167},
  {"uml",    168},
  {"copy",   169},
  {"ordf",   170},
  {"laquo",  171},
  {"not",    172},
  {"shy",    173},
  {"reg",    174},
  {"macr",   175},
  {"deg",    176},
  {"plusmn", 177},
  {"sup2",   178},
  {"sup3",   179},
  {"acute",  180},
  {"micro",  181},
  {"para",   182},
  {"middot", 183},
  {"cedil",  184},
  {"sup1",   185},
  {"ordm",   186},
  {"raquo",  187},
  {"frac14", 188},
  {"frac12", 189},
  {"frac34", 190},
  {"iquest", 191},
  {"Agrave", 192},
  {"Aacute", 193},
  {"Acirc",  194},
  {"Atilde", 195},
  {"Auml",   196},
  {"Aring",  197},
  {"AElig",  198},
  {"Ccedil", 199},
  {"Egrave", 200},
  {"Eacute", 201},
  {"Ecirc",  202},
  {"Euml",   203},
  {"Igrave", 204},
  {"Iacute", 205},
  {"Icirc",  206},
  {"Iuml",   207},
  {"ETH",    208},
  {"Ntilde", 209},
  {"Ograve", 210},
  {"Oacute", 211},
  {"Ocirc",  212},
  {"Otilde", 213},
  {"Ouml",   214},
  {"times",  215},
  {"Oslash", 216},
  {"Ugrave", 217},
  {"Uacute", 218},
  {"Ucirc",  219},
  {"Uuml",   220},
  {"Yacute", 221},
  {"THORN",  222},
  {"szlig",  223},
  {"agrave", 224},
  {"aacute", 225},
  {"acirc",  226},
  {"atilde", 227},
  {"auml",   228},
  {"aring",  229},
  {"aelig",  230},
  {"ccedil", 231},
  {"egrave", 232},
  {"eacute", 233},
  {"ecirc",  234},
  {"euml",   235},
  {"igrave", 236},
  {"iacute", 237},
  {"icirc",  238},
  {"iuml",   239},
  {"eth",    240},
  {"ntilde", 241},
  {"ograve", 242},
  {"oacute", 243},
  {"ocirc",  244},
  {"otilde", 245},
  {"ouml",   246},
  {"divide", 247},
  {"oslash", 248},
  {"ugrave", 249},
  {"uacute", 250},
  {"ucirc",  251},
  {"uuml",   252},
  {"yacute", 253},
  {"thorn",  254},
  {"yuml",   255},
  {"fnof",     402},
  {"Alpha",    913},
  {"Beta",     914},
  {"Gamma",    915},
  {"Delta",    916},
  {"Epsilon",  917},
  {"Zeta",     918},
  {"Eta",      919},
  {"Theta",    920},
  {"Iota",     921},
  {"Kappa",    922},
  {"Lambda",   923},
  {"Mu",       924},
  {"Nu",       925},
  {"Xi",       926},
  {"Omicron",  927},
  {"Pi",       928},
  {"Rho",      929},
  {"Sigma",    931},
  {"Tau",      932},
  {"Upsilon",  933},
  {"Phi",      934},
  {"Chi",      935},
  {"Psi",      936},
  {"Omega",    937},
  {"alpha",    945},
  {"beta",     946},
  {"gamma",    947},
  {"delta",    948},
  {"epsilon",  949},
  {"zeta",     950},
  {"eta",      951},
  {"theta",    952},
  {"iota",     953},
  {"kappa",    954},
  {"lambda",   955},
  {"mu",       956},
  {"nu",       957},
  {"xi",       958},
  {"omicron",  959},
  {"pi",       960},
  {"rho",      961},
  {"sigmaf",   962},
  {"sigma",    963},
  {"tau",      964},
  {"upsilon",  965},
  {"phi",      966},
  {"chi",      967},
  {"psi",      968},
  {"omega",    969},
  {"thetasym", 977},
  {"upsih",    978},
  {"piv",      982},
  {"bull",     8226},
  {"hellip",   8230},
  {"prime",    8242},
  {"Prime",    8243},
  {"oline",    8254},
  {"frasl",    8260},
  {"weierp",   8472},
  {"image",    8465},
  {"real",     8476},
  {"trade",    8482},
  {"alefsym",  8501},
  {"larr",     8592},
  {"uarr",     8593},
  {"rarr",     8594},
  {"darr",     8595},
  {"harr",     8596},
  {"crarr",    8629},
  {"lArr",     8656},
  {"uArr",     8657},
  {"rArr",     8658},
  {"dArr",     8659},
  {"hArr",     8660},
  {"forall",   8704},
  {"part",     8706},
  {"exist",    8707},
  {"empty",    8709},
  {"nabla",    8711},
  {"isin",     8712},
  {"notin",    8713},
  {"ni",       8715},
  {"prod",     8719},
  {"sum",      8721},
  {"minus",    8722},
  {"lowast",   8727},
  {"radic",    8730},
  {"prop",     8733},
  {"infin",    8734},
  {"ang",      8736},
  {"and",      8743},
  {"or",       8744},
  {"cap",      8745},
  {"cup",      8746},
  {"int",      8747},
  {"there4",   8756},
  {"sim",      8764},
  {"cong",     8773},
  {"asymp",    8776},
  {"ne",       8800},
  {"equiv",    8801},
  {"le",       8804},
  {"ge",       8805},
  {"sub",      8834},
  {"sup",      8835},
  {"nsub",     8836},
  {"sube",     8838},
  {"supe",     8839},
  {"oplus",    8853},
  {"otimes",   8855},
  {"perp",     8869},
  {"sdot",     8901},
  {"lceil",    8968},
  {"rceil",    8969},
  {"lfloor",   8970},
  {"rfloor",   8971},
  {"lang",     9001},
  {"rang",     9002},
  {"loz",      9674},
  {"spades",   9824},
  {"clubs",    9827},
  {"hearts",   9829},
  {"diams",    9830},
  {"OElig",   338},
  {"oelig",   339},
  {"Scaron",  352},
  {"scaron",  353},
  {"Yuml",    376},
  {"circ",    710},
  {"tilde",   732},
  {"ensp",    8194},
  {"emsp",    8195},
  {"thinsp",  8201},
  {"zwnj",    8204},
  {"zwj",     8205},
  {"lrm",     8206},
  {"rlm",     8207},
  {"ndash",   8211},
  {"mdash",   8212},
  {"lsquo",   8216},
  {"rsquo",   8217},
  {"sbquo",   8218},
  {"ldquo",   8220},
  {"rdquo",   8221},
  {"bdquo",   8222},
  {"dagger",  8224},
  {"Dagger",  8225},
  {"permil",  8240},
  {"lsaquo",  8249},
  {"rsaquo",  8250},
  {"euro",    8364},
  {NULL, 0}
};

int
html_entity_lookup(const char *name)
{
  struct html_entity *e;

  for(e = &html_entities[0]; e->name != NULL; e++)
    if(strcasecmp(e->name, name) == 0)
      return e->code;

  return -1;
}


/**
 *
 */
static void
add_unicode(struct cdata_content_queue *ccq, int c)
{
  cdata_content_t *cc;
  char *q;
  uint8_t tmp;

  cc = malloc(sizeof(cdata_content_t) + 6);
  cc->cc_encoding = XML_ENCODING_UTF8;
  q = cc->cc_buf;

  TAILQ_INSERT_TAIL(ccq, cc, cc_link);
  cc->cc_start = cc->cc_buf;

  PUT_UTF8(c, tmp, *q++ = tmp;)
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
      switch(c) {
      case '0' ... '9':
	v = v * 0x10 + c - '0';
	break;
      case 'a' ... 'f':
	v = v * 0x10 + c - 'a' + 10;
	break;
      case 'A' ... 'F':
	v = v * 0x10 + c - 'A' + 10;
	break;
      case ';':
	(*src)++;
	return v;
      default:
	return 0;
      }
      (*src)++;
    }

  } else {

    /* decimal */
    while(1) {
      c = **src;
      switch(c) {
      case '0' ... '9':
	v = v * 10 + c - '0';
	(*src)++;
	break;
      case ';':
	(*src)++;
	return v;
      default:
	return 0;
      }
    }
  }
}

/**
 *
 */
static inline int
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
  free(ns->xmlns_norm);
  free(ns);
}

/**
 *
 */
static char *
htsmsg_xml_parse_attrib(xmlparser_t *xp, htsmsg_t *msg, char *src,
			struct xmlns_list *xmlns_scope_list)
{
  char *attribname, *payload;
  int attriblen, payloadlen;
  char quote;
  htsmsg_field_t *f;
  xmlns_t *ns;

  attribname = src;
  /* Parse attribute name */
  while(1) {
    if(*src == 0) {
      xmlerr(xp, "Unexpected end of file during attribute name parsing");
      return NULL;
    }

    if(is_xmlws(*src) || *src == '=')
      break;
    src++;
  }

  attriblen = src - attribname;
  if(attriblen < 1 || attriblen > 65535) {
    xmlerr(xp, "Invalid attribute name");
    return NULL;
  }

  while(is_xmlws(*src))
    src++;

  if(*src != '=') {
    xmlerr(xp, "Expected '=' in attribute parsing");
    return NULL;
  }
  src++;

  while(is_xmlws(*src))
    src++;

  
  /* Parse attribute payload */
  quote = *src++;
  if(quote != '"' && quote != '\'') {
    xmlerr(xp, "Expected ' or \" before attribute value");
    return NULL;
  }

  payload = src;
  while(1) {
    if(*src == 0) {
      xmlerr(xp, "Unexpected end of file during attribute value parsing");
      return NULL;
    }
    if(*src == quote)
      break;
    src++;
  }

  payloadlen = src - payload;
  if(payloadlen < 0 || payloadlen > 65535) {
    xmlerr(xp, "Invalid attribute value");
    return NULL;
  }

  src++;
  while(is_xmlws(*src))
    src++;

  if(xmlns_scope_list != NULL && 
     attriblen > 6 && !memcmp(attribname, "xmlns:", 6)) {

    attribname += 6;
    attriblen  -= 6;

    ns = malloc(sizeof(xmlns_t));

    ns->xmlns_prefix = malloc(attriblen + 1);
    memcpy(ns->xmlns_prefix, attribname, attriblen);
    ns->xmlns_prefix[attriblen] = 0;
    ns->xmlns_prefix_len = attriblen;

    ns->xmlns_norm = malloc(payloadlen + 1);
    memcpy(ns->xmlns_norm, payload, payloadlen);
    ns->xmlns_norm[payloadlen] = 0;
    ns->xmlns_norm_len = payloadlen;

    LIST_INSERT_HEAD(&xp->xp_namespaces, ns, xmlns_global_link);
    LIST_INSERT_HEAD(xmlns_scope_list,   ns, xmlns_scope_link);
    return src;
  }

  xp->xp_srcdataused = 1;
  attribname[attriblen] = 0;
  payload[payloadlen] = 0;

  f = htsmsg_field_add(msg, attribname, HMF_STR, 0);
  f->hmf_str = payload;
  return src;
}

/**
 *
 */
static char *
htsmsg_xml_parse_tag(xmlparser_t *xp, htsmsg_t *parent, char *src)
{
  htsmsg_t *m, *attrs;
  struct xmlns_list nslist;
  char *tagname;
  int taglen, empty = 0, i;
  xmlns_t *ns;

  tagname = src;

  LIST_INIT(&nslist);

  while(1) {
    if(*src == 0) {
      xmlerr(xp, "Unexpected end of file during tag name parsing");
      return NULL;
    }
    if(is_xmlws(*src) || *src == '>' || *src == '/')
      break;
    src++;
  }

  taglen = src - tagname;
  if(taglen < 1 || taglen > 65535) {
    xmlerr(xp, "Invalid tag name");
    return NULL;
  }

  attrs = htsmsg_create_map();

  while(1) {

    while(is_xmlws(*src))
      src++;

    if(*src == 0) {
      htsmsg_destroy(attrs);
      xmlerr(xp, "Unexpected end of file in tag");
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

    if((src = htsmsg_xml_parse_attrib(xp, attrs, src, &nslist)) == NULL) {
      htsmsg_destroy(attrs);
      return NULL;
    }
  }

  m = htsmsg_create_map();

  if(TAILQ_FIRST(&attrs->hm_fields) != NULL) {
    htsmsg_add_msg_extname(m, "attrib", attrs);
  } else {
    htsmsg_destroy(attrs);
  }

  if(!empty)
    src = htsmsg_xml_parse_cd(xp, m, src);

  for(i = 0; i < taglen - 1; i++) {
    if(tagname[i] == ':') {

      LIST_FOREACH(ns, &xp->xp_namespaces, xmlns_global_link) {
	if(ns->xmlns_prefix_len == i && 
	   !memcmp(ns->xmlns_prefix, tagname, ns->xmlns_prefix_len)) {

	  int llen = taglen - i - 1;
	  char *n = malloc(ns->xmlns_norm_len + llen + 1);

	  n[ns->xmlns_norm_len + llen] = 0;
	  memcpy(n, ns->xmlns_norm, ns->xmlns_norm_len);
	  memcpy(n + ns->xmlns_norm_len, tagname + i + 1, llen);
	  htsmsg_add_msg(parent, n, m);
	  free(n);
	  goto done;
	}
      }
    }
  }

  xp->xp_srcdataused = 1;
  tagname[taglen] = 0;
  htsmsg_add_msg_extname(parent, tagname, m);

 done:
  while((ns = LIST_FIRST(&nslist)) != NULL)
    xmlns_destroy(ns);
  return src;
}





/**
 *
 */
static char *
htsmsg_xml_parse_pi(xmlparser_t *xp, htsmsg_t *parent, char *src)
{
  htsmsg_t *attrs;
  char *s = src;
  char *piname;
  int l;

  while(1) {
    if(*src == 0) {
      xmlerr(xp, "Unexpected end of file during parsing of "
	     "Processing instructions");
      return NULL;
    }

    if(is_xmlws(*src) || *src == '?')
      break;
    src++;
  }

  l = src - s;
  if(l < 1 || l > 65536) {
    xmlerr(xp, "Invalid 'Processing instructions' name");
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
      htsmsg_destroy(attrs);
      xmlerr(xp, "Unexpected end of file during parsing of "
	     "Processing instructions");
      return NULL;
    }

    if(src[0] == '?' && src[1] == '>') {
      src += 2;
      break;
    }

    if((src = htsmsg_xml_parse_attrib(xp, attrs, src, NULL)) == NULL) {
      htsmsg_destroy(attrs);
      return NULL;
    }
  }


  if(TAILQ_FIRST(&attrs->hm_fields) != NULL && parent != NULL) {
    htsmsg_add_msg(parent, piname, attrs);
  } else {
    htsmsg_destroy(attrs);
  }
  return src;
}


/**
 *
 */
static char *
xml_parse_comment(xmlparser_t *xp, char *src)
{
  /* comment */
  while(1) {
    if(*src == 0) { /* EOF inside comment is invalid */
      xmlerr(xp, "Unexpected end of file inside a comment");
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

  while(*src != 0 && *src != ';')
    src++;
  if(*src == 0) {
    xmlerr(xp, "Unexpected end of file during parsing of label reference");
    return NULL;
  }

  l = src - s;
  if(l < 1 || l > 65535)
    return NULL;
  label = alloca(l + 1);
  memcpy(label, s, l);
  label[l] = 0;
  src++;
  
  code = html_entity_lookup(label);
  if(code != -1)
    add_unicode(ccq, code);
  else {
    xmlerr(xp, "Unknown label referense: \"&%s;\"\n", label);
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
		     htsmsg_t *pis, char *src, int raw)
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
	src = htsmsg_xml_parse_pi(xp, pis, src);
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
	  src = htsmsg_xml_parse_cd0(xp, ccq, tags, pis, src, 1);
	  continue;
	}
	xmlerr(xp, "Unknown syntatic element: <!%.10s", src);
	return NULL;
      }

      if(*src == '/') {
	/* End-tag */
	src++;
	while(*src != '>') {
	  if(*src == 0) { /* EOF inside endtag */
	    xmlerr(xp, "Unexpected end of file inside close tag");
	    return NULL;
	  }
	  src++;
	}
	src++;
	break;
      }

      src = htsmsg_xml_parse_tag(xp, tags, src);
      continue;
    }
    
    if(*src == '&' && !raw) {
      if(cc != NULL)
	cc->cc_end = src;
      cc = NULL;

      src++;

      if(*src == '#') {
	src++;
	/* Character reference */
	if((c = decode_character_reference(&src)) != 0)
	  add_unicode(ccq, c);
	else {
	  xmlerr(xp, "Invalid character reference");
	  return NULL;
	}
      } else {
	/* Label references */
	src = decode_label_reference(xp, ccq, src);
      }
      continue;
    }

    if(cc == NULL) {
      if(*src <= 32) {
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
htsmsg_xml_parse_cd(xmlparser_t *xp, htsmsg_t *parent, char *src)
{
  struct cdata_content_queue ccq;
  htsmsg_field_t *f;
  cdata_content_t *cc;
  int c = 0, l, y = 0;
  char *body;
  char *x;
  htsmsg_t *tags = htsmsg_create_map();
  uint8_t tmp;
  
  TAILQ_INIT(&ccq);
  src = htsmsg_xml_parse_cd0(xp, &ccq, tags, NULL, src, 0);

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
	l += 1 + (*x >= 0x80);

      c += l;
      y += 1 + (l != cc->cc_end - cc->cc_start);
      break;
    }
  }

  if(y == 1 && c > 1) {
    /* One segment UTF-8 (or 7bit ASCII),
       use data directly from source */

    cc = TAILQ_FIRST(&ccq);

    assert(cc != NULL);
    assert(TAILQ_NEXT(cc, cc_link) == NULL);
    
    f = htsmsg_field_add(parent, "cdata", HMF_STR, 0);
    f->hmf_str = cc->cc_start;
    *cc->cc_end = 0;
    free(cc);

  } else if(c > 1) {
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
	for(x = cc->cc_start; x < cc->cc_end; x++) {
	  PUT_UTF8(*x, tmp, body[c++] = tmp;)
	    }
	break;
      }
      
      TAILQ_REMOVE(&ccq, cc, cc_link);
      free(cc);
    }
    body[c] = 0;

    f = htsmsg_field_add(parent, "cdata", HMF_STR, HMF_ALLOCED);
    f->hmf_str = body;

  } else {

    while((cc = TAILQ_FIRST(&ccq)) != NULL) {
      TAILQ_REMOVE(&ccq, cc, cc_link);
      free(cc);
    }
  }


  if(src == NULL) {
    htsmsg_destroy(tags);
    return NULL;
  }

  if(TAILQ_FIRST(&tags->hm_fields) != NULL) {
    htsmsg_add_msg_extname(parent, "tags", tags);
  } else {
    htsmsg_destroy(tags);
  }

  return src;
}


/**
 *
 */
static char *
htsmsg_parse_prolog(xmlparser_t *xp, char *src)
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
      src = htsmsg_xml_parse_pi(xp, pis, src);
      continue;
    }

    if(!strncmp(src, "<!--", 4)) {
      src = xml_parse_comment(xp, src + 4);
      continue;
    }

    if(!strncmp(src, "<!DOCTYPE", 9)) {
      while(*src != 0) {
	if(*src == '>') {
	  src++;
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

  htsmsg_destroy(pis);

  return src;
}



/**
 *
 */
htsmsg_t *
htsmsg_xml_deserialize(char *src, char *errbuf, size_t errbufsize)
{
  htsmsg_t *m;
  xmlparser_t xp;
  char *src0 = src;
  int i;

  xp.xp_errmsg[0] = 0;
  xp.xp_encoding = XML_ENCODING_UTF8;
  LIST_INIT(&xp.xp_namespaces);

  if((src = htsmsg_parse_prolog(&xp, src)) == NULL)
    goto err;

  m = htsmsg_create_map();

  if(htsmsg_xml_parse_cd(&xp, m, src) == NULL) {
    htsmsg_destroy(m);
    goto err;
  }

  if(xp.xp_srcdataused) {
    m->hm_data = src0;
  } else {
    free(src0);
  }

  return m;

 err:
  free(src);
  snprintf(errbuf, errbufsize, "%s", xp.xp_errmsg);
  
  /* Remove any odd chars inside of errmsg */
  for(i = 0; i < errbufsize; i++) {
    if(errbuf[i] < 32) {
      errbuf[i] = 0;
      break;
    }
  }

  return NULL;
}
