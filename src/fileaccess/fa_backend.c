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
#include "playqueue.h"

typedef struct be_file_page {
  nav_page_t h;

  prop_t *bfp_nodes;
  prop_t *bfp_viewprop;

  int bfp_stop;

  hts_thread_t bfp_scanner_thread;

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
const char *type2str[] = {
  [CONTENT_DIR]      = "directory",
  [CONTENT_FILE]     = "file",
  [CONTENT_AUDIO]    = "audio",
  [CONTENT_ARCHIVE]  = "archive",
  [CONTENT_VIDEO]    = "video",
  [CONTENT_PLAYLIST] = "playlist",
  [CONTENT_DVD]      = "dvd",
  [CONTENT_IMAGE]    = "image",
};


/**
 *
 */
static void
set_type(prop_t *proproot, unsigned int type)
{
  if(type < sizeof(type2str) / sizeof(type2str[0]) && type2str[type] != NULL)
    prop_set_string(prop_create(proproot, "type"), type2str[type]);
}


/**
 *
 */
static void *
scanner(void *aux)
{
  be_file_page_t *bfp = aux;
  prop_t *metadata, *p, *p2;
  int r, destroyed = 0;
  nav_dir_t *nd;
  nav_dir_entry_t *nde;
  void *ref;
  int images;
  int album_score = 0;
  const char *s;
  char album_name[128];
  
  ref = fa_reference(bfp->h.np_url);

  if((nd = fa_scandir(bfp->h.np_url, NULL, 0)) == NULL) {
    fa_unreference(ref);
    return NULL;
  }
  if(nd->nd_count == 0) {
    prop_set_string(bfp->bfp_viewprop, "empty");
    nav_dir_free(nd);
    fa_unreference(ref);
    return NULL;
  }

  nav_dir_sort(nd);


  images = 0;
  /* Select a view based on filenames */
  TAILQ_FOREACH(nde, &nd->nd_entries, nde_link) {
    if(bfp->bfp_stop)
      break;

    if((s = strchr(nde->nde_filename, '.')) == NULL)
      continue;
    s++;

    if(!strcasecmp(s, "jpg") || !strcmp(s, "jpeg"))
      images++;
  }

  if(images * 4 > nd->nd_count * 3)
    prop_set_string(bfp->bfp_viewprop, "images");


  /* Add filename and type */
  TAILQ_FOREACH(nde, &nd->nd_entries, nde_link) {

    if(bfp->bfp_stop)
      break;

    p = prop_create(NULL, "node");
    prop_set_string(prop_create(p, "url"), nde->nde_url);
    set_type(p, nde->nde_type);

    metadata = prop_create(p, "metadata");
    prop_set_string(prop_create(metadata, "title"), nde->nde_filename);

    if(prop_set_parent(p, bfp->bfp_nodes))
      prop_destroy(p);
    else
      nde->nde_opaque = p;
  }

  images = 0;
  album_name[0] = 0;
  /* Do full probe */
  TAILQ_FOREACH(nde, &nd->nd_entries, nde_link) {

    if(bfp->bfp_stop)
      break;

    if((p = nde->nde_opaque) == NULL)
      continue;

    metadata = prop_create(p, "metadata");

    if(nde->nde_type == CONTENT_DIR) {
      r = fa_probe_dir(metadata, nde->nde_url);
    } else {
      r = fa_probe(metadata, nde->nde_url, NULL, 0, NULL, 0);
    }

    set_type(p, r);
    nde->nde_type = r;

    switch(r) {
    case CONTENT_AUDIO:
      if((p2 = prop_get_by_names(metadata, "album", NULL)) != NULL) {
	char buf[128];
	if(!prop_get_string(p2, buf, sizeof(buf))) {

	  if(album_name[0] == 0) {
	    snprintf(album_name, sizeof(album_name), "%s", buf);
	    album_score++;
	  } else if(!strcasecmp(album_name, buf)) {
	    album_score++;
	  } else {
	    album_score--;
	  }
	}
      }
      break;

    case CONTENT_UNKNOWN:
      destroyed++;
      prop_destroy(p);
      break;

    case CONTENT_IMAGE:
      images++;
      break;

    default:
      album_score--;
    }
  }

  if(!bfp->bfp_stop) {


    if(album_score > 0) {
      
      /* It is an album */
      prop_set_string(bfp->bfp_viewprop, "album");
      
      prop_set_string(prop_create(bfp->h.np_prop_root, "album_name"), 
		      album_name);
      
      /* Remove everything that is not audio */
      TAILQ_FOREACH(nde, &nd->nd_entries, nde_link) {
	if(bfp->bfp_stop)
	  break;
	if((p = nde->nde_opaque) == NULL)
	  continue;
	
	if(nde->nde_type != CONTENT_AUDIO) {
	  prop_destroy(p);
	}
      }
      
    } else if(nd->nd_count == destroyed) {
      prop_set_string(bfp->bfp_viewprop, "empty");
    } else if(images * 4 > nd->nd_count * 3) {
      prop_set_string(bfp->bfp_viewprop, "images");
    }
  }

  nav_dir_free(nd);
  fa_unreference(ref);
  return NULL;
}

/**
 *
 */
static void
dir_close_page(nav_page_t *np)
{
  be_file_page_t *bfp = (be_file_page_t *)np;

  bfp->bfp_stop = 1; 
  hts_thread_join(&bfp->bfp_scanner_thread);
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
  int type;
  const char *dirname;

  type = fa_probe_dir(NULL, url0);

  if(type == CONTENT_DVD) {
    file_open_video(url0, npp);
    return 0;
  }

  bfp = nav_page_create(url0, sizeof(be_file_page_t), dir_close_page,
			NAV_PAGE_DONT_CLOSE_ON_BACK);
  p = bfp->h.np_prop_root;

  prop_set_string(prop_create(p, "type"), "directory");

  bfp->bfp_viewprop = prop_create(p, "view");
  prop_set_string(bfp->bfp_viewprop, "list");
  
  if((dirname = strrchr(url0, '/')) != NULL)
    dirname++;
  else
    dirname = url0;

  prop_set_string(prop_create(p, "title"), dirname);

  bfp->bfp_nodes = prop_create(p, "nodes");
  
  hts_thread_create_joinable(&bfp->bfp_scanner_thread, scanner, bfp);
  *npp = &bfp->h;
  return 0;
}



/**
 *
 */
static int
file_open_file(const char *url0, nav_page_t **npp, char *errbuf, size_t errlen)
{
  char redir[512];
  int r;
  prop_t *media;

  media = prop_create(NULL, "metadata");

  r = fa_probe(media, url0, redir, sizeof(redir), errbuf, errlen);
  
  switch(r) {
  case CONTENT_ARCHIVE:
    prop_destroy(media);
    return file_open_dir(redir, npp, errbuf, errlen);
  case CONTENT_AUDIO:
    playqueue_play(url0, NULL, media, 0);
    *npp = NULL;
    return 0;
  case CONTENT_VIDEO:
  case CONTENT_DVD:
    prop_destroy(media);
    file_open_video(url0, npp);
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
be_file_open(const char *url0, nav_page_t **npp, char *errbuf, size_t errlen)
{
  struct stat buf;

  if(fa_stat(url0, &buf, errbuf, errlen))
    return -1;

  return S_ISDIR(buf.st_mode) ? file_open_dir(url0, npp, errbuf, errlen) :
    file_open_file(url0, npp, errbuf, errlen);
}

/**
 *
 */
static nav_dir_t *
be_scandir(const char *url, char *errbuf, size_t errsize)
{
  nav_dir_t *nd = fa_scandir(url, errbuf, errsize);
  
  if(nd != NULL)
    nav_dir_sort(nd);
  return nd;
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
  .nb_probe = fa_probe,
  .nb_scandir = be_scandir,
};


