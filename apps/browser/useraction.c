/*
 *  User-defined actions
 *  Copyright (C) 2008 Magnus Lundström
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

#define _GNU_SOURCE
#include <pthread.h>

#include <assert.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "layout/layout.h"

#include "browser.h"
#include "browser_view.h"
#include "navigator.h"
#if 0
#include "useraction.h"


static int
useraction_perform (const char *appname, input_key_t key,
		    const char *filename) {
  int r;
  char cmd[PATH_MAX];

  snprintf(cmd, sizeof(cmd),
	   "~/.showtime/useraction/%s/%i %s %i \"%s\"",
	   appname, key - '0',
	   appname, key - '0', filename);

  r = system(cmd);
  if (r == -1 || WEXITSTATUS(r) != 0)
    return -1;

  return 0;
}

int
useraction_slideshow(glw_t *w, input_key_t key) {
  glw_t *b;
  const char *filename;

  if ((b = w->glw_selected) == NULL ||
      b->glw_class != GLW_BITMAP ||
      (filename = glw_bitmap_get_filename(b)) == NULL)
    return -1;

  if (useraction_perform("slideshow", key, filename) == -1)
    return -1;

  /* Trigger reload */
  glw_set(b, GLW_ATTRIB_FILENAME, filename, NULL);

  return 0;
}
#endif
