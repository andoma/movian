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
#include "fileaccess.h"

#if defined(__ANDROID__)
FILE *
funopen(const void *cookie, int (*readfn)(void *, char *, int),
	int (*writefn)(void *, const char *, int),
	fpos_t (*seekfn)(void *, fpos_t, int), int (*closefn)(void *));
#endif


/**
 *
 */
static int
read_FILE(void *fh, char *buf, int size)
{
  return fa_read(fh, buf, size);
}


/**
 *
 */
static fpos_t
seek_FILE(void *fh, fpos_t pos, int whence)
{
  return fa_seek(fh, pos, whence);
}


/**
 *
 */
static int
close_FILE(void *fh)
{
  fa_close(fh);
  return 0;
}


/**
 *
 */
FILE *
fa_fopen(fa_handle_t *fh, int doclose)
{
  return funopen(fh, read_FILE, NULL, seek_FILE, doclose ? close_FILE : NULL);
}
