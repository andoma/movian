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
#include "htsmsg/htsmsg_json.h"
#include "media.h"

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
		   char *errbuf, size_t errlen,
		   video_queue_t *vq, struct vsource_list *vsl,
		   const video_args_t *va)
{
  backend_t *nb = backend_canhandle(url);

  if(nb == NULL || nb->be_play_video == NULL) {
    snprintf(errbuf, errlen, "No backend for URL");
    return NULL;
  }

  mp_set_url(mp, va->canonical_url);

  return nb->be_play_video(url, mp, errbuf, errlen, vq, vsl, va);
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
be_page_open(prop_t *root, const char *url0, int sync)
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




typedef struct cached_image {
  LIST_ENTRY(cached_image) ci_link;
  
  rstr_t *ci_url;
  pixmap_t *ci_pixmap;

} cached_image_t;




/**
 *
 */
struct pixmap *
backend_imageloader(rstr_t *url0, const image_meta_t *im0,
		    const char **vpaths, char *errbuf, size_t errlen,
		    int *cache_control, be_load_cb_t *cb, void *opaque)
{
  const char *url = rstr_get(url0);
  htsmsg_t *m = NULL;

  if(im0->im_req_width < -1 || im0->im_req_height < -1) {
    snprintf(errbuf, errlen, "Invalid dimensions");
    return NULL;
  }

  image_meta_t im = *im0;

  if(!strncmp(url, "thumb://", 8)) {
    url += 8;
    im.im_want_thumb = 1;
  }

  if(!strncmp(url, "imageset:", 9)) {

    m = htsmsg_json_deserialize(url+9);
    if(m == NULL) {
      snprintf(errbuf, errlen, "Invalid JSON");
      return NULL;
    }
    htsmsg_field_t *f;

    const char *best = NULL;
    int best_width = -1;
    int best_height = -1;

    HTSMSG_FOREACH(f, m) {
      htsmsg_t *img = htsmsg_get_map_by_field(f);
      if(img == NULL)
	continue;
      int w = htsmsg_get_u32_or_default(img, "width", 10000);
      int h = htsmsg_get_u32_or_default(img, "height", 10000);
      const char *u = htsmsg_get_str(img, "url");
      if(!u)
	continue;

      if(best != NULL) {

	if(im.im_req_width != -1) {
	  if(w >= im.im_req_width &&
	     (w < best_width || best_width < im.im_req_width))
	    goto gotone;
	  if(w < im.im_req_width && w > best_width)
	    goto gotone;

	} else if(im.im_req_height != -1) {
	  if(h >= im.im_req_height &&
	     (h < best_height || best_height < im.im_req_height))
	    goto gotone;
	  if(h < im.im_req_height && h > best_height)
	    goto gotone;
	} else {
	  if(w > best_width)
	    goto gotone;
	  if(h > best_height)
	    goto gotone;
	}
	continue;
      }
    gotone:
      best = u;
      best_width = w;
      best_height = h;

    }
    if(best == NULL) {
      snprintf(errbuf, errlen, "No image in set");
      htsmsg_destroy(m);
      return NULL;
    }
    url = best;
  }


  im.im_margin = MAX(im.im_shadow * 2, im.im_margin);
  backend_t *nb = backend_canhandle(url);
  pixmap_t *pm = NULL;
  if(nb == NULL || nb->be_imageloader == NULL) {
    snprintf(errbuf, errlen, "No backend for URL");
  } else {
    pm = nb->be_imageloader(url, &im, vpaths, errbuf, errlen, cache_control,
			    cb, opaque);
    if(pm != NULL && pm != NOT_MODIFIED && !im.im_no_decoding) {
      pm = pixmap_decode(pm, &im, errbuf, errlen);

      if(pm != NULL && pm->pm_type == PIXMAP_VECTOR)
        pm = pixmap_rasterize_ft(pm);

      if(pm != NULL && im.im_shadow)
        pixmap_drop_shadow(pm, im.im_shadow, im.im_shadow);

      if(pm != NULL && im.im_corner_radius)
	pm = pixmap_rounded_corners(pm, im.im_corner_radius,
				    im.im_corner_selection);

    }
  }
  if(m)
    htsmsg_destroy(m);
  return pm;
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
backend_open_video(prop_t *page, const char *url, int sync)
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
backend_open(prop_t *page, const char *url, int sync)
{
  backend_t *be;
  char urlbuf[URL_MAX];

  LIST_FOREACH(be, &backends, be_global_link) {
    if(be->be_flags & BACKEND_OPEN_CHECKS_URI) {
      if(be->be_open(page, url, sync))
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

  be->be_open(page, url, sync);
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

