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
#include <stdio.h>
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>

#include "main.h"
#include "fileaccess.h"
#include "fa_proto.h"
#include "misc/callout.h"
#include "service.h"
#include "usage.h"

#include "ext/libntfs_ext/include/ntfs.h"
#include "ext/libntfs_ext/source/logging.h"



typedef struct fs_handle {
  fa_handle_t h;
  int fd;
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


/**
 *
 */
static int
ntfs_scandir(fa_protocol_t *fap, fa_dir_t *fd,
	     const char *url, char *errbuf, size_t errlen, int flags)
{
  DIR_ITER *di = ps3ntfs_diropen(url);
  char filename[1024];
  char buf[URL_MAX];
  struct stat st;
  int type;

  while(1) {
    if(ps3ntfs_dirnext(di, filename, &st))
      break;
    
  switch(st.st_mode & S_IFMT) {
    case S_IFDIR:
      type = CONTENT_DIR;
      break;

    case S_IFREG:
      type = CONTENT_FILE;
      break;

    default:
      continue;
    }

    fs_urlsnprintf(buf, sizeof(buf), "", url, filename);
    fa_dir_add(fd, buf, filename, type);
  }
  ps3ntfs_dirclose(di);
  return 0;
}


/**
 *
 */
static fa_handle_t *
ntfs_open(struct fa_protocol *fap, const char *url,
          char *errbuf, size_t errsize, int flags,
          struct fa_open_extra *foe)
{
  int fd;

  if(flags & FA_WRITE) {

    int open_flags = O_RDWR | O_CREAT;

    if(!(flags & FA_APPEND))
      open_flags |= O_TRUNC;

    fd = ps3ntfs_open(url, open_flags, 0666);

    if(fd >= 0 && (flags & FA_APPEND))
      ps3ntfs_seek64(fd, 0, SEEK_END);

  } else {
    fd = ps3ntfs_open(url, O_RDONLY, 0);
  }

  if(fd == -1) {
    snprintf(errbuf, errsize, "%s", strerror(ps3ntfs_errno()));
    return NULL;
  }
  fs_handle_t *fh = calloc(1, sizeof(fs_handle_t));
  fh->fd = fd;
  fh->h.fh_proto = fap;
  return &fh->h;
}


/**
 *
 */
static void
ntfs_close(fa_handle_t *fh0)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;
  ps3ntfs_close(fh->fd);
  free(fh);

}


/**
 * Read from file. Same semantics as POSIX read(2)
 */
static int
ntfs_read(fa_handle_t *fh0, void *buf, size_t size0)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;
  return ps3ntfs_read(fh->fd, buf, size0);
}



/**
 *
 */
static int
ntfs_write(fa_handle_t *fh0, const void *buf, size_t size0)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;
  return ps3ntfs_write(fh->fd, buf, size0);
}


/**
 * Seek in file. Same semantics as POSIX lseek(2)
 */
static int64_t
ntfs_seek(fa_handle_t *fh0, int64_t pos, int whence, int lazy)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;
  return ps3ntfs_seek64(fh->fd, pos, whence);
}


/**
 * Return size of file
 */
static int64_t
ntfs_fsize(fa_handle_t *fh0)
{
  fs_handle_t *fh = (fs_handle_t *)fh0;
  struct stat st;
  ps3ntfs_fstat(fh->fd, &st);
  return st.st_size;
}

/**
 * stat(2) file
 *
 * If non_interactive is set, this is probe request and it must not
 * ask for any user input (access credentials, etc)
 */
static int
ntfs_stat(struct fa_protocol *fap, const char *url, struct fa_stat *fs,
	  char *errbuf, size_t errsize, int non_interactive)
{
  struct stat st;
  if(ps3ntfs_stat(url, &st)) {
    snprintf(errbuf, errsize, "No such file");
    return FAP_ERROR;
  }
  memset(fs, 0, sizeof(struct fa_stat));
  fs->fs_size = st.st_size;
  fs->fs_mtime = st.st_mtime;
  fs->fs_type = S_ISDIR(st.st_mode) ? CONTENT_DIR : CONTENT_FILE;
  return FAP_OK;
}

static int
ntfs_unlink(const fa_protocol_t *fap, const char *url,
	    char *errbuf, size_t errlen)
{
  if(ps3ntfs_unlink(url)) {
    snprintf(errbuf, errlen, "%s", strerror(ps3ntfs_errno()));
    return -1;
  }
  return 0;
}


/**
 * Rmdir (remove directory)
 */
static int
ntfs_rmdir(const fa_protocol_t *fap, const char *url, char *errbuf, size_t errlen)
{
  return ntfs_unlink(fap, url, errbuf, errlen);
}

/**
 *
 */
static int
ntfs_rename(const fa_protocol_t *fap, const char *old, const char *new,
	    char *errbuf, size_t errlen)
{
  if(ps3ntfs_rename(old, new)) {
    snprintf(errbuf, errlen, "%s", strerror(ps3ntfs_errno()));
    return -1;
  }
  return 0;
}


/**
 *
 */
static fa_err_code_t
ntfs_makedir(struct fa_protocol *fap, const char *url)
{
  if(!ps3ntfs_mkdir(url, 0777))
    return 0;

  switch(ps3ntfs_errno()) {
    case ENOENT:  return FAP_NOENT;
    case EPERM:   return FAP_PERMISSION_DENIED;
    case EEXIST:  return FAP_EXIST;
    default:      return FAP_ERROR;
  }
  return 0;
}



static callout_t ntfs_callout;

typedef struct ntfs_dev {
  char inserted;
  char hold_counter;
  int partitions_mounted;
  ntfs_md *mounts;
  service_t **services;
} ntfs_dev_t;

static ntfs_dev_t ntfs_devs[8];

static const DISC_INTERFACE *disc_ntfs[8]= {
    &__io_ntfs_usb000,
    &__io_ntfs_usb001,
    &__io_ntfs_usb002,
    &__io_ntfs_usb003,
    &__io_ntfs_usb004,
    &__io_ntfs_usb005,
    &__io_ntfs_usb006,
    &__io_ntfs_usb007
};

/**
 *
 */
static void
ntfs_periodic(struct callout *c, void *opaque)
{
  for(int i = 0; i < 8; i++) {
    int r = PS3_NTFS_IsInserted(i);
    ntfs_dev_t *d = &ntfs_devs[i];

    if(!r) {
      d->hold_counter = 0;
      if(!d->inserted)
	continue;

      d->inserted = 0;

      if(d->partitions_mounted) {
	for(int j = 0; j < d->partitions_mounted; j++) {
	  if(d->mounts[j].name[0]) {
	    TRACE(TRACE_DEBUG, "NTFS", "Unmounting %s", d->mounts[j].name);
	    ntfsUnmount(d->mounts[j].name, 1);
	  }
	}

	free(d->mounts);
	d->mounts = NULL;

	for(int j = 0; j < d->partitions_mounted; j++)
	  service_destroy(d->services[j]);

	free(d->services);
	d->services = NULL;

	d->partitions_mounted = 0;
      }
      continue;
    }

    if(d->inserted == 2)
      continue;

    d->inserted = 1;
    if(d->hold_counter < 5) {
      d->hold_counter++;
      TRACE(TRACE_DEBUG, "NTFS", "Waiting for device %d to settle", i);
      continue;
    }


    d->inserted = 2;
    d->partitions_mounted =
      ntfsMountDevice(disc_ntfs[i], &d->mounts, NTFS_DEFAULT | NTFS_RECOVER);

    TRACE(TRACE_DEBUG, "NTFS", "Mounted %d partitions on device %d",
	  d->partitions_mounted, i);

    usage_event("NTFS Mount", 1, NULL);

    d->services = malloc(sizeof(service_t *) * d->partitions_mounted);

    for(int j = 0; j < d->partitions_mounted; j++) {
      TRACE(TRACE_DEBUG, "NTFS", "Mounted %s", d->mounts[j].name);
      char url[64];
      snprintf(url, sizeof(url), "%s://", d->mounts[j].name);
      d->services[j] = service_create_managed(url, d->mounts[j].name,
					      url, "usb", NULL, 0, 1, 
					      SVC_ORIGIN_MEDIA,
					      "External NTFS volume");
    }
  }
  callout_arm(&ntfs_callout, ntfs_periodic, NULL, 1);
}


static int
ntfs_log(const char *function, const char *file, int line,
	u32 level, void *data, const char *format, va_list args)
{
  char buf[1024];
  vsnprintf(buf, sizeof(buf), format, args);
  tracelog(TRACE_NO_PROP, TRACE_DEBUG, "NTFS", "[%x]: %s: %s:%d %s",
           (int)hts_thread_current(),
           function, file, line, buf);
  return 0;
}


/**
 *
 */
static void
ntfs_init(void)
{
  extern void ntfsInit(void);

  ntfsInit();
  if(0) {
    ntfs_log_set_levels(-1);
    ntfs_log_set_handler(ntfs_log);
  } else {
    ntfs_log_clear_levels(-1);
  }
  callout_arm(&ntfs_callout, ntfs_periodic, NULL, 1);
}


/**
 *
 */
static int
ntfs_match_proto(const char *str)
{
  if(strncmp(str, "ntfs", 4))
    return 1;
  return !(str[4] >= '0' && str[4] <= '8');
}

/**
 * Main NTFS protocol dispatch
 */
static fa_protocol_t fa_protocol_ntfs = {
  .fap_flags = FAP_INCLUDE_PROTO_IN_URL,
  .fap_init  = ntfs_init,
  .fap_match_proto = ntfs_match_proto,
  .fap_scan  = ntfs_scandir,
  .fap_open  = ntfs_open,
  .fap_close = ntfs_close,
  .fap_read  = ntfs_read,
  .fap_write = ntfs_write,
  .fap_seek  = ntfs_seek,
  .fap_fsize = ntfs_fsize,
  .fap_stat  = ntfs_stat,
  .fap_unlink= ntfs_unlink,
  .fap_rmdir = ntfs_rmdir,
  .fap_rename = ntfs_rename,
  .fap_makedir = ntfs_makedir,


};
FAP_REGISTER(ntfs);
