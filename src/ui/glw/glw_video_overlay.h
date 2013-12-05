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

#ifndef GLW_VIDEO_OVERLAY_H
#define GLW_VIDEO_OVERLAY_H

void glw_video_overlay_deinit(glw_video_t *gv);

void glw_video_overlay_layout(glw_video_t *gv, glw_rctx_t *rc,
			      glw_rctx_t *vrc);

void glw_video_overlay_render(glw_video_t *gv, const glw_rctx_t *rc, 
			      const glw_rctx_t *vrc);

int glw_video_overlay_pointer_event(video_decoder_t *vd, int width, int height,
				    glw_pointer_event_t *gpe, media_pipe_t *mp);

void glw_video_overlay_set_pts(glw_video_t *gv, int64_t pts);

#endif // GLW_VIDEO_OVERLAY_H
