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

#ifndef FA_VIDEO_H
#define FA_VIDEO_H

#include "media.h"

struct video_queue;
struct fa_handle;
struct vsource_list;
struct rstr;
struct video_args;

event_t *be_file_playvideo(const char *url, media_pipe_t *mp,
			   char *errbuf, size_t errlen,
			   struct video_queue *vq, struct vsource_list *vsl,
			   const struct video_args *va);

event_t *be_file_playvideo_fh(const char *url, media_pipe_t *mp,
                              char *errbuf, size_t errlen,
                              struct video_queue *vq,
                              struct fa_handle *fh,
			      const struct video_args *va);

#endif /* FA_VIDEO_H */
