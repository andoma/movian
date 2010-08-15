/*
 *  Unified search
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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "showtime.h"
#include "settings.h"
#include "event.h"
#include "navigator.h"
#include "backend/backend.h"
#include "backend/backend_prop.h"
#include "backend/search.h"


/**
 *
 */
int
search_class_create(prop_t *parent, prop_t **nodesp, prop_t **entriesp,
		    const char *title, const char *icon)
{
  prop_t *p = prop_create(NULL, NULL);
  prop_t *m = prop_create(p, "metadata");
  prop_t *n, *e;
  
  char url[URL_MAX];

  backend_prop_make(p, url, sizeof(url));
  prop_set_string(prop_create(p, "url"), url);

  prop_set_string(prop_create(m, "title"), title);
  if(icon != NULL)
    prop_set_string(prop_create(m, "icon"), icon);
  prop_set_string(prop_create(p, "type"), "directory");
      
  n = prop_create(p, "nodes");
  e = prop_create(m, "entries");
  prop_set_int(e, 0);

  prop_ref_inc(n);
  prop_ref_inc(e);
  *nodesp = n;
  *entriesp = e;

  if(prop_set_parent(p, parent)) {
    prop_destroy(p);
    return 1;
  }
  return 0;
}


/**
 *
 */
prop_t *
search_get_settings(void)
{
  static prop_t *p;

  if(p == NULL)
    p = settings_add_dir(NULL, "search", "Search", "search");
  return p;
}


/**
 *
 */
static int
search_canhandle(backend_t *be, const char *url)
{
  return !strncmp(url, "search:", strlen("search:"));
}


/**
 *
 */
static nav_page_t *
search_open(backend_t *beself, struct navigator *nav,
	    const char *url0, const char *view,
	    char *errbuf, size_t errlen)
{
  const char *url;
  nav_page_t *np;
  prop_t *model, *meta;

  if((url = strchr(url0, ':')) == NULL)
    abort();
  url++;

  if((np = backend_open(nav, url, view, errbuf, errlen)) != BACKEND_NOURI)
    return np;
  
  np = nav_page_create(nav, url0, view, NAV_PAGE_DONT_CLOSE_ON_BACK);

  model = prop_create(np->np_prop_root, "model");
  prop_set_string(prop_create(model, "type"), "directory");
  
  meta = prop_create(model, "metadata");
  prop_set_string(prop_create(meta, "title"), url);

  backend_search(model, url);
  return np;
}

/**
 *
 */
static backend_t be_search = {
  .be_canhandle = search_canhandle,
  .be_open = search_open,
};

BE_REGISTER(search);
