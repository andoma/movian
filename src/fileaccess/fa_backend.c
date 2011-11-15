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
static void
file_open_browse(prop_t *page, const char *url, time_t mtime)
{
  prop_t *model;
  char parent[URL_MAX];

  model = prop_create(page, "model");
  prop_set_string(prop_create(model, "type"), "directory");
  
  /* Find a meaningful page title (last component of URL) */
  set_title_from_url(prop_create(model, "metadata"), url);
  
  // Set parent
  if(!fa_parent(parent, sizeof(parent), url))
    prop_set_string(prop_create(page, "parent"), parent);
  
  fa_scanner(url, mtime, model, NULL, prop_create(page, "directClose"));
}

/**
 *
 */
static void
file_open_dir(prop_t *page, const char *url, time_t mtime)
{
  fa_handle_t *ref = fa_reference(url);
  metadata_t *md = fa_probe_dir(url);

  switch(md->md_contenttype) {
  case CONTENT_DVD:
    backend_open_video(page, url);
    break;
    
  case CONTENT_DIR:
  case CONTENT_ARCHIVE:
    file_open_browse(page, url, mtime);
    break;

  default:
    nav_open_errorf(page, _("Can't handle content type %d"),
		    md->md_contenttype);
    break;
  }
  metadata_destroy(md);
  fa_unreference(ref);
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

  fa_scanner(parent, fs.fs_mtime, model, url, prop_create(page, "directClose"));
  return 0;
}


/**
 *
 */
static void
file_open_file(prop_t *page, const char *url, fa_stat_t *fs)
{
  char errbuf[200];
  prop_t *meta;
  metadata_t *md;

  void *db = metadb_get();
  md = metadb_metadata_get(db, url, fs->fs_mtime);
  metadb_close(db);

  if(md == NULL)
    md = fa_probe_metadata(url, errbuf, sizeof(errbuf));

  if(md == NULL) {
    nav_open_errorf(page, _("Unable to open file: %s"), errbuf);
    return;
  }

  if(md->md_redirect != NULL)
    url = md->md_redirect;

  meta = prop_create_root("metadata");

  switch(md->md_contenttype) {
  case CONTENT_ARCHIVE:
  case CONTENT_ALBUM:
    prop_destroy(meta);
    file_open_browse(page, url, fs->fs_mtime);
    break;

  case CONTENT_AUDIO:
    if(!file_open_audio(page, url)) {
      prop_destroy(meta);
      break;
    }
    playqueue_play(url, meta, 0);
    playqueue_open(page);
    break;

  case CONTENT_VIDEO:
  case CONTENT_DVD:
    prop_destroy(meta);
    backend_open_video(page, url);
    break;

  case CONTENT_IMAGE:
    file_open_image(page, meta);
    break;

  case CONTENT_PLUGIN:
    plugin_open_file(page, url);
    break;

  default:
    prop_destroy(meta);
    nav_open_errorf(page, _("Can't handle content type %d"),
		    md->md_contenttype);
    break;
  }
  metadata_destroy(md);
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

  if(fs.fs_type == CONTENT_DIR) {
    file_open_dir(page, url, fs.fs_mtime);
  } else {
    file_open_file(page, url, &fs);
  }
  return 0;
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
