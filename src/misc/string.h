#ifndef STRING_H__
#define STRING_H__

#include <stdlib.h>
#include <stdint.h>

void http_deescape(char *s);

void path_escape(char *dest, int size, const char *src);

void html_entities_decode(char *s);

int html_entity_lookup(const char *name);

void 
url_split(char *proto, int proto_size,
	  char *authorization, int authorization_size,
	  char *hostname, int hostname_size,
	  int *port_ptr,
	  char *path, int path_size,
	  const char *url, int escape_path);

int dictcmp(const char *a, const char *b);

int utf8_get(const char **s);

int utf8_verify(const char *str);

int utf8_put(char *out, int c);

const char *mystrstr(const char *haystack, const char *needle);

void strvec_addp(char ***str, const char *v);

void strvec_addpn(char ***str, const char *v, size_t len);

char **strvec_split(const char *str, char ch);

void strvec_free(char **s);

int hex2bin(uint8_t *buf, size_t buflen, const char *str);

void unicode_init(void);

char *utf8_from_ISO_8859_1(const char *str, int len);

char *url_resolve_relative(const char *proto, const char *hostname, int port,
			   const char *path, const char *ref);

#endif
