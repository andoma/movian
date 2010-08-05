/*
 *  Backend
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

#ifndef BACKEND_H__
#define BACKEND_H__

#include "event.h"
#include "prop/prop.h"

struct pixmap;
struct media_pipe;
struct nav_page;
struct navigator;

/**
 * Kept in sync with service_status_t
 */
typedef enum {
  BACKEND_PROBE_OK,
  BACKEND_PROBE_AUTH,
  BACKEND_PROBE_NO_HANDLER,
  BACKEND_PROBE_FAIL,
} backend_probe_result_t;


/**
 *
 */
typedef struct backend {

  LIST_ENTRY(backend) be_global_link;

  int be_flags;
#define BACKEND_OPEN_CHECKS_URI 0x1

  int (*be_init)(void);

  int (*be_canhandle)(struct backend *be, const char *ur);

  struct nav_page *(*be_open)(struct backend *be, struct navigator *nav, 
			      const char *url, const char *view,
			      char *errbuf, size_t errlen);

#define BACKEND_NOURI ((struct nav_page *)-1)

  event_t *(*be_play_video)(struct backend *be, const char *url,
			    struct media_pipe *mp,
			    int primary, int priority, 
			    char *errbuf, size_t errlen);

  event_t *(*be_play_audio)(struct backend *be, const char *url,
			    struct media_pipe *mp,
			    char *errbuf, size_t errlen);

  prop_t *(*be_list)(struct backend *be, const char *url, 
		     char *errbuf, size_t errsize);

  struct pixmap *(*be_imageloader)(struct backend *be, 
				   const char *url, int want_thumb,
				   const char *theme,
				   char *errbuf, size_t errlen);

  int (*be_normalize)(struct backend *be, const char *url,
		      char *dst, size_t dstlen);

  int (*be_probe)(struct backend *be, const char *url,
		  char *errbuf, size_t errlen);

  void (*be_search)(struct backend *be, struct prop *model, const char *query);

} backend_t;



/**
 *
 */
void backend_init(void);

struct nav_page *backend_open(struct navigator *nav, const char *url,
			      const char *view, char *errbuf, size_t errlen);


event_t *backend_play_video(const char *url, struct media_pipe *mp,
			    int primary, int priority,
			    char *errbuf, size_t errlen);

event_t *backend_play_audio(const char *url, struct media_pipe *mp,
			    char *errbuf, size_t errlen);

prop_t *backend_list(const char *url, char *errbuf, size_t errlen);

struct pixmap *backend_imageloader(const char *url, int want_thumb,
				   const char *theme,
				   char *errbuf, size_t errlen);

backend_t *backend_canhandle(const char *url);

backend_probe_result_t backend_probe(const char *url,
				     char *errbuf, size_t errlen);

void backend_register(backend_t *be);

struct nav_page *backend_open_video(backend_t *be, 
				    struct navigator *nav, const char *url,
				    const char *view,
				    char *errbuf, size_t errlen);

void backend_search(prop_t *model, const char *url);

#define BE_REGISTER(name) \
  static void  __attribute__((constructor)) backend_init_ ## name(void) {\
  static int cnt;							\
  if(cnt == 0)								\
    backend_register(&be_ ## name);					\
  cnt++;								\
  }


prop_t *search_get_settings(void);

int search_class_create(prop_t *parent, prop_t **nodesp, prop_t **entriesp,
			const char *title);

#endif /* BACKEND_H__ */
