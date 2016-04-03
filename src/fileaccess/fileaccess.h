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
#ifndef FILEACCESS_H
#define FILEACCESS_H

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>

#include "misc/queue.h"
#include "networking/http.h"
#include "metadata/metadata.h"
#include "navigator.h"
#include "misc/redblack.h"
#include "misc/buf.h"
#include "misc/cancellable.h"
#include "fa_defs.h"

typedef void (fa_load_cb_t)(void *opaque, int loaded, int total);

struct prop;
struct backend;
struct htsbuf_queue;

int fileaccess_init(void);


/**
 *
 */
RB_HEAD(fa_dir_entry_tree, fa_dir_entry);


/**
 *
 */
typedef struct fa_fsinfo {
  uint64_t ffi_size;
  uint64_t ffi_avail;
} fa_fsinfo_t;

/**
 *
 */
typedef struct fa_stat {
  int64_t fs_size; // -1 if unknown (a pipe)

  int fs_type; /* CONTENT_ .. types from metadata.h */

  time_t fs_mtime;

} fa_stat_t;

/**
 *
 */
typedef struct fa_dir_entry {
  RB_ENTRY(fa_dir_entry) fde_link;
  rstr_t *fde_filename;
  rstr_t *fde_url;
  int   fde_type; /* CONTENT_ .. types from metadata.h */
  struct prop *fde_prop;
  struct prop *fde_metadata;

  enum {
    FDE_PROBED_NONE,
    FDE_PROBED_FILENAME,
    FDE_PROBED_CONTENTS,

  } fde_probestatus;

  char fde_statdone;
  char fde_ignore_cache;
  char fde_bound_to_metadb;
  struct fa_stat fde_stat;

#if ENABLE_METADATA
  struct metadata *fde_md;
#endif

} fa_dir_entry_t;

/**
 *
 */
typedef struct fa_dir {
  struct fa_dir_entry_tree fd_entries;
  int fd_count;
} fa_dir_t;

fa_dir_t *fa_dir_alloc(void);

void fa_dir_free(fa_dir_t *fd);

fa_dir_entry_t *fa_dir_add(fa_dir_t *fd, const char *path, const char *name, int type);

fa_dir_entry_t *fa_dir_find(const fa_dir_t *fd, rstr_t *url);

void fa_dir_entry_free(fa_dir_t *fd, fa_dir_entry_t *fde);

int fa_dir_entry_stat(fa_dir_entry_t *fde);

void fa_dir_insert(fa_dir_t *fd, fa_dir_entry_t *fde);

void fa_dir_remove(fa_dir_t *fd, fa_dir_entry_t *fde);

void fa_dir_print(fa_dir_t *fd);

/**
 *
 */
LIST_HEAD(fa_protocol_list, fa_protocol);


/**
 *
 */
typedef struct fa_handle {
  const struct fa_protocol *fh_proto;
#ifdef FA_DUMP
  int fh_dump_fd;
#endif
} fa_handle_t;


struct http_header_list;

/**
 *
 */
typedef struct fa_open_extra {
  const struct http_header_list *foe_request_headers;
  struct http_header_list *foe_response_headers;
  struct prop *foe_stats;
  struct cancellable *foe_cancellable;
  int foe_open_timeout; // In ms
  int foe_protocol_error; // Protocol error (HTTP can set 404 here, etc)
} fa_open_extra_t;


/**
 *
 */
typedef struct fa_resolver {
  struct fa_resolver *far_next;
  void (*far_cleanup)(struct fa_resolver *far);
  const char *(*far_vpath)(struct fa_resolver *far, const char *proto);

  buf_t *(*far_load)(struct fa_resolver *far, const char *url,
                      char *errbuf, size_t errlen);

  atomic_t far_refcount;

} fa_resolver_t;


void far_release(fa_resolver_t *far);

fa_resolver_t *far_retain(fa_resolver_t *far) attribute_unused_result;


/**
 *
 */
typedef enum {
  FA_NOTIFY_ADD,
  FA_NOTIFY_DEL,
  FA_NOTIFY_DIR_CHANGE,
} fa_notify_op_t;


/**
 *
 */
typedef enum {
  FAP_OK                =   0,
  FAP_ERROR             =  -1,
  FAP_NEED_AUTH         =  -2,
  FAP_NOT_SUPPORTED     =  -3,
  FAP_PERMISSION_DENIED =  -4,
  FAP_NOENT             =  -5,
  FAP_EXIST             =  -6,
} fa_err_code_t;


fa_dir_t *fa_scandir(const char *url, char *errbuf, size_t errsize);
int fa_scandir2(fa_dir_t *fd, const char *url, char *errbuf, size_t errsize,
                int flags);

fa_dir_t *fa_get_parts(const char *url, char *errbuf, size_t errsize);

#define fa_open(u, e, es) fa_open_ex(u, e, es, 0, NULL)

void *fa_open_ex(const char *url, char *errbuf, size_t errsize, int flags,
		 struct fa_open_extra *foe);

void *fa_open_resolver(const char *url, fa_resolver_t *far,
		     char *errbuf, size_t errsize, int flags,
                     struct fa_open_extra *foe);

void fa_close(void *fh);
void fa_close_with_park(fa_handle_t *fh, int park);
int fa_read(void *fh, void *buf, size_t size);
void fa_deadline(void *fh_, int deadline);
int fa_write(void *fh, const void *buf, size_t size);

int64_t fa_seek4(void *fh, int64_t pos, int whence, int lazy);

#define fa_seek(fh, pos, whence) fa_seek4(fh, pos, whence, 0)

#define fa_seek_lazy(fh, pos, whence) fa_seek4(fh, pos, whence, 1)

int64_t fa_fsize(void *fh);
int fa_ftruncate(void *fh, uint64_t newsize);

int fa_stat_ex(const char *url, struct fa_stat *buf, char *errbuf,
               size_t errsize, int flags);

#define fa_stat(a, b, c, d) fa_stat_ex(a, b, c, d, 0)

int fa_findfile(const char *path, const char *file, 
		char *fullpath, size_t fullpathlen);
void fa_set_read_timeout(void *fh_, int ms);

int fa_can_handle(const char *url, char *errbuf, size_t errsize);

fa_handle_t *fa_reference(const char *url);
void fa_unreference(fa_handle_t *fh);

int fa_unlink_recursive(const char *url, char *errbuf, size_t errsize,
                        int verify);

int fa_unlink(const char *url, char *errbuf, size_t errsize);

int fa_rmdir(const char *url, char *errbuf, size_t errsize);

int fa_rename(const char *old, const char *new, char *errbuf, size_t errsize);

fa_err_code_t fa_set_xattr(const char *url, const char *name,
                           const void *data, size_t len);

fa_err_code_t fa_get_xattr(const char *url, const char *name,
                           void **datap, size_t *lenp);

fa_err_code_t fa_fsinfo(const char *url, fa_fsinfo_t *ffi);

int fa_copy_from_fh(const char *to, fa_handle_t *src,
                    char *errbuf, size_t errsize);

int fa_copy(const char *to, const char *from, char *errbuf, size_t errsize);

int fa_makedirs(const char *url, char *errbuf, size_t errsize);

fa_err_code_t fa_makedir(const char *url);

void fa_sanitize_filename(char *filename);

fa_handle_t *fa_notify_start(const char *url, void *opaque,
                             void (*change)(void *opaque,
                                            fa_notify_op_t op, 
                                            const char *filename,
                                            const char *url,
                                            int type));

void fa_notify_stop(fa_handle_t *fh);

void fa_libav_error_to_txt(int err, char *buf, size_t buflen);

void fa_scanner_page(const char *url, time_t mtime, 
                     prop_t *model, const char *playme,
                     prop_t *direct_close, rstr_t *title);

int fa_scanner_scan(const char *url, time_t mtime);

/**
 *
 * A word about cache control
 *
 * NULL          - Normal transparent caching (With expiry, etc)
 *
 * ptr to an int - If it's pointing to an int the load will stop if
 *                 there is an entry in the cache and it is
 *                 expired. In this case the pointed to int will be
 *                 set to 1.
 *
 * DISABLE_CACHE - No cache operations at all
 *
 * BYPASS_CACHE - Don't read from cache. But use cache metadata when
 *                loading object from network. This is used to refresh
 *                objects we already know are expired. Typically
 *                used by image loaders to refresh already displayed
 *                expired images.
 */

enum {
  FA_LOAD_TAG_ERRBUF = 1,
  FA_LOAD_TAG_CACHE_CONTROL,
  FA_LOAD_TAG_FLAGS,
  FA_LOAD_TAG_PROGRESS_CALLBACK,
  FA_LOAD_TAG_CANCELLABLE,
  FA_LOAD_TAG_RESOLVER,
  FA_LOAD_TAG_QUERY_ARG,
  FA_LOAD_TAG_QUERY_ARGVEC,
  FA_LOAD_TAG_MIN_EXPIRE,
  FA_LOAD_TAG_REQUEST_HEADERS,
  FA_LOAD_TAG_RESPONSE_HEADERS,
  FA_LOAD_TAG_LOCATION,
  FA_LOAD_TAG_CACHE_INFO,
  FA_LOAD_TAG_PROTOCOL_CODE, // HTTP code (400, 403, etc)
};

#define FA_LOAD_ERRBUF(a, b)            FA_LOAD_TAG_ERRBUF, a, b
#define FA_LOAD_CACHE_CONTROL(a)        FA_LOAD_TAG_CACHE_CONTROL, a
#define FA_LOAD_FLAGS(a)                FA_LOAD_TAG_FLAGS, a
#define FA_LOAD_PROGRESS_CALLBACK(a, b) FA_LOAD_TAG_PROGRESS_CALLBACK, a, b
#define FA_LOAD_CANCELLABLE(a)          FA_LOAD_TAG_CANCELLABLE, a
#define FA_LOAD_RESOLVER(a)             FA_LOAD_TAG_RESOLVER, a
#define FA_LOAD_QUERY_ARG(a, b)         FA_LOAD_TAG_QUERY_ARG, a, b
#define FA_LOAD_QUERY_ARGVEC(a)         FA_LOAD_TAG_QUERY_ARGVEC, a
#define FA_LOAD_MIN_EXPIRE(a)           FA_LOAD_TAG_MIN_EXPIRE, a
#define FA_LOAD_REQUEST_HEADERS(a)      FA_LOAD_TAG_REQUEST_HEADERS, a
#define FA_LOAD_RESPONSE_HEADERS(a)     FA_LOAD_TAG_RESPONSE_HEADERS, a
#define FA_LOAD_LOCATION(a)             FA_LOAD_TAG_LOCATION, a
#define FA_LOAD_CACHE_INFO(a)           FA_LOAD_TAG_CACHE_INFO, a
#define FA_LOAD_PROTOCOL_CODE(a)        FA_LOAD_TAG_PROTOCOL_CODE, a

#define FA_CACHE_INFO_FROM_CACHE              1
#define FA_CACHE_INFO_FROM_CACHE_NOT_MODIFIED 2
#define FA_CACHE_INFO_EXPIRED_FROM_CACHE      3

buf_t *fa_load(const char *url, ...) attribute_null_sentinel;

buf_t *fa_load_and_close(fa_handle_t *fh);

int fa_parent(char *dst, size_t dstlen, const char *url)
  attribute_unused_result;

int fa_normalize(const char *url, char *dst, size_t dstlen);

rstr_t *fa_absolute_path(rstr_t *filename, rstr_t *at);

rstr_t *fa_get_title(const char *url);

int fa_check_url(const char *url, char *errbuf, size_t errlen, int timeout_ms);

int fa_read_to_htsbuf(struct htsbuf_queue *hq, fa_handle_t *fh, int maxbytes);

void fa_pathjoin(char *dst, size_t dstlen, const char *p1, const char *p2);

void fa_url_get_last_component(char *dst, size_t dstlen, const char *url);

// Buffered I/O

fa_handle_t *fa_buffered_open(const char *url, char *errbuf, size_t errsize,
			      int flags, struct fa_open_extra *foe);

// Memory backed files

int memfile_register(const void *data, size_t len);

void memfile_unregister(int id);

fa_handle_t *memfile_make(const void *mem, size_t len);

// Expose part of a file as a new file, fa is owned by the slicer
// so you must never touch it again

fa_handle_t *fa_slice_open(fa_handle_t *fa, int64_t offset, int64_t size);

// fa to FILE wrapper

FILE *fa_fopen(fa_handle_t *fh, int doclose);

fa_handle_t *fa_aescbc_open(fa_handle_t *fa, const uint8_t *iv,
			    const uint8_t *key);


// Bandwidth limiter

fa_handle_t *fa_bwlimit_open(fa_handle_t *fa, int bps);

// Compare reader

fa_handle_t *fa_cmp_open(fa_handle_t *fa, const char *locafile);

#endif /* FILEACCESS_H */

