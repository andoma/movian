/*
 *  Fa to FILE wrappers (BSD)
 *  Copyright (C) 2013 Andreas Öman
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

#include <stdio.h>
#include "fileaccess.h"

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
