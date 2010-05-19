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

#include "navigator.h"

int fileaccess_init(void);


/**
 *
 */
TAILQ_HEAD(fa_dir_entry_queue, fa_dir_entry);

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

  int fde_statdone;
  struct stat fde_stat;
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

void fa_dir_sort(fa_dir_t *nd);

fa_dir_entry_t *fa_dir_insert(fa_dir_t *fd, const char *url,
			      const char *filename, int type);

int fa_dir_entry_stat(fa_dir_entry_t *fde);

/**
 *
 */
LIST_HEAD(fa_protocol_list, fa_protocol);
extern struct fa_protocol_list fileaccess_all_protocols;


/**
 *
 */
typedef struct fa_handle {
  const struct fa_protocol *fh_proto;
} fa_handle_t;


/**
 *
 */
typedef enum {
  FA_NOTIFY_ADD,
  FA_NOTIFY_DEL,
} fa_notify_op_t;


fa_dir_t *fa_scandir(const char *url, char *errbuf, size_t errsize);

#define FA_SCAN_ARCHIVES 0x1

fa_dir_t *fa_scandir_recursive(const char *url, char *errbuf, size_t errsize,
			       int flags);

void *fa_open(const char *url, char *errbuf, size_t errsize);
void *fa_open_theme(const char *url, const char *themepath);
void fa_close(void *fh);
int fa_read(void *fh, void *buf, size_t size);
int64_t fa_seek(void *fh, int64_t pos, int whence);
int64_t fa_fsize(void *fh);
int fa_stat(const char *url, struct stat *buf, char *errbuf, size_t errsize);
int fa_findfile(const char *path, const char *file, 
		char *fullpath, size_t fullpathlen);

int fa_can_handle(const char *url, char *errbuf, size_t errsize);

void *fa_reference(const char *url);
void fa_unreference(void *fh);

int fa_notify(const char *url, void *opaque,
	      void (*change)(void *opaque,
			     fa_notify_op_t op, 
			     const char *filename,
			     const char *url,
			     int type),
	      int (*breakcheck)(void *opaque));

const char *fa_ffmpeg_error_to_txt(int err);

void fa_scanner(const char *url, prop_t *source, prop_t *view, const char *playme);

void fa_scanner_find_albumart(const char *url, prop_t *album_art);

void *fa_quickload(const char *filename, size_t *sizeptr, const char *theme,
		   char *errbuf, size_t errlen);

int fa_parent(char *dst, size_t dstlen, const char *url);

int fa_normalize(const char *url, char *dst, size_t dstlen);

struct htsbuf_queue;
int http_request(const char *url, const char **arguments, 
		 char **result, size_t *result_sizep,
		 char *errbuf, size_t errlen,
		 struct htsbuf_queue *postdata, const char *postcontenttype);

#include <libavformat/avio.h>

int fa_lavf_reopen(ByteIOContext **p, fa_handle_t *fa);


#endif /* FILEACCESS_H */
