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

#ifndef FA_PROTO_H__
#define FA_PROTO_H__

#include "fileaccess.h"

/**
 * File access protocol
 */
typedef struct fa_protocol {

  int fap_flags;
#define FAP_INCLUDE_PROTO_IN_URL 0x1

  void (*fap_init)(void);

  LIST_ENTRY(fa_protocol) fap_link;

  const char *fap_name;

  int (*fap_scan)(nav_dir_t *nd, const char *url,
		  char *errbuf, size_t errsize);

  fa_handle_t *(*fap_open)(struct fa_protocol *fap, const char *url,
			   char *errbuf, size_t errsize);

  void (*fap_close)(fa_handle_t *fh);
  int (*fap_read)(fa_handle_t *fh, void *buf, size_t size);
  int64_t (*fap_seek)(fa_handle_t *fh, int64_t pos, int whence);
  int64_t (*fap_fsize)(fa_handle_t *fh);
  int (*fap_stat)(struct fa_protocol *fap, const char *url, struct stat *buf,
		  char *errbuf, size_t errsize);

  fa_handle_t *(*fap_reference)(struct fa_protocol *fap, const char *url);
  void (*fap_unreference)(fa_handle_t *fh);

} fa_protocol_t;


#endif /* FA_PROTO_H__ */
