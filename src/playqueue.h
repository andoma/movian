/*
 *  Playqueue
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

#ifndef PLAYQUEUE_H__
#define PLAYQUEUE_H__

#include "navigator.h"
struct event;

void playqueue_play(const char *url, prop_t *meta, int paused);

void playqueue_event_handler(struct event *e);

#define PQ_PAUSED 0x1
#define PQ_NO_SKIP 0x2

void playqueue_load_with_source(prop_t *track, prop_t *source, int flags);

struct backend;

int playqueue_open(prop_t *page);

#endif /* PLAYQUEUE_H__ */
