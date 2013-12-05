/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

#ifndef GLW_EVENT_H__
#define GLW_EVENT_H__

#include "event.h"

/** 
 *
 */
typedef struct glw_event_map {
  LIST_ENTRY(glw_event_map) gem_link;

  action_type_t gem_action;

  void (*gem_fire)(glw_t *w, struct glw_event_map *gem, struct event *src);
  void (*gem_dtor)(glw_root_t *gr, struct glw_event_map *gem);

} glw_event_map_t;

/**
 *
 */
void glw_event_map_remove_by_action(glw_t *w, action_type_t action);


/**
 *
 */
void glw_event_map_add(glw_t *w, glw_event_map_t *gem);

/**
 *
 */
void glw_event_map_destroy(glw_event_map_t *gem);

/**
 *
 */
int glw_event_map_intercept(glw_t *w, struct event *e);


/**
 *
 */
glw_event_map_t *glw_event_map_navOpen_create(const char *url,
					      const char *view,
					      prop_t *origin,
					      prop_t *model,
					      const char *how);


/**
 *
 */
glw_event_map_t *glw_event_map_selectTrack_create(const char *id,
						  event_type_t type);


/**
 *
 */
glw_event_map_t *glw_event_map_playTrack_create(prop_t *track,
						prop_t *source, int mode);


/**
 *
 */
glw_event_map_t *glw_event_map_internal_create(const char *target,
					       action_type_t event,
					       int uc);


/**
 *
 */
glw_event_map_t *glw_event_map_deliverEvent_create(prop_t *target,
						   rstr_t *action);


/**
 *
 */
glw_event_map_t *glw_event_map_propref_create(prop_t *prop, prop_t *target);

#endif /* GLW_EVENT_H__ */
