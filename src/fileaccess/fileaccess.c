/*
 *  File access common functions
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

#include "showtime.h"
#include "fileaccess.h"
#include "backend/backend.h"

#include "fileaccess/svfs.h"

#include "fa_proto.h"
#include "fa_probe.h"
#include "fa_imageloader.h"
#include "blobcache.h"
#include "htsmsg/htsbuf.h"
#include "fa_indexer.h"

struct fa_protocol_list fileaccess_all_protocols;


/**
 *
 */
static char *
fa_resolve_proto(const char *url, fa_protocol_t **p,
		 const char **vpaths, char *errbuf, size_t errsize)
{
  extern fa_protocol_t fa_protocol_fs;
  struct fa_stat fs;
  fa_protocol_t *fap;
  const char *url0 = url;
  char buf[URL_MAX];
  int n = 0;

  while(*url != ':' && *url>31 && n < sizeof(buf) - 1)
    buf[n++] = *url++;

  buf[n] = 0;

  if(url[0] != ':' || url[1] != '/' || url[2] != '/') {
    /* No protocol specified, assume a plain file */
    fap = &fa_protocol_fs;
    if(url0[0] != '/' && fap->fap_stat(fap, url0, &fs, NULL, 0, 0)) {
      snprintf(errbuf, errsize, "File not found");
      return NULL;
    }
    *p = fap;
    return strdup(url0);
  }

  url += 3;

  if(!strcmp("dataroot", buf)) {
    const char *pfx = showtime_dataroot();
    snprintf(buf, sizeof(buf), "%s%s%s", 
	     pfx, pfx[strlen(pfx) - 1] == '/' ? "" : "/", url);
    return fa_resolve_proto(buf, p, NULL, errbuf, errsize);
  }

  if(vpaths != NULL) {
    
    while(*vpaths != NULL) {
      if(!strcmp(vpaths[0], buf)) {
	const char *pfx = vpaths[1];
	snprintf(buf, sizeof(buf), "%s%s%s", 
		 pfx, pfx[strlen(pfx) - 1] == '/' ? "" : "/", url);
	return fa_resolve_proto(buf, p, NULL, errbuf, errsize);
      }
      vpaths += 2;
    }
  }

  LIST_FOREACH(fap, &fileaccess_all_protocols, fap_link) {
    if(strcmp(fap->fap_name, buf))
      continue;
    *p = fap;
    return strdup(fap->fap_flags & FAP_INCLUDE_PROTO_IN_URL ? url0 : url);
  }
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

  // XXX: Not good, should send vpaths in here instead
  if(!strncmp(url, "skin://", strlen("skin://")))
    return 1;

  if((filename = fa_resolve_proto(url, &fap, NULL, errbuf, errsize)) == NULL)
    return 0;
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

  if((filename = fa_resolve_proto(url, &fap, NULL, NULL, 0)) == NULL)
    return -1;
  
  r = fap->fap_normalize ? fap->fap_normalize(fap, url, dst, dstlen) : -1;
  free(filename);
  return r;
}


/**
 *
 */
void *
fa_open_ex(const char *url, char *errbuf, size_t errsize, int flags,
	   struct prop *stats)
{
  fa_protocol_t *fap;
  char *filename;
  fa_handle_t *fh;

#if ENABLE_READAHEAD_CACHE
  if(flags & FA_CACHE)
    return fa_cache_open(url, errbuf, errsize, flags & ~FA_CACHE, stats);
#endif
  if(flags & (FA_BUFFERED_SMALL | FA_BUFFERED_BIG))
    return fa_buffered_open(url, errbuf, errsize, flags, stats);

  if((filename = fa_resolve_proto(url, &fap, NULL, errbuf, errsize)) == NULL)
    return NULL;
  
  fh = fap->fap_open(fap, filename, errbuf, errsize, flags, stats);
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
fa_open_vpaths(const char *url, const char **vpaths,
	       char *errbuf, size_t errsize, int flags)
{
  fa_protocol_t *fap;
  char *filename;
  fa_handle_t *fh;

  if((filename = fa_resolve_proto(url, &fap, vpaths, errbuf, errsize)) == NULL)
    return NULL;
  
  if(flags & (FA_BUFFERED_SMALL | FA_BUFFERED_BIG))
    return fa_buffered_open(url, errbuf, errsize, flags, NULL);

  fh = fap->fap_open(fap, filename, errbuf, errsize, flags, NULL);
#ifdef FA_DUMP
  fh->fh_dump_fd = -1;
#endif
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
int
fa_seek_is_fast(void *fh_)
{
  fa_handle_t *fh = fh_;
  if(fh->fh_proto->fap_seek_is_fast != NULL)
    return fh->fh_proto->fap_seek_is_fast(fh);
  return 1;
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
int64_t
fa_seek(void *fh_, int64_t pos, int whence)
{
  fa_handle_t *fh = fh_;
#ifdef FA_DUMP
  if(fh->fh_dump_fd != -1) {
    printf("--------------------- Dumpfile seek to %ld (%d)\n", pos, whence);
    lseek(fh->fh_dump_fd, pos, whence);
  }
#endif
  return fh->fh_proto->fap_seek(fh, pos, whence);
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
fa_stat(const char *url, struct fa_stat *buf, char *errbuf, size_t errsize)
{
  fa_protocol_t *fap;
  char *filename;
  int r;

  if((filename = fa_resolve_proto(url, &fap, NULL, errbuf, errsize)) == NULL)
    return -1;

  r = fap->fap_stat(fap, filename, buf, errbuf, errsize, 0);
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

  if((filename = fa_resolve_proto(url, &fap, NULL, errbuf, errsize)) == NULL)
    return NULL;

  if(fap->fap_scan != NULL) {
    fd = fa_dir_alloc();
    if(fap->fap_scan(fd, filename, errbuf, errsize)) {
      fa_dir_free(fd);
      fd = NULL;
    }
  } else {
    snprintf(errbuf, errsize, "Protocol does not implement directory scanning");
    fd = NULL;
  }
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

  if((filename = fa_resolve_proto(url, &fap, NULL, NULL, 0)) == NULL)
    return NULL;

  fh = fap->fap_reference != NULL ? fap->fap_reference(fap, filename) : NULL;
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
int
fa_notify(const char *url, void *opaque,
	  void (*change)(void *opaque,
			 fa_notify_op_t op, 
			 const char *filename,
			 const char *url,
			 int type),
	  int (*breakcheck)(void *opaque))
{
  fa_protocol_t *fap;
  char *filename;

  if((filename = fa_resolve_proto(url, &fap, NULL, NULL, 0)) == NULL)
    return -1;

  if(fap->fap_notify == NULL) {
    free(filename);
    return -1;
  }

  fap->fap_notify(fap, filename, opaque, change, breakcheck);
  free(filename);
  return 0;
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

  if(fde->fde_md != NULL)
    metadata_destroy(fde->fde_md);

  fd->fd_count--;
  fa_dir_remove(fd, fde);
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
 *
 */
void
fileaccess_register_entry(fa_protocol_t *fap)
{
  LIST_INSERT_HEAD(&fileaccess_all_protocols, fap, fap_link);
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

#if ENABLE_READAHEAD_CACHE
  fa_cache_init();
#endif

  fa_indexer_init();
  return 0;
}


/**
 *
 */
buf_t *
fa_load(const char *url, const char **vpaths,
	char *errbuf, size_t errlen, int *cache_control, int flags,
	fa_load_cb_t *cb, void *opaque)
{
  fa_protocol_t *fap;
  fa_handle_t *fh;
  char *filename;
  int r;
  char *etag = NULL;
  time_t mtime = 0;
  int is_expired = 0;
  buf_t *buf = NULL;

  if((filename = fa_resolve_proto(url, &fap, vpaths, errbuf, errlen)) == NULL)
    return NULL;

  if(fap->fap_load != NULL) {
    buf_t *data2;
    int max_age = 0;

    if(cache_control != BYPASS_CACHE && cache_control != DISABLE_CACHE) {
      buf = blobcache_get(url, "fa_load", 1, &is_expired, &etag, &mtime);

      if(buf != NULL) {
	if(cache_control != NULL) {
	  // Upper layer can deal with expired data, pass it
	  *cache_control = is_expired;
	  free(etag);
	  free(filename);
	  return buf;
	}
	
	// It was not expired, return it
	if(!is_expired) {
	  free(etag);
	  free(filename);
	  return buf;
	}
      } else if(cache_control != NULL) {
	snprintf(errbuf, errlen, "Not cached");
	free(etag);
	free(filename);
	return NULL;
      }
    }

    if(cache_control == BYPASS_CACHE)
      blobcache_get_meta(url, "fa_load", &etag, &mtime);
    
    data2 = fap->fap_load(fap, filename, errbuf, errlen,
			  &etag, &mtime, &max_age, flags, cb, opaque);
    
    free(filename);
    if(data2 == NOT_MODIFIED) {
      if(cache_control == BYPASS_CACHE)
	return NOT_MODIFIED;

      free(etag);
      return buf;
    }

    buf_release(buf);

    int d;
    if(data2 && cache_control != DISABLE_CACHE &&
       (cache_control || max_age || etag || mtime)) {
      d = blobcache_put(url, "fa_load", data2, max_age, etag, mtime);
    } else {
      d = 0;
    }
    free(etag);

    if(cache_control == BYPASS_CACHE && d) {
      free(data2);
      return NOT_MODIFIED;
    }

    return data2;
  }

  fh = fap->fap_open(fap, filename, errbuf, errlen, 0, 0);
#ifdef FA_DUMP
  fh->fh_dump_fd = -1;
#endif
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
    }
  }
  return -1;
}


int
fa_check_url(const char *url, char *errbuf, size_t errlen)
{
  fa_protocol_t *fap;
  char *filename;
  int r;
  struct fa_stat fs;

  if((filename = fa_resolve_proto(url, &fap, NULL, errbuf, errlen)) == NULL)
    return BACKEND_PROBE_NO_HANDLER;

  
  r = fap->fap_stat(fap, filename, &fs, errbuf, errlen, 1);
  free(filename);

  if(r == 0)
    return BACKEND_PROBE_OK;
  if(r == FAP_STAT_NEED_AUTH) {
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
  int l1 = strlen(p1);
  int sep = l1 > 0 && p1[l1 - 1] == '/';
  snprintf(dst, dstlen, "%s%s%s", p1, sep ? "" : "/", p2);
}



/**
 *
 */
void
fa_url_get_last_component(char *dst, size_t dstlen, const char *url)
{
  int e, b;
  fa_protocol_t *fap;
  char *filename;

  if(dstlen == 0)
    return;

  if((filename = fa_resolve_proto(url, &fap, NULL, NULL, 0)) != NULL) {
    if(fap->fap_get_last_component != NULL) {
      fap->fap_get_last_component(fap, filename, dst, dstlen);
      free(filename);
      return;
    }
    free(filename);
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
buf_t *
fa_load_and_close(fa_handle_t *fh)
{
  size_t r;
  size_t size = fa_fsize(fh);
  if(size == -1)
    return NULL;

  uint8_t *mem = mymalloc(size+1);
  if(mem == NULL)
    return NULL;

  fa_seek(fh, 0, SEEK_SET);
  r = fa_read(fh, mem, size);
  fa_close(fh);

  if(r != size) {
    free(mem);
    return NULL;
  }

  mem[size] = 0;
  return buf_create_and_adopt(size, mem, &free);
}


/**
 *
 */
buf_t *
fa_load_query(const char *url0,
	      char *errbuf, size_t errlen, int *cache_control,
	      const char **arguments, int flags)
{
  htsbuf_queue_t q;
  htsbuf_queue_init(&q, 0);

  htsbuf_append(&q, url0, strlen(url0));
  if(arguments != NULL) {
    const char **args = arguments;
    char prefix = '?';
    
    while(args[0] != NULL) {
      if(args[1] != NULL) {
	htsbuf_append(&q, &prefix, 1);
	htsbuf_append_and_escape_url(&q, args[0]);
	htsbuf_append(&q, "=", 1);
	htsbuf_append_and_escape_url(&q, args[1]);
	prefix = '&';
      }
      args += 2;
    }
  }
  
  char *url = htsbuf_to_string(&q);

  buf_t *b = fa_load(url, NULL, errbuf, errlen, cache_control, flags,
                     NULL, NULL);
  free(url);
  htsbuf_queue_flush(&q);
  return b;
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
