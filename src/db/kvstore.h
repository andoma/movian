#pragma once

void kvstore_init(void);

void kvstore_fini(void);

void *kvstore_get(void);

void kvstore_close(void *db);

void kv_prop_bind_create(prop_t *p, const char *url);

rstr_t *kv_url_opt_get_rstr(const char *url, int domain, const char *key);

void kv_url_opt_set_str(const char *url, int domain, const char *key,
			const char *value);

#define KVSTORE_PAGE_DOMAIN_SYS  1
#define KVSTORE_PAGE_DOMAIN_PROP 2

