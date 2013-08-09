#pragma once

void kvstore_init(void);

void kvstore_fini(void);

void *kvstore_get(void);

void kvstore_close(void *db);

void kv_prop_bind_create(prop_t *p, const char *url);

// Direct access

#define KVSTORE_DOMAIN_SYS     1
#define KVSTORE_DOMAIN_PROP    2
#define KVSTORE_DOMAIN_PLUGIN  3
#define KVSTORE_DOMAIN_SETTING 4

rstr_t *kv_url_opt_get_rstr(const char *url, int domain, const char *key);

int kv_url_opt_get_int(const char *url, int domain, const char *key, int def);

#define KVSTORE_SET_STRING 1
#define KVSTORE_SET_INT    2
#define KVSTORE_SET_VOID   3

void kv_url_opt_set(const char *url, int domain, const char *key,
		    int type, ...);


void kv_url_opt_set_deferred(const char *url, int domain, const char *key,
                             int type, ...);

void kvstore_deferred_flush(void);
