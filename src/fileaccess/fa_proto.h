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
#ifndef FA_PROTO_H__
#define FA_PROTO_H__

#include "fileaccess.h"


/**
 * File access protocol
 */
typedef struct fa_protocol {

  int fap_flags;
#define FAP_INCLUDE_PROTO_IN_URL 0x1
#define FAP_ALLOW_CACHE          0x2

  atomic_t fap_refcount;

  void (*fap_init)(void);

  void (*fap_fini)(struct fa_protocol *fap);

  LIST_ENTRY(fa_protocol) fap_link;

  const char *fap_name;

  /**
   * If set, it will superseed fap_name. Return 0 for match
   */
  int (*fap_match_proto)(const char *prefix);

  /**
   * If set, it will superseed fap_match_proto and fap_name. Return 0 for match
   */
  int (*fap_match_uri)(const char *uri);

  /**
   * Directory scan url for files.
   */
  int (*fap_scan)(struct fa_protocol *fap, fa_dir_t *fa, const char *url,
		  char *errbuf, size_t errsize, int flags);

  /**
   * Open url for reading
   */
  fa_handle_t *(*fap_open)(struct fa_protocol *fap, const char *url,
			   char *errbuf, size_t errsize, int flags,
                           fa_open_extra_t *foe);

  /**
   * Close filehandle
   */
  void (*fap_close)(fa_handle_t *fh);

  /**
   * Park filehandle, used by fa_buffer to park file handle which can
   * later be reused
   */
  void (*fap_park)(fa_handle_t *fh);

  /**
   * Read from file. Same semantics as POSIX read(2)
   */
  int (*fap_read)(fa_handle_t *fh, void *buf, size_t size);

  /**
   * Read from file. Same semantics as POSIX write(2)
   */
  int (*fap_write)(fa_handle_t *fh, const void *buf, size_t size);

  /**
   * Seek in file. Same semantics as POSIX lseek(2)
   */
  int64_t (*fap_seek)(fa_handle_t *fh, int64_t pos, int whence, int lazy);

  /**
   * Return size of file
   */
  int64_t (*fap_fsize)(fa_handle_t *fh);

  /**
   * Truncate file
   */
  fa_err_code_t (*fap_ftruncate)(fa_handle_t *fh, uint64_t newsize);

  /**
   * stat(2) file
   */
  int (*fap_stat)(struct fa_protocol *fap, const char *url, struct fa_stat *buf,
		  int flags, char *errbuf, size_t errsize);

  /**
   * unlink (ie, delete) file
   */
  int (*fap_unlink)(const struct fa_protocol *fap, const char *url,
                    char *errbuf, size_t errsize);

  /**
   * delete directory
   */
  int (*fap_rmdir)(const struct fa_protocol *fap, const char *url,
                   char *errbuf, size_t errsize);


  /**
   * Returns -2 if not on same FS
   */
  int (*fap_rename)(const struct fa_protocol *fap, const char *old,
                    const char *new, char *errbuf, size_t errsize);

  /**
   * Add a reference to the url.
   *
   * Let underlying protocols know that we probably want to read more info
   * about the url in the future. Mostly useful for archive wrappers that
   * might want to keep the archive indexed in memory
   *
   * Optional
   */
  fa_handle_t *(*fap_reference)(struct fa_protocol *fap, const char *url);

  /**
   * Release a reference obtained by fap_reference()
   */
  void (*fap_unreference)(fa_handle_t *fh);

  /**
   * Monitor the filesystem directory described by url for changes
   *
   * If a change occures, change() is invoked
   */
  fa_handle_t *(*fap_notify_start)(struct fa_protocol *fap, const char *url,
                                   void *opaque,
                                   void (*change)(void *opaque,
                                                  fa_notify_op_t op,
                                                  const char *filename,
                                                  const char *url,
                                                  int type));

  void (*fap_notify_stop)(fa_handle_t *fh);

  /**
   * Load the 'url' into memory
   */
  buf_t *(*fap_load)(struct fa_protocol *fap, const char *url,
                     char *errbuf, size_t errlen,
                     char **etag, time_t *mtime, int *max_age,
                     int flags, fa_load_cb_t *cb, void *opaque,
                     cancellable_t *c,
                     struct http_header_list *request_headers,
                     struct http_header_list *response_headers,
                     char **location, int *protocol_code);

  /**
   * Normalize the given URL.
   *
   * Remove any relative components and symlinks, etc
   */
  int (*fap_normalize)(struct fa_protocol *fap, const char *url,
		       char *dst, size_t dstlen);
  
  /**
   * Extract the last component of the URL (ie. the filename)
   */
  void (*fap_get_last_component)(struct fa_protocol *fap, const char *url,
				 char *dst, size_t dstlen);

  /**
   * Return all parts that relates to the given URL
   *
   * For RAR archives this would be all part-files.
   *
   * This is used, for example, to to delete all .rXX files when
   * deleting the original .rar file.
   *
   */
  int (*fap_get_parts)(fa_dir_t *fa, const char *url,
		       char *errbuf, size_t errsize);

  /**
   * Make directory
   */
  fa_err_code_t (*fap_makedir)(struct fa_protocol *fap, const char *url);


  /**
   * Set read timeout
   *
   * If a read cannot be satisfied within this time, we return error
   */
  void (*fap_set_read_timeout)(fa_handle_t *fh, int ms);

  /**
   * Set extended attribute
   */
  fa_err_code_t (*fap_set_xattr)(struct fa_protocol *fap, const char *url,
                                 const char *name,
                                 const void *data, size_t len);

  /**
   * Get extended attribute
   */
  fa_err_code_t (*fap_get_xattr)(struct fa_protocol *fap, const char *url,
                                 const char *name,
                                 void **datap, size_t *lenp);

  /**
   * Set a deadline for when we expect the next read to complete
   *
   * deadline is delta time un µs
   */
  void (*fap_deadline)(fa_handle_t *fh, int deadline);

  /**
   * Return file system info
   */
  fa_err_code_t (*fap_fsinfo)(struct fa_protocol *fap, const char *url,
                              fa_fsinfo_t *ffi);

  /**
   * Return nice (user facing) title for URL
   */
  rstr_t *(*fap_title)(struct fa_protocol *fap, const char *url);

  /**
   * Check if a file handle may be parked
   */
  int (*fap_no_parking)(fa_handle_t *fh);

  /**
   * Check if a file URL should be redirected to something else
   */
  rstr_t *(*fap_redirect)(struct fa_protocol *fap, const char *url);

  /**
   * For dynamic FAPs
   */
  void *fap_opaque;

} fa_protocol_t;



char *fa_resolve_proto(const char *url, fa_protocol_t **p,
                       char *errbuf, size_t errsize);

void fap_release(fa_protocol_t *fap);

void fileaccess_register_dynamic(fa_protocol_t *fap);

void fileaccess_unregister_dynamic(fa_protocol_t *fap);

void fileaccess_register_entry(fa_protocol_t *fap);

#define FAP_REGISTER(name)                                              \
  INITIALIZER(fap_register_ ## name) {                                  \
    fileaccess_register_entry(&fa_protocol_ ## name);			\
  }

#endif /* FA_PROTO_H__ */
