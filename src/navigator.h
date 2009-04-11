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

#ifndef NAVIGATOR_H__
#define NAVIGATOR_H__

#include "event.h"
#include "prop.h"


TAILQ_HEAD(nav_dir_entry_queue, nav_dir_entry);

/**
 *
 */
typedef struct nav_dir_entry {
  TAILQ_ENTRY(nav_dir_entry) nde_link;
  char *nde_filename;
  char *nde_url;
  int   nde_type; /* CONTENT_ .. types from showtime.h */
  void *nde_opaque;
  prop_t *nde_metadata;
} nav_dir_entry_t;

/**
 *
 */
typedef struct nav_dir {
  struct nav_dir_entry_queue nd_entries;
  int nd_count;
} nav_dir_t;

nav_dir_t *nav_dir_alloc(void);

void nav_dir_free(nav_dir_t *nd);

void nav_dir_add(nav_dir_t *nd, const char *path, const char *name, int type,
		 prop_t *metadata);

void nav_dir_sort(nav_dir_t *nd);


/**
 *
 */
TAILQ_HEAD(nav_page_queue, nav_page);
LIST_HEAD(nav_backend_list, nav_backend);


/**
 *
 */
typedef struct nav_page {
  TAILQ_ENTRY(nav_page) np_global_link;
  TAILQ_ENTRY(nav_page) np_history_link;
  int np_inhistory;

  prop_t *np_prop_root;
  char *np_url;

  int np_flags;

#define NAV_PAGE_DONT_CLOSE_ON_BACK 0x1

  void (*np_close)(struct nav_page *np);

} nav_page_t;


struct media_pipe;
/**
 *
 */
typedef struct nav_backend {

  LIST_ENTRY(nav_backend) nb_global_link;

  int (*nb_init)(void);

  int (*nb_canhandle)(const char *ur);

  int (*nb_open)(const char *url, nav_page_t **npp,
		 char *errbuf, size_t errlen);

  event_t *(*nb_play_video)(const char *url, struct media_pipe *mp,
			    char *errbuf, size_t errlen);

  event_t *(*nb_play_audio)(const char *url, struct media_pipe *mp,
			    char *errbuf, size_t errlen);

  unsigned int (*nb_probe)(prop_t *proproot, const char *url,
			   char *newurl, size_t newurlsize,
			   char *errbuf, size_t errsize);

  nav_dir_t *(*nb_scandir)(const char *url, char *errbuf, size_t errsize);

} nav_backend_t;


/**
 *
 */
void nav_init(void);

void nav_close(nav_page_t *np);

#define NAV_OPEN_ASYNC 0x1
void nav_open(const char *url, int flags);

void nav_back(void);

void *nav_page_create(const char *url, size_t allocsize,
		      void (*closefunc)(struct nav_page *np),
		      int flags);

event_t *nav_play_video(const char *url, struct media_pipe *mp,
			char *errbuf, size_t errlen);

event_t *nav_play_audio(const char *url, struct media_pipe *mp,
			char *errbuf, size_t errlen);

unsigned int nav_probe(prop_t *proproot, const char *url,
		       char *newurl, size_t newurlsize,
		       char *errbuf, size_t errsize);

nav_dir_t *nav_scandir(const char *url, char *errbuf, size_t errlen);

#endif /* NAVIGATOR_H__ */
