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

#include <queue.h>

#include "navigator.h"

void fileaccess_init(void);

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



nav_dir_t *fa_scandir(const char *url, char *errbuf, size_t errsize);
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

#endif /* FILEACCESS_H */
