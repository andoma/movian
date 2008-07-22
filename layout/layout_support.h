/*
 *  Layout support functions
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

#ifndef LAYOUT_SUPPORT_H
#define LAYOUT_SUPPORT_H

#if 0

struct AVCodecContext;

void layout_update_codec_info(glw_t *w, const char *id, 
			      struct AVCodecContext *ctx);

void layout_update_time(glw_t *w, const char *id, int s);

void layout_update_int(glw_t *w, const char *id, int v);

void layout_update_bar(glw_t *w, const char *id, float v);

void layout_update_str(glw_t *w, const char *id, const char *str);

void layout_update_model(glw_t *w, const char *id, const char *model);

void layout_update_multilinetext(glw_t *w, const char *id, const char *txt,
				 int total_lines, glw_alignment_t alignment);

void layout_update_source(glw_t *w, const char *id, const char *filename);

#endif

#endif /* LAYOUT_SUPPORT_H */

