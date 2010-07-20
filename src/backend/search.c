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
  return
    !strncmp(url, "search:", strlen("search:")) ||
    !strncmp(url, "audiosearch:", strlen("audiosearch:")) ||
    !strncmp(url, "videosearch:", strlen("videosearch:"));
    
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
  backend_t *be;
  prop_t *src, *meta;
  backend_search_type_t type;

  if((url = strchr(url0, ':')) == NULL)
    abort();
  url++;

  if(url0[0] == 'v')
    type = BACKEND_SEARCH_VIDEO;
  else if(url0[0] == 'a')
    type = BACKEND_SEARCH_AUDIO;
  else
    type = BACKEND_SEARCH_ALL;



  if((be = backend_canhandle(url)) != NULL) 
    return be->be_open(be, nav, url, view, errbuf, errlen);
  
  np = nav_page_create(nav, url0, view, NAV_PAGE_DONT_CLOSE_ON_BACK);

  prop_set_string(prop_create(np->np_prop_root, "view"), "list");

  src = prop_create(np->np_prop_root, "model");
  prop_set_string(prop_create(src, "type"), "directory");
  
  meta = prop_create(src, "metadata");
  prop_set_string(prop_create(meta, "title"), url);

  LIST_FOREACH(be, &backends, be_global_link)
    if(be->be_search != NULL)
      be->be_search(be, src, url, type);
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
