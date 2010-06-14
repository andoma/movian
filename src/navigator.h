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

#define NAV_HOME "page:home"

/**
 *
 */
typedef struct nav_page {
  struct navigator *np_nav;

  TAILQ_ENTRY(nav_page) np_global_link;
  TAILQ_ENTRY(nav_page) np_history_link;
  int np_inhistory;

  prop_t *np_prop_root;
  char *np_url;
  char *np_view;

  int np_flags;

#define NAV_PAGE_DONT_CLOSE_ON_BACK 0x1
#define NAV_PAGE_PRESET_VIEW        0x2

  prop_sub_t *np_close_sub;

} nav_page_t;



/**
 *
 */
void nav_init(void);

prop_t *nav_spawn(void);

void nav_open(const char *url, const char *view);

void *nav_page_create(struct navigator *nav, const char *url, const char *view,
		      size_t allocsize, int flags);

#endif /* NAVIGATOR_H__ */
