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
#include "plugins.h"


/**
 *
 */
static int
be_file_canhandle(const char *url)
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
static int
file_open_dir(prop_t *page, const char *url)
{
  prop_t *model;
  int type;
  char parent[URL_MAX];
  int r;
  fa_handle_t *ref;

  ref = fa_reference(url);
  type = fa_probe_dir(NULL, url);
  if(type == CONTENT_DVD) {
    r =  backend_open_video(page, url);
  } else {

    model = prop_create(page, "model");
    prop_set_string(prop_create(model, "type"), "directory");

    /* Find a meaningful page title (last component of URL) */
    set_title_from_url(prop_create(model, "metadata"), url);

    // Set parent
    if(!fa_parent(parent, sizeof(parent), url))
      prop_set_string(prop_create(page, "parent"), parent);

    fa_scanner(url, model, NULL);
    r = 0;
  }
  fa_unreference(ref);

  return r;
}


/**
 *
 */
static int
file_open_image(prop_t *page, prop_t *meta)
{
  prop_t *model = prop_create(page, "model");

  prop_set_string(prop_create(model, "type"), "image");

  if(prop_set_parent(meta, model))
    abort();
  return 0;
}


/**
 * Try to open the given URL with a playqueue context
 */
static int
file_open_audio(prop_t *page, const char *url)
{
  char parent[URL_MAX];
  char parent2[URL_MAX];
  struct fa_stat fs;
  prop_t *model;

  if(fa_parent(parent, sizeof(parent), url))
    return 1;

  if(fa_stat(parent, &fs, NULL, 0))
    return 1;
  
  model = prop_create(page, "model");
  prop_set_string(prop_create(model, "type"), "directory");

  /* Find a meaningful page title (last component of URL) */
  set_title_from_url(prop_create(model, "metadata"), parent);

  // Set parent
  if(!fa_parent(parent2, sizeof(parent2), parent))
    prop_set_string(prop_create(page, "parent"), parent2);

  fa_scanner(parent, model, url);
  return 0;
}


/**
 *
 */
static int
file_open_file(prop_t *page, const char *url, struct fa_stat *fs)
{
  char redir[URL_MAX];
  char errbuf[200];
  int c;
  prop_t *meta;

  meta = prop_create_root("metadata");

  c = fa_probe(meta, url, redir, sizeof(redir), errbuf, sizeof(errbuf), fs, 1);

  switch(c) {
  case CONTENT_ARCHIVE:
  case CONTENT_ALBUM:
    prop_destroy(meta);
    return file_open_dir(page, redir);

  case CONTENT_AUDIO:
    if(!file_open_audio(page, url)) {
      prop_destroy(meta);
      return 0;
    }

    playqueue_play(url, meta, 0);
    return playqueue_open(page);

  case CONTENT_VIDEO:
  case CONTENT_DVD:
    prop_destroy(meta);
    return backend_open_video(page, url);

  case CONTENT_IMAGE:
    return file_open_image(page, meta);

  case CONTENT_PLUGIN:
    plugin_open_file(page, url);
    return 0;

  default:
    prop_destroy(meta);
    return nav_open_error(page, errbuf);
  }
}

/**
 *
 */
static int
be_file_open(prop_t *page, const char *url)
{
  struct fa_stat fs;
  char errbuf[200];

  if(fa_stat(url, &fs, errbuf, sizeof(errbuf)))
    return nav_open_error(page, errbuf);

  return fs.fs_type == CONTENT_DIR ? 
    file_open_dir(page, url) : file_open_file(page, url, &fs);
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
  .be_imageloader = fa_imageloader,
  .be_normalize = fa_normalize,
  .be_probe = fa_check_url,
};

BE_REGISTER(file);
