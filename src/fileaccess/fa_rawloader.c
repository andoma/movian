/*
 *  raw loader, used by libglw
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

#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "showtime.h"
#include "fileaccess.h"
#include "fa_rawloader.h"

void *
fa_rawloader(const char *filename, size_t *sizeptr, const char *theme)
{
  fa_handle_t *fh;
  size_t size;
  char *data;
  int r;

  if((fh = fa_open_theme(filename, theme)) == NULL)
    return NULL;

  size = fa_fsize(fh);
  data = malloc(size + 1);

  r = fa_read(fh, data, size);

  fa_close(fh);

  if(r != size) {
    free(data);
    return NULL;
  }
  data[size] = 0;
  if(sizeptr)
    *sizeptr = size;
  return data;

}



void 
fa_rawunload(void *data)
{
  free(data);
}
