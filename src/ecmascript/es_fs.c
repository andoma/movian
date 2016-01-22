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
#include <unistd.h>
#include <assert.h>

#include "main.h"
#include "ecmascript.h"
#include "fileaccess/http_client.h"
#include "fileaccess/fileaccess.h"
#include "misc/str.h"
#include "misc/regex.h"
#include "htsmsg/htsbuf.h"
#include "task.h"

/**
 *
 */
typedef struct es_fd_t {
  es_resource_t super;
  char *efd_path;
  fa_handle_t *efd_fh;
} es_fd_t;

/**
 *
 */
static void
es_fd_destroy(es_resource_t *eres)
{
  es_fd_t *efd = (es_fd_t *)eres;
  if(efd->efd_fh != NULL) {
    fa_close(efd->efd_fh);
    efd->efd_fh = NULL;
  }
  free(efd->efd_path);
  es_resource_unlink(&efd->super);
}


/**
 *
 */
static void
es_fd_info(es_resource_t *eres, char *dst, size_t dstsize)
{
  es_fd_t *efd = (es_fd_t *)eres;
  snprintf(dst, dstsize, "%s", efd->efd_path);
}


/**
 *
 */
const es_resource_class_t es_resource_fd = {
  .erc_name = "filedescriptor",
  .erc_size = sizeof(es_fd_t),
  .erc_destroy = es_fd_destroy,
  .erc_info = es_fd_info,
};


/**
 *
 */
static const char *
get_filename(duk_context *ctx, int index, const es_context_t *ec,
             int for_write)
{
  const char *filename = duk_to_string(ctx, index);

  if(gconf.bypass_ecmascript_acl)
    return filename;

  if(for_write && ec->ec_bypass_file_acl_write)
    return filename;

  if(!for_write && ec->ec_bypass_file_acl_read)
    return filename;

  if(strstr(filename, "../") || strstr(filename, "/.."))
    duk_error(ctx, DUK_ERR_ERROR,
              "Bad filename %s -- Contains parent references", filename);

  if((ec->ec_storage != NULL && mystrbegins(filename, ec->ec_storage)) ||
     (ec->ec_path    != NULL && mystrbegins(filename, ec->ec_path)))
    return filename;

  duk_error(ctx, DUK_ERR_ERROR, "Bad filename %s -- Access not allowed",
            filename);
}

/**
 *
 */
static int
es_file_open(duk_context *ctx)
{
  char errbuf[512];

  es_context_t *ec = es_get(ctx);

  const char *flagsstr = duk_to_string(ctx, 1);
  //  int mode = duk_to_int(ctx, 2);

  int flags;


  if(!strcmp(flagsstr, "r")) {
    flags = 0;
  } else if(!strcmp(flagsstr, "w")) {
    flags = FA_WRITE;
  } else if(!strcmp(flagsstr, "a")) {
    flags = FA_WRITE | FA_APPEND;
  } else {
    duk_error(ctx, DUK_ERR_ERROR, "Invalid flags '%s' to open", flagsstr);
  }

  const char *filename = get_filename(ctx, 0, ec, !!flags);

  fa_handle_t *fh = fa_open_ex(filename, errbuf, sizeof(errbuf), flags, NULL);
  if(fh == NULL)
    duk_error(ctx, DUK_ERR_ERROR, "Unable to open file '%s' -- %s",
              filename, errbuf);

  es_fd_t *efd = es_resource_create(ec, &es_resource_fd, 0);
  efd->efd_path = strdup(filename);
  efd->efd_fh = fh;

  es_resource_push(ctx, &efd->super);
  return 1;
}


/**
 *
 */
static es_fd_t *
es_fd_get(duk_context *ctx, int idx)
{
  es_fd_t *efd = es_resource_get(ctx, idx, &es_resource_fd);
  if(efd->efd_fh == NULL)
    duk_error(ctx, DUK_ERR_ERROR, "Filehandle for %s is closed",
              efd->efd_path);
  return efd;
}


/**
 * fd, buffer, offset, length, position
 */
static int
es_file_read(duk_context *ctx)
{
  es_fd_t *efd = es_fd_get(ctx, 0);
  duk_size_t bufsize;
  char *buf = duk_require_buffer(ctx, 1, &bufsize);

  const int offset = duk_to_int(ctx, 2);
  const int len = duk_to_int(ctx, 3);

  if(offset + len > bufsize)
    duk_error(ctx, DUK_ERR_ERROR, "Buffer too small %zd < %d + %d",
              bufsize, offset + len);

  if(!duk_is_null(ctx, 4)) {
    // Seek
    fa_seek(efd->efd_fh, duk_require_number(ctx, 4), SEEK_SET);
  }

  int r = fa_read(efd->efd_fh, buf + offset, len);
  if(r < 0)
    duk_error(ctx, DUK_ERR_ERROR, "Read error from '%s'", efd->efd_path);

  duk_push_int(ctx, r);
  return 1;
}



/**
 * fd, buffer, offset, length, position
 */
static int
es_file_write(duk_context *ctx)
{
  es_fd_t *efd = es_fd_get(ctx, 0);
  duk_size_t bufsize;
  char *buf = duk_to_buffer(ctx, 1, &bufsize);
  int len;

  const int offset = duk_to_int(ctx, 2);
  if(duk_is_null(ctx, 3)) {
    len = bufsize;
  } else {
    len = duk_to_int(ctx, 3);
  }

  // Don't read past buffer end
  if(offset + len > bufsize)
    len = bufsize - offset;

  if(!duk_is_null(ctx, 4)) {
    // Seek
    fa_seek(efd->efd_fh, duk_require_number(ctx, 4), SEEK_SET);
  }

  int r = fa_write(efd->efd_fh, buf + offset, len);
  if(r < 0)
    duk_error(ctx, DUK_ERR_ERROR, "Write error to '%s'", efd->efd_path);

  duk_push_int(ctx, r);
  return 1;
}


/**
 * fd
 */
static int
es_file_fsize(duk_context *ctx)
{
  es_fd_t *efd = es_fd_get(ctx, 0);

  int64_t siz = fa_fsize(efd->efd_fh);
  if(siz < 0)
    duk_error(ctx, DUK_ERR_ERROR, "File not seekable");
  duk_push_number(ctx, siz);
  return 1;
}


/**
 * fd, newsize
 */
static int
es_file_ftruncate(duk_context *ctx)
{
  es_fd_t *efd = es_fd_get(ctx, 0);
  fa_ftruncate(efd->efd_fh, duk_to_number(ctx, 1));
  return 0;
}


/**
 * oldname, newname
 */
static int
es_file_rename(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);

  const char *oldname = get_filename(ctx, 0, ec, 0);
  const char *newname = get_filename(ctx, 1, ec, 1);
  char errbuf[512];

  if(fa_rename(oldname, newname, errbuf, sizeof(errbuf)))
    duk_error(ctx, DUK_ERR_ERROR, "Unable to rename '%s' to '%s' -- %s",
              oldname, newname, errbuf);

  return 0;
}


/**
 * oldname, newname
 */
static int
es_file_mkdirs(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);

  const char *filename = get_filename(ctx, 0, ec, 1);
  char errbuf[512];

  if(fa_makedirs(filename, errbuf, sizeof(errbuf)))
    duk_error(ctx, DUK_ERR_ERROR, "Unable to mkdir '%s' -- %s",
              filename, errbuf);

  return 0;
}


/**
 *
 */
static int
es_file_dirname(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);
  const char *filename = mystrdupa(get_filename(ctx, 0, ec, 0));

  char *x = strrchr(filename, '/');
  if(x) {
    *x = 0;
    duk_push_string(ctx, filename);
  }
  return 1;
}


/**
 *
 */
static int
es_file_basename(duk_context *ctx)
{
  es_context_t *ec = es_get(ctx);
  char tmp[URL_MAX];

  fa_url_get_last_component(tmp, sizeof(tmp), get_filename(ctx, 0, ec, 0));
  duk_push_string(ctx, tmp);
  return 1;
}


/**
 *
 */
static int
es_file_copy(duk_context *ctx)
{
  char *cleanup;
  char path[URL_MAX];
  char errbuf[256];
  es_context_t *ec = es_get(ctx);

  const char *from = duk_to_string(ctx, 0);
  const char *to   = duk_to_string(ctx, 1);

  cleanup = mystrdupa(to);

  fa_sanitize_filename(cleanup);

  snprintf(path, sizeof(path), "%s/copy/%s",
           ec->ec_storage, cleanup);

  TRACE(TRACE_DEBUG, "JS", "Copying file from '%s' to '%s'", from, path);

  if(fa_copy(path, from, errbuf, sizeof(errbuf)))
    duk_error(ctx, DUK_ERR_ERROR, "Copy failed: %s", errbuf);

  duk_push_string(ctx, path);
  return 1;
}

static const duk_function_list_entry fnlist_fs[] = {
  { "open",             es_file_open,             3 },
  { "read",             es_file_read,             5 },
  { "write",            es_file_write,            5 },
  { "fsize",            es_file_fsize,            1 },
  { "ftrunctae",        es_file_ftruncate,        2 },
  { "rename",           es_file_rename,           2 },
  { "mkdirs",           es_file_mkdirs,           2 },
  { "dirname",          es_file_dirname,          1 },
  { "basename",         es_file_basename,         1 },
  { "copyfile",         es_file_copy,             2 },
  { NULL, NULL, 0}
};

ES_MODULE("fs", fnlist_fs);
