/*
 *  File access, abstract interface for accessing files
 *  Copyright (C) 2008 Andreas Öman
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

#ifndef FILEACCESS_H
#define FILEACCESS_H

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "misc/queue.h"
#include "networking/http.h"
#include "metadata/metadata.h"
#include "navigator.h"
#include "misc/redblack.h"
#include "misc/buf.h"

typedef int (fa_load_cb_t)(void *opaque, int loaded, int total);

struct prop;
struct backend;
struct htsbuf_queue;
struct http_auth_req;

int fileaccess_init(void);


/**
 *
 */
RB_HEAD(fa_dir_entry_tree, fa_dir_entry);


/**
 *
 */
typedef struct fa_stat {
  int64_t fs_size; // -1 if unknown (a pipe)

  int fs_type; /* CONTENT_ .. types from showtime.h */

  time_t fs_mtime;

} fa_stat_t;

/**
 *
 */
typedef struct fa_dir_entry {
  RB_ENTRY(fa_dir_entry) fde_link;
  rstr_t *fde_filename;
  rstr_t *fde_url;
  int   fde_type; /* CONTENT_ .. types from showtime.h */
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

  struct metadata *fde_md;

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

#define FA_DEBUG           0x1
// #define FA_DUMP  0x2
#define FA_STREAMING       0x4
#define FA_CACHE           0x8
#define FA_BUFFERED_SMALL  0x10
#define FA_BUFFERED_BIG    0x20
#define FA_DISABLE_AUTH    0x40
#define FA_COMPRESSION     0x80
#define FA_NOFOLLOW        0x100
#define FA_FAST_FAIL       0x200
#define FA_WRITE           0x400  // Open for writing (always creates file)
#define FA_APPEND          0x800  /* Only if FA_WRITE:
                                     Seek to EOF when opening
                                     otherwise truncate */

/**
 *
 */
typedef struct fa_handle {
  const struct fa_protocol *fh_proto;
#ifdef FA_DUMP
  int fh_dump_fd;
#endif
} fa_handle_t;


/**
 *
 */
typedef enum {
  FA_NOTIFY_ADD,
  FA_NOTIFY_DEL,
  FA_NOTIFY_DIR_CHANGE,
} fa_notify_op_t;


fa_dir_t *fa_scandir(const char *url, char *errbuf, size_t errsize);
int fa_scandir2(fa_dir_t *fd, const char *url, char *errbuf, size_t errsize);

fa_dir_t *fa_get_parts(const char *url, char *errbuf, size_t errsize);

#define fa_open(u, e, es) fa_open_ex(u, e, es, 0, NULL)

void *fa_open_ex(const char *url, char *errbuf, size_t errsize, int flags,
		 struct prop *stats);
void *fa_open_vpaths(const char *url, const char **vpaths,
		     char *errbuf, size_t errsize, int flags);
void fa_close(void *fh);
int fa_read(void *fh, void *buf, size_t size);
int fa_write(void *fh, const void *buf, size_t size);
int64_t fa_seek(void *fh, int64_t pos, int whence);
int64_t fa_fsize(void *fh);
int fa_seek_is_fast(void *fh);
int fa_stat(const char *url, struct fa_stat *buf, char *errbuf, size_t errsize);
int fa_findfile(const char *path, const char *file, 
		char *fullpath, size_t fullpathlen);

int fa_can_handle(const char *url, char *errbuf, size_t errsize);

fa_handle_t *fa_reference(const char *url);
void fa_unreference(fa_handle_t *fh);

int fa_unlink_recursive(const char *url, char *errbuf, size_t errsize,
                        int verify);

int fa_unlink(const char *url, char *errbuf, size_t errsize);

int fa_rmdir(const char *url, char *errbuf, size_t errsize);

int fa_rename(const char *old, const char *new, char *errbuf, size_t errsize);

int fa_copy_from_fh(const char *to, fa_handle_t *src,
                    char *errbuf, size_t errsize);

int fa_copy(const char *to, const char *from, char *errbuf, size_t errsize);

int fa_makedirs(const char *url, char *errbuf, size_t errsize);

void fa_sanitize_filename(char *filename);

fa_handle_t *fa_notify_start(const char *url, void *opaque,
                             void (*change)(void *opaque,
                                            fa_notify_op_t op, 
                                            const char *filename,
                                            const char *url,
                                            int type));

void fa_notify_stop(fa_handle_t *fh);

void fa_ffmpeg_error_to_txt(int err, char *buf, size_t buflen);

void fa_scanner_page(const char *url, time_t mtime, 
                     prop_t *model, const char *playme,
                     prop_t *direct_close, rstr_t *title);

int fa_scanner_scan(const char *url, time_t mtime);

buf_t *fa_load(const char *url, const char **vpaths,
               char *errbuf, size_t errlen, int *cache_control, int flags,
               fa_load_cb_t *cb, void *opaque);

buf_t *fa_load_and_close(fa_handle_t *fh);

buf_t *fa_load_query(const char *url,
                     char *errbuf, size_t errlen, int *cache_control,
                     const char **arguments, int flags);

int fa_parent(char *dst, size_t dstlen, const char *url);

int fa_normalize(const char *url, char *dst, size_t dstlen);

rstr_t *fa_absolute_path(rstr_t *filename, rstr_t *at);

int fa_check_url(const char *url, char *errbuf, size_t errlen);

int fa_read_to_htsbuf(struct htsbuf_queue *hq, fa_handle_t *fh, int maxbytes);

void fa_pathjoin(char *dst, size_t dstlen, const char *p1, const char *p2);

void fa_url_get_last_component(char *dst, size_t dstlen, const char *url);

// Cache

void fa_cache_init(void);

fa_handle_t *fa_cache_open(const char *url, char *errbuf,
			   size_t errsize, int flags, struct prop *stats);

// Buffered I/O

fa_handle_t *fa_buffered_open(const char *url, char *errbuf, size_t errsize,
			      int flags, struct prop *stats);

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

// HTTP client

enum {
  HTTP_TAG_ARG = 1,
  HTTP_TAG_ARGINT,
  HTTP_TAG_ARGLIST,
  HTTP_TAG_RESULT_PTR,
  HTTP_TAG_ERRBUF,
  HTTP_TAG_POSTDATA,
  HTTP_TAG_FLAGS,
  HTTP_TAG_REQUEST_HEADER,
  HTTP_TAG_REQUEST_HEADERS,
  HTTP_TAG_RESPONSE_HEADERS,
  HTTP_TAG_METHOD,
  HTTP_TAG_PROGRESS_CALLBACK,
};


#define HTTP_ARG(a, b)                     HTTP_TAG_ARG, a, b
#define HTTP_ARGINT(a, b)                  HTTP_TAG_ARGINT, a, b
#define HTTP_ARGLIST(a)                    HTTP_TAG_ARGLIST, a
#define HTTP_RESULT_PTR(a)                 HTTP_TAG_RESULT_PTR, a
#define HTTP_ERRBUF(a, b)                  HTTP_TAG_ERRBUF, a, b
#define HTTP_POSTDATA(a, b)                HTTP_TAG_POSTDATA, a, b
#define HTTP_FLAGS(a)                      HTTP_TAG_FLAGS, a
#define HTTP_REQUEST_HEADER(a, b)          HTTP_TAG_REQUEST_HEADER, a, b
#define HTTP_REQUEST_HEADERS(a)            HTTP_TAG_REQUEST_HEADERS, a
#define HTTP_RESPONSE_HEADERS(a)           HTTP_TAG_RESPONSE_HEADERS, a
#define HTTP_METHOD(a)                     HTTP_TAG_METHOD, a
#define HTTP_PROGRESS_CALLBACK(a, b)       HTTP_TAG_PROGRESS_CALLBACK, a, b

int http_req(const char *url, ...);

int http_client_oauth(struct http_auth_req *har,
		      const char *consumer_key,
		      const char *consumer_secret,
		      const char *token,
		      const char *token_secret);

int http_client_rawauth(struct http_auth_req *har, const char *str);

void http_client_set_header(struct http_auth_req *har, const char *key,
			    const char *value);

void http_client_fail_req(struct http_auth_req *har, const char *reason);

#endif /* FILEACCESS_H */

