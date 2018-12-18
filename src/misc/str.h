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
#ifndef STRING_H__
#define STRING_H__

#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

struct buf;

uint32_t html_makecolor(const char *str);

void url_deescape(char *s);

void deescape_cstyle(char *s);

#define URL_ESCAPE_PATH       1
#define URL_ESCAPE_PARAM      2
#define URL_ESCAPE_SPACE_ONLY 3

int url_escape(char *dest, const int size, const char *src, int how);

void html_entities_decode(char *s);

int html_entity_lookup(const char *name);

size_t html_enteties_escape(const char *src, char *dst);

char *fmtstr(const char *fmt, ...);

char *fmtstrv(const char *fmt, va_list ap);

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

int strvec_len(char **s);

void strappend(char **strp, const char *src);

int hex2binl(uint8_t *buf, size_t buflen, const char *str, int maxlen);

#define hex2bin(a,b,c) hex2binl(a,b,c,INT32_MAX)

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

struct buf *utf16_to_utf8(struct buf *b);


typedef struct charset {
  const char *id, *title;
  const uint16_t *table;
  int (*convert)(const struct charset *cs, char *dst,
                 const uint8_t *src, int len, int strict);
  const char **aliases;
} charset_t;

struct buf *utf8_from_bytes(const void *str, int len, const charset_t *cs,
                            char *msg, size_t msglen);

struct rstr *rstr_from_bytes(const char *str, char *how, size_t howlen);

struct rstr *rstr_from_bytes_len(const char *str, int len,
                                 char *how, size_t howlen);

const charset_t *charset_get(const char *id);

const charset_t *charset_get_idx(unsigned int i);

const char *charset_get_name(const void *ptr);

struct rstr *get_random_string(void);

char *lp_get(char **lp);

#define LINEPARSE(out, src) for(char *lp = src, *out; (out = lp_get(&lp)) != NULL; )

char *find_str(const char *s, int len, const char *needle);

void mystrlower(char *s);

void rgbstr_to_floatvec(const char *s, float *out);

int pattern_match(const char *str, const char *pat);

void freecharp(char **ptr);

#define scoped_char char __attribute__((cleanup(freecharp)))

char *fmtv(const char *fmt, va_list ap);

char *fmt(const char *fmt, ...)  __attribute__ ((format (printf, 1, 2)));

#endif
