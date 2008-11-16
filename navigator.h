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

#include "ui/glw/glw.h"

TAILQ_HEAD(nav_page_queue, nav_page);
LIST_HEAD(nav_backend_list, nav_backend);


/**
 *
 */
typedef struct nav_page {
  glw_event_queue_t np_geq;
 
  TAILQ_ENTRY(nav_page) np_global_link;
  TAILQ_ENTRY(nav_page) np_history_link;
  int np_inhistory;

  hts_prop_t *np_prop_root;
  char *np_url;

  struct nav_backend *np_be;

} nav_page_t;


/**
 *
 */
typedef struct nav_backend {

  LIST_ENTRY(nav_backend) nb_global_link;

  int (*nb_canhandle)(const char *url);

  nav_page_t *(*nb_open)(const char *url, char *errbuf, size_t errlen);

} nav_backend_t;


/**
 *
 */
void nav_init(void);

void nav_close(nav_page_t *np);

void nav_open(const char *url);

void *nav_page_create(struct nav_backend *be, const char *url, 
		      size_t allocsize);

#endif /* NAVIGATOR_H__ */
