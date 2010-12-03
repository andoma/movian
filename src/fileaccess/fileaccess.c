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
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>

#include "showtime.h"
#include "fileaccess.h"
#include "backend/backend.h"

#include <libavutil/avstring.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>

#include <fileaccess/svfs.h>

#include "fa_proto.h"
#include "fa_probe.h"
#include "blobcache.h"

struct fa_protocol_list fileaccess_all_protocols;
static URLProtocol fa_lavf_proto;


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
  if(!strncmp(url, "theme://", strlen("theme://")))
    return 1;
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
fa_open(const char *url, char *errbuf, size_t errsize)
{
  fa_protocol_t *fap;
  char *filename;
  fa_handle_t *fh;

  if((filename = fa_resolve_proto(url, &fap, NULL, errbuf, errsize)) == NULL)
    return NULL;
  
  fh = fap->fap_open(fap, filename, errbuf, errsize);
  free(filename);

  return fh;
}


/**
 *
 */
void *
fa_open_vpaths(const char *url, const char **vpaths)
{
  fa_protocol_t *fap;
  char *filename;
  fa_handle_t *fh;

  if((filename = fa_resolve_proto(url, &fap, vpaths, NULL, 0)) == NULL)
    return NULL;
  
  fh = fap->fap_open(fap, filename, NULL, 0);
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
  fh->fh_proto->fap_close(fh);
}

/**
 *
 */
int
fa_read(void *fh_, void *buf, size_t size)
{
  fa_handle_t *fh = fh_;
  return fh->fh_proto->fap_read(fh, buf, size);
}

/**
 *
 */
int64_t
fa_seek(void *fh_, int64_t pos, int whence)
{
  fa_handle_t *fh = fh_;
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
    return AVERROR_NOENT;

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
static void
move_fdes(fa_dir_t *to, fa_dir_t *from)
{
  fa_dir_entry_t *fde;

  while((fde = TAILQ_LAST(&from->fd_entries, fa_dir_entry_queue)) != NULL) {
    TAILQ_REMOVE(&from->fd_entries, fde, fde_link);
    TAILQ_INSERT_HEAD(&to->fd_entries, fde, fde_link);
  }
}


/**
 *
 */
fa_dir_t *
fa_scandir_recursive(const char *url, char *errbuf, size_t errsize,
		     int flags)
{
  fa_protocol_t *fap;
  fa_dir_t *fd;
  char *filename;
  fa_dir_entry_t *fde;

  if((filename = fa_resolve_proto(url, &fap, NULL, errbuf, errsize)) == NULL)
    return NULL;

  if(fap->fap_scan != NULL) {
    
    fd = fa_dir_alloc();
    if(!fap->fap_scan(fd, filename, errbuf, errsize)) {
      TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {
	char *s = NULL;
	if(fde->fde_type == CONTENT_DIR) {
	  s = strdup(fde->fde_url);
	} else if(flags & FA_SCAN_ARCHIVES && fde->fde_type == CONTENT_FILE) {
	  const char *f = strrchr(fde->fde_url, '.');
	  if(f != NULL && !strcasecmp(f, ".rar")) {
	    s = malloc(URL_MAX);
	    snprintf(s, URL_MAX, "rar://%s|", fde->fde_url);
	  } else if(f != NULL && !strcasecmp(f, ".zip")) {
	    s = malloc(URL_MAX);
	    snprintf(s, URL_MAX, "zip://%s|", fde->fde_url);
	  }
	}

	if(s != NULL) {
	  fa_dir_t *sub = fa_scandir_recursive(s, NULL, 0, flags);
	  free(s);
	  if(sub != NULL) {
	    move_fdes(fd, sub);
	    fa_dir_free(sub);
	  }
	}
      }
    } else {
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

  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link)
    if(!strcasecmp(fde->fde_filename, file)) {
      snprintf(fullpath, fullpathlen, "%s%s%s", path, 
	       path[strlen(path)-1] == '/' ? "" : "/",
	       fde->fde_filename);
      fa_dir_free(fd);
      return 0;
    }

  fa_dir_free(fd);
  return -1;
}


/**
 *
 */
void *
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
fa_unreference(void *fh_)
{
  fa_handle_t *fh = fh_;
  if(fh_ == NULL)
    return;

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
  TAILQ_INIT(&fd->fd_entries);
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

  fd->fd_count--;
  TAILQ_REMOVE(&fd->fd_entries, fde, fde_link);
  free(fde->fde_filename);
  free(fde->fde_url);
  free(fde);
}


/**
 *
 */
void
fa_dir_free(fa_dir_t *fd)
{
  fa_dir_entry_t *fde;

  while((fde = TAILQ_FIRST(&fd->fd_entries)) != NULL)
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
 
  fde->fde_url      = strdup(url);
  fde->fde_filename = strdup(filename);
  fde->fde_type     = type;

  TAILQ_INSERT_TAIL(&fd->fd_entries, fde, fde_link);

  fd->fd_count++;
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
int
fa_dir_entry_stat(fa_dir_entry_t *fde)
{
  if(fde->fde_statdone)
    return 0;

  if(!fa_stat(fde->fde_url, &fde->fde_stat, NULL, 0))
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
  fa_probe_init();

  LIST_FOREACH(fap, &fileaccess_all_protocols, fap_link)
    if(fap->fap_init != NULL)
      fap->fap_init();

  av_register_protocol2(&fa_lavf_proto, sizeof(fa_lavf_proto));
  return 0;
}



/**
 * lavf -> fileaccess open wrapper
 */
static int
fa_lavf_open(URLContext *h, const char *url, int flags)
{
  fa_protocol_t *fap;
  void *fh;
  char *filename;

  av_strstart(url, "showtime:", &url);  

  if((filename = fa_resolve_proto(url, &fap, NULL, NULL, 0)) == NULL)
    return AVERROR_NOENT;
  
  fh = fap->fap_open(fap, filename, NULL, 0);
  free(filename);
  
  if(fh == NULL) 
    return AVERROR_NOENT;
  
  h->priv_data = fh;
  return 0;
}

/**
 * lavf -> fileaccess read wrapper
 */
static int
fa_lavf_read(URLContext *h, unsigned char *buf, int size)
{
  return fa_read(h->priv_data, buf, size);
}

/**
 * lavf -> fileaccess seek wrapper
 */
static int64_t
fa_lavf_seek(URLContext *h, int64_t pos, int whence)
{
  if(whence == AVSEEK_SIZE)
    return fa_fsize(h->priv_data);

  return fa_seek(h->priv_data, pos, whence & ~AVSEEK_FORCE);
}

/**
 * lavf -> fileaccess close wrapper
 */
static int
fa_lavf_close(URLContext *h)
{
  fa_close(h->priv_data);
  return 0;
}


static URLProtocol fa_lavf_proto = {
    "showtime",
    fa_lavf_open,
    fa_lavf_read,
    NULL,            /* Write */
    fa_lavf_seek,
    fa_lavf_close,
};


/**
 * lavf -> fileaccess open wrapper
 */
static int
fa_lavf_open2(URLContext *h, const char *url, int flags)
{
  void *fh;

  if(sscanf(url, "%p", &fh) != 1)
    return AVERROR_NOENT;
  h->priv_data = fh;
  fa_seek(fh, 0, SEEK_SET);
  return 0;
}


/**
 *
 */
static URLProtocol fa_lavf_proto2 = {
    "showtime_ptr",
    fa_lavf_open2,
    fa_lavf_read,
    NULL,            /* Write */
    fa_lavf_seek,
    fa_lavf_close,
};


/**
 * Transform a fa_filehandle_t into FFmpeg ByteIOContext
 *
 * We do this by passing the pointer as an URL (in ascii) to the open
 * function (above). This is insanity
 */
int
fa_lavf_reopen(ByteIOContext **s, fa_handle_t *fh)
{
  char url[64];
  URLContext *uc;
  snprintf(url, sizeof(url), "%p", fh);
  if(url_open_protocol(&uc, &fa_lavf_proto2, url, 0)) {
    fa_close(fh);
    return -1;
  }

  /* Okay, see if lavf can find out anything about the file */
  if(url_fdopen(s, uc) < 0) {
    url_close(uc);
    return -1;
  }
  return 0;
}


/**
 *
 */
const char *
fa_ffmpeg_error_to_txt(int err)
{
  switch(err) {
  case AVERROR_IO:           return "I/O error";
  case AVERROR_NUMEXPECTED:  return "Number syntax expected in filename";
  case AVERROR_INVALIDDATA:  return "Invalid data found";
  case AVERROR_NOMEM:        return "Out of memory";
  case AVERROR_NOFMT:        return "Unknown format";
  case AVERROR_NOTSUPP:      return "Operation not supported";
  case AVERROR_NOENT:        return "No such file or directory";
  case AVERROR_EOF:          return "End of file";
  case AVERROR_PATCHWELCOME: return "Not yet implemented, patch welcome";
  }
  return "Unknown errorcode";
}


/**
 *
 */
void *
fa_quickload(const char *url, struct fa_stat *fs, const char **vpaths,
	     char *errbuf, size_t errlen)
{
  fa_protocol_t *fap;
  fa_handle_t *fh;
  size_t size;
  char *data, *filename;
  int r;

  data = blobcache_get(url, "fa_quickload", &size, 1);
  if(data != NULL) {
    if(fs != NULL)
      fs->fs_size = size;
    return data;
  }

  if((filename = fa_resolve_proto(url, &fap, vpaths, errbuf, errlen)) == NULL)
    return NULL;

  if(fap->fap_quickload != NULL) {
    data = fap->fap_quickload(fap, filename, fs, errbuf, errlen);

    if(fs->fs_cache_age > 0)
      blobcache_put(url, "fa_quickload", data, fs->fs_size, fs->fs_cache_age);

    free(filename);
    return data;
  }

  fh = fap->fap_open(fap, filename, errbuf, errlen);
  free(filename);

  if(fh == NULL)
    return NULL;

  size = fa_fsize(fh);
  data = malloc(size + 1);

  r = fa_read(fh, data, size);

  fa_close(fh);

  if(r != size) {
    snprintf(errbuf, errlen, "Short read");
    free(data);
    return NULL;
  }
  data[size] = 0;
  if(fs != NULL)
    fs->fs_size = size;
  return data;
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
