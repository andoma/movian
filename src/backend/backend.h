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
#include "prop.h"

LIST_HEAD(backend_list, backend);

struct pixmap;
struct media_pipe;
struct nav_page;
struct navigator;

/**
 *
 */
typedef struct backend {

  LIST_ENTRY(backend) be_global_link;

  int (*be_init)(void);

  int (*be_canhandle)(const char *ur);

  struct nav_page *(*be_open)(struct navigator *nav, const char *url, 
			      char *errbuf, size_t errlen);

  event_t *(*be_play_video)(const char *url, struct media_pipe *mp,
			    char *errbuf, size_t errlen);

  event_t *(*be_play_audio)(const char *url, struct media_pipe *mp,
			    char *errbuf, size_t errlen);

  prop_t *(*be_list)(const char *url, char *errbuf, size_t errsize);

  struct pixmap *(*be_imageloader)(const char *url, int want_thumb,
				   const char *theme,
				   char *errbuf, size_t errlen);

  int (*be_normalize)(const char *url, char *dst, size_t dstlen);

} backend_t;


/**
 *
 */
void backend_init(void);

event_t *backend_play_video(const char *url, struct media_pipe *mp,
			    char *errbuf, size_t errlen);

event_t *backend_play_audio(const char *url, struct media_pipe *mp,
			    char *errbuf, size_t errlen);

prop_t *backend_list(const char *url, char *errbuf, size_t errlen);

struct pixmap *backend_imageloader(const char *url, int want_thumb,
				   const char *theme,
				   char *errbuf, size_t errlen);

backend_t *backend_canhandle(const char *url);

void backend_register(backend_t *be);

struct nav_page *backend_open_video(struct navigator *nav, const char *url,
				    char *errbuf, size_t errlen);

#define BE_REGISTER(name) \
  static void  __attribute__((constructor)) backend_init_ ## name(void) {\
    backend_register(&be_ ## name); }


#endif /* BACKEND_H__ */
