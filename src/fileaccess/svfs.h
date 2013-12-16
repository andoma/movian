/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
#ifndef SVFS_H__
#define SVFS_H__

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>

struct svfs_ops {
  void *(*open)(const char *url);
  void (*close)(void *handle);
  int (*read)(void *handle, void *buf, size_t size);
  int64_t (*seek)(void *handle, int64_t pos, int whence);
  int (*stat)(const char *url, struct stat *buf);
  int (*findfile)(const char *path, const char *file, 
		  char *fullpath, size_t fullpathlen);
};

#endif /* SVFS_H__ */
