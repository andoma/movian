#ifndef STRING_H__
#define STRING_H__

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

double strtod_ex(const char *s, char decimal_point_char, char **ep);

int dictcmp(const char *a, const char *b);

int utf8_get(const char **s);

const char *mystrstr(const char *haystack, const char *needle);

char **strvec_split(const char *str, char ch);

void strvec_free(char **s);

#endif
