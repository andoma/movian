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
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <sqlite3.h>

#include "main.h"
#include "arch/atomic.h"
#include "misc/sha.h"
#include "misc/minmax.h"
#include "fileaccess/fileaccess.h"

#include "arch/arch.h"

#if 0
#define VFSTRACE(x...) TRACE(TRACE_DEBUG, "SQLITE_VFS", x)
#else
#define VFSTRACE(x...)
#endif

typedef struct vfsfile {
  struct sqlite3_file hdr;
  fa_handle_t *fh;
  char *fname;
} vfsfile_t;


static int
vfs_fs_Close(sqlite3_file *id)
{
  vfsfile_t *vf = (vfsfile_t *)id;
  if(vf->fh != NULL)
    fa_close(vf->fh);
  free(vf->fname);
  return SQLITE_OK;
}


static int
vfs_fs_Read(sqlite3_file *id, void *pBuf, int amt, sqlite3_int64 offset)
{
  vfsfile_t *vf = (vfsfile_t *)id;
  int got;
  int64_t pos = fa_seek(vf->fh, offset, SEEK_SET);
  
  if(pos != offset)
    return SQLITE_IOERR;
  
  got = fa_read(vf->fh, pBuf, amt);
  
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
  int64_t pos = fa_seek(vf->fh, offset, SEEK_SET);
  
  if(pos != offset)
    return SQLITE_IOERR;

  got = fa_write(vf->fh, pBuf, amt);
  
  VFSTRACE("Write file %s : %d bytes : %s",
	   vf->fname, amt, got == amt ? "OK" : "FAIL");

#if 0
  if(got == -1 && errno == ENOSPC)
    return SQLITE_FULL;
#endif
  if(got != amt)
    return SQLITE_IOERR_WRITE;
  return SQLITE_OK;
}
 

static int
vfs_fs_Truncate( sqlite3_file *id, sqlite3_int64 nByte )
{
  vfsfile_t *vf = (vfsfile_t *)id;
  return fa_ftruncate(vf->fh, nByte) < 0 ? SQLITE_IOERR_TRUNCATE : SQLITE_OK;
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
  *pSize = fa_fsize(vf->fh);
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
  return SQLITE_NOTFOUND;
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
  static atomic_t tmpfiletally;
  char tmpfile[256];
  char errbuf[256];
  int v;

  vfsfile_t *vf = (vfsfile_t *)id;
  vf->hdr.pMethods = &vfs_fs_methods;

  //  int isExclusive  = (flags & SQLITE_OPEN_EXCLUSIVE);
  int isDelete     = (flags & SQLITE_OPEN_DELETEONCLOSE);
  //  int isCreate     = (flags & SQLITE_OPEN_CREATE);
  //  int isReadonly   = (flags & SQLITE_OPEN_READONLY);
  //  int isReadWrite  = (flags & SQLITE_OPEN_READWRITE);

  int openflags;

  if(flags & SQLITE_OPEN_READONLY) {
    openflags = 0;
  } else {
    openflags = FA_WRITE | FA_APPEND;
  }


  if(zName == NULL) {
    v = atomic_add_and_fetch(&tmpfiletally, 1);
    snprintf(tmpfile, sizeof(tmpfile), "%s/sqlite.tmp.%d",
	     gconf.cache_path, v);
    zName = tmpfile;
  } else {
    vf->fname = strdup(zName);
  }

  errbuf[0] = 0;
  vf->fh = fa_open_ex(zName, errbuf, sizeof(errbuf), openflags, NULL);

  VFSTRACE("Open%s file %s : %s -- %s openflags:0x%x",
	   isDelete ? "+Delete" : "",
	   zName,
	   vf->fh == NULL ? "Fail" : "OK",
           errbuf, openflags);

  if(vf->fh == NULL)
    return SQLITE_IOERR;


  if(isDelete)
    fa_unlink(zName, NULL, 0);

  return SQLITE_OK;
}


static int
vfs_delete(sqlite3_vfs *NotUsed, const char *zPath, int dirSync)
{
  VFSTRACE("Delete file %s", zPath);
  fa_unlink(zPath, NULL, 0);
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
  arch_get_random_bytes(zBuf, nBuf);
  return nBuf;
}

static sqlite3_vfs vfs = {
  3,                 /* iVersion */
  sizeof(vfsfile_t),   /* szOsFile */
  PATH_MAX,      /* mxPathname */
  0,                 /* pNext */
  APPNAME,        /* zName */
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
  sqlite3_vfs_register(&vfs, 1);
  return SQLITE_OK; 
}

int
sqlite3_os_end(void)
{
  return SQLITE_OK; 
}
