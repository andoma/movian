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
#include "navigator.h"
#include "fileaccess.h"
#include "fa_probe.h"
#include "fa_video.h"
#include "fa_audio.h"
#include "fa_imageloader.h"
#include "playqueue.h"
#include "fileaccess.h"


typedef struct be_file_page {
  nav_page_t h;

} be_file_page_t;


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
file_open_video(const char *url0, nav_page_t **npp)
{
  nav_page_t *np;
  prop_t *p;

  np = nav_page_create(url0, sizeof(nav_page_t), NULL, 0);

  p = np->np_prop_root;
  prop_set_string(prop_create(p, "type"), "video");
  *npp = np;
}


/**
 *
 */
static int
file_open_dir(const char *url0, nav_page_t **npp, char *errbuf, size_t errlen)
{
  be_file_page_t *bfp;
  prop_t *p;
  int type, l;
  char *dirname;
  char *parent, *x;
  const char *proto;
  type = fa_probe_dir(NULL, url0);

  if(type == CONTENT_DVD) {
    file_open_video(url0, npp);
    return 0;
  }

  bfp = nav_page_create(url0, sizeof(be_file_page_t), NULL,
			NAV_PAGE_DONT_CLOSE_ON_BACK);
  p = bfp->h.np_prop_root;

  prop_set_string(prop_create(p, "type"), "directory");
  prop_set_string(prop_create(p, "view"), "list");

  l = strlen(url0);
  dirname = alloca(l + 1);
  parent  = alloca(l + 1);

  memcpy(dirname, url0, l + 1);
  memcpy(parent,  url0, l + 1);

  /* Find a meaningfull page title (last component of URL) */
  memcpy(dirname, url0, l + 1);
  if(l > 0 && dirname[l - 1] == '/')
    dirname[l - 1] = 0;

  dirname = strrchr(dirname, '/') ? strrchr(dirname, '/') + 1 : dirname;
  prop_set_string(prop_create(p, "title"), dirname);

  x = strstr(parent, "://");
  if(x != NULL) {
    proto = parent;
    *x = 0;
    parent = x + 3;
  } else {
    proto = "file";
  }

  if(strcmp(parent, "/")) {
    /* Set parent */
    if((x = strrchr(parent, '/')) != NULL) {
      *x = 0;
      if(x[1] == 0) {
	/* Trailing slash */
	if((x = strrchr(parent, '/')) != NULL) {
	  *x = 0;
	}
      }
      prop_set_stringf(prop_create(p, "parent"), "%s://%s/", proto, parent);
    }
  }

  fa_scanner(url0, p, FA_SCANNER_DETERMINE_VIEW);

  *npp = &bfp->h;
  return 0;
}



/**
 *
 */
static int
file_open_file(const char *url, nav_page_t **npp, char *errbuf, size_t errlen,
	       struct stat *st)
{
  char redir[URL_MAX];
  int r;
  char *parent, *x;
  prop_t *meta, *album_art;

  meta = prop_create(NULL, "metadata");

  r = fa_probe(meta, url, redir, sizeof(redir), errbuf, errlen, st);
  
  switch(r) {
  case CONTENT_ARCHIVE:
    prop_destroy(meta);
    return file_open_dir(redir, npp, errbuf, errlen);
  case CONTENT_AUDIO:
 
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

    playqueue_play(url, parent, meta, 0);
    free(parent);
    *npp = NULL;
    return 0;

  case CONTENT_VIDEO:
  case CONTENT_DVD:
    prop_destroy(meta);
    file_open_video(url, npp);
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
be_file_open(const char *url0, const char *type0, const char *parent,
	     nav_page_t **npp, char *errbuf, size_t errlen)
{
  struct stat buf;

  if(fa_stat(url0, &buf, errbuf, errlen))
    return -1;

  return S_ISDIR(buf.st_mode) ? file_open_dir(url0, npp, errbuf, errlen) :
    file_open_file(url0, npp, errbuf, errlen, &buf);
}


/**
 *
 */
static prop_t *
be_list(const char *url, char *errbuf, size_t errsize)
{
  prop_t *p = prop_create(NULL, NULL);

  fa_scanner(url, p, 0);
  return p;
}



/**
 *
 */
nav_backend_t be_file = {
  .nb_init = fileaccess_init,
  .nb_canhandle = be_file_canhandle,
  .nb_open = be_file_open,
  .nb_play_video = be_file_playvideo,
  .nb_play_audio = be_file_playaudio,
  .nb_list = be_list,
  .nb_imageloader = fa_imageloader,
};


