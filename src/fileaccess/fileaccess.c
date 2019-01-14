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
#include "config.h"

#include <assert.h>
#ifdef FA_DUMP
#include <sys/types.h>
#include <fcntl.h>
#endif
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <limits.h>

#include "main.h"
#include "fileaccess.h"
#include "backend/backend.h"

#include "fileaccess/svfs.h"

#include "fa_proto.h"
#include "fa_probe.h"
#include "fa_imageloader.h"
#include "blobcache.h"
#include "htsmsg/htsbuf.h"
#include "htsmsg/htsmsg_store.h"
#include "settings.h"
#include "notifications.h"
#include "misc/minmax.h"

#if ENABLE_METADATA
#include "fa_indexer.h"
#endif

static struct fa_protocol_list fileaccess_all_protocols;
static HTS_MUTEX_DECL(fap_mutex);
static fa_protocol_t *native_fap;

/**
 *
 */
void
fap_release(fa_protocol_t *fap)
{
  if(fap->fap_fini == NULL)
    return;

  if(atomic_dec(&fap->fap_refcount))
    return;

  fap->fap_fini(fap);
}


/**
 *
 */
static void
fap_retain(fa_protocol_t *fap)
{
  atomic_inc(&fap->fap_refcount);
}

/**
 *
 */
char *
fa_resolve_proto(const char *url, fa_protocol_t **p,
		 char *errbuf, size_t errsize)
{
  struct fa_stat fs;
  fa_protocol_t *fap;
  const char *url0 = url;
  char buf[URL_MAX];
  int n = 0;

  while(*url != ':' && *url>31 && n < sizeof(buf) - 1)
    buf[n++] = *url++;

  buf[n] = 0;

  if(url[0] != ':') {
    /* No protocol specified, assume a plain file */
    if(native_fap == NULL ||
       (url0[0] != '/' &&
        native_fap->fap_stat(native_fap, url0, &fs, 0, NULL, 0))) {
      snprintf(errbuf, errsize, "File not found");
      return NULL;
    }
    *p = native_fap;
    return strdup(url0);
  }
  url++;

  if(!strcmp("dataroot", buf)) {
    const char *pfx = app_dataroot();
    snprintf(buf, sizeof(buf), "%s%s%s",
	     pfx, pfx[strlen(pfx) - 1] == '/' ? "" : "/", url);
    return fa_resolve_proto(buf, p, errbuf, errsize);
  }


  hts_mutex_lock(&fap_mutex);

  LIST_FOREACH(fap, &fileaccess_all_protocols, fap_link) {

    if(fap->fap_match_uri != NULL) {
      if(fap->fap_match_uri(url0))
	continue;
    } else if(fap->fap_match_proto != NULL) {
      if(fap->fap_match_proto(buf))
	continue;
    } else {
      if(strcmp(fap->fap_name, buf))
	continue;
    }

    fap_retain(fap);
    hts_mutex_unlock(&fap_mutex);

    const char *fname;

    if(fap->fap_flags & FAP_INCLUDE_PROTO_IN_URL) {
      fname = url0;
    } else {
      fname = url;
      if(fname[0] == '/' && fname[1] == '/')
        fname += 2;
    }

    if(fap->fap_redirect != NULL) {
      rstr_t *newurl = fap->fap_redirect(fap, fname);
      if(newurl != NULL) {
        fap_release(fap);
        char *r = fa_resolve_proto(rstr_get(newurl), p, errbuf, errsize);
        rstr_release(newurl);
        return r;
      }
    }

    *p = fap;
    return strdup(fname);
  }
  hts_mutex_unlock(&fap_mutex);
  snprintf(errbuf, errsize, "Protocol %s not supported", buf);
  return NULL;
}

/**
 *
 */
int
fa_can_handle(const char *url, char *errbuf, size_t errsize)
{
  fa_protocol_t *fap;
  char *filename;

  if((filename = fa_resolve_proto(url, &fap, errbuf, errsize)) == NULL)
    return 0;
  fap_release(fap);
  free(filename);
  return 1;
}


/**
 *
 */
rstr_t *
fa_absolute_path(rstr_t *filename, rstr_t *at)
{
  char *b = strrchr(rstr_get(at), '/');
  const char *f = rstr_get(filename);
  char buf[PATH_MAX];
  if(strchr(f, ':') || *f == 0 || *f == '/' || b == NULL || !memcmp(f, "./", 2))
    return rstr_dup(filename);

  snprintf(buf, sizeof(buf), "%.*s%s", (int)(b - rstr_get(at)) + 1,
	   rstr_get(at), rstr_get(filename));
  return rstr_alloc(buf);
}


/**
 *
 */
int
fa_normalize(const char *url, char *dst, size_t dstlen)
{
  fa_protocol_t *fap;
  char *filename;
  int r;

  if((filename = fa_resolve_proto(url, &fap, NULL, 0)) == NULL)
    return -1;

  r = fap->fap_normalize ? fap->fap_normalize(fap, filename, dst, dstlen) : -1;
  fap_release(fap);
  free(filename);
  return r;
}


/**
 *
 */
void *
fa_open_ex(const char *url, char *errbuf, size_t errsize, int flags,
	   struct fa_open_extra *foe)
{
  fa_protocol_t *fap;
  char *filename;
  fa_handle_t *fh;

  if(!(flags & FA_WRITE)) {
    // Only do caching if we are in read only mode

    if(flags & (FA_BUFFERED_SMALL | FA_BUFFERED_BIG))
      return fa_buffered_open(url, errbuf, errsize, flags, foe);
  }

  if((filename = fa_resolve_proto(url, &fap, errbuf, errsize)) == NULL)
    return NULL;

  if(flags & FA_WRITE && fap->fap_write == NULL) {
    snprintf(errbuf, errsize, "FS does not support writing");
    fh = NULL;
  } else {
    fh = fap->fap_open(fap, filename, errbuf, errsize, flags, foe);
  }
  fap_release(fap);
  free(filename);
#ifdef FA_DUMP
  if(flags & FA_DUMP)
    fh->fh_dump_fd = open("dumpfile.bin", O_CREAT | O_TRUNC | O_WRONLY, 0666);
  else
    fh->fh_dump_fd = -1;
#endif
  return fh;
}


/**
 *
 */
void *
fa_open_resolver(const char *url, char *errbuf, size_t errsize, int flags,
                 fa_open_extra_t *foe)
{
  fa_protocol_t *fap;
  char *filename;
  fa_handle_t *fh;

  if((filename = fa_resolve_proto(url, &fap, errbuf, errsize)) == NULL)
    return NULL;

  fh = fap->fap_open(fap, filename, errbuf, errsize, flags, foe);
#ifdef FA_DUMP
  fh->fh_dump_fd = -1;
#endif
  fap_release(fap);
  free(filename);
  return fh;
}

/**
 *
 */
void
fa_close(void *fh_)
{
  fa_handle_t *fh = fh_;
#ifdef FA_DUMP
  if(fh->fh_dump_fd != -1)
    close(fh->fh_dump_fd);
#endif
  fh->fh_proto->fap_close(fh);
}


/**
 *
 */
void
fa_close_with_park(fa_handle_t *fh, int park)
{
  if(park && fh->fh_proto->fap_park != NULL)
    fh->fh_proto->fap_park(fh);
  else
    fh->fh_proto->fap_close(fh);
}


/**
 *
 */
int
fa_read(void *fh_, void *buf, size_t size)
{
  fa_handle_t *fh = fh_;
  if(size == 0)
    return 0;
  int r = fh->fh_proto->fap_read(fh, buf, size);
#ifdef FA_DUMP
  if(fh->fh_dump_fd != -1) {
    printf("---------------- Dumpfile write %zd bytes at %ld\n",
	   size, lseek(fh->fh_dump_fd, 0, SEEK_CUR));
    if(write(fh->fh_dump_fd, buf, size) != size)
      printf("Warning: Dump data write error\n");
  }
#endif
  return r;
}

/**
 *
 */
void
fa_deadline(void *fh_, int deadline)
{
  fa_handle_t *fh = fh_;

  if(fh->fh_proto->fap_deadline != NULL)
    fh->fh_proto->fap_deadline(fh, deadline);
}


/**
 *
 */
int
fa_write(void *fh_, const void *buf, size_t size)
{
  fa_handle_t *fh = fh_;
  if(size == 0)
    return 0;
  return fh->fh_proto->fap_write(fh, buf, size);
}

/**
 *
 */
int64_t
fa_seek4(void *fh_, int64_t pos, int whence, int lazy)
{
  fa_handle_t *fh = fh_;
#ifdef FA_DUMP
  if(fh->fh_dump_fd != -1) {
    printf("--------------------- Dumpfile seek to %ld (%d)\n", pos, whence);
    lseek(fh->fh_dump_fd, pos, whence);
  }
#endif
  return fh->fh_proto->fap_seek(fh, pos, whence, lazy);
}

/**
 *
 */
int64_t
fa_fsize(void *fh_)
{
  fa_handle_t *fh = fh_;
  return fh->fh_proto->fap_fsize(fh);
}


/**
 *
 */
int
fa_ftruncate(void *fh_, uint64_t newsize)
{
  fa_handle_t *fh = fh_;
  if(fh->fh_proto->fap_ftruncate == NULL)
    return FAP_NOT_SUPPORTED;
  return fh->fh_proto->fap_ftruncate(fh, newsize);
}


/**
 *
 */
void
fa_set_read_timeout(void *fh_, int ms)
{
  fa_handle_t *fh = fh_;
  if(fh->fh_proto->fap_set_read_timeout == NULL)
    return;
  fh->fh_proto->fap_set_read_timeout(fh, ms);
}

/**
 *
 */
int
fa_stat_ex(const char *url, struct fa_stat *buf, char *errbuf, size_t errsize,
           int flags)
{
  fa_protocol_t *fap;
  char *filename;
  int r;

  if((filename = fa_resolve_proto(url, &fap, errbuf, errsize)) == NULL)
    return -1;

  r = fap->fap_stat(fap, filename, buf, flags, errbuf, errsize);
  fap_release(fap);
  free(filename);

  return r;
}


/**
 *
 */
fa_dir_t *
fa_scandir(const char *url, char *errbuf, size_t errsize)
{
  fa_protocol_t *fap;
  fa_dir_t *fd;
  char *filename;

  if((filename = fa_resolve_proto(url, &fap, errbuf, errsize)) == NULL)
    return NULL;

  if(fap->fap_scan != NULL) {
    fd = fa_dir_alloc();
    if(fap->fap_scan(fap, fd, filename, errbuf, errsize, 0)) {
      fa_dir_free(fd);
      fd = NULL;
    }
  } else {
    snprintf(errbuf, errsize, "Protocol does not implement directory scanning");
    fd = NULL;
  }
  fap_release(fap);
  free(filename);
  return fd;
}


/**
 *
 */
int
fa_scandir2(fa_dir_t *fd, const char *url, char *errbuf, size_t errsize,
            int flags)
{
  fa_protocol_t *fap;
  char *filename;
  int rval = 0;

  if((filename = fa_resolve_proto(url, &fap, errbuf, errsize)) == NULL)
    return -1;

  if(fap->fap_scan != NULL) {
    if(fap->fap_scan(fap, fd, filename, errbuf, errsize, flags))
      rval = -1;

  } else {
    snprintf(errbuf, errsize, "Protocol does not implement directory scanning");
    rval = -1;
  }
  fap_release(fap);
  free(filename);
  return rval;
}


/**
 *
 */
fa_dir_t *
fa_get_parts(const char *url, char *errbuf, size_t errsize)
{
  fa_protocol_t *fap;
  fa_dir_t *fd;
  char *filename;

  if((filename = fa_resolve_proto(url, &fap, errbuf, errsize)) == NULL)
    return NULL;

  if(fap->fap_get_parts != NULL) {
    fd = fa_dir_alloc();
    if(fap->fap_get_parts(fd, filename, errbuf, errsize)) {
      fa_dir_free(fd);
      fd = NULL;
    }
  } else {
    snprintf(errbuf, errsize, "Protocol does not implement part scanning");
    fd = NULL;
  }
  fap_release(fap);
  free(filename);
  return fd;
}


/**
 *
 */
int
fa_findfile(const char *path, const char *file,
	    char *fullpath, size_t fullpathlen)
{
  fa_dir_t *fd = fa_scandir(path, NULL, 0);
  fa_dir_entry_t *fde;

  if(fd == NULL)
    return -2;

  RB_FOREACH(fde, &fd->fd_entries, fde_link)
    if(!strcasecmp(rstr_get(fde->fde_filename), file)) {
      snprintf(fullpath, fullpathlen, "%s%s%s", path,
	       path[strlen(path)-1] == '/' ? "" : "/",
	       rstr_get(fde->fde_filename));
      fa_dir_free(fd);
      return 0;
    }

  fa_dir_free(fd);
  return -1;
}


/**
 *
 */
fa_handle_t *
fa_reference(const char *url)
{
  fa_protocol_t *fap;
  char *filename;
  fa_handle_t *fh;

  if((filename = fa_resolve_proto(url, &fap, NULL, 0)) == NULL)
    return NULL;

  fh = fap->fap_reference != NULL ? fap->fap_reference(fap, filename) : NULL;
  fap_release(fap);
  free(filename);
  return fh;
}


/**
 *
 */
void
fa_unreference(fa_handle_t *fh)
{
  if(fh != NULL)
    fh->fh_proto->fap_unreference(fh);
}


/**
 *
 */
fa_handle_t *
fa_notify_start(const char *url, void *opaque,
                void (*change)(void *opaque,
                               fa_notify_op_t op,
                               const char *filename,
                               const char *url,
                               int type))
{
  fa_protocol_t *fap;
  char *filename;
  fa_handle_t *fh;
  if((filename = fa_resolve_proto(url, &fap, NULL, 0)) == NULL)
    return NULL;

  if(fap->fap_notify_start == NULL) {
    fap_release(fap);
    free(filename);
    return NULL;
  }

  fh = fap->fap_notify_start(fap, filename, opaque, change);
  fap_release(fap);
  free(filename);
  return fh;
}


/**
 *
 */
void
fa_notify_stop(fa_handle_t *fh)
{
  fh->fh_proto->fap_notify_stop(fh);
}


/**
 *
 */
fa_dir_t *
fa_dir_alloc(void)
{
  fa_dir_t *fd = malloc(sizeof(fa_dir_t));
  RB_INIT(&fd->fd_entries);
  fd->fd_count = 0;
  return fd;
}


/**
 *
 */
void
fa_dir_entry_free(fa_dir_t *fd, fa_dir_entry_t *fde)
{
  if(fde->fde_prop != NULL)
    prop_ref_dec(fde->fde_prop);

  if(fde->fde_metadata != NULL)
    prop_destroy(fde->fde_metadata);

#if ENABLE_METADATA
  if(fde->fde_md != NULL)
    metadata_destroy(fde->fde_md);
#endif

  if(fd != NULL) {
    fd->fd_count--;
    fa_dir_remove(fd, fde);
  }
  rstr_release(fde->fde_filename);
  rstr_release(fde->fde_url);
  free(fde);
}


/**
 *
 */
void
fa_dir_free(fa_dir_t *fd)
{
  fa_dir_entry_t *fde;

  while((fde = fd->fd_entries.root) != NULL)
    fa_dir_entry_free(fd, fde);
  free(fd);
}


/**
 *
 */
static fa_dir_entry_t *
fde_create(fa_dir_t *fd, const char *url, const char *filename, int type)
{
  fa_dir_entry_t *fde;

  if(filename[0] == '.')
    return NULL; /* Skip all dot-filenames */

  fde = calloc(1, sizeof(fa_dir_entry_t));

  fde->fde_url      = rstr_alloc(url);
  fde->fde_filename = rstr_alloc(filename);
  fde->fde_type     = type;

  if(fd != NULL)
    fa_dir_insert(fd, fde);

  return fde;
}

/**
 *
 */
fa_dir_entry_t *
fa_dir_add(fa_dir_t *fd, const char *url, const char *filename, int type)
{
  return fde_create(fd, url, filename, type);
}


/**
 *
 */
static int
fa_dir_cmp1(const fa_dir_entry_t *a, const fa_dir_entry_t *b)
{
  int r = strcmp(rstr_get(a->fde_url), rstr_get(b->fde_url));
  if(r)
    return r;
  return a < b ? 1 : -1;
}


/**
 *
 */
void
fa_dir_insert(fa_dir_t *fd, fa_dir_entry_t *fde)
{
  if(RB_INSERT_SORTED(&fd->fd_entries, fde, fde_link, fa_dir_cmp1))
    abort();
  fd->fd_count++;
}

void
fa_dir_remove(fa_dir_t *fd, fa_dir_entry_t *fde)
{
  RB_REMOVE(&fd->fd_entries, fde, fde_link);
  fd->fd_count--;
}


/**
 *
 */
static int
fa_dir_cmp2(const fa_dir_entry_t *a, const fa_dir_entry_t *b)
{
  return strcmp(rstr_get(a->fde_url), rstr_get(b->fde_url));
}


/**
 *
 */
fa_dir_entry_t *
fa_dir_find(const fa_dir_t *fd, rstr_t *url)
{
  fa_dir_entry_t fde;
  fde.fde_url = url;
  return RB_FIND(&fd->fd_entries, &fde, fde_link, fa_dir_cmp2);
}


/**
 *
 */
void
fa_dir_print(fa_dir_t *fd)
{
  fa_dir_entry_t *fde;
  RB_FOREACH(fde, &fd->fd_entries, fde_link) {
    printf("%s <%s>\n\t%s\n", rstr_get(fde->fde_filename),
           fde->fde_type == CONTENT_FILE ? "file" : "dir",
           rstr_get(fde->fde_url));
  }
}



/**
 *
 */
int
fa_dir_entry_stat(fa_dir_entry_t *fde)
{
  if(fde->fde_statdone)
    return 0;

  if(!fa_stat(rstr_get(fde->fde_url), &fde->fde_stat, NULL, 0))
    fde->fde_statdone = 1;
  return !fde->fde_statdone;
}


/**
 * recursive directory scan
 */
static void
fa_rscan(const char *url, void (*fn)(void *aux, fa_dir_entry_t *fde),
         void *aux)
{
  fa_dir_t *fd = fa_scandir(url, NULL, 0);
  fa_dir_entry_t *fde;
  if(fd == NULL)
    return;

  RB_FOREACH(fde, &fd->fd_entries, fde_link) {
    if(fde->fde_type == CONTENT_DIR)
      fa_rscan(rstr_get(fde->fde_url), fn, aux);
    else
      fn(aux, fde);
  }
  fa_dir_free(fd);

  fde = fde_create(NULL, url, url, CONTENT_DIR);
  fn(aux, fde);
  fa_dir_entry_free(NULL, fde);
}

TAILQ_HEAD(delscan_item_queue, delscan_item);

/**
 *
 */
typedef struct delscan_item {
  TAILQ_ENTRY(delscan_item) link;
  rstr_t *url;
  int type;
} delscan_item_t;



/**
 *
 */
static void
delitem_add(struct delscan_item_queue *diq, const char *url,
	    int type)
{
  delscan_item_t *di = malloc(sizeof(delscan_item_t));
  di->url = rstr_alloc(url);
  di->type = type;
  TAILQ_INSERT_TAIL(diq, di, link);
}


/**
 *
 */
static void
delitem_addr(struct delscan_item_queue *diq, rstr_t *url, int type)
{
  delscan_item_t *di = malloc(sizeof(delscan_item_t));
  di->url = rstr_dup(url);
  di->type = type;
  TAILQ_INSERT_TAIL(diq, di, link);
}


/**
 *
 */
static void
delitem_add_fde(void *aux, fa_dir_entry_t *fde)
{
  delitem_addr(aux, fde->fde_url, fde->fde_type);
}



/**
 *
 */
static void
unlink_items(const struct delscan_item_queue *diq)
{
  const delscan_item_t *di;
  char errbuf[256];
  fa_protocol_t *fap;
  char *filename;

  TAILQ_FOREACH(di, diq, link) {

    if((filename = fa_resolve_proto(rstr_get(di->url), &fap,
				    errbuf, sizeof(errbuf))) == NULL) {
      TRACE(TRACE_ERROR, "FS", "Unable to resolve %s -- %s",
	    rstr_get(di->url), errbuf);
      continue;
    }

    // #define NO_ACTUAL_UNLINK // good when testing and I don't wanna zap stuff

    int r;
    if(di->type == CONTENT_DIR) {
#ifdef NO_ACTUAL_UNLINK
      printf("rmdir(%s)\n", filename);
      r = 0;
#else
      r = fap->fap_rmdir(fap, filename, errbuf, sizeof(errbuf));
#endif
    } else {
#ifdef NO_ACTUAL_UNLINK
      printf("unlink(%s)\n", filename);
      r = 0;
#else
      r = fap->fap_unlink(fap, filename, errbuf, sizeof(errbuf));
#endif
    }

    if(r)
      TRACE(TRACE_ERROR, "FS", "Unable to delete %s -- %s",
	    rstr_get(di->url), errbuf);
    fap_release(fap);
    free(filename);
  }
}


/**
 *
 */
static int
free_items(struct delscan_item_queue *diq)
{
  delscan_item_t *a, *b;
  int cnt = 0;
  for(a = TAILQ_FIRST(diq); a != NULL; a = b) {
    b = TAILQ_NEXT(a, link);
    rstr_release(a->url);
    free(a);
    cnt++;
  }
  return cnt;
}

/**
 *
 */
static int
verify_delete(const struct delscan_item_queue *diq)
{
  int files = 0;
  int dirs = 0;
  int parts = 0;
  const delscan_item_t *di;
  TAILQ_FOREACH(di, diq, link) {
    switch(di->type) {
    case CONTENT_DIR:
      dirs++;
      break;
    case CONTENT_FILE:
      files++;
      break;
    case 1000:
      parts++;
      break;
    }
  }

  rstr_t *ftxt = _pl("%d files",         "%d file",         files);
  rstr_t *dtxt = _pl("%d directories",   "%d directory",    dirs);
  rstr_t *ptxt = _pl("%d archive parts", "%d archive part", parts);
  rstr_t *del  = _("Are you sure you want to delete:");

  char tmp[512];
  int l = snprintf(tmp, sizeof(tmp), "%s\n", rstr_get_always(del));

  if(files) {
    l += snprintf(tmp + l, sizeof(tmp) - l, rstr_get(ftxt), files);
    l += snprintf(tmp + l, sizeof(tmp) - l, "\n");
  }
  if(dirs) {
    l += snprintf(tmp + l, sizeof(tmp) - l, rstr_get(dtxt), dirs);
    l += snprintf(tmp + l, sizeof(tmp) - l, "\n");
  }
  if(parts) {
    l += snprintf(tmp + l, sizeof(tmp) - l, rstr_get(ptxt), parts);
    l += snprintf(tmp + l, sizeof(tmp) - l, "\n");
  }

  int x = message_popup(tmp, MESSAGE_POPUP_CANCEL | MESSAGE_POPUP_OK, NULL);

  rstr_release(ftxt);
  rstr_release(dtxt);
  rstr_release(del);
  return x;
}


/**
 *
 */
int
fa_unlink_recursive(const char *url, char *errbuf, size_t errsize, int verify)
{
  fa_protocol_t *fap;
  fa_stat_t st;
  char *filename;
  struct delscan_item_queue diq;
  TAILQ_INIT(&diq);

  if((filename = fa_resolve_proto(url, &fap, errbuf, errsize)) == NULL)
    return -1;

  if(fap->fap_stat(fap, filename, &st, 0, errbuf, errsize))
    goto bad;

  if(st.fs_type == CONTENT_FILE) {
    if(fap->fap_unlink == NULL) {
      snprintf(errbuf, errsize,
	       "Deleting not supported for this file system");
      goto bad;
    }

    const char *pfx = strrchr(url, '.');
    if(!strcasecmp(pfx, ".rar")) {
      char path[PATH_MAX];
      snprintf(path, sizeof(path), "rar://%s", url);
      fa_dir_t *fd = fa_get_parts(path, errbuf, errsize);
      if(fd == NULL)
	goto bad;

      fa_dir_entry_t *fde;
      RB_FOREACH(fde, &fd->fd_entries, fde_link)
	delitem_addr(&diq, fde->fde_url, 1000);

      fa_dir_free(fd);


    } else {
      delitem_add(&diq, url, CONTENT_FILE);
    }

  } else if(st.fs_type == CONTENT_DIR) {

    if(fap->fap_unlink == NULL || fap->fap_rmdir == NULL) {
      snprintf(errbuf, errsize,
	       "Deleting not supported for this file system");
      goto bad;
    }

    fa_rscan(url, delitem_add_fde, &diq);
  } else {
    snprintf(errbuf, errsize, "Can't delete this type");
    goto bad;
  }

  if(verify && verify_delete(&diq) != MESSAGE_POPUP_OK) {
    snprintf(errbuf, errsize, "Canceled by user");
    goto bad;
  }

  unlink_items(&diq);
  int retval = free_items(&diq);

  fap_release(fap);
  free(filename);
  return retval;

bad:
  fap_release(fap);
  free(filename);
  free_items(&diq);
  return -1;

}

/**
 *
 */
int
fa_copy_from_fh(const char *to, fa_handle_t *src, char *errbuf, size_t errsize)
{
  char tmp[8192];

  if(fa_parent(tmp, sizeof(tmp), to)) {
    snprintf(errbuf, errsize, "Unable to figure out parent dir for dest");
    fa_close(src);
    return -1;
  }

  if(fa_makedirs(tmp, errbuf, errsize)) {
    fa_close(src);
    return -1;
  }

  fa_handle_t *dst = fa_open_ex(to, errbuf, errsize, FA_WRITE, NULL);
  if(dst == NULL) {
    fa_close(src);
    return -1;
  }

  int r;
  while((r = fa_read(src, tmp, sizeof(tmp))) > 0) {
    if(fa_write(dst, tmp, r) != r) {
      snprintf(errbuf, errsize, "Write error");
      r = -2;
      break;
    }
  }

  if(r == -1)
    snprintf(errbuf, errsize, "Read error");

  const fa_protocol_t *dfap = dst->fh_proto;

  fa_close(dst);
  fa_close(src);
  if(r < 0 && dfap->fap_unlink != NULL) {
    // Remove destination file if we screwed up
    dfap->fap_unlink(dfap, to, NULL, 0);
  }

  return r;
}


/**
 *
 */
int
fa_copy(const char *to, const char *from, char *errbuf, size_t errsize)
{
  fa_handle_t *src = fa_open_ex(from, errbuf, errsize, 0, NULL);
  if(src == NULL)
    return -1;
  return fa_copy_from_fh(to, src, errbuf, errsize);
}



static fa_err_code_t
fa_makedir_p(fa_protocol_t *fap, const char *path)
{
  struct fa_stat fs;
  char *p;
  int l, r;

  if(!fap->fap_stat(fap, path, &fs, 0, NULL, 0) &&
     fs.fs_type == CONTENT_DIR)
    return 0; /* Dir already there */

  r = fap->fap_makedir(fap, path);
  if(r == 0)
    return 0; /* Dir created ok */

  if(r == FAP_NOENT) {

    /* Parent does not exist, try to create it */
    /* Allocate new path buffer and strip off last directory component */

    l = strlen(path);
    p = alloca(l + 1);
    memcpy(p, path, l);
    p[l--] = 0;

    for(; l >= 0; l--)
      if(p[l] == '/')
	break;
    if(l == 0)
      return FAP_NOENT;

    p[l] = 0;

    if((r = fa_makedir_p(fap, p)) != 0)
      return r;

    /* Try again */
    r = fap->fap_makedir(fap, path);
    if(r == 0)
      return 0; /* Dir created ok */
  }
  return r;
}


/**
 *
 */
int
fa_makedirs(const char *url, char *errbuf, size_t errsize)
{
  fa_protocol_t *fap;
  char *filename;
  int r;

  if((filename = fa_resolve_proto(url, &fap, errbuf, errsize)) == NULL)
    return -1;

  if(fap->fap_makedir == NULL) {
    snprintf(errbuf, errsize, "No mkdir support in filesystem");
    r = -1;
  } else {
    r = fa_makedir_p(fap, filename);
    if(r)
      snprintf(errbuf, errsize, "Failed to create directory, error %d", r);
  }
  fap_release(fap);
  free(filename);
  return r;
}


/**
 *
 */
fa_err_code_t
fa_makedir(const char *url)
{
  fa_protocol_t *fap;
  char *filename;
  int r;

  if((filename = fa_resolve_proto(url, &fap, NULL, 0)) == NULL)
    return FAP_NOENT;

  if(fap->fap_makedir == NULL) {
    r = FAP_NOT_SUPPORTED;
  } else {
    r = fap->fap_makedir(fap, filename);
  }
  fap_release(fap);
  free(filename);
  return r;
}


/**
 *
 */
int
fa_unlink(const char *url, char *errbuf, size_t errsize)
{
  fa_protocol_t *fap;
  char *filename;
  int r;

  if((filename = fa_resolve_proto(url, &fap, errbuf, errsize)) == NULL)
    return -1;

  if(fap->fap_unlink == NULL) {
    snprintf(errbuf, errsize, "No unlink support in filesystem");
    r = -1;
  } else {
    r = fap->fap_unlink(fap, filename, errbuf, errsize);
  }
  fap_release(fap);
  free(filename);
  return r;
}


/**
 *
 */
int
fa_rmdir(const char *url, char *errbuf, size_t errsize)
{
  fa_protocol_t *fap;
  char *filename;
  int r;

  if((filename = fa_resolve_proto(url, &fap, errbuf, errsize)) == NULL)
    return -1;

  if(fap->fap_rmdir == NULL) {
    snprintf(errbuf, errsize, "No rmdir support in filesystem");
    r = -1;
  } else {
    r = fap->fap_rmdir(fap, filename, errbuf, errsize);
  }
  fap_release(fap);
  free(filename);
  return r;
}


/**
 *
 */
int
fa_rename(const char *old, const char *new, char *errbuf, size_t errsize)
{
  fa_protocol_t *old_fap;
  char *old_filename;
  fa_protocol_t *new_fap;
  char *new_filename;
  int r;

  if((old_filename = fa_resolve_proto(old, &old_fap,
                                      errbuf, errsize)) == NULL)
    return -1;

  if((new_filename = fa_resolve_proto(new, &new_fap,
                                      errbuf, errsize)) == NULL) {

    fap_release(old_fap);
    free(old_filename);
    return -1;
  }

  r = -1;

  if(old_fap != new_fap) {
    snprintf(errbuf, errsize, "Cross filesystem renames not supported");
  } else {

    if(old_fap->fap_rename == NULL) {
      snprintf(errbuf, errsize, "No rename support in filesystem");
    } else {
      r = old_fap->fap_rename(old_fap, old_filename, new_filename,
                              errbuf, errsize);
    }
  }

  fap_release(old_fap);
  free(old_filename);
  fap_release(new_fap);
  free(new_filename);
  return r;
}


/**
 *
 */
fa_err_code_t
fa_set_xattr(const char *url, const char *name, const void *data, size_t len)
{
  fa_protocol_t *fap;
  char *filename;
  int r;

  if((filename = fa_resolve_proto(url, &fap, NULL, 0)) == NULL)
    return FAP_NOT_SUPPORTED;

  if(fap->fap_set_xattr == NULL) {
    r = FAP_NOT_SUPPORTED;
  } else {
    r = fap->fap_set_xattr(fap, filename, name, data, len);
  }
  fap_release(fap);
  free(filename);
  return r;
}


/**
 *
 */
fa_err_code_t
fa_get_xattr(const char *url, const char *name, void **datap, size_t *lenp)
{
  fa_protocol_t *fap;
  char *filename;
  int r;

  if((filename = fa_resolve_proto(url, &fap, NULL, 0)) == NULL)
    return FAP_NOT_SUPPORTED;

  if(fap->fap_get_xattr == NULL) {
    r = FAP_NOT_SUPPORTED;
  } else {
    r = fap->fap_get_xattr(fap, filename, name, datap, lenp);
  }
  fap_release(fap);
  free(filename);
  return r;
}


/**
 *
 */
fa_err_code_t
fa_fsinfo(const char *url, fa_fsinfo_t *ffi)
{
  fa_protocol_t *fap;
  char *filename;
  int r;

  if((filename = fa_resolve_proto(url, &fap, NULL, 0)) == NULL)
    return FAP_NOT_SUPPORTED;

  if(fap->fap_fsinfo == NULL) {
    r = FAP_NOT_SUPPORTED;
  } else {
    r = fap->fap_fsinfo(fap, filename, ffi);
  }
  fap_release(fap);
  free(filename);
  return r;
}


/**
 *
 */
void
fileaccess_register_entry(fa_protocol_t *fap)
{
  LIST_INSERT_HEAD(&fileaccess_all_protocols, fap, fap_link);
  if(fap->fap_name != NULL && !strcmp(fap->fap_name, "file"))
    native_fap = fap;
}


/**
 *
 */
void
fileaccess_register_dynamic(fa_protocol_t *fap)
{
  atomic_set(&fap->fap_refcount, 1);
  hts_mutex_lock(&fap_mutex);
  LIST_INSERT_HEAD(&fileaccess_all_protocols, fap, fap_link);
  hts_mutex_unlock(&fap_mutex);
}


/**
 *
 */
void
fileaccess_unregister_dynamic(fa_protocol_t *fap)
{
  hts_mutex_lock(&fap_mutex);
  LIST_REMOVE(fap, fap_link);
  hts_mutex_unlock(&fap_mutex);
  fap_release(fap);
}


/**
 *
 */
int
fileaccess_init(void)
{
  fa_protocol_t *fap;
  fa_imageloader_init();

  LIST_FOREACH(fap, &fileaccess_all_protocols, fap_link)
    if(fap->fap_init != NULL)
      fap->fap_init();

#if ENABLE_METADATA
  fa_indexer_init();
#endif

  prop_t *dir = setting_get_dir("general:filebrowse");

  setting_create(SETTING_BOOL, dir, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Enable file deletion from item menu")),
                 SETTING_WRITE_BOOL(&gconf.fa_allow_delete),
                 SETTING_STORE("faconf", "delete"),
                 NULL);

  setting_create(SETTING_BOOL, dir, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Store per-file settings in filesystem")),
                 SETTING_WRITE_BOOL(&gconf.fa_kvstore_as_xattr),
                 SETTING_VALUE(1),
                 SETTING_STORE("faconf", "enablexattr"),
                 NULL);

  setting_create(SETTING_BOOL, dir, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Show filename extensions")),
                 SETTING_WRITE_BOOL(&gconf.show_filename_extensions),
                 SETTING_STORE("faconf", "filenameextensions"),
                 NULL);

  setting_create(SETTING_BOOL, dir, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Browse archives as folders")),
                 SETTING_WRITE_BOOL(&gconf.fa_browse_archives),
                 SETTING_VALUE(1),
                 SETTING_STORE("faconf", "browsearchives"),
                 NULL);
  return 0;
}


/**
 *
 */
typedef struct loadarg {
  SIMPLEQ_ENTRY(loadarg) link;
  const char *key;
  const char *value;
} loadarg_t;

#define FA_LOAD_CACHE_STASH "fa-load"


/**
 *
 */
buf_t *
fa_load(const char *url, ...)
{
  char *errbuf = NULL;
  size_t errlen = 0;
  int *cache_control = NULL;
  int flags = 0;
  fa_load_cb_t *cb = NULL;
  void *opaque = NULL;
  cancellable_t *c = NULL;
  int min_expire = 0;
  struct http_header_list *request_headers = NULL;
  struct http_header_list *response_headers = NULL;

  fa_protocol_t *fap;
  fa_handle_t *fh;
  char *filename;
  int r;
  char *etag = NULL;
  time_t mtime = 0;
  int is_expired = 0;
  buf_t *buf = NULL;
  int tag;
  loadarg_t *la;
  char **locationptr = NULL;
  int *cache_info_ptr = NULL;
  int protocol_code = 0;
  int *protocol_codep = &protocol_code;
  int no_fallback = 0;
  int cache_evict = 0;

  va_list ap;
  va_start(ap, url);

  SIMPLEQ_HEAD(, loadarg) qargs;
  SIMPLEQ_INIT(&qargs);

  while((tag = va_arg(ap, int)) != 0) {
    switch(tag) {
    case FA_LOAD_TAG_ERRBUF:
      errbuf = va_arg(ap, char *);
      errlen = va_arg(ap, size_t);
      break;

    case FA_LOAD_TAG_CACHE_CONTROL:
      cache_control = va_arg(ap, int *);
      break;

    case FA_LOAD_TAG_FLAGS:
      flags = va_arg(ap, int);
      break;

    case FA_LOAD_TAG_PROGRESS_CALLBACK:
      cb = va_arg(ap, fa_load_cb_t *);
      opaque = va_arg(ap, void *);
      break;

    case FA_LOAD_TAG_CANCELLABLE:
      c = va_arg(ap, cancellable_t *);
      break;

    case FA_LOAD_TAG_QUERY_ARG:
      la = alloca(sizeof(loadarg_t));
      la->key = va_arg(ap, const char *);
      la->value = va_arg(ap, const char *);
      if(la->value != NULL && la->key != NULL)
        SIMPLEQ_INSERT_TAIL(&qargs, la, link);
      break;

    case FA_LOAD_TAG_QUERY_ARGVEC: {
      const char **args = va_arg(ap, const char **);

      if(args != NULL) {
        while(args[0] != NULL) {
          if(args[1] != NULL) {
            la = alloca(sizeof(loadarg_t));
            la->key = args[0];
            la->value = args[1];
            SIMPLEQ_INSERT_TAIL(&qargs, la, link);
          }
          args += 2;
        }
      }
      break;
    }

    case FA_LOAD_TAG_MIN_EXPIRE:
      min_expire = va_arg(ap, int);
      break;

    case FA_LOAD_TAG_REQUEST_HEADERS:
      request_headers = va_arg(ap, struct http_header_list *);
      break;

    case FA_LOAD_TAG_RESPONSE_HEADERS:
      response_headers = va_arg(ap, struct http_header_list *);
      LIST_INIT(response_headers);
      break;

    case FA_LOAD_TAG_LOCATION:
      locationptr = va_arg(ap, char **);
      break;

    case FA_LOAD_TAG_CACHE_INFO:
      cache_info_ptr = va_arg(ap, int *);
      *cache_info_ptr = 0;
      break;

    case FA_LOAD_TAG_PROTOCOL_CODE:
      protocol_codep = va_arg(ap, int *);
      break;

    case FA_LOAD_TAG_NO_FALLBACK:
      no_fallback = 1;
      break;

    case FA_LOAD_TAG_CACHE_EVICT:
      cache_evict = 1;
      break;

    default:
      abort();
    }
  }

  va_end(ap);

  *protocol_codep = 0;

  if(SIMPLEQ_FIRST(&qargs) != NULL) {
    // Construct URL with query args
    htsbuf_queue_t q;
    htsbuf_queue_init(&q, 0);

    htsbuf_append(&q, url, strlen(url));
    char prefix = strchr(url, '?') ? '&' : '?';
    SIMPLEQ_FOREACH(la, &qargs, link) {
      htsbuf_append(&q, &prefix, 1);
      htsbuf_append_and_escape_url(&q, la->key);
      htsbuf_append(&q, "=", 1);
      htsbuf_append_and_escape_url(&q, la->value);
      prefix = '&';
    }
    char *newurl = htsbuf_to_string(&q);
    url = mystrdupa(newurl); // Copy it to stack to avoid all free()s
    free(newurl);
  }

  if(cache_evict) {
    blobcache_evict(url, FA_LOAD_CACHE_STASH);
    return NULL;
  }

  if(locationptr != NULL)
    *locationptr = strdup(url);

  if((filename = fa_resolve_proto(url, &fap, errbuf, errlen)) == NULL)
    return NULL;

  if(fap->fap_load != NULL) {
    buf_t *data2;
    int max_age = 0;

    if(cache_control != BYPASS_CACHE && cache_control != DISABLE_CACHE) {
      buf = blobcache_get(url, FA_LOAD_CACHE_STASH,
                          1, &is_expired, &etag, &mtime);

      if(buf != NULL) {
	if(cache_control != NULL) {
	  // Upper layer can deal with expired data, pass it
	  *cache_control = is_expired;
	  free(etag);
          fap_release(fap);
          free(filename);
          if(cache_info_ptr != NULL)
            *cache_info_ptr = FA_CACHE_INFO_EXPIRED_FROM_CACHE;
	  return buf;
	}

	// It was not expired, return it
	if(!is_expired) {
	  free(etag);
          fap_release(fap);
	  free(filename);
          if(cache_info_ptr != NULL)
            *cache_info_ptr = FA_CACHE_INFO_FROM_CACHE;
	  return buf;
	}
      } else if(cache_control != NULL) {
	snprintf(errbuf, errlen, "Not cached");
	free(etag);
        fap_release(fap);
	free(filename);
	return NULL;
      }
    }

    if(cache_control == BYPASS_CACHE)
      blobcache_get_meta(url, FA_LOAD_CACHE_STASH, &etag, &mtime);

    data2 = fap->fap_load(fap, filename, errbuf, errlen,
			  &etag, &mtime, &max_age, flags, cb, opaque, c,
                          request_headers, response_headers, locationptr,
                          protocol_codep);

    fap_release(fap);
    free(filename);
    if(data2 == NOT_MODIFIED) {
      if(cache_control == BYPASS_CACHE)
	return NOT_MODIFIED;

      free(etag);
      if(cache_info_ptr != NULL)
        *cache_info_ptr = FA_CACHE_INFO_FROM_CACHE_NOT_MODIFIED;
      return buf;
    }

    if(data2 == NULL && buf != NULL) {
      /* We have a cached entry and failed to load the new one.
         Return the cached entry anyway. It must be better than nothing
      */
      if(*protocol_codep == 401 ||
         *protocol_codep == 403 ||
         *protocol_codep == 400) {
        free(etag);
        buf_release(buf);
        return NULL;
      }

      free(etag);
      if(cache_info_ptr != NULL)
        *cache_info_ptr = FA_CACHE_INFO_EXPIRED_FROM_CACHE;
      return buf;
    }

    buf_release(buf);

      /* no_change is set if we stored same object again in cache.
         It is cheap to compute since we need to calc md5 sum of blob
         to store anyway.
      */
    int no_change;

    /*
     * If caller specified a minimum expire (to force stuff to stay in
     * cache for a given time), up max_age
     */
    max_age = MAX(min_expire, max_age);
    if(data2 && cache_control != DISABLE_CACHE &&
       *protocol_codep < 300 &&
       (cache_control || max_age || etag || mtime)) {

      int bc_flags = 0;
      if(flags & FA_IMPORTANT)
        bc_flags |= BLOBCACHE_IMPORTANT_ITEM;

      no_change = blobcache_put(url, FA_LOAD_CACHE_STASH, data2, max_age,
                                etag, mtime, bc_flags);

    } else {
      no_change = 0;
    }
    free(etag);

    if(cache_control == BYPASS_CACHE && no_change) {
      buf_release(data2);
      return NOT_MODIFIED;
    }
    return data2;
  }

  if(no_fallback) {
    free(filename);
    return NO_LOAD_METHOD;
  }

  fh = fap->fap_open(fap, filename, errbuf, errlen, 0, 0);
#ifdef FA_DUMP
  fh->fh_dump_fd = -1;
#endif
  fap_release(fap);
  free(filename);

  if(fh == NULL)
    return NULL;

  size_t size = fa_fsize(fh);
  if(size == -1) {
    snprintf(errbuf, errlen, "Unable to load file from non-seekable fs");
    fa_close(fh);
    return NULL;
  }

  uint8_t *data = mymalloc(size + 1);
  if(data == NULL) {
    snprintf(errbuf, errlen, "Out of memory");
    fa_close(fh);
    return NULL;
  }
  r = fa_read(fh, data, size);

  fa_close(fh);

  if(r != size) {
    snprintf(errbuf, errlen, "Short read");
    free(data);
    return NULL;
  }
  data[size] = 0;
  return buf_create_and_adopt(size, data, &free);
}


/**
 *
 */
int
fa_parent(char *dst, size_t dstlen, const char *url)
{
  const char *proto;
  int l = strlen(url);
  char *x, *parent  = alloca(l + 1);

  memcpy(parent, url, l + 1);

  x = strstr(parent, "://");
  if(x != NULL) {
    proto = parent;
    *x = 0;
    parent = x + 3;
  } else {
    proto = "file";
  }

  if(strcmp(parent, "/")) {
    /* Set parent */
    if((x = strrchr(parent, '/')) != NULL) {
      *x = 0;
      if(x[1] == 0) {
	/* Trailing slash */
	if((x = strrchr(parent, '/')) != NULL) {
	  *x = 0;
	}
      }
      snprintf(dst, dstlen, "%s://%s/", proto, parent);
      return 0;
    } else {
#if PS3
      if(!strncmp(proto, "ntfs", 4)) {
	snprintf(dst, dstlen, "%s://", proto);
	return 0;
      }
#endif
    }
  }
  return -1;
}


int
fa_check_url(const char *url, char *errbuf, size_t errlen, int timeout_ms)
{
  fa_protocol_t *fap;
  char *filename;
  int r;
  struct fa_stat fs;

  if((filename = fa_resolve_proto(url, &fap, errbuf, errlen)) == NULL)
    return BACKEND_PROBE_NO_HANDLER;


  r = fap->fap_stat(fap, filename, &fs, FA_NON_INTERACTIVE, errbuf, errlen);
  fap_release(fap);
  free(filename);

  if(r == 0)
    return BACKEND_PROBE_OK;
  if(r == FAP_NEED_AUTH) {
    snprintf(errbuf, errlen, "Authentication required");
    return BACKEND_PROBE_AUTH;
  }
  return BACKEND_PROBE_FAIL;
}


/**
 *
 */
void
fa_pathjoin(char *dst, size_t dstlen, const char *p1, const char *p2)
{
  while(!strncmp(p2, "./", 2))
    p2 += 2;
  int l1 = strlen(p1);
  int sep = l1 > 0 && p1[l1 - 1] == '/';
  snprintf(dst, dstlen, "%s%s%s", p1, sep ? "" : "/", p2);
}


/**
 *
 */
static void
fa_url_get_last_component_i(fa_protocol_t *fap, const char *filename,
                            char *dst, size_t dstlen, const char *url)
{
  int e, b;

  if(dstlen == 0)
    return;

  if(filename != NULL) {
    assert(fap != NULL);
    if(fap->fap_get_last_component != NULL) {
      fap->fap_get_last_component(fap, filename, dst, dstlen);
      return;
    }
  }

  e = strlen(url);
  if(e > 0 && url[e-1] == '/')
    e--;
  if(e > 0 && url[e-1] == '|')
    e--;

  if(e == 0) {
    *dst = 0;
    return;
  }

  b = e;
  while(b > 0) {
    b--;
    if(url[b] == '/') {
      b++;
      break;
    }
  }


  if(dstlen > e - b + 1)
    dstlen = e - b + 1;
  memcpy(dst, url + b, dstlen);
  dst[dstlen - 1] = 0;
}



/**
 *
 */
void
fa_url_get_last_component(char *dst, size_t dstlen, const char *url)
{
  fa_protocol_t *fap;
  char *filename;

  if(dstlen == 0)
    return;

  filename = fa_resolve_proto(url, &fap, NULL, 0);
  fa_url_get_last_component_i(fap, filename, dst, dstlen, url);
  free(filename);
  if(fap != NULL)
    fap_release(fap);
  return;
}


/**
 *
 */
rstr_t *
fa_get_title(const char *url)
{
  char str[256];
  fa_protocol_t *fap;
  char *filename;

  filename = fa_resolve_proto(url, &fap, NULL, 0);

  if(filename != NULL && fap->fap_title != NULL) {

    rstr_t *r = fap->fap_title(fap, filename);

    if(r != NULL) {
      free(filename);
      fap_release(fap);
      return r;
    }
  }

  fa_url_get_last_component_i(fap, filename, str, sizeof(str), url);

  free(filename);
  if(fap != NULL)
    fap_release(fap);

  return rstr_alloc(str);
}


/**
 *
 */
buf_t *
fa_load_and_close(fa_handle_t *fh)
{
  int r;
  size_t size = fa_fsize(fh);
  uint8_t *mem;

  fa_seek(fh, 0, SEEK_SET);

  if(size == -1) {
    size = 0;
    size_t alloced = 0;
    mem = NULL;

    while(1) {

      alloced = alloced * 2 + 1000;
      mem = myreallocf(mem, alloced + 1);
      if(mem == NULL) {
        fa_close(fh);
        return NULL;
      }

      int max_read = alloced - size;
      r = fa_read(fh, mem + size, max_read);
      if(r < 0) {
        fa_close(fh);
        free(mem);
        return NULL;
      }

      size += r;

      if(r < max_read)
        break;
    }

  } else {

    mem = mymalloc(size+1);
    if(mem == NULL) {
      fa_close(fh);
      return NULL;
    }

    r = fa_read(fh, mem, size);
    fa_close(fh);

    if(r != size) {
      free(mem);
      return NULL;
    }
  }

  mem[size] = 0;
  return buf_create_and_adopt(size, mem, &free);
}


/**
 *
 */
int
fa_read_to_htsbuf(struct htsbuf_queue *hq, fa_handle_t *fh, int maxbytes)
{
  const int chunksize = 4096;
  while(maxbytes > 0) {
    char *buf = malloc(chunksize);
    int l = fa_read(fh, buf, chunksize);
    if(l < 0)
      return -1;

    if(l > 0) {
      htsbuf_append_prealloc(hq, buf, l);
    } else {
      free(buf);
    }
    if(l != chunksize)
      return 0;
    maxbytes -= l;
  }
  return -1;
}


/**
 *
 */
void
fa_sanitize_filename(char *f)
{
  while(*f) {
    switch((uint8_t)*f) {

    case 1 ... 31:
    case '/':
    case '\\':
    case ':':
    case '?':
    case '"':
    case '|':
    case '<':
    case '>':
    case 128 ... 255:
      *f = '_';
      break;
    default:
      break;
    }
    f++;
  }
}

const char *
fa_err_code_str(fa_err_code_t errcode)
{
  switch(errcode) {
  case FAP_OK: return "OK";
  case FAP_ERROR: return "Error";
  case FAP_NEED_AUTH: return "Authentication needed";
  case FAP_NOT_SUPPORTED: return "Operation not supported";
  case FAP_PERMISSION_DENIED: return "Permission denied";
  case FAP_NOENT: return "No such entry";
  case FAP_EXIST: return "Item already exist";
  default: return "Unmapped errorcode";
  }
}
