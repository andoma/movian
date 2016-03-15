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
#pragma once
#include "event.h"

/**
 *
 */
typedef struct glw_event_map {
  LIST_ENTRY(glw_event_map) gem_link;

  rstr_t *gem_filter;

  void (*gem_fire)(glw_t *w, struct glw_event_map *gem, struct event *src);
  void (*gem_dtor)(glw_root_t *gr, struct glw_event_map *gem);

  int gem_id;

  char gem_early; // Intercepts on event descent
  char gem_final;

  rstr_t *gem_file;
  int gem_line;

} glw_event_map_t;


/**
 *
 */
void glw_event_map_remove_by_id(glw_t *w, int id);

void glw_event_map_add(glw_t *w, glw_event_map_t *gem);

void glw_event_map_destroy(glw_root_t *gr, glw_event_map_t *gem);

int glw_event_map_intercept(glw_t *w, struct event *e, char early);


glw_event_map_t *glw_event_map_playTrack_create(prop_t *track,
                                                prop_t *source,
                                                int mode);
glw_event_map_t *glw_event_map_external_create(event_t *e);

glw_event_map_t *glw_event_map_internal_create(const char *target,
					       action_type_t event,
					       int uc);

glw_event_map_t *glw_event_map_deliverEvent_create(prop_t *target,
                                                   event_t *event);

glw_event_map_t *glw_event_map_propref_create(prop_t *prop, prop_t *target);

int glw_event_glw_action(glw_t *w, const char *action);
