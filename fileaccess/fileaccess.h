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

LIST_HEAD(fa_protocol_list, fa_protocol);
extern struct fa_protocol_list fileaccess_all_protocols;

/**
 * types
 */
#define FA_DIR  1
#define FA_FILE 2

typedef void (fa_scandir_callback_t)(void *arg, const char *url,
				     const char *filename, int type);

/**
 * File access protocol
 */
typedef struct fa_protocol {

  LIST_ENTRY(fa_protocol) fap_link;

  const char *fap_name;

  int (*fap_scan)(const char *url, fa_scandir_callback_t *cb, void *arg);

#if 0
  browser_stream_t *(*bp_open)(const char *url);
  void (*bp_close)(browser_stream_t *bs);
  int (*bp_read)(browser_stream_t *bs, void *buf, size_t size);
  offset_t (*bp_seek)(browser_stream_t *bs, offset_t pos, int whence);
#endif

} fa_protocol_t;


int fileaccess_scandir(const char *url, fa_scandir_callback_t *cb, void *arg);

void fileaccess_init(void);

#endif /* FILEACCESS_H */
