/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "main.h"
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
#include "usage.h"


/**
 *
 */
static int
be_file_canhandle(const char *url)
{
  return fa_can_handle(url, NULL, 0);
}

#if ENABLE_METADATA

/**
 *
 */
static rstr_t *
title_from_url(const char *url)
{
  return fa_get_title(url);
}


/**
 *
 */
static void
file_open_browse(prop_t *page, const char *url, time_t mtime,
		 prop_t *model)
{
  char parent[URL_MAX];

  prop_set(model, "type", PROP_SET_STRING, "directory");
  
  /* Find a meaningful page title (last component of URL) */

  rstr_t *title = title_from_url(url);

  prop_setv(model, "metadata", "title", NULL, PROP_SET_RSTRING, title);
  
  // Set parent
  if(!fa_parent(parent, sizeof(parent), url))
    prop_set(page, "parent", PROP_SET_STRING, parent);
  
  prop_t *dc = prop_create_r(page, "directClose");

  fa_scanner_page(url, mtime, model, NULL, dc,title);
  rstr_release(title);
  prop_ref_dec(dc);
}

/**
 *
 */
static void
file_open_dir(prop_t *page, const char *url, time_t mtime,
	      prop_t *model)
{
  fa_handle_t *ref = fa_reference(url);
  metadata_t *md = fa_probe_dir(url);

  switch(md->md_contenttype) {
  case CONTENT_DVD:
    backend_open_video(page, url, 0);
    break;
    
  case CONTENT_DIR:
  case CONTENT_SHARE:
  case CONTENT_ARCHIVE:
    file_open_browse(page, url, mtime, model);
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
file_open_image(prop_t *model, prop_t *meta)
{
  prop_set(model, "type", PROP_SET_STRING, "image");

  if(prop_set_parent(meta, model))
    prop_destroy(meta);
  return 0;
}


/**
 * Try to open the given URL with a playqueue context
 */
static int
file_open_audio(prop_t *page, const char *url, prop_t *model)
{
  char parent[URL_MAX];
  char parent2[URL_MAX];
  struct fa_stat fs;

  if(fa_parent(parent, sizeof(parent), url))
    return 1;

  if(fa_stat(parent, &fs, NULL, 0))
    return 1;
  
  prop_set(model, "type", PROP_SET_STRING, "directory");

  /* Find a meaningful page title (last component of URL) */
  rstr_t *title = title_from_url(parent);
  prop_setv(model, "metadata", "title", NULL, PROP_SET_RSTRING, title);

  // Set parent
  if(!fa_parent(parent2, sizeof(parent2), parent))
    prop_set(page, "parent", PROP_SET_STRING, parent2);

  prop_t *dc = prop_create_r(page, "directClose");

  fa_scanner_page(parent, fs.fs_mtime, model, url, dc, title);
  rstr_release(title);
  prop_ref_dec(dc);
  return 0;
}


/**
 *
 */
static void
file_open_file(prop_t *page, const char *url, fa_stat_t *fs,
	       prop_t *model, prop_t *loading, prop_t *io,
	       prop_t *loading_status)
{
  char errbuf[200];
  metadata_t *md = NULL;

  void *db = metadb_get();
  md = metadb_metadata_get(db, url, fs->fs_mtime);
  metadb_close(db);

  if(md == NULL) {
    prop_link(_p("Checking file contents"), loading_status);
    md = fa_probe_metadata(url, errbuf, sizeof(errbuf), NULL, io);
    prop_unlink(loading_status);
  }

  if(md == NULL) {
    nav_open_errorf(page, _("Unable to open file: %s"), errbuf);
    return;
  }

  if(md->md_redirect != NULL)
    url = md->md_redirect;

  prop_t *meta = prop_create_root("metadata");

  metadata_to_proptree(md, meta, 0);

  switch(md->md_contenttype) {
  case CONTENT_ARCHIVE:
  case CONTENT_ALBUM:
    file_open_browse(page, url, fs->fs_mtime, model);
    break;

  case CONTENT_AUDIO:
    if(!file_open_audio(page, url, model)) {
      break;
    }
    playqueue_play(url, meta, 0);
    playqueue_open(page);
    meta = NULL;
    break;

  case CONTENT_VIDEO:
  case CONTENT_DVD:
    backend_open_video(page, url, 0);
    break;

  case CONTENT_IMAGE:
    file_open_image(model, meta);
    prop_set_int(loading, 0);
    meta = NULL;
    break;

#if ENABLE_PLUGINS
  case CONTENT_PLUGIN:
    prop_set_int(loading, 0);
    plugin_open_file(page, url);
    break;
#endif

  default:
    nav_open_errorf(page, _("Can't handle content type %d"),
		    md->md_contenttype);
    break;
  }
  prop_destroy(meta);
  metadata_destroy(md);
}


/**
 *
 */
static int
be_file_open(prop_t *page, const char *url, int sync)
{
  struct fa_stat fs;
  char errbuf[200];

  prop_t *model = prop_create_r(page, "model");
  prop_t *loading = prop_create_r(model, "loading");
  prop_t *loading_status = prop_create_r(model, "loadingStatus");
  prop_t *io = prop_create_r(model, "io");
  prop_set_int(loading, 1);

  if(fa_stat(url, &fs, errbuf, sizeof(errbuf))) {
    nav_open_error(page, errbuf);
  } else if(fs.fs_type == CONTENT_DIR) {
    usage_page_open(sync, "Directory");
    file_open_dir(page, url, fs.fs_mtime, model);
  } else if(fs.fs_type == CONTENT_SHARE) {
    usage_page_open(sync, "Share");
    file_open_browse(page, url, fs.fs_mtime, model);
  } else {
    usage_page_open(sync, "File");
    file_open_file(page, url, &fs, model, loading, io, loading_status);
  }

  prop_ref_dec(model);
  prop_ref_dec(loading);
  prop_ref_dec(loading_status);
  prop_ref_dec(io);
  return 0;
}

#else

static int
be_file_open(prop_t *page, const char *url, int sync)
{
  nav_open_error(page, "Browsing not available");
  return 0;
}

#endif

/**
 *
 */
backend_t be_file = {
  .be_init = fileaccess_init,
  .be_canhandle = be_file_canhandle,
  .be_imageloader = fa_imageloader,
  .be_open = be_file_open,
  .be_normalize = fa_normalize,

#if ENABLE_METADATA
  .be_play_video = be_file_playvideo,
  .be_play_audio = be_file_playaudio,
  .be_probe = fa_check_url,
#endif
};

BE_REGISTER(file);
