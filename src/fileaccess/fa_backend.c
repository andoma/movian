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
#include "playqueue.h"
#include "fileaccess.h"


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
file_open_video(struct navigator *nav, const char *url0, nav_page_t **npp)
{
  nav_page_t *np;
  prop_t *src;

  np = nav_page_create(nav, url0, sizeof(nav_page_t), 0);

  src = prop_create(np->np_prop_root, "source");
  prop_set_string(prop_create(src, "type"), "video");
  *npp = np;
}


/**
 *
 */
static int
file_open_dir(struct navigator *nav, 
	      const char *url0, nav_page_t **npp, char *errbuf, size_t errlen)
{
  nav_page_t *np;
  prop_t *src, *view, *metadata;
  int type, l;
  char *dirname;
  char parent[URL_MAX];
  type = fa_probe_dir(NULL, url0);

  if(type == CONTENT_DVD) {
    file_open_video(nav, url0, npp);
    return 0;
  }

  np = nav_page_create(nav, url0, sizeof(nav_page_t),
			NAV_PAGE_DONT_CLOSE_ON_BACK);

  view = prop_create(np->np_prop_root, "view");

  src = prop_create(np->np_prop_root, "source");
  prop_set_string(prop_create(src, "type"), "directory");
  metadata = prop_create(src, "metadata");

  /* Find a meaningfull page title (last component of URL) */
  l = strlen(url0);
  dirname = alloca(l + 1);
  memcpy(dirname, url0, l + 1);
  if(l > 0 && dirname[l - 1] == '/')
    dirname[l - 1] = 0;

  dirname = strrchr(dirname, '/') ? strrchr(dirname, '/') + 1 : dirname;
  prop_set_string(prop_create(metadata, "title"), dirname);

  // Set parent
  if(!fa_parent(parent, sizeof(parent), url0))
    prop_set_string(prop_create(np->np_prop_root, "parent"), parent);

  fa_scanner(url0, src, view);
  *npp = np;
  return 0;
}



/**
 *
 */
static int
file_open_file(struct navigator *nav,
	       const char *url, nav_page_t **npp, char *errbuf, size_t errlen,
	       struct stat *st)
{
  char redir[URL_MAX];
  int r;
  //  char *parent;//, *x;
  prop_t *meta;//, *album_art;

  meta = prop_create(NULL, "metadata");

  r = fa_probe(meta, url, redir, sizeof(redir), errbuf, errlen, st);
  
  switch(r) {
  case CONTENT_ARCHIVE:
    prop_destroy(meta);
    return file_open_dir(nav, redir, npp, errbuf, errlen);
  case CONTENT_AUDIO:
#if 0 
    parent = strdup(url);
    if((x = strrchr(parent, '/')) != NULL)
      *x = 0;
    else {
      free(parent);
      parent = NULL;
      
    }
    
    if(parent != NULL) {
      album_art = prop_create(meta, "album_art");
      prop_ref_inc(album_art);
      fa_scanner_find_albumart(parent, album_art);
    }
#endif

    playqueue_play(url, meta);
    //    free(parent);
    *npp = NULL;
    return 0;

  case CONTENT_VIDEO:
  case CONTENT_DVD:
    prop_destroy(meta);
    file_open_video(nav, url, npp);
    return 0;

  default:
    snprintf(errbuf, errlen, "Can not handle file contents");
    return -1;
  }
}

/**
 *
 */
static int
be_file_open(struct navigator *nav, const char *url0,
	     nav_page_t **npp, char *errbuf, size_t errlen)
{
  struct stat buf;

  if(fa_stat(url0, &buf, errbuf, errlen))
    return -1;

  return S_ISDIR(buf.st_mode) ? file_open_dir(nav, url0, npp, errbuf, errlen) :
    file_open_file(nav, url0, npp, errbuf, errlen, &buf);
}


/**
 *
 */
static prop_t *
be_list(const char *url, char *errbuf, size_t errsize)
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
};

BE_REGISTER(file);
