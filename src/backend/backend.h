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
#ifndef BACKEND_H__
#define BACKEND_H__

#include "prop/prop.h"
#include "misc/cancellable.h"

struct image;
struct image_meta;
struct media_pipe;
struct navigator;
struct event;
struct vsource_list;
typedef struct video_queue video_queue_t;

typedef void (be_load_cb_t)(void *opaque, int loaded, int total);

/**
 * Kept in sync with service_status_t
 * These are also directly exposed in the javascript API so they
 * can never be changed. OK must be 0, all other are errors but should
 * not be renumbered
 */
typedef enum {
  BACKEND_PROBE_OK = 0,
  BACKEND_PROBE_AUTH,
  BACKEND_PROBE_NO_HANDLER,
  BACKEND_PROBE_FAIL,
} backend_probe_result_t;


#define BACKEND_VIDEO_PRIMARY      0x1
#define BACKEND_VIDEO_NO_AUDIO     0x2
#define BACKEND_VIDEO_NO_FS_SCAN   0x4 // Don't scan FS for subtitles
#define BACKEND_VIDEO_SET_TITLE    0x8
#define BACKEND_VIDEO_NO_SUBTITLE_SCAN 0x10 // Don't scan for subtitles at all


/**
 *
 */
typedef struct video_args {
  int flags;
  int priority;
  int resume_mode;
  
  const char *mimetype;
  const char *canonical_url;
  const char *title;
  const char *imdb;
  int season;
  int episode;
  int year;

  int hash_valid;
  int64_t filesize;
  uint64_t opensubhash;
  uint8_t subdbhash[16]; // md5sum of first 64k + last 64k
  const char *parent_url;
  const char *parent_title;
  prop_t *origin;
  int64_t load_request_timestamp; // For start latency statistics
} video_args_t;


/**
 *
 */
typedef struct backend {

  LIST_ENTRY(backend) be_global_link;

  int be_flags;
#define BACKEND_OPEN_CHECKS_URI 0x1

  int (*be_init)(void);

  void (*be_fini)(void);

  int (*be_canhandle)(const char *ur);

  int (*be_open)(prop_t *page, const char *url, int sync);

  struct event *(*be_play_video)(const char *url,
				 struct media_pipe *mp,
				 char *errbuf, size_t errlen,
				 video_queue_t *vq,
                                 struct vsource_list *vsl,
				 const video_args_t *va);

  struct event *(*be_play_audio)(const char *url, struct media_pipe *mp,
				 char *errbuf, size_t errlen, int paused,
				 const char *mimetype);

  struct image *(*be_imageloader)(const char *url, const struct image_meta *im,
                                  const char **vpaths,
                                  char *errbuf, size_t errlen,
                                  int *cache_control,
                                  cancellable_t *c);

  int (*be_normalize)(const char *url, char *dst, size_t dstlen);

  int (*be_probe)(const char *url, char *errbuf, size_t errlen, int timeout_ms);

  void (*be_search)(struct prop *model, const char *query, prop_t *loading);

  int (*be_resolve_item)(const char *url, prop_t *item);

} backend_t;



/**
 *
 */
void backend_init(void);

void backend_fini(void);

int backend_open(struct prop *page, const char *url, int sync)
     attribute_unused_result;

struct event *backend_play_video(const char *url, struct media_pipe *mp,
				 char *errbuf, size_t errlen,
				 video_queue_t *vq,
                                 struct vsource_list *vsl,
				 const video_args_t *va)
     attribute_unused_result;


struct event *backend_play_audio(const char *url, struct media_pipe *mp,
				 char *errbuf, size_t errlen, int paused,
				 const char *mimetype)
     attribute_unused_result;


struct image *backend_imageloader(rstr_t *url, const struct image_meta *im,
                                  const char **vpaths,
                                  char *errbuf, size_t errlen,
                                  int *cache_control,
                                  cancellable_t *c)
     attribute_unused_result;

backend_t *backend_canhandle(const char *url)
     attribute_unused_result;


backend_probe_result_t backend_probe(const char *url,
				     char *errbuf, size_t errlen,
                                     int timeout_ms)
     attribute_unused_result;

void backend_register(backend_t *be);

int backend_open_video(prop_t *page, const char *url, int sync);

int backend_resolve_item(const char *url, prop_t *item)
     attribute_unused_result;

rstr_t *backend_normalize(rstr_t *url);

void backend_search(prop_t *model, const char *url, prop_t *loading);

int backend_page_open(prop_t *root, const char *url0, int sync);

#define BE_REGISTER(name)                                              \
  INITIALIZER(backend_init_ ## name) {                                 \
    backend_register(&be_ ## name);                                    \
  }


#endif /* BACKEND_H__ */
