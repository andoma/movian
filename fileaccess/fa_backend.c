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
be_file_canhandle(const char *uri)
{
  return fa_can_handle(uri);
}

/**
 *
 */
static void *
scanner(void *aux)
{
  be_file_page_t *bfp = aux;
  prop_t *media, *p;
  int r, destroyed = 0;
  fa_dir_t *fd;
  fa_dir_entry_t *fde;
  void *ref;
  int images = 0;

  ref = fa_reference(bfp->h.np_uri);

  if((fd = fa_scandir(bfp->h.np_uri)) == NULL) {
    fa_unreference(ref);
    return NULL;
  }
  if(fd->fd_count == 0) {
    prop_set_string(bfp->bfp_viewprop, "empty");
    fa_dir_free(fd);
    fa_unreference(ref);
    return NULL;
  }

  fa_dir_sort(fd);

  /* First pass, just add filename and type */
  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {

    if(bfp->bfp_stop)
      break;

    p = prop_create(NULL, "node");
    prop_set_string(prop_create(p, "filename"), fde->fde_filename);
    prop_set_string(prop_create(p, "url"), fde->fde_url);

    media = prop_create(p, "media");
    prop_set_string(prop_create(media, "title"), fde->fde_filename);

    fa_set_type(media, fde->fde_type);

    fde->fde_opaque = p;
    prop_set_parent(p, bfp->bfp_nodes);
  }

  /* Second pass, do full probe */
  TAILQ_FOREACH(fde, &fd->fd_entries, fde_link) {

    if(bfp->bfp_stop)
      break;

    p = fde->fde_opaque;
    media = prop_create(p, "media");

    if(fde->fde_type == FA_DIR) {
      r = fa_probe_dir(media, fde->fde_url);
    } else {
      r = fa_probe(media, fde->fde_url, NULL, 0);
    }

    switch(r) {
    case FA_UNKNOWN:
      destroyed++;
      prop_destroy(p);
      break;

    case FA_IMAGE:
      images++;
      break;
    }
  }

  if(!bfp->bfp_stop) {

    if(fd->fd_count == destroyed)
      prop_set_string(bfp->bfp_viewprop, "empty");
    else if(images * 4 > fd->fd_count * 3) {
      prop_set_string(bfp->bfp_viewprop, "images");
    }
  }

  fa_dir_free(fd);
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
file_open_video(const char *uri0, nav_page_t **npp)
{
  nav_page_t *np;
  prop_t *p;

  np = nav_page_create(uri0, sizeof(nav_page_t), NULL, 0);

  p = np->np_prop_root;
  prop_set_string(prop_create(p, "type"), "video");
  *npp = np;
}


/**
 *
 */
static int
file_open_dir(const char *uri0, nav_page_t **npp, char *errbuf, size_t errlen)
{
  be_file_page_t *bfp;
  prop_t *p;
  int type;

  type = fa_probe_dir(NULL, uri0);

  if(type == FA_DVD) {
    file_open_video(uri0, npp);
    return 0;
  }

  /* TODO: Check if it is a DVD */

  bfp = nav_page_create(uri0, sizeof(be_file_page_t), dir_close_page,
			NAV_PAGE_DONT_CLOSE_ON_BACK);
  p = bfp->h.np_prop_root;

  prop_set_string(prop_create(p, "type"), "filedirectory");

  bfp->bfp_viewprop = prop_create(p, "view");
  prop_set_string(bfp->bfp_viewprop, "list");
  
  bfp->bfp_nodes = prop_create(p, "nodes");
  
  hts_thread_create_joinable(&bfp->bfp_scanner_thread, scanner, bfp);
  *npp = &bfp->h;
  return 0;
}



/**
 *
 */
static int
file_open_file(const char *uri0, nav_page_t **npp, char *errbuf, size_t errlen)
{
  char redir[512];
  int r;
  prop_t *media;

  media = prop_create(NULL, "media");

  r = fa_probe(media, uri0, redir, sizeof(redir));
  
  switch(r) {
  case FA_ARCHIVE:
    prop_destroy(media);
    return file_open_dir(redir, npp, errbuf, errlen);
  case FA_AUDIO:
    playqueue_play(uri0, NULL, media, 0);
    *npp = NULL;
    return 0;
  case FA_VIDEO:
    prop_destroy(media);
    file_open_video(uri0, npp);
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
be_file_open(const char *uri0, nav_page_t **npp, char *errbuf, size_t errlen)
{
  struct stat buf;

  if(fa_stat(uri0, &buf)) {
    snprintf(errbuf, errlen, "Unable to stat uri");
    return -1;
  }

  return S_ISDIR(buf.st_mode) ? file_open_dir(uri0, npp, errbuf, errlen) :
    file_open_file(uri0, npp, errbuf, errlen);
}


/**
 *
 */
nav_backend_t be_file = {
  .nb_canhandle = be_file_canhandle,
  .nb_open = be_file_open,
  .nb_play_video = be_file_playvideo,
};


