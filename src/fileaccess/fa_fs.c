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
#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include "main.h"
#include "fileaccess.h"
#include "misc/minmax.h"

#include "fa_proto.h"

#if defined(__APPLE__) || (defined(__linux__) && !defined(__ANDROID__))
#define HAVE_XATTR
#include <sys/xattr.h>
#endif


typedef struct part {
  int fd;
  int64_t size;
} part_t;

typedef struct fs_handle {
  fa_handle_t h;
  int part_count;
  int64_t total_size; // Only valid if part_count != 1
  int64_t read_pos;
  part_t parts[0];
} fs_handle_t;



static void
fs_urlsnprintf(char *buf, size_t bufsize, const char *prefix, const char *base,
	       const char *fname)
{
  int blen = strlen(base);
  if(!strcmp(base, "/"))
    base = "";

  snprintf(buf, bufsize, "%s%s%s%s", prefix, base,
	   blen > 0 && base[blen - 1] == '/' ? "" : "/", fname);
}

static int
is_splitted_file_name(const char *s)
{
  // foobar.00x
  size_t size = strlen(s);
  if(size < 4)
    return 0;
  s += size - 4;

  if(s[0] == '.' &&
     s[1] >= '0' && s[1] <= '9' &&
     s[2] >= '0' && s[2] <= '9' &&
     s[3] >= '0' && s[3] <= '9')
    return atoi(s+1);
  return 0;
}

static int
file_exists(char *fn)
{
  struct stat st;
  return !stat(fn,&st) && !S_ISDIR(st.st_mode);
}

static void
get_split_piece_name(char *dst, size_t dstlen, const char *fn, int num)
{
  snprintf(dst, dstlen, "%s.%03d", fn, num + 1);
}

static int
split_exists(const char* fn,int num)
{
  char buf[PATH_MAX];
  get_split_piece_name(buf, sizeof(buf), fn, num);
  return file_exists(buf);
}

static unsigned int
get_split_piece_count(const char *fn)
{
  int count = 0;
  while(split_exists(fn, count))
    count++;
  return count;
}

static int
fs_scandir(fa_protocol_t *fap, fa_dir_t *fd, const char *url,
           char *errbuf, size_t errlen, int flags)
{
  char buf[URL_MAX];
  struct stat st;
  struct dirent *d;
  int type;
  DIR *dir;
  int split_num;

  if((dir = opendir(url)) == NULL) {
    snprintf(errbuf, errlen, "%s", strerror(errno));
    return -1;
  }

  while((d = readdir(dir)) != NULL) {
    fs_urlsnprintf(buf, sizeof(buf), "", url, d->d_name);

    if(stat(buf, &st))
      continue;

    switch(st.st_mode & S_IFMT) {
    case S_IFDIR:
      type = CONTENT_DIR;
      break;

    case S_IFREG:
      type = CONTENT_FILE;

      if((split_num = is_splitted_file_name(d->d_name)) > 0) {
        if(split_num != 1)
          continue;
        // Strip off last part (.00x) of filename
        buf[strlen(buf) - 4] = 0;
        d->d_name[strlen(d->d_name) - 4] = 0;
      }
      break;

    default:
      continue;
    }

    fs_urlsnprintf(buf, sizeof(buf), "file://", url, d->d_name);
    fa_dir_add(fd, buf, d->d_name, type);
  }
  closedir(dir);
  return 0;
}


/**
 * Close file
 */
static void
fs_close(fa_handle_t *fh0)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;
  int i;
  for(i = 0; i < fh->part_count; i++)
    if(fh->parts[i].fd != -1)
      close(fh->parts[i].fd);
  free(fh);
}


/**
 * Open file
 */
static fa_handle_t *
fs_open(fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen,
	int flags, struct fa_open_extra *foe)
{
  fs_handle_t *fh=NULL;
  struct stat st;
  int i;
  int fd;


  if(!strcmp(url, "/dev/stdout") || !strcmp(url, "/dev/stderr")) {
    fd = open(url, O_WRONLY);
    if(fd == -1) {
      snprintf(errbuf, errlen, "%s", strerror(errno));
      return NULL;
    }
    goto open_ok;
  }

  
  if(flags & FA_WRITE) {

    int open_flags = O_RDWR | O_CREAT;

    if(!(flags & FA_APPEND))
      open_flags |= O_TRUNC;

    fd = open(url, open_flags, 0666);
    if(fd == -1) {
      snprintf(errbuf, errlen, "%s", strerror(errno));
      return NULL;
    }


    if(flags & FA_APPEND)
      lseek(fd, 0, SEEK_END);

    goto open_ok;
  }

  fd = open(url, O_RDONLY, 0);

  if(fd == -1) {
    snprintf(errbuf, errlen, "%s", strerror(errno));
    int c = get_split_piece_count(url);
    if(c == 0)
      return NULL;

    fh = calloc(1, sizeof(fs_handle_t) + sizeof(part_t) * c);
    for(i = 0; i < c; i++)
      fh->parts[i].fd = -1;

    fh->part_count = c;
    for(i = 0; i < c; i++) {
      char buf[PATH_MAX];
      get_split_piece_name(buf, sizeof(buf), url, i);

      if((fh->parts[i].fd = open(buf, O_RDONLY)) == -1) {
        snprintf(errbuf, errlen, "%s", strerror(errno));
        fs_close(&fh->h);
        return NULL;
      }

      if(fstat(fh->parts[i].fd, &st)) {
        snprintf(errbuf, errlen, "%s", strerror(errno));
        fs_close(&fh->h);
        return NULL;
      }

      fh->parts[i].size = st.st_size;
      fh->total_size += st.st_size;
    }
  } else {
    // normal file
  open_ok:
    fh = calloc(1, sizeof(fs_handle_t) + sizeof(part_t));
    fh->part_count = 1;
    fh->parts[0].fd = fd;
  }

  fh->h.fh_proto = fap;
  return &fh->h;
}


static int
get_current_read_piece_num(fs_handle_t *fh)
{
  int i = 0;
  int64_t size = fh->parts[0].size;
  for(i = 0; i < fh->part_count; i++) {
    if(fh->read_pos <= size)
      break;
    size += fh->parts[i+1].size;
  }
  return MIN(i, fh->part_count - 1);
}

/**
 * Read from file
 */
static int
fs_read(fa_handle_t *fh0, void *buf, size_t size)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;
  if(fh->part_count == 1)
    return read(fh->parts[0].fd, buf, size);

  int pn = get_current_read_piece_num(fh);
  int rsize = read(fh->parts[pn].fd, buf, size);
  if(rsize < size && pn < fh->part_count - 1) {
    lseek(fh->parts[pn + 1].fd, 0, SEEK_SET);
    rsize += read(fh->parts[pn + 1].fd, buf + rsize, size - rsize);
  }
  fh->read_pos += rsize;
  return rsize;
}


/**
 * Write to file
 */
static int
fs_write(fa_handle_t *fh0, const void *buf, size_t size)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;
  if(fh->part_count == 1)
    return write(fh->parts[0].fd, buf, size);
  return 0;
}

/**
 * Seek in file
 */
static int64_t
fs_seek(fa_handle_t *fh0, int64_t pos, int whence, int lazy)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;

  if(fh->part_count == 1)
    return lseek(fh->parts[0].fd, pos, whence);

  int64_t act_pos = fh->read_pos;
  int i;
  int pn;

  switch(whence) {
  case SEEK_SET:
    act_pos = pos;
    break;

  case SEEK_CUR:
    act_pos += pos;
    break;

  case SEEK_END:
    act_pos = fh->total_size - pos;
    break;
  }

  fh->read_pos = act_pos;
  pn = get_current_read_piece_num(fh);
  pos = act_pos;
  for(i = 0; i < pn ;i++)
    pos = pos - fh->parts[i].size;

  return lseek(fh->parts[pn].fd, pos, SEEK_SET);
}

/**
 * Return size of file
 */
static int64_t
fs_fsize(fa_handle_t *fh0)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;
  if(fh->part_count == 1) {
    struct stat st;
    if(fstat(fh->parts[0].fd, &st) < 0)
      return -1;
    return st.st_size;
  }

  return fh->total_size;
}


/**
 * Standard unix stat
 */
static int
fs_stat(fa_protocol_t *fap, const char *url, struct fa_stat *fs,
	int flags, char *errbuf, size_t errlen)
{
  struct stat st;
  int piece_num,i;
  if(stat(url, &st)) {
    snprintf(errbuf, errlen, "%s", strerror(errno));

    piece_num = get_split_piece_count(url);
    if(piece_num == 0)
      return FAP_ERROR;

    memset(fs, 0, sizeof(struct fa_stat));
    for(i = 0; i < piece_num; i++) {
      char buf[PATH_MAX];
      get_split_piece_name(buf, sizeof(buf), url,i);

      if(stat(buf, &st)) {
        snprintf(errbuf, errlen, "%s", strerror(errno));
        return FAP_ERROR;
      }
      fs->fs_size += st.st_size;
      fs->fs_mtime = st.st_mtime;
      fs->fs_type = CONTENT_FILE;
    }
    return FAP_OK;
  }

  memset(fs, 0, sizeof(struct fa_stat));
  fs->fs_size = st.st_size;
  fs->fs_mtime = st.st_mtime;
  fs->fs_type = S_ISDIR(st.st_mode) ? CONTENT_DIR : CONTENT_FILE;
  return FAP_OK;
}

/**
 * Rmdir (remove directory)
 */
static int
fs_rmdir(const fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen)
{
  if(rmdir(url)) {
    snprintf(errbuf, errlen, "%s", strerror(errno));
    return -1;
  }
  return 0;
}


/**
 * Unlink (remove file)
 */
static int
fs_unlink(const fa_protocol_t *fap, const char *url,
          char *errbuf, size_t errlen)
{
  if(unlink(url)) {
    int piece_num,i;
    snprintf(errbuf, errlen, "%s", strerror(errno));

    piece_num = get_split_piece_count(url);
    if(piece_num == 0)
      return -1;

    for(i = 0; i < piece_num; i++) {
      char buf[PATH_MAX];
      get_split_piece_name(buf, sizeof(buf), url,i);

      if(unlink(buf)) {
        snprintf(errbuf, errlen, "%s", strerror(errno));
        return -1;
      }
    }
    return 0;
  }
  return 0;
}



/**
 *
 */
static fa_err_code_t
fs_makedir(struct fa_protocol *fap, const char *url)
{
  if(mkdir(url, 0770)) {
    switch(errno) {
    case ENOENT:  return FAP_NOENT;
    case EPERM:   return FAP_PERMISSION_DENIED;
    case EEXIST:  return FAP_EXIST;
    default:      return FAP_ERROR;
    }
  }
  return 0;
}


/**
 *
 */
static int
fs_rename(const fa_protocol_t *fap, const char *old, const char *new,
          char *errbuf, size_t errlen)
{
  if(rename(old, new)) {
    snprintf(errbuf, errlen, "%s", strerror(errno));
    return -1;
  }
  return 0;
}

/**
 * FS change notification 
 */
#if 0
#if ENABLE_INOTIFY
#include <sys/inotify.h>
#include <poll.h>

typedef struct notify_created_file {
  char *name;
  LIST_ENTRY(notify_created_file) link;

} notify_created_file_t;



static void
fs_notify(struct fa_protocol *fap, const char *url,
	  void *opaque,
	  void (*change)(void *opaque,
			 fa_notify_op_t op, 
			 const char *filename,
			 const char *url,
			 int type),
	  int (*breakcheck)(void *opaque))
{
  int fd, n;
  char buf[1024];
  char buf2[URL_MAX];
  struct pollfd fds;
  struct inotify_event *e;
  LIST_HEAD(, notify_created_file) pending_create;
  notify_created_file_t *ncf;

  if((fd = inotify_init()) == -1)
    return;

  fds.fd = fd;
  fds.events = POLLIN;

  if(inotify_add_watch(fd, url, IN_ONLYDIR | IN_CREATE | IN_CLOSE_WRITE | 
		       IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO) == -1) {
    TRACE(TRACE_DEBUG, "FS", "Unable to watch %s -- %s",
	  url, strerror(errno));
    close(fd);
    return;
  }

  LIST_INIT(&pending_create);
  
  while(1) {
    n = poll(&fds, 1, 1000);
    
    if(n < 0)
      break;

    if(breakcheck(opaque))
      break;

    if(n != 1)
      continue;
      
    n = read(fd, buf, sizeof(buf));
    e = (struct inotify_event *)&buf[0];

    if(e->len == 0)
      continue;

    fs_urlsnprintf(buf2, sizeof(buf2), "file://", url, e->name);

    if(e->mask & IN_CREATE) {
      if(e->mask & IN_ISDIR) {
	TRACE(TRACE_DEBUG, "FS", "Directory %s created in %s", e->name, url);
	change(opaque, FA_NOTIFY_ADD, e->name, buf2, CONTENT_DIR);
      } else {
	ncf = malloc(sizeof(notify_created_file_t));
	ncf->name = strdup(e->name);
	LIST_INSERT_HEAD(&pending_create, ncf, link);
	TRACE(TRACE_DEBUG, "FS", "File %s created in %s", e->name, url);
      }
    }

    if(e->mask & IN_CLOSE_WRITE) {

      LIST_FOREACH(ncf, &pending_create, link) 
	if(!strcmp(ncf->name, e->name))
	  break;

      if(ncf != NULL) {
	TRACE(TRACE_DEBUG, "FS", "File %s created and closed", e->name);
	change(opaque, FA_NOTIFY_ADD, e->name, buf2, 
	       e->mask & IN_ISDIR ? CONTENT_DIR : CONTENT_FILE);
	LIST_REMOVE(ncf, link);
	free(ncf->name);
	free(ncf);
      }
    }

    if(e->mask & IN_DELETE) {
      TRACE(TRACE_DEBUG, "FS", "File %s deleted", e->name);
      change(opaque, FA_NOTIFY_DEL, e->name, buf2, 
	     e->mask & IN_ISDIR ? CONTENT_DIR : CONTENT_FILE);
    }

    if(e->mask & IN_MOVED_FROM) {
      TRACE(TRACE_DEBUG, "FS", "File %s moved away from %s", e->name, url);
      change(opaque, FA_NOTIFY_DEL, e->name, buf2, 
	     e->mask & IN_ISDIR ? CONTENT_DIR : CONTENT_FILE);
    }

    if(e->mask & IN_MOVED_TO) {
      TRACE(TRACE_DEBUG, "FS", "File %s moved in to %s", e->name, url);
      change(opaque, FA_NOTIFY_ADD, e->name, buf2, 
	     e->mask & IN_ISDIR ? CONTENT_DIR : CONTENT_FILE);
    }
  }

  while((ncf = LIST_FIRST(&pending_create)) != NULL) {
    LIST_REMOVE(ncf, link);
    free(ncf->name);
    free(ncf);
  }

  close(fd);
}

#endif
#endif

#if ENABLE_FSEVENTS
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>

struct fs_notify_aux {
  fa_handle_t h;

  void *opaque;
  void (*change)(void *opaque,
		 fa_notify_op_t op,
		 const char *filename,
		 const char *url,
		 int type);

  FSEventStreamRef fse;
};


static void
fs_notify_callback(ConstFSEventStreamRef streamRef,
		   void *clientCallBackInfo,
		   size_t numEvents,
		   void *eventPaths,
		   const FSEventStreamEventFlags eventFlags[],
		   const FSEventStreamEventId eventIds[])
{
  struct fs_notify_aux *fna = clientCallBackInfo;
  fna->change(fna->opaque, FA_NOTIFY_DIR_CHANGE, NULL, NULL, 0);
}


/**
 *
 */
static fa_handle_t *
fs_notify_start(struct fa_protocol *fap, const char *url,
                void *opaque,
                void (*change)(void *opaque,
                               fa_notify_op_t op,
                               const char *filename,
                               const char *url,
                               int type))
{
  FSEventStreamContext ctx = {0};
  struct fs_notify_aux *fna = calloc(1, sizeof(struct fs_notify_aux));
  fna->opaque = opaque;
  fna->change = change;
  ctx.info = fna;

  CFStringRef p = CFStringCreateWithCString(NULL, url, kCFStringEncodingUTF8);

  CFArrayRef paths = CFArrayCreate(NULL, (const void **)&p, 1, NULL);

  fna->fse = FSEventStreamCreate(kCFAllocatorDefault,
                                 fs_notify_callback, &ctx, paths,
                                 kFSEventStreamEventIdSinceNow,
                                 0.1, 0);
  CFRelease(paths);
  CFRelease(p);

  FSEventStreamScheduleWithRunLoop(fna->fse, CFRunLoopGetMain(),
				   kCFRunLoopDefaultMode);
  FSEventStreamStart(fna->fse);
  return &fna->h;
}


static void
fs_notify_stop(fa_handle_t *fh)
{
  struct fs_notify_aux *fna = (struct fs_notify_aux *)fh;
  FSEventStreamStop(fna->fse);
  FSEventStreamInvalidate(fna->fse);
  FSEventStreamRelease(fna->fse);
  free(fna);
}

#endif


#if ENABLE_REALPATH
/**
 *
 */
static int
fs_normalize(struct fa_protocol *fap, const char *url, char *dst, size_t dstlen)
{
  char res[PATH_MAX];
  
  if(realpath(url, res) == NULL)
    return -1;
  snprintf(dst, dstlen, "file://%s", res);
  return 0;
}
#endif


#ifdef HAVE_XATTR
/**
 *
 */
static fa_err_code_t
fs_set_xattr(struct fa_protocol *fap, const char *url,
             const char *name,
             const void *data, size_t len)
{
#ifdef __linux__
  char name2[512];
  snprintf(name2, sizeof(name2), "user.%s", name);
  name = name2;
#endif

  if(data == NULL) {
    removexattr(url, name
#ifdef __APPLE__
                ,0
#endif
                );
    return 0;
  }

  if(!setxattr(url, name, data, len
#ifdef __APPLE__
               , 0
#endif
               , 0))
    return 0;

  switch(errno) {
  case EROFS:
    return FAP_PERMISSION_DENIED;
  case ENOTSUP:
    return FAP_NOT_SUPPORTED;
  default:
    return FAP_ERROR;
  }
}


/**
 *
 */
static fa_err_code_t
fs_get_xattr(struct fa_protocol *fap, const char *url,
             const char *name,
             void **datap, size_t *lenp)
{
#ifdef __linux__
  char name2[512];
  snprintf(name2, sizeof(name2), "user.%s", name);
  name = name2;
#endif

  int len = getxattr(url, name, NULL, 0
#ifdef __APPLE__
                     ,0, 0
#endif
                     );
  if(len < 0) {
    switch(errno) {
#ifdef __APPLE__
    case ENOATTR:
#else
    case ENODATA:
#endif
      *datap = NULL;
      *lenp = 0;
      return 0;
    case ENOTSUP:
      return FAP_NOT_SUPPORTED;
    default:
      return FAP_ERROR;
    }
  }
  *datap = malloc(len);
  *lenp = len;
  if(getxattr(url, name, *datap, len
#ifdef __APPLE__
              , 0, 0
#endif
              ) < 0) {
    free(*datap);
    *datap = NULL;
    return FAP_ERROR;
  }
  return 0;
}
#endif



#if defined(__APPLE__) || defined(__linux__)

#if defined(__APPLE__)
#include <sys/param.h>
#include <sys/mount.h>
#else
#include <sys/vfs.h>
#endif

static fa_err_code_t
fs_fsinfo(struct fa_protocol *fap, const char *url, fa_fsinfo_t *ffi)
{
  struct statfs f;
  if(statfs(url, &f))
    return FAP_ERROR;

  ffi->ffi_size  = (int64_t)f.f_bsize * f.f_blocks;
  ffi->ffi_avail = (int64_t)f.f_bsize * f.f_bavail;
  return 0;
}

#elif defined(__PPU__)

#include <psl1ght/lv2.h>

static fa_err_code_t
fs_fsinfo(struct fa_protocol *fap, const char *url, fa_fsinfo_t *ffi)
{
  const char *path = "/dev_hdd0/game/HTSS00003/";

  if(mystrbegins(url, "/dev_hdd0/game/HTSS00003/") == NULL)
    return FAP_NOT_SUPPORTED;

  int r = Lv2Syscall3(840,
                      (uint64_t)path,
                      (uint64_t)&ffi->ffi_size,
                      (uint64_t)&ffi->ffi_avail);

  return r ? FAP_ERROR : 0;
}

#else
#error Not sure how to do fs_fsinfo()
#endif


static int
fs_ftruncate(fa_handle_t *fh0, uint64_t newsize)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;
  if(fh->part_count == 1 && !ftruncate(fh->parts[0].fd, newsize))
    return FAP_OK;
  return FAP_ERROR;
}


fa_protocol_t fa_protocol_fs = {
  .fap_name = "file",
  .fap_scan = fs_scandir,
  .fap_open  = fs_open,
  .fap_close = fs_close,
  .fap_read  = fs_read,
  .fap_write = fs_write,
  .fap_seek  = fs_seek,
  .fap_fsize = fs_fsize,
  .fap_stat  = fs_stat,
  .fap_unlink= fs_unlink,
  .fap_rmdir = fs_rmdir,
  .fap_rename = fs_rename,
#if ENABLE_INOTIFY
  //  .fap_notify = fs_notify,
#endif
#if ENABLE_FSEVENTS
  .fap_notify_start = fs_notify_start,
  .fap_notify_stop  = fs_notify_stop,
#endif
#if ENABLE_REALPATH
  .fap_normalize = fs_normalize,
#endif
  .fap_makedir = fs_makedir,

#ifdef HAVE_XATTR
  .fap_set_xattr = fs_set_xattr,
  .fap_get_xattr = fs_get_xattr,
#endif

  .fap_fsinfo = fs_fsinfo,
  .fap_ftruncate = fs_ftruncate,

};

FAP_REGISTER(fs);
