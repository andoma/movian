#pragma once

struct buf;

void persistent_store_sync(void);

struct buf *persistent_load(const char *group, const char *key,
                            char *errbuf, size_t errlen);

void persistent_write(const char *group, const char *key,
                      const void *data, int len);

void persistent_remove(const char *group, const char *key);

