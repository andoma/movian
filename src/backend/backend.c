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
#include "config.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <ctype.h>

#include "main.h"
#include "backend.h"
#include "navigator.h"
#include "event.h"
#include "notifications.h"
#include "image/image.h"
#include "image/pixmap.h"
#include "htsmsg/htsmsg_json.h"
#include "media/media.h"
#include "misc/minmax.h"


prop_t *global_sources; // Move someplace else


LIST_HEAD(backend_list, backend);

static struct backend_list backends;



typedef struct loading_image {
  LIST_ENTRY(loading_image) li_link;
  TAILQ_ENTRY(loading_image) li_cache_link;
  image_t *li_image;
  int li_waiters;
  char li_done;
  char li_url[0];
} loading_image_t;


LIST_HEAD(loading_image_list, loading_image);
TAILQ_HEAD(loading_image_queue, loading_image);

static struct loading_image_list loading_images;
static struct loading_image_queue cached_images;
static int num_cached_images;

static hts_mutex_t imageloader_mutex;
static hts_cond_t imageloader_cond;


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

  hts_mutex_init(&imageloader_mutex);
  hts_cond_init(&imageloader_cond, &imageloader_mutex);

  TAILQ_INIT(&cached_images);

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
int
backend_page_open(prop_t *root, const char *url0, int sync)
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
  .be_open = backend_page_open,
};

BE_REGISTER(page);


/**
 *
 */
static void
prune_image_cache(void)
{
  while(num_cached_images > 3) {
    loading_image_t *li = TAILQ_FIRST(&cached_images);
    assert(li->li_waiters == 0);
    assert(li->li_done == 1);
    assert(li->li_image != NULL);

    num_cached_images--;
    TAILQ_REMOVE(&cached_images, li, li_cache_link);
    image_release(li->li_image);
    LIST_REMOVE(li, li_link);
    free(li);
  }
}

/**
 *
 */
image_t *
backend_imageloader(rstr_t *url0, const image_meta_t *im0,
		    struct fa_resolver *far, char *errbuf, size_t errlen,
		    int *cache_control, cancellable_t *c)
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
      htsmsg_release(m);
      return NULL;
    }
    url = best;
  }


  image_t *img = NULL;

  im.im_margin = MAX(im.im_shadow * 2, im.im_margin);

  hts_mutex_lock(&imageloader_mutex);

  loading_image_t *li;
  LIST_FOREACH(li, &loading_images, li_link)
    if(!strcmp(li->li_url, url))
      break;

  if(li == NULL) {
    li = calloc(1, sizeof(loading_image_t) + strlen(url) + 1);
    LIST_INSERT_HEAD(&loading_images, li, li_link);
    li->li_waiters = 1;
    strcpy(li->li_url, url);
  } else {

    if(li->li_waiters == 0) {
      num_cached_images--;
      TAILQ_REMOVE(&cached_images, li, li_cache_link);
    }

    li->li_waiters++;

    while(li->li_done == 0)
      hts_cond_wait(&imageloader_cond, &imageloader_mutex);

    if(li->li_image != NULL && im.im_want_thumb == 0) {
      img = image_retain(li->li_image);
    }

  }
  li->li_done = 0;

  hts_mutex_unlock(&imageloader_mutex);

  if(img == NULL) {
    backend_t *nb = backend_canhandle(url);
    if(nb == NULL || nb->be_imageloader == NULL) {
      snprintf(errbuf, errlen, "No backend for URL");
      img = NULL;
    } else {
      img = nb->be_imageloader(url, &im, far, errbuf,
                               errlen, cache_control, c);
    }
  }
  if(cancellable_is_cancelled(c)) {
    snprintf(errbuf, errlen, "Cancelled");
    if(img != NOT_MODIFIED)
      image_release(img);
    img = NULL;
  }

  if(img != NULL && img != NOT_MODIFIED) {

    if(!(img->im_flags & IMAGE_ADAPTED) && li->li_image == NULL)
      li->li_image = image_retain(img);

    if(!im.im_no_decoding) {
      img = image_decode(img, &im, errbuf, errlen);
    }

  }

  hts_mutex_lock(&imageloader_mutex);
  li->li_waiters--;
  li->li_done = 1;

  if(li->li_waiters == 0) {
    // No more waiters.

    if(li->li_image != NULL) {

      prune_image_cache();

      num_cached_images++;
      TAILQ_INSERT_TAIL(&cached_images, li, li_cache_link);
    } else {
      LIST_REMOVE(li, li_link);
      free(li);
    }

  } else {
    hts_cond_broadcast(&imageloader_cond);
  }

  hts_mutex_unlock(&imageloader_mutex);

  if(m)
    htsmsg_release(m);
  return img;
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
backend_probe(const char *url, char *errbuf, size_t errlen, int timeout_ms)
{
  if(timeout_ms <= 0)
    timeout_ms = 5000;

  backend_t *be = backend_canhandle(url);
  if(be == NULL) {
    snprintf(errbuf, errlen, "No handler for URL");
    return BACKEND_PROBE_NO_HANDLER;
  }

  if(be->be_probe == NULL)
    return BACKEND_PROBE_OK;

  return be->be_probe(url, errbuf, errlen, timeout_ms);
}


/**
 *
 */
int
backend_open_video(prop_t *page, const char *url, int sync)
{
  prop_set(page, "directClose", PROP_SET_INT, 1);
  prop_set(page, "source", PROP_SET_STRING, url);

  prop_t *m = prop_create_r(page, "model");
  prop_set(m, "type", PROP_SET_STRING, "video");
  prop_set(m, "loading", PROP_SET_INT, 0);
  prop_ref_dec(m);
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

  prop_set(page, "url", PROP_SET_STRING, url);

  be->be_open(page, url, sync);
  return 0;
}


/**
 *
 */
rstr_t *
backend_normalize(rstr_t *url)
{
  if(url == NULL)
    return NULL;

  backend_t *be = backend_canhandle(rstr_get(url));
  char tmp[1024];
  if(be == NULL || be->be_normalize == NULL)
    return url;

  if(!be->be_normalize(rstr_get(url), tmp, sizeof(tmp))) {
    rstr_release(url);
    return rstr_alloc(tmp);
  }
  return url;
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
backend_search(prop_t *model, const char *url, prop_t *loading)
{
  backend_t *be;

  LIST_FOREACH(be, &backends, be_global_link)
    if(be->be_search != NULL)
      be->be_search(model, url, loading);
}

