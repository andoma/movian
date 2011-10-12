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
#include <sys/types.h>
#include <sys/stat.h>

#include "misc/queue.h"
#include "networking/http.h"
#include "metadata.h"
#include "navigator.h"

struct prop;

#define FA_LOCALFILES_ICON "bundle://resources/fileaccess/fs_icon.png"

int fileaccess_init(void);


/**
 *
 */
TAILQ_HEAD(fa_dir_entry_queue, fa_dir_entry);


/**
 *
 */
typedef struct fa_stat {
  int64_t fs_size; // -1 if unknown (a pipe)

  int fs_type; /* CONTENT_ .. types from showtime.h */

  time_t fs_mtime;

  int fs_cache_age;

  uint8_t fs_mimetype[32];
  
} fa_stat_t;

/**
 *
 */
typedef struct fa_dir_entry {
  TAILQ_ENTRY(fa_dir_entry) fde_link;
  char *fde_filename;
  char *fde_url;
  int   fde_type; /* CONTENT_ .. types from showtime.h */
  struct prop *fde_prop;
  struct prop *fde_metadata;

  enum {
    FDE_PROBE_NONE,
    FDE_PROBE_FILENAME,
    FDE_PROBE_DEEP,

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
  struct fa_dir_entry_queue fd_entries;
  int fd_count;
} fa_dir_t;

fa_dir_t *fa_dir_alloc(void);

void fa_dir_free(fa_dir_t *nd);

fa_dir_entry_t *fa_dir_add(fa_dir_t *nd, const char *path, const char *name, int type);

void fa_dir_entry_free(fa_dir_t *fd, fa_dir_entry_t *fde);

int fa_dir_entry_stat(fa_dir_entry_t *fde);

/**
 *
 */
LIST_HEAD(fa_protocol_list, fa_protocol);
extern struct fa_protocol_list fileaccess_all_protocols;


#define FA_DEBUG 0x1
// #define FA_DUMP  0x2
#define FA_STREAMING 0x4
#define FA_CACHE     0x8
#define FA_BUFFERED  0x10

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
} fa_notify_op_t;


fa_dir_t *fa_scandir(const char *url, char *errbuf, size_t errsize);

#define fa_open(u, e, es) fa_open_ex(u, e, es, 0, NULL)

void *fa_open_ex(const char *url, char *errbuf, size_t errsize, int flags,
		 struct prop *stats);
void *fa_open_vpaths(const char *url, const char **vpaths,
		     char *errbuf, size_t errsize, int flags);
void fa_close(void *fh);
int fa_read(void *fh, void *buf, size_t size);
int64_t fa_seek(void *fh, int64_t pos, int whence);
int64_t fa_fsize(void *fh);
int fa_seek_is_fast(void *fh);
int fa_stat(const char *url, struct fa_stat *buf, char *errbuf, size_t errsize);
int fa_findfile(const char *path, const char *file, 
		char *fullpath, size_t fullpathlen);

int fa_can_handle(const char *url, char *errbuf, size_t errsize);

fa_handle_t *fa_reference(const char *url);
void fa_unreference(fa_handle_t *fh);

int fa_notify(const char *url, void *opaque,
	      void (*change)(void *opaque,
			     fa_notify_op_t op, 
			     const char *filename,
			     const char *url,
			     int type),
	      int (*breakcheck)(void *opaque));

void fa_ffmpeg_error_to_txt(int err, char *buf, size_t buflen);

void fa_scanner(const char *url, time_t mtime, 
		prop_t *model, const char *playme);

void *fa_quickload(const char *url, struct fa_stat *fs, const char **vpaths,
		   char *errbuf, size_t errlen);

uint8_t *fa_load_and_close(fa_handle_t *fh, size_t *sizep);

int fa_parent(char *dst, size_t dstlen, const char *url);

struct backend;

int fa_normalize(const char *url, char *dst, size_t dstlen);

int fa_check_url(const char *url, char *errbuf, size_t errlen);

struct htsbuf_queue;

#define HTTP_DISABLE_AUTH  0x1
#define HTTP_REQUEST_DEBUG 0x2

int http_request(const char *url, const char **arguments, 
		 char **result, size_t *result_sizep,
		 char *errbuf, size_t errlen,
		 struct htsbuf_queue *postdata, const char *postcontenttype,
		 int flags, struct http_header_list *headers_out,
		 const struct http_header_list *headers_in, const char *method);

struct http_auth_req;
int http_client_oauth(struct http_auth_req *har,
		      const char *consumer_key,
		      const char *consumer_secret,
		      const char *token,
		      const char *token_secret);

int http_client_rawauth(struct http_auth_req *har, const char *str);

void http_client_set_header(struct http_auth_req *har, const char *key,
			    const char *value);

void fa_pathjoin(char *dst, size_t dstlen, const char *p1, const char *p2);

void fa_url_get_last_component(char *dst, size_t dstlen, const char *url);

// Cache

void fa_cache_init(void);

fa_handle_t *fa_cache_open(const char *url, char *errbuf,
			   size_t errsize, int flags, struct prop *stats);

// Buffered I/O

fa_handle_t *fa_buffered_open(const char *url, char *errbuf, size_t errsize,
			      int flags, struct prop *stats);


#endif /* FILEACCESS_H */
