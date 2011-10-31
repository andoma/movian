/*
 *  Blob cache
 *  Copyright (C) 2010 Andreas Öman
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
 */


#ifndef BLOBCACHE_H__
#define BLOBCACHE_H__

void *blobcache_get(const char *key, const char *stash, size_t *sizep, int pad,
		    int *is_expired, char **etag, time_t *mtime);

int blobcache_get_meta(const char *key, const char *stash,
		       char **etag, time_t *mtime);

void blobcache_put(const char *key, const char *stash, const void *data,
		   size_t size, int maxage, const char *etag, time_t mtime);

void blobcache_init(void);

void blobcache_fini(void);

#endif // BLOBCACHE_H__
