/*
 *  Video playback
 *  Copyright (C) 2009 Andreas Öman
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

#ifndef VIDEO_PLAYBACK_H
#define VIDEO_PLAYBACK_H

#include "media.h"

struct rstr;
struct video_queue;
struct vsource_list;

void video_playback_create(media_pipe_t *mp);

void video_playback_destroy(media_pipe_t *mp);

struct rstr *video_queue_find_next(struct video_queue *vq, 
				   const char *url, int reverse,
				   int wrap);

void vsource_add_hls(struct vsource_list *vsl, char *hlslist, const char *url);

#endif /* PLAY_VIDEO_H */
