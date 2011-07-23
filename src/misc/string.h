#ifndef STRING_H__
#define STRING_H__

#include <stdlib.h>
#include <stdint.h>

void url_deescape(char *s);

#define URL_ESCAPE_PATH   1
#define URL_ESCAPE_PARAM  2

int url_escape(char *dest, const int size, const char *src, int how);

void html_entities_decode(char *s);

int html_entity_lookup(const char *name);

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

const char *mystrstr(const char *haystack, const char *needle);

void strvec_addp(char ***str, const char *v);

void strvec_addpn(char ***str, const char *v, size_t len);

char **strvec_split(const char *str, char ch);

void strvec_free(char **s);

void strappend(char **strp, const char *src);

int hex2bin(uint8_t *buf, size_t buflen, const char *str);

void unicode_init(void);

char *url_resolve_relative(const char *proto, const char *hostname, int port,
			   const char *path, const char *ref);

char *url_resolve_relative_from_base(const char *base, const char *url);


// ISO-8859-X  ->  UTF-8

#define ISO_8859_1 NULL
extern const uint16_t ISO_8859_2[];
extern const uint16_t ISO_8859_3[];
extern const uint16_t ISO_8859_4[];
extern const uint16_t ISO_8859_5[];
extern const uint16_t ISO_8859_6[];
extern const uint16_t ISO_8859_7[];
extern const uint16_t ISO_8859_8[];
extern const uint16_t ISO_8859_9[];
extern const uint16_t ISO_8859_10[];
extern const uint16_t ISO_8859_11[];
extern const uint16_t ISO_8859_13[];
extern const uint16_t ISO_8859_14[];
extern const uint16_t ISO_8859_15[];
extern const uint16_t ISO_8859_16[];
extern const uint16_t CP1250[];
extern const uint16_t CP1251[];

char *utf8_from_bytes(const char *str, int len, const uint16_t *table);

int hexnibble(char c);


#endif
