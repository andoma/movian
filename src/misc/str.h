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
#ifndef STRING_H__
#define STRING_H__

#include <stdlib.h>
#include <stdint.h>

uint32_t html_makecolor(const char *str);

void url_deescape(char *s);

#define URL_ESCAPE_PATH   1
#define URL_ESCAPE_PARAM  2

int url_escape(char *dest, const int size, const char *src, int how);

void html_entities_decode(char *s);

int html_entity_lookup(const char *name);

size_t html_enteties_escape(const char *src, char *dst);

void 
url_split(char *proto, int proto_size,
	  char *authorization, int authorization_size,
	  char *hostname, int hostname_size,
	  int *port_ptr,
	  char *path, int path_size,
	  const char *url);

int dictcmp(const char *a, const char *b);

int utf8_get(const char **s);

int utf8_verify(const char *str);

int utf8_put(char *out, int c);

char *utf8_cleanup(const char *str);

const char *mystrstr(const char *haystack, const char *needle);

void strvec_addp(char ***str, const char *v);

void strvec_addpn(char ***str, const char *v, size_t len);

char **strvec_split(const char *str, char ch);

void strvec_free(char **s);

void strappend(char **strp, const char *src);

int hex2bin(uint8_t *buf, size_t buflen, const char *str);

void bin2hex(char *dst, size_t dstlen, const uint8_t *src, size_t srclen);

void str_cleanup(char *s, const char *forbidden);

void unicode_init(void);

char *url_resolve_relative(const char *proto, const char *hostname, int port,
			   const char *path, const char *ref);

char *url_resolve_relative_from_base(const char *base, const char *url);



int hexnibble(char c);

void ucs2_to_utf8(uint8_t *dst, size_t dstlen,
		  const uint8_t *src, size_t srclen, int little_endian);

size_t utf8_to_ucs2(uint8_t *dst, const char *src, int little_endian);

size_t utf8_to_ascii(uint8_t *dst, const char *src);

void utf16_to_utf8(char **bufp, size_t *lenp);


typedef struct charset {
  const char *id, *title;
  const uint16_t *ptr;
  const char **aliases;
} charset_t;

char *utf8_from_bytes(const char *str, int len, const charset_t *cs,
		      char *msg, size_t msglen);

const charset_t *charset_get(const char *id);

const charset_t *charset_get_idx(unsigned int i);

const char *charset_get_name(const void *ptr);

struct rstr *get_random_string(void);

char *lp_get(char **lp);

#define LINEPARSE(out, src) for(char *lp = src, *out; (out = lp_get(&lp)) != NULL; )

#endif
