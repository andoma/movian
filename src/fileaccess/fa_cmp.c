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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#include "arch/halloc.h"
#include "main.h"
#include "fileaccess.h"
#include "fa_proto.h"

/**
 *
 */
typedef struct cmp {
  fa_handle_t h;
  fa_handle_t *s_src;
  int s_fd;
} cmp_t;


/**
 *
 */
static void
cmp_close(fa_handle_t *h)
{
  cmp_t *s = (cmp_t *)h;
  fa_close(s->s_src);
  close(s->s_fd);
  free(s);
}


/**
 *
 */
static int64_t
cmp_seek(fa_handle_t *handle, int64_t pos, int whence, int lazy)
{
  cmp_t *s = (cmp_t *)handle;
  int64_t r1 = fa_seek4(s->s_src, pos, whence, lazy);
  int64_t r2 = lseek(s->s_fd, pos, whence);

  if(r1 != r2) {
    TRACE(TRACE_ERROR, "FACMP",
	  "seek(%"PRId64", %d) failed fa:%"PRId64" local:%"PRId64,
	  pos, whence, r1, r2);
    exit(1);
  }
  return r1;
}


/**
 *
 */
static int64_t
cmp_fsize(fa_handle_t *handle)
{
  cmp_t *s = (cmp_t *)handle;
  int64_t r1 = fa_fsize(s->s_src);
  
  struct stat st;
  if(fstat(s->s_fd, &st)) {
    TRACE(TRACE_ERROR, "FACMP","Stat failed -- %s", strerror(errno));
    exit(1);
  }

  if(st.st_size != r1) {
    TRACE(TRACE_ERROR, "FACMP",
	  "fsize() failed fa:%"PRId64" local:%"PRId64,
	  r1, st.st_size);
    exit(1);
  }
  return r1;

}



/**
 *
 */
static int
cmp_read(fa_handle_t *handle, void *buf, size_t size)
{
  cmp_t *s = (cmp_t *)handle;
  int r1 = fa_read(s->s_src, buf, size);

  void *tmp = malloc(size);


  int64_t pos = lseek(s->s_fd, 0, SEEK_CUR);

  int r2 = read(s->s_fd, tmp, size);


  if(r1 != r2) {
    TRACE(TRACE_ERROR, "FACMP",
	  "read(%d) @ %"PRId64" failed fa:%d local:%d",
	  (int)size, pos, r1, r2);
    exit(1);
  }

  char *m1 = buf;
  char *m2 = tmp;
  int i;
  for(i = 0; i < r1; i++) {
    if(m1[i] != m2[i]) {
      TRACE(TRACE_ERROR, "FACMP",
	    "Read(%d) mismatch @  %"PRId64" + %d got %02x expected %02x",
	    (int)size, pos, i, m1[i], m2[i]);
      exit(1);
    }
  }
  free(tmp);
  return r1;
}


/**
 *
 */
static fa_protocol_t fa_protocol_cmp = {
  .fap_name  = "cmp",
  .fap_close = cmp_close,
  .fap_read  = cmp_read,
  .fap_seek  = cmp_seek,
  .fap_fsize = cmp_fsize,
};


/**
 *
 */
fa_handle_t *
fa_cmp_open(fa_handle_t *fa, const char *fname)
{
  int fd = open(fname, O_RDONLY);
  if(fd == -1)
    return fa;

  TRACE(TRACE_INFO, "FACMP", "Using %s as reference for compare", fname);

  cmp_t *s = calloc(1, sizeof(cmp_t));
  s->h.fh_proto = &fa_protocol_cmp;
  s->s_fd = fd;
  s->s_src = fa;
  return &s->h;
}
