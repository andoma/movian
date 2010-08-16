/*
 *  Blob cache
 *  Copyright (C) 2010 Andreas Öman
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
#include <sys/file.h>
#include <fcntl.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <dirent.h>

#include <libavutil/sha1.h>

#include "showtime.h"
#include "blobcache.h"
#include "misc/fs.h"

/**
 *
 */
static void
digest_url(const char *url, int qualifier, uint8_t *d)
{
  struct AVSHA1 *shactx = alloca(av_sha1_size);

  av_sha1_init(shactx);
  av_sha1_update(shactx, (const uint8_t *)url, strlen(url));
  av_sha1_update(shactx, (const uint8_t *)&qualifier, sizeof(qualifier));
  av_sha1_final(shactx, d);
}


/**
 *
 */
static void
digest_to_path(uint8_t *d, char *path, size_t pathlen)
{
  snprintf(path, pathlen, "%s/blobcache/"
	   "%02x/%02x%02x%02x"
	   "%02x%02x%02x%02x"
	   "%02x%02x%02x%02x"
	   "%02x%02x%02x%02x"
	   "%02x%02x%02x%02x",
	   showtime_cache_path,
	   d[0],  d[1],  d[2],  d[3],
	   d[4],  d[5],  d[6],  d[7],
	   d[8],  d[9],  d[10], d[11],
	   d[12], d[13], d[14], d[15],
	   d[16], d[17], d[18], d[19]);
}


/**
 *
 */
static void *
blobcache_load(const char *path, int fd, size_t *sizep)
{
  struct stat st;
  uint8_t buf[4];
  void *r;
  time_t exp;
  size_t l;

  if(flock(fd, LOCK_SH))
    return NULL;

  if(fstat(fd, &st))
    return NULL;

  if(read(fd, buf, 4) != 4)
    return NULL;

  exp = buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3];

  if(exp < time(NULL)) {
    unlink(path);
    return NULL;
  }

  l = st.st_size - 4;

  r = malloc(l);

  if(read(fd, r, l) != l)
    return NULL;

  *sizep = l;
  return r;
}


/**
 *
 */
void *
blobcache_get(const char *url, int qualifier, size_t *sizep)
{
  char path[PATH_MAX];
  int fd;
  void *r;
  uint8_t d[20];

  digest_url(url, qualifier, d);

  digest_to_path(d, path, sizeof(path));

  if((fd = open(path, O_RDONLY)) == -1)
    return NULL;

  r = blobcache_load(path, fd, sizep);
  close(fd);
  return r;
}


/**
 *
 */
static int
blobcache_save(int fd, const void *data, size_t size, time_t expire)
{
  uint8_t buf[4];

  if(flock(fd, LOCK_EX))
    return -1;
  
  buf[0] = expire >> 24;
  buf[1] = expire >> 16;
  buf[2] = expire >> 8;
  buf[3] = expire;

  if(write(fd, buf, 4) != 4)
    return -1;

  if(write(fd, data, size) != size)
    return -1;

  if(ftruncate(fd, size + 4))
    return -1;
  return 0;
}


/**
 *
 */
void
blobcache_put(const char *url, int qualifier, 
	      const void *data, size_t size, time_t expire)
{
  char path[PATH_MAX];
  int fd;
  uint8_t d[20];

  digest_url(url, qualifier, d);
  snprintf(path, sizeof(path), "%s/blobcache/%02x", showtime_cache_path, d[0]);

  if(makedirs(path))
    return;

  digest_to_path(d, path, sizeof(path));

  if((fd = open(path, O_CREAT | O_WRONLY, 0666)) == -1)
    return;

  if(blobcache_save(fd, data, size, expire))
    unlink(path);

  close(fd);
}


/**
 *
 */
static void
blobcache_check_size(void)
{
  struct dirent **namelist;
  struct dirent **namelist2;
  int n;
  char path[PATH_MAX];
  char path2[PATH_MAX];
  char path3[PATH_MAX];
  struct stat st;
  
  uint64_t tsize = 0;

  snprintf(path, sizeof(path), "%s/blobcache", showtime_cache_path);
  
  n = scandir(path, &namelist, NULL, NULL);
  if(n < 0)
    return;

  while(n--) {
    if(namelist[n]->d_name[0] != '.') {
      snprintf(path2, sizeof(path2), "%s/blobcache/%s",
	       showtime_cache_path, namelist[n]->d_name);

      int m = scandir(path2, &namelist2, NULL, NULL);
      if(m >= 0) {
	while(m--) {
          if(namelist2[m]->d_name[0] != '.') {

	    snprintf(path3, sizeof(path3), "%s/blobcache/%s/%s",
		     showtime_cache_path, namelist[n]->d_name,
		     namelist2[m]->d_name);
	    
	    if(!stat(path3, &st))
	      tsize += st.st_size;
	  }
	  free(namelist2[m]);
	}
	free(namelist2);
      }
    }
    free(namelist[n]);
  }

  free(namelist);
  printf("Total cache size: %lld\n", tsize);

}


/**
 *
 */
void
blobcache_init(void)
{
  blobcache_check_size();
}
