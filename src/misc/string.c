/*
 *  Various string manipulation functions
 *  Copyright (C) 2010 Andreas Öman
 *  Copyright (C) 2010 Mattias Wadman
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
#include <stdlib.h>
#include <string.h>

#include <libavutil/avstring.h>
#include <libavutil/common.h>

#include "misc/string.h"
#include "showtime.h"


/**
 * De-escape HTTP URL
 */
void
http_deescape(char *s)
{
  char v, *d = s;

  while(*s) {
    if(*s == '+') {
      *d++ = ' ';
      s++;
    } else if(*s == '%') {
      s++;
      switch(*s) {
      case '0' ... '9':
	v = (*s - '0') << 4;
	break;
      case 'a' ... 'f':
	v = (*s - 'a' + 10) << 4;
	break;
      case 'A' ... 'F':
	v = (*s - 'A' + 10) << 4;
	break;
      default:
	*d = 0;
	return;
      }
      s++;
      switch(*s) {
      case '0' ... '9':
	v |= (*s - '0');
	break;
      case 'a' ... 'f':
	v |= (*s - 'a' + 10);
	break;
      case 'A' ... 'F':
	v |= (*s - 'A' + 10);
	break;
      default:
	*d = 0;
	return;
      }
      s++;

      *d++ = v;
    } else {
      *d++ = *s++;
    }
  }
  *d = 0;
}

static const char hexchars[16] = "0123456789abcdef";

/**
 *
 */
void
path_escape(char *dest, int size, const char *src)
{
  unsigned char s;

  while(size > 1) {

    s = *src++;
    if(s == 0)
      break;

    if((s >= '0' && s <= '9') ||
       (s >= 'a' && s <= 'z') ||
       (s >= 'A' && s <= 'Z') ||
       s == '/' ||
       s == '_' ||
       s == '.' ||
       s == '-') {
      *dest++ = s;
      size--;
    } else {
      if(size > 4) {
	*dest++ = '%';
	*dest++ = hexchars[(s >> 4) & 0xf];
	*dest++ = hexchars[s & 0xf];
	size -= 3;
      }
    }
  }
  *dest = 0;
}

/* inplace decode html entities, this relies on that no entity has a
 * code point in utf8 that is more bytes then the entity string */
void
html_entities_decode(char *s)
{
  char *e;
  int code;
  char name[10];
  uint8_t tmp;

  for(; *s; s++) {
    if(*s != '&')
      continue;
    
    e = strchr(s, ';');
    if(e == NULL)
      continue;
    
    snprintf(name, sizeof(name), "%.*s", (int)(intptr_t)(e - s - 1), s + 1);
    code = html_entity_lookup(name);
    
    if(code == -1)
      continue;

    PUT_UTF8(code, tmp, *s++ = tmp;);
    
    memmove(s, e + 1, strlen(e + 1) + 1);
    s--;
  }
}

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
 * based on url_split form ffmpeg, renamed to 
 */
void 
url_split(char *proto, int proto_size,
	  char *authorization, int authorization_size,
	  char *hostname, int hostname_size,
	  int *port_ptr,
	  char *path, int path_size,
	  const char *url)
{
  const char *p, *ls, *at, *col, *brk;

  if (port_ptr)               *port_ptr = -1;
  if (proto_size > 0)         proto[0] = 0;
  if (authorization_size > 0) authorization[0] = 0;
  if (hostname_size > 0)      hostname[0] = 0;
  if (path_size > 0)          path[0] = 0;

  /* parse protocol */
  if ((p = strchr(url, ':'))) {
    av_strlcpy(proto, url, MIN(proto_size, p + 1 - url));
    p++; /* skip ':' */
    if (*p == '/') p++;
    if (*p == '/') p++;
  } else {
    /* no protocol means plain filename */
    path_escape(path, path_size, url);
    return;
  }

  /* separate path from hostname */
  ls = strchr(p, '/');
  if(!ls)
    ls = strchr(p, '?');
  if(ls)
    path_escape(path, path_size, ls);
  else
    ls = &p[strlen(p)]; // XXX

  /* the rest is hostname, use that to parse auth/port */
  if (ls != p) {
    /* authorization (user[:pass]@hostname) */
    if ((at = strchr(p, '@')) && at < ls) {
      av_strlcpy(authorization, p,
		 MIN(authorization_size, at + 1 - p));
      p = at + 1; /* skip '@' */
    }

    if (*p == '[' && (brk = strchr(p, ']')) && brk < ls) {
      /* [host]:port */
      av_strlcpy(hostname, p + 1,
		 MIN(hostname_size, brk - p));
      if (brk[1] == ':' && port_ptr)
	*port_ptr = atoi(brk + 2);
    } else if ((col = strchr(p, ':')) && col < ls) {
      av_strlcpy(hostname, p,
		 MIN(col + 1 - p, hostname_size));
      if (port_ptr) *port_ptr = atoi(col + 1);
    } else
      av_strlcpy(hostname, p,
		 MIN(ls + 1 - p, hostname_size));
  }
}

