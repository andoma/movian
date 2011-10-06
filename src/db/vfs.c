/*
 *  Showtime mediacenter
 *  Copyright (C) 2007-2011 Andreas Öman
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "showtime.h"
#include "ext/sqlite/sqlite3.h"
#include "arch/atomic.h"
#include "misc/sha.h"
#if 0
#define VFSTRACE(x...) TRACE(TRACE_DEBUG, "SQLITE_VFS", x)
#else
#define VFSTRACE(x...)
#endif

static uint64_t random_seed;


typedef struct vfsfile {
  struct sqlite3_file hdr;
  int fd;
  char *fname;
} vfsfile_t;


static int
vfs_fs_Close(sqlite3_file *id)
{
  vfsfile_t *vf = (vfsfile_t *)id;
  close(vf->fd);
  free(vf->fname);
  return SQLITE_OK;
}


static int
vfs_fs_Read(sqlite3_file *id, void *pBuf, int amt, sqlite3_int64 offset)
{
  vfsfile_t *vf = (vfsfile_t *)id;
  int got;
  off_t pos = lseek(vf->fd, offset, SEEK_SET);
  
  if(pos != offset)
    return SQLITE_IOERR;
  
  got = read(vf->fd, pBuf, amt);
  
  VFSTRACE("Read file %s : %d bytes : %s",
	   vf->fname, amt, got == amt ? "OK" : "FAIL");

  if(got == amt)
    return SQLITE_OK;
  else if(got < 0)
    return SQLITE_IOERR;
  else {
    memset(&((char*)pBuf)[got], 0, amt-got);
    return SQLITE_IOERR_SHORT_READ;
  }
}

static int
vfs_fs_Write(sqlite3_file *id, const void *pBuf, int amt,sqlite3_int64 offset)
{
  vfsfile_t *vf = (vfsfile_t *)id;
  int got;
  off_t pos = lseek(vf->fd, offset, SEEK_SET);
  
  if(pos != offset)
    return SQLITE_IOERR;

  got = write(vf->fd, pBuf, amt);
  
  VFSTRACE("Write file %s : %d bytes : %s",
	   vf->fname, amt, got == amt ? "OK" : "FAIL");

  if(got == -1 && errno == ENOSPC)
    return SQLITE_FULL;
  if(got != amt)
    return SQLITE_IOERR_WRITE;
  return SQLITE_OK;
}
 

static int
vfs_fs_Truncate( sqlite3_file *id, sqlite3_int64 nByte )
{
  vfsfile_t *vf = (vfsfile_t *)id;
  return ftruncate(vf->fd, nByte) < 0 ? SQLITE_IOERR_TRUNCATE : SQLITE_OK;
}

static int
vfs_fs_Sync(sqlite3_file *id, int flags)
{
  return SQLITE_OK;
}

static int
vfs_fs_FileSize(sqlite3_file *id, sqlite3_int64 *pSize)
{
  vfsfile_t *vf = (vfsfile_t *)id;
  struct stat st;
  if(fstat(vf->fd, &st) < 0)
    return SQLITE_IOERR;
  *pSize = st.st_size;
  return SQLITE_OK;
}


static int vfs_fs_Lock(sqlite3_file *pFile, int eLock){
  // This is safe as long as we always run with SHARED_CACHE
  return SQLITE_OK;
}
static int vfs_fs_Unlock(sqlite3_file *pFile, int eLock){
  // This is safe as long as we always run with SHARED_CACHE
  return SQLITE_OK;
}
static int vfs_fs_CheckReservedLock(sqlite3_file *pFile, int *pResOut){
  // This is safe as long as we always run with SHARED_CACHE
  *pResOut = 0;
  return SQLITE_OK;
}

static int vfs_fs_FileControl(sqlite3_file *pFile, int op, void *pArg){
  return SQLITE_OK;
}

static int vfs_fs_SectorSize(sqlite3_file *pFile){
  return 0;
}
static int vfs_fs_DeviceCharacteristics(sqlite3_file *pFile){
  return 0;
}

static const struct sqlite3_io_methods vfs_fs_methods = {
  1,                                  /* iVersion */
  vfs_fs_Close,                       /* xClose */
  vfs_fs_Read,                        /* xRead */
  vfs_fs_Write,                       /* xWrite */
  vfs_fs_Truncate,                    /* xTruncate */
  vfs_fs_Sync,                        /* xSync */
  vfs_fs_FileSize,                    /* xFileSize */
  vfs_fs_Lock,                        /* xLock */
  vfs_fs_Unlock,                      /* xUnlock */
  vfs_fs_CheckReservedLock,           /* xCheckReservedLock */
  vfs_fs_FileControl,                 /* xFileControl */
  vfs_fs_SectorSize,                  /* xSectorSize */
  vfs_fs_DeviceCharacteristics,       /* xDeviceCharacteristics */
};





static int
vfs_open(sqlite3_vfs *pVfs, const char *zName, sqlite3_file *id, int flags,
	 int *pOutFlags)
{
  static int tmpfiletally;
  char tmpfile[256];
  int v;

  vfsfile_t *vf = (vfsfile_t *)id;
  vf->hdr.pMethods = &vfs_fs_methods;

  int isExclusive  = (flags & SQLITE_OPEN_EXCLUSIVE);
  int isDelete     = (flags & SQLITE_OPEN_DELETEONCLOSE);
  int isCreate     = (flags & SQLITE_OPEN_CREATE);
  int isReadonly   = (flags & SQLITE_OPEN_READONLY);
  int isReadWrite  = (flags & SQLITE_OPEN_READWRITE);
  int openFlags = 0;

  if( isReadonly )  openFlags |= O_RDONLY;
  if( isReadWrite ) openFlags |= O_RDWR;
  if( isCreate )    openFlags |= O_CREAT;
  if( isExclusive ) openFlags |= O_EXCL;


  if(zName == NULL) {
    v = atomic_add(&tmpfiletally, 1);
    snprintf(tmpfile, sizeof(tmpfile), "%s/sqlite.tmp.%d",
	     showtime_cache_path, v);
    zName = tmpfile;
  }

  vf->fd = open(zName, openFlags, 0666);
  vf->fname = strdup(zName);

  VFSTRACE("Open%s file %s : %s",
	   isDelete ? "+Delete" : "",
	   zName,
	   vf->fd == -1 ? "Fail" : "OK");

  if(vf->fd == -1)
    return SQLITE_IOERR;

  if(isDelete)
    unlink(zName);
  
  return SQLITE_OK;
}


static int
vfs_delete(sqlite3_vfs *NotUsed, const char *zPath, int dirSync)
{
  VFSTRACE("Delete file %s", zPath);
  unlink(zPath);
  return SQLITE_OK;
}


static int
vfs_sleep(sqlite3_vfs *NotUsed, int nMicro)
{
  sleep(nMicro / 1000000);
  usleep(nMicro % 1000000);
  return nMicro;
}


static int
vfs_getlasterror(sqlite3_vfs *NotUsed, int NotUsed2, char *NotUsed3)
{
  return 0;
}



static int
vfs_current_time64(sqlite3_vfs *NotUsed, sqlite3_int64 *piNow)
{
  static const sqlite3_int64 unixEpoch = 24405875*(sqlite3_int64)8640000;
  struct timeval sNow;
  gettimeofday(&sNow, 0);
  *piNow = unixEpoch + 1000*(sqlite3_int64)sNow.tv_sec + sNow.tv_usec/1000;
  return 0;
}

static int
vfs_current_time(sqlite3_vfs *NotUsed, double *prNow)
{
  sqlite3_int64 i;
  vfs_current_time64(0, &i);
  *prNow = i/86400000.0;
  return 0;
}

static int
vfs_full_pathname(sqlite3_vfs *pVfs, const char *zPath, int nOut, char *zOut)
{
  sqlite3_snprintf(nOut, zOut, "%s", zPath);
  return SQLITE_OK;
}


static int
vfs_access(sqlite3_vfs *NotUsed, const char *zPath, int flags, int *pResOut)
{
  struct stat st;
  *pResOut = stat(zPath, &st) == 0;
  return SQLITE_OK;
}


static int
vfs_randomness(sqlite3_vfs *NotUsed, int nBuf, char *zBuf)
{
  sha1_decl(shactx);
  uint8_t d[20];
  int w;

  while(nBuf > 0) {
    sha1_init(shactx);
    sha1_update(shactx, (void *)&random_seed, sizeof(uint64_t));
    sha1_final(shactx, d);

    w = MIN(20, nBuf);
    memcpy(zBuf, d, w);

    nBuf -= w;
    zBuf += w;
    memcpy(&random_seed, d, sizeof(uint64_t));
  }
  return SQLITE_OK;
}

static sqlite3_vfs vfs = {
  3,                 /* iVersion */
  sizeof(vfsfile_t),   /* szOsFile */
  PATH_MAX,      /* mxPathname */
  0,                 /* pNext */
  "showtime",        /* zName */
  0,                 /* pAppData */

  vfs_open,           /* xOpen */
  vfs_delete,         /* xDelete */
  vfs_access,         /* xAccess */
  vfs_full_pathname,   /* xFullPathname */
  0,         /* xDlOpen */
  0,        /* xDlError */
  0,          /* xDlSym */
  0,        /* xDlClose */
  vfs_randomness,     /* xRandomness */
  vfs_sleep,          /* xSleep */
  vfs_current_time,    /* xCurrentTime */
  vfs_getlasterror,   /* xGetLastError */
  vfs_current_time64, /* xCurrentTimeInt64 */
  0,                 /* xSetSystemCall */
  0,                 /* xGetSystemCall */
  0                  /* xNextSystemCall */
};


int
sqlite3_os_init(void)
{
  random_seed = arch_get_seed();
  sqlite3_vfs_register(&vfs, 1);
  return SQLITE_OK; 
}

int
sqlite3_os_end(void)
{
  return SQLITE_OK; 
}
