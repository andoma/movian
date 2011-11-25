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
#include "misc/pixmap.h"

prop_t *global_sources; // Move someplace else


LIST_HEAD(backend_list, backend);

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

  LIST_FOREACH(be, &backends, be_global_link)
    if(be->be_init != NULL)
      be->be_init();
}


/**
 *
 */
void
backend_fini(void)
{
  backend_t *be;

  LIST_FOREACH(be, &backends, be_global_link)
    if(be->be_fini != NULL)
      be->be_fini();
}


/**
 *
 */
event_t *
backend_play_video(const char *url, struct media_pipe *mp,
		   int flags, int priority,
		   char *errbuf, size_t errlen,
		   const char *mimetype, 
		   const char *canonical_url)
{
  backend_t *nb = backend_canhandle(url);

  if(nb == NULL || nb->be_play_video == NULL) {
    snprintf(errbuf, errlen, "No backend for URL");
    return NULL;
  }

  return nb->be_play_video(url, mp, flags, priority, errbuf, errlen, mimetype,
			   canonical_url);
}


/**
 *
 */
event_t *
backend_play_audio(const char *url, struct media_pipe *mp,
		   char *errbuf, size_t errlen, int paused,
		   const char *mimetype)
{
  backend_t *nb = backend_canhandle(url);
  
  if(nb == NULL || nb->be_play_audio == NULL) {
    snprintf(errbuf, errlen, "No backend for URL");
    return NULL;
  }
  return nb->be_play_audio(url, mp, errbuf, errlen, paused, mimetype);
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
be_page_open(prop_t *root, const char *url0)
{
  prop_t *src = prop_create(root, "model");
  prop_t *metadata = prop_create(src, "metadata");
  char *cap = mystrdupa(url0 + strlen("page:"));

  prop_set_string(prop_create(src, "type"), cap);
  cap[0] = toupper((int)cap[0]);
  prop_set_string(prop_create(metadata, "title"), cap);
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
backend_imageloader(const char *url, const image_meta_t *im,
		    const char **vpaths, char *errbuf, size_t errlen)
{
  if(!strncmp(url, "thumb://", 8)) {
    image_meta_t im0;

    if(im != NULL) {
      im0 = *im;
    } else {
      memset(&im0, 0, sizeof(im0));
    }
    url = url + 8;
    im0.im_want_thumb = 1;
    im = &im0;
  }

  backend_t *nb = backend_canhandle(url);
  pixmap_t *pm;
  if(nb == NULL || nb->be_imageloader == NULL) {
    snprintf(errbuf, errlen, "No backend for URL");
    return NULL;
  }
  pm = nb->be_imageloader(url, im, vpaths, errbuf, errlen);
  if(pm == NULL)
    return NULL;

  return pixmap_decode(pm, im, errbuf, errlen);
}


/**
 *
 */
backend_t *
backend_canhandle(const char *url)
{
  backend_t *be, *best = NULL;
  int score = 0, s;

  LIST_FOREACH(be, &backends, be_global_link) {
    if(be->be_canhandle == NULL)
      continue; 
    s = be->be_canhandle(url);
    if(s > score) {
      best = be;
      score = s;
    }
  }
  return best;
}


/**
 *
 */
backend_probe_result_t 
backend_probe(const char *url, char *errbuf, size_t errlen)
{
  backend_t *be = backend_canhandle(url);
  if(be == NULL) {
    snprintf(errbuf, errlen, "No handler for URL");
    return BACKEND_PROBE_NO_HANDLER;
  }

  if(be->be_probe == NULL)
    return BACKEND_PROBE_OK;

  return be->be_probe(url, errbuf, errlen);
}


/**
 *
 */
int
backend_open_video(prop_t *page, const char *url)
{
  prop_set_int(prop_create(page, "directClose"), 1);
  prop_set_string(prop_create(page, "source"), url);
  prop_set_string(prop_create(prop_create(page, "model"), "type"), "video");
  return 0;
}


static int
be_vp_canhandle(const char *url)
{
  return !strncmp(url, "videoparams:", strlen("videoparams:"));
}




/**
 *
 */
static backend_t be_videoparams = {
  .be_canhandle = be_vp_canhandle,
  .be_open = backend_open_video,
};

BE_REGISTER(videoparams);


/**
 *
 */
int
backend_open(prop_t *page, const char *url)
{
  backend_t *be;
  char urlbuf[URL_MAX];

  LIST_FOREACH(be, &backends, be_global_link) {
    if(be->be_flags & BACKEND_OPEN_CHECKS_URI) {
      if(be->be_open(page, url))
	continue;
      return 0;
    }
  }

  be = backend_canhandle(url);

  if(be == NULL)
    return 1;

  if(be->be_normalize != NULL && 
     !be->be_normalize(url, urlbuf, sizeof(urlbuf)))
    url = urlbuf;

  be->be_open(page, url);
  return 0;
}


/**
 *
 */
int
backend_resolve_item(const char *url, prop_t *item)
{
  backend_t *be = backend_canhandle(url);
  if(be == NULL || be->be_resolve_item == NULL)
    return -1;

  return be->be_resolve_item(url, item);
}


/**
 *
 */
void
backend_search(prop_t *model, const char *url)
{
  backend_t *be;

  LIST_FOREACH(be, &backends, be_global_link)
    if(be->be_search != NULL)
      be->be_search(model, url);
}
