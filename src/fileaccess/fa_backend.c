/*
 *  Backend using file I/O
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
#include <stdio.h>
#include <unistd.h>

#include "showtime.h"
#include "backend/backend.h"
#include "navigator.h"
#include "fileaccess.h"
#include "fa_probe.h"
#include "fa_video.h"
#include "fa_audio.h"
#include "fa_imageloader.h"
#include "fa_search.h"
#include "playqueue.h"
#include "fileaccess.h"


/**
 *
 */
static int
be_file_canhandle(backend_t *be, const char *url)
{
  return fa_can_handle(url, NULL, 0);
}


/**
 *
 */
static void
set_title_from_url(prop_t *metadata, const char *url)
{
  char tmp[1024];
  fa_url_get_last_component(tmp, sizeof(tmp), url);
  prop_set_string(prop_create(metadata, "title"), tmp);
}


/**
 *
 */
static nav_page_t *
file_open_dir(backend_t *be, struct navigator *nav,
	      const char *url, const char *view,
	      char *errbuf, size_t errlen)
{
  nav_page_t *np;
  prop_t *model;
  int type;
  char parent[URL_MAX];

  type = fa_probe_dir(NULL, url);

  if(type == CONTENT_DVD)
    return backend_open_video(be, nav, url, view, errbuf, errlen);

  np = nav_page_create(nav, url, view, NAV_PAGE_DONT_CLOSE_ON_BACK);

  model = prop_create(np->np_prop_root, "model");
  prop_set_string(prop_create(model, "type"), "directory");

  /* Find a meaningful page title (last component of URL) */
  set_title_from_url(prop_create(model, "metadata"), url);

  // Set parent
  if(!fa_parent(parent, sizeof(parent), url))
    prop_set_string(prop_create(np->np_prop_root, "parent"), parent);

  fa_scanner(url, model, NULL);
  return np;
}


/**
 *
 */
static nav_page_t *
file_open_image(backend_t *be, struct navigator *nav,
		const char *url, const char *view,
		char *errbuf, size_t errlen, prop_t *meta)
{
  nav_page_t *np = nav_page_create(nav, url, view, NAV_PAGE_DONT_CLOSE_ON_BACK);
  prop_t *model = prop_create(np->np_prop_root, "model");

  prop_set_string(prop_create(model, "type"), "image");

  if(prop_set_parent(meta, model))
    abort();
  return np;
}


/**
 * Try to open the given URL with a playqueue context
 */
static nav_page_t *
file_open_audio(struct navigator *nav, const char *url, const char *view)
{
  char parent[URL_MAX];
  char parent2[URL_MAX];
  struct fa_stat fs;
  nav_page_t *np;
  prop_t *model;

  if(fa_parent(parent, sizeof(parent), url))
    return NULL;

  if(fa_stat(parent, &fs, NULL, 0))
    return NULL;
  
  np = nav_page_create(nav, parent, view, NAV_PAGE_DONT_CLOSE_ON_BACK);

  model = prop_create(np->np_prop_root, "model");
  prop_set_string(prop_create(model, "type"), "directory");

  /* Find a meaningful page title (last component of URL) */
  set_title_from_url(prop_create(model, "metadata"), parent);

  // Set parent
  if(!fa_parent(parent2, sizeof(parent2), parent))
    prop_set_string(prop_create(np->np_prop_root, "parent"), parent2);

  fa_scanner(parent, model, url);
  return np;
}


/**
 *
 */
static nav_page_t *
file_open_file(backend_t *be, struct navigator *nav,
	       const char *url, const char *view,
	       char *errbuf, size_t errlen, struct fa_stat *fs)
{
  char redir[URL_MAX];
  int r;
  prop_t *meta;
  nav_page_t *np;

  meta = prop_create(NULL, "metadata");

  r = fa_probe(meta, url, redir, sizeof(redir), errbuf, errlen, fs);

  switch(r) {
  case CONTENT_ARCHIVE:
  case CONTENT_ALBUM:
    prop_destroy(meta);
    return file_open_dir(be, nav, redir, view, errbuf, errlen);

  case CONTENT_AUDIO:
    if((np = file_open_audio(nav, url, view)) != NULL) {
      prop_destroy(meta);
      return np;
    }

    playqueue_play(url, meta, 0);
    return playqueue_open(be, nav, view);

  case CONTENT_VIDEO:
  case CONTENT_DVD:
    prop_destroy(meta);
    return backend_open_video(be, nav, url, view, errbuf, errlen);

  case CONTENT_IMAGE:
    return file_open_image(be, nav, url, view, errbuf, errlen, meta);

  default:
    prop_destroy(meta);
    snprintf(errbuf, errlen, "Can not handle file contents");
    return NULL;
  }
}

/**
 *
 */
static nav_page_t *
be_file_open(backend_t *be, struct navigator *nav,
	     const char *url, const char *view,
	     char *errbuf, size_t errlen)
{
  struct fa_stat fs;

  if(fa_stat(url, &fs, errbuf, errlen))
    return NULL;

  return fs.fs_type == CONTENT_DIR ? 
    file_open_dir (be, nav, url, view, errbuf, errlen) :
    file_open_file(be, nav, url, view, errbuf, errlen, &fs);
}


/**
 *
 */
static prop_t *
be_list(backend_t *be, const char *url, char *errbuf, size_t errsize)
{
  prop_t *p = prop_create(NULL, NULL);
  fa_scanner(url, p, NULL);
  return p;
}


/**
 *
 */
backend_t be_file = {
  .be_init = fileaccess_init,
  .be_canhandle = be_file_canhandle,
  .be_open = be_file_open,
  .be_play_video = be_file_playvideo,
  .be_play_audio = be_file_playaudio,
  .be_list = be_list,
  .be_imageloader = fa_imageloader,
  .be_normalize = fa_normalize,
  .be_probe = fa_check_url,
};

BE_REGISTER(file);
