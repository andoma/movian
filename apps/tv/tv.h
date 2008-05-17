/*
 *  TV application
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

#ifndef TV_H_
#define TV_H_

#include <libglw/glw.h>
#include "app.h"

TAILQ_HEAD(tv_ch_group_queue, tv_ch_group);
TAILQ_HEAD(tv_channel_queue,  tv_channel);

typedef struct tv_ch_group {
  char *tcg_name;
  struct tv_channel_queue tcg_channels;
  glw_t *tcg_widget, *tcg_tab, *tcg_channel_list;

  TAILQ_ENTRY(tv_ch_group) tcg_link;
} tv_ch_group_t;


typedef struct tv_channel {
  char *ch_name;
  uint32_t ch_tag;
  
  glw_t *ch_widget;

  TAILQ_ENTRY(tv_channel) ch_link;
} tv_channel_t;



typedef struct tv {
  appi_t *tv_ai;
  glw_t *tv_miniature;
  glw_t *tv_stack;

  pthread_mutex_t tv_ch_mutex;
  struct tv_ch_group_queue tv_groups;

  glw_t *tv_root;

} tv_t;

tv_ch_group_t *tv_channel_group_find(tv_t *tv, const char *name, int create);

tv_channel_t *tv_channel_find(tv_t *tv, tv_ch_group_t *tcg, 
			      const char *name, int create);

void tv_channel_set_icon(tv_channel_t *ch, const char *icon);

void tv_channel_set_current_event(tv_channel_t *ch, int index, 
				  const char *title, time_t start, time_t stop);

tv_channel_t *tv_channel_find_by_tag(tv_t *tv, uint32_t tag);

#endif /* TV_H_ */
