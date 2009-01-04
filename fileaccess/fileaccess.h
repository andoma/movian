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

#include <libhts/htsq.h>

TAILQ_HEAD(fa_dir_entry_queue, fa_dir_entry);

/**
 *
 */
typedef struct fa_dir_entry {
  TAILQ_ENTRY(fa_dir_entry) fde_link;
  char *fde_filename;
  char *fde_url;
  int fde_type; /* FA_ .. types above */
} fa_dir_entry_t;

/**
 *
 */
typedef struct fa_dir {
  struct fa_dir_entry_queue fd_entries;
  int fd_count;
} fa_dir_t;

fa_dir_t *fa_dir_alloc(void);

void fa_dir_free(fa_dir_t *fd);

void fa_dir_add(fa_dir_t *fd, const char *path, const char *name, int type);

void fa_dir_sort(fa_dir_t *fd);

/**
 *
 */
LIST_HEAD(fa_protocol_list, fa_protocol);
extern struct fa_protocol_list fileaccess_all_protocols;

/**
 * types
 */
#define FA_UNKNOWN  0
#define FA_DIR      1
#define FA_FILE     2
#define FA_ARCHIVE  3 /* Archive (a file, but we can dive into it) */
#define FA_AUDIO    4
#define FA_VIDEO    5
#define FA_PLAYLIST 6
#define FA_DVD      7
#define FA_IMAGE    8


typedef void (fa_scandir_callback_t)(void *arg, const char *url,
				     const char *filename, int type);

/**
 * File access protocol
 */
typedef struct fa_protocol {

  int fap_flags;
#define FAP_INCLUDE_PROTO_IN_URL 0x1

  void (*fap_init)(void);

  LIST_ENTRY(fa_protocol) fap_link;

  const char *fap_name;

  int (*fap_scan)(fa_dir_t *fd, const char *url);

  void *(*fap_open)(const char *url);
  void (*fap_close)(void *handle);
  int (*fap_read)(void *handle, void *buf, size_t size);
  off_t (*fap_seek)(void *handle, off_t pos, int whence);
  off_t (*fap_fsize)(void *handle);
  int   (*fap_stat)(const char *url, struct stat *buf);
} fa_protocol_t;


fa_dir_t *fileaccess_scandir(const char *url);

void fileaccess_init(void);

const char *fa_resolve_proto(const char *url, fa_protocol_t **p);

off_t fileaccess_size(const char *url);

#endif /* FILEACCESS_H */
