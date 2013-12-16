/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#define _GNU_SOURCE 
#include <stdio.h>
#include "fileaccess.h"



/**
 *
 */
static ssize_t
cookie_read(void *fh, char *buf, size_t size)
{
  return fa_read(fh, buf, size);
}


/**
 *
 */
static int
cookie_seek(void *fh, off64_t *offsetp, int whence)
{
  int64_t s = fa_seek(fh, *offsetp, whence);
  *offsetp = s;
  return 0;
}


/**
 *
 */
static int
cookie_close(void *fh)
{
  fa_close(fh);
  return 0;
}

static cookie_io_functions_t fn_full = {
  .read  = cookie_read,
  .seek  = cookie_seek,
  .close = cookie_close,
};


static cookie_io_functions_t fn_noclose = {
  .read  = cookie_read,
  .seek  = cookie_seek,
};

/**
 *
 */
FILE *
fa_fopen(fa_handle_t *fh, int doclose)
{
  return fopencookie(fh, "rb", doclose ? fn_full : fn_noclose);
}
