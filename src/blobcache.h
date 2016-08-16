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
#ifndef BLOBCACHE_H__
#define BLOBCACHE_H__

#include "misc/buf.h"

buf_t *blobcache_get(const char *key, const char *stash, int pad,
		    int *is_expired, char **etag, time_t *mtime);

int blobcache_get_meta(const char *key, const char *stash,
		       char **etag, time_t *mtime);

int blobcache_put(const char *key, const char *stash, buf_t *buf,
		  int maxage, const char *etag, time_t mtime,
                  int flags);

void blobcache_evict(const char *key, const char *stash);

#define BLOBCACHE_IMPORTANT_ITEM 0x1

void blobcache_init(void);

void blobcache_fini(void);

#endif // BLOBCACHE_H__
