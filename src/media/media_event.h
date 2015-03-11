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
void media_eventsink(void *opaque, event_t *e);

void mp_seek_by_propchange(void *opaque, prop_event_t event, ...);

void mp_enqueue_event(struct media_pipe *mp, struct event *e);

void mp_enqueue_event_locked(struct media_pipe *mp, event_t *e);

struct event *mp_dequeue_event(struct media_pipe *mp);

struct event *mp_dequeue_event_deadline(struct media_pipe *mp, int timeout);

struct event *mp_wait_for_empty_queues(struct media_pipe *mp);

void mp_event_dispatch(struct media_pipe *mp, struct event *e);

void mp_event_set_callback(struct media_pipe *mp,
                           int (*mp_callback)(struct media_pipe *mp,
                                              void *opaque,
                                              event_t *e),
                           void *opaque);

