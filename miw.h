/*
 *  Media info widgets
 *  Copyright (C) 2007 Andreas Öman
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

#ifndef MIW_H
#define MIW_H

#include "media.h"

glw_t *miw_create(glw_t *parent, const char *title, const char *album,
		  const char *author, int duration, int track,
		  const char *image);

glw_t *miw_playstatus_create(glw_t *parent, media_pipe_t *mp);

glw_t *miw_audiotime_create(glw_t *parent, media_pipe_t *mp, float weight,
			    glw_alignment_t align);

void miw_create_buffer_meta(media_pipe_t *mp, glw_t *parent, int video);

glw_t *miw_loading(glw_t *parent, const char *what);

glw_t *meta_container(glw_t *p, float weight);

glw_t *miw_mastervol_create(glw_t *parent);

void miw_add_queue(glw_t *y, media_queue_t *mq, const char *icon);

#endif /* MIW_H */
