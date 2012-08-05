#pragma once

void kvstore_init(void);

void kvstore_fini(void);

void *kvstore_get(void);

void kvstore_close(void *db);

void kv_prop_bind_create(prop_t *p, const char *url);
