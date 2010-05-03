/*
 *  Navigator
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
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>

#include "showtime.h"
#include "backend.h"
#include "navigator.h"
#include "event.h"
#include "notifications.h"

prop_t *global_sources;

static struct backend_list backends;

/**
 *
 */
void
backend_register(backend_t *be)
{
  LIST_INSERT_HEAD(&backends, be, be_global_link);
}


/**
 *
 */
void
backend_init(void)
{
  backend_t *be;

  global_sources =
    prop_create_ex(prop_get_global(), "sources", NULL, 
		   PROP_SORTED_CHILDS | PROP_SORT_CASE_INSENSITIVE);

  LIST_FOREACH(be, &backends, be_global_link)
    if(be->be_init != NULL)
      be->be_init();
}


/**
 *
 */
event_t *
backend_play_video(const char *url, struct media_pipe *mp,
		   char *errbuf, size_t errlen)
{
  backend_t *nb;

  LIST_FOREACH(nb, &backends, be_global_link)
    if(nb->be_canhandle(url))
      break;
  
  if(nb == NULL || nb->be_play_video == NULL) {
    snprintf(errbuf, errlen, "No backend for URL");
    return NULL;
  }
  return nb->be_play_video(url, mp, errbuf, errlen);
}


/**
 *
 */
event_t *
backend_play_audio(const char *url, struct media_pipe *mp,
	       char *errbuf, size_t errlen)
{
  backend_t *nb;

  LIST_FOREACH(nb, &backends, be_global_link)
    if(nb->be_canhandle(url))
      break;
  
  if(nb == NULL || nb->be_play_audio == NULL) {
    snprintf(errbuf, errlen, "No backend for URL");
    return NULL;
  }
  return nb->be_play_audio(url, mp, errbuf, errlen);
}


/**
 *
 */
prop_t *
backend_list(const char *url, char *errbuf, size_t errlen)
{
  backend_t *nb;

  LIST_FOREACH(nb, &backends, be_global_link)
    if(nb->be_canhandle(url))
      break;
  
  if(nb == NULL || nb->be_list == NULL) {
    snprintf(errbuf, errlen, "No backend for URL");
    return NULL;
  }
  return nb->be_list(url, errbuf, errlen);
}


/**
 *
 */
int
backend_get_parent(const char *url, char *parent, size_t parentlen,
		   char *errbuf, size_t errlen)
{
  backend_t *nb;

  LIST_FOREACH(nb, &backends, be_global_link)
    if(nb->be_canhandle(url))
      break;
  
  if(nb == NULL || nb->be_get_parent == NULL) {
    snprintf(errbuf, errlen, "No backend for URL");
    return -1;
  }
  return nb->be_get_parent(url, parent, parentlen, errbuf, errlen);
}

/**
 * Static content
 */
static int
be_page_canhandle(const char *url)
{
  return !strncmp(url, "page:", strlen("page:"));
}


/**
 *
 */
static int
be_page_open(struct navigator *nav, 
	     const char *url0, const char *type, prop_t *psource,
	     nav_page_t **npp, char *errbuf, size_t errlen)
{
  nav_page_t *n = nav_page_create(nav, url0, sizeof(nav_page_t), 0);
  prop_t *src = prop_create(n->np_prop_root, "source");
  prop_t *metadata = prop_create(src, "metadata");
  char *cap = mystrdupa(url0 + strlen("page:"));

  prop_set_string(prop_create(src, "type"), cap);
  cap[0] = toupper((int)cap[0]);
  prop_set_string(prop_create(metadata, "title"), cap);

  *npp = n;
  return 0;
}


/**
 *
 */
static backend_t be_page = {
  .be_canhandle = be_page_canhandle,
  .be_open = be_page_open,
};

BE_REGISTER(page);


/**
 *
 */
struct pixmap *
backend_imageloader(const char *url, int want_thumb, const char *theme,
		    char *errbuf, size_t errlen)
{
  backend_t *nb;

  LIST_FOREACH(nb, &backends, be_global_link)
    if(nb->be_canhandle(url))
      break;
  
  if(nb == NULL || nb->be_imageloader == NULL) {
    snprintf(errbuf, errlen, "No backend for URL");
    return NULL;
  }
  return nb->be_imageloader(url, want_thumb, theme, errbuf, errlen);
}


/**
 *
 */
backend_t *
backend_canhandle(const char *url)
{
  backend_t *be;

  LIST_FOREACH(be, &backends, be_global_link)
    if(be->be_canhandle(url))
      return be;
  return NULL;
}

