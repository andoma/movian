/*
 *  Static backend, just serving a page
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


#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "showtime.h"
#include "navigator.h"

/**
 *
 */
static int
be_page_canhandle(const char *url)
{
  return !strncmp(url, "page://", strlen("page://"));
}


/**
 *
 */
static int
be_page_open(const char *url0, nav_page_t **npp, char *errbuf, size_t errlen)
{
  nav_page_t *n;
  prop_t *p;

  *npp = n = nav_page_create(url0, sizeof(nav_page_t), NULL, 0);

  p = n->np_prop_root;

  prop_set_string(prop_create(p, "type"), url0 + strlen("page://"));
  return 0;
}


/**
 *
 */
nav_backend_t be_page = {
  .nb_canhandle = be_page_canhandle,
  .nb_open = be_page_open,
};


