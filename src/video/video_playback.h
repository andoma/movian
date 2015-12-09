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

struct media_pipe;
struct rstr;
struct video_queue;
struct vsource_list;
struct prop;
struct video_args;
struct htsmsg;

#include "compiler.h"
#include "arch/atomic.h"
#include "misc/queue.h"

void video_playback_create(struct media_pipe *mp);

void video_playback_destroy(struct media_pipe *mp);

struct prop *video_queue_find_next(struct video_queue *vq,
				   struct prop *current, int reverse,
				   int wrap);

/**
 * Video playback info
 */

struct htsmsg *video_playback_info_create(const struct video_args *va);


typedef enum {
  VPI_START,
  VPI_STOP,
} vpi_op_t;

typedef struct video_playback_info_handler {
  void (*invoke)(vpi_op_t op, struct htsmsg *info, struct prop *mp_root);
  LIST_ENTRY(video_playback_info_handler) link;
} video_playback_info_handler_t;

void register_video_playback_info_handler(video_playback_info_handler_t *vpih);

void video_playback_info_invoke(vpi_op_t op, struct htsmsg *vpi, struct prop *p);

#define VPI_REGISTER(handler) \
  static video_playback_info_handler_t handler ## _strct = { handler}; \
  INITIALIZER(handler ## _init) {                               \
    register_video_playback_info_handler(&handler ## _strct); }


