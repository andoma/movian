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
#pragma once

#include "config.h"

#define PQ_PAUSED 0x1
#define PQ_NO_SKIP 0x2

#if ENABLE_PLAYQUEUE

struct event;
struct backend;
struct prop;

void playqueue_play(const char *url, struct prop *meta, int paused);

void playqueue_event_handler(struct event *e);

void playqueue_load_with_source(struct prop *track,
                                struct prop *source, int flags);


int playqueue_open(struct prop *page);

void playqueue_fini(void);

#else

#define playqueue_play(url, meta, paused)

#define playqueue_event_handler(event)

#define playqueue_load_with_source(track, source, flags)

#define playqueue_open(page)

#define playqueue_fini()

#endif
