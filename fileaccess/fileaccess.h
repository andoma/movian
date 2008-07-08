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

#include <sys/stat.h>

LIST_HEAD(fa_protocol_list, fa_protocol);
extern struct fa_protocol_list fileaccess_all_protocols;

/**
 * types
 */
#define FA_DIR  1
#define FA_FILE 2
#define FA_NONE 3 /* Entry should be hidden */

typedef void (fa_scandir_callback_t)(void *arg, const char *url,
				     const char *filename, int type);

/**
 * File access protocol
 */
typedef struct fa_protocol {

  LIST_ENTRY(fa_protocol) fap_link;

  const char *fap_name;

  int (*fap_scan)(const char *url, fa_scandir_callback_t *cb, void *arg);

  void *(*fap_open)(const char *url);
  void (*fap_close)(void *handle);
  int (*fap_read)(void *handle, void *buf, size_t size);
  off_t (*fap_seek)(void *handle, off_t pos, int whence);
  off_t (*fap_fsize)(void *handle);
  int   (*fap_stat)(const char *url, struct stat *buf);
} fa_protocol_t;


int fileaccess_scandir(const char *url, fa_scandir_callback_t *cb, void *arg);

void fileaccess_init(void);

const char *fa_resolve_proto(const char *url, fa_protocol_t **p);

off_t fileaccess_size(const char *url);

/**
 * POSIX style interface for showtime vfs
 */

int fa_posix_open(const char *filename, int flags, ...);
ssize_t fa_posix_read(int fd, void *buf, size_t size);
off_t fa_posix_seek(int fd, off_t pos, int whence);
int fa_posix_close(int fd);
int fa_posix_stat(const char *filename, struct stat *buf);
off_t fa_posix_filesize(int fd);

#endif /* FILEACCESS_H */
