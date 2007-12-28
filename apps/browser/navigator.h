/*
 *  Browser navigator
 *  Copyright (C) 2007 Andreas Öman
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

#ifndef NAVIGATOR_H
#define NAVIGATOR_H


#define NAV_HASH 503



typedef struct naventry {
  LIST_ENTRY(naventry) ne_link;
  TAILQ_ENTRY(naventry) ne_parent_link;
  int ne_id;

  const char *ne_url;

  glw_t *ne_naventry;
  glw_t *ne_title_xfader;
  struct navdir *ne_dir;

  mediainfo_t ne_mi;

  time_t ne_flush_content_time;
  TAILQ_ENTRY(naventry) ne_flush_link;
} naventry_t;

#define NE_IS_DIR(ne) ((ne)->ne_mi.mi_type == MI_DIR)



typedef struct navdir {
  LIST_ENTRY(navdir) nd_link;
  int nd_id;

  glw_t *nd_widget;
  void *nd_backend;

  struct navdir *nd_parent;
  TAILQ_HEAD(, naventry) nd_childs;

  const char *nd_icon;

  glw_t *nd_slideshow;

} navdir_t;


typedef struct nav {
  LIST_HEAD(, navdir) n_alldirs[NAV_HASH];
  LIST_HEAD(, naventry) n_allentries[NAV_HASH];
  TAILQ_HEAD(, naventry) n_flushqueue;

  navdir_t *n_curdir;
  int n_dir_id_tally;

  browser_interface_t *n_interface;

  void *n_curprobe;

  appi_t *n_ai;

  glw_t *n_top_nav;
  glw_t *n_backdrop_xfader;
  navdir_t *n_backdrop_dir;

  /* Slideshow members */

  float n_slideshow_blend;
  int n_slideshow_mode;
} nav_t;


void nav_slideshow(nav_t *n, navdir_t *nd, naventry_t *ne);

void nav_dirscan(nav_t *n, const char *path);

#endif /* NAVIGATOR_H */
