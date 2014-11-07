#include <unistd.h>
#include <assert.h>

#include "showtime.h"
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
get_filename(duk_context *ctx, int index)
{
  const char *filename = duk_to_string(ctx, index);

  if(strstr(filename, "../") || strstr(filename, "/.."))
    duk_error(ctx, DUK_ERR_ERROR, "Bad filename");
  return filename;
}

/**
 *
 */
static int
es_file_open(duk_context *ctx)
{
  char path[PATH_MAX];
  char errbuf[512];

  const char *filename = get_filename(ctx, 0);

  const char *flagsstr = duk_to_string(ctx, 1);
  //  int mode = duk_to_int(ctx, 2);

  int flags;

  es_context_t *ec = es_get(ctx);

  if(!strcmp(flagsstr, "r")) {
    flags = 0;
  } else if(!strcmp(flagsstr, "w")) {
    flags = FA_WRITE;
  } else if(!strcmp(flagsstr, "a")) {
    flags = FA_WRITE | FA_APPEND;
  } else {
    duk_error(ctx, DUK_ERR_ERROR, "Invalid flags '%s' to open", flagsstr);
  }

  snprintf(path, sizeof(path), "%s/%s", ec->ec_storage, filename);

  fa_handle_t *fh = fa_open_ex(path, errbuf, sizeof(errbuf), flags, NULL);
  if(fh == NULL)
    duk_error(ctx, DUK_ERR_ERROR, "Unable to open file '%s' -- %s",
              path, errbuf);

  es_fd_t *efd = es_resource_create(ec, &es_resource_fd, 0);
  efd->efd_path = strdup(path);
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

  const int offset = duk_to_int(ctx, 2);
  int len = duk_to_int(ctx, 3);

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
  const char *oldname = get_filename(ctx, 0);
  const char *newname = get_filename(ctx, 1);
  char errbuf[512];
  char oldpath[PATH_MAX];
  char newpath[PATH_MAX];

  es_context_t *ec = es_get(ctx);

  snprintf(oldpath, sizeof(oldpath), "%s/%s", ec->ec_storage, oldname);
  snprintf(newpath, sizeof(newpath), "%s/%s", ec->ec_storage, newname);


  if(fa_rename(oldpath, newpath, errbuf, sizeof(errbuf)))
    duk_error(ctx, DUK_ERR_ERROR, "Unable to rename '%s' to '%s' -- %s",
              oldpath, newpath, errbuf);

  return 0;
}


/**
 * oldname, newname
 */
static int
es_file_mkdirs(duk_context *ctx)
{
  const char *filename = get_filename(ctx, 0);
  char errbuf[512];
  char path[PATH_MAX];

  es_context_t *ec = es_get(ctx);

  snprintf(path, sizeof(path), "%s/%s", ec->ec_storage, filename);

  if(fa_makedirs(path, errbuf, sizeof(errbuf)))
    duk_error(ctx, DUK_ERR_ERROR, "Unable to mkdir '%s' to '%s' -- %s",
              path, errbuf);

  return 0;
}


/**
 *
 */
static int
es_file_dirname(duk_context *ctx)
{
  const char *filename = mystrdupa(get_filename(ctx, 0));

  char *x = strrchr(filename, '/');
  if(x) {
    *x = 0;
    duk_push_string(ctx, filename);
  }
  return 1;
}



/**
 * Showtime object exposed functions
 */
const duk_function_list_entry fnlist_Showtime_fs[] = {
  { "open",             es_file_open,             3 },
  { "read",             es_file_read,             5 },
  { "write",            es_file_write,            5 },
  { "fsize",            es_file_fsize,            1 },
  { "ftrunctae",        es_file_ftruncate,        2 },
  { "rename",           es_file_rename,           2 },
  { "mkdirs",           es_file_mkdirs,           2 },
  { "dirname",          es_file_dirname,          1 },
  { NULL, NULL, 0}
};
