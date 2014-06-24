
#pragma once

#include "htsmsg/htsmsg.h"

typedef void (bencode_pase_cb_t)(void *opaque, const char *name,
                                 const void *start, size_t len);


htsmsg_t *bencode_deserialize(const char *src, const char *stop,
                              char *errbuf, size_t errlen,
                              bencode_pase_cb_t *cb, void *opaque);

buf_t *bencode_serialize(htsmsg_t *src);
