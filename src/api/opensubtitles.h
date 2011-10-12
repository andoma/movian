/*
 *  Open subtitles interface
 *  Copyright (C) 2010 Andreas Öman
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

#ifndef OPENSUBTITLES_H__
#define OPENSUBTITLES_H__

#include "htsmsg/htsmsg.h"

struct prop;
struct fa_handle;
void opensub_init(void);

int opensub_compute_hash(struct fa_handle *fh, uint64_t *hashp);

htsmsg_t *opensub_build_query(const char *lang, int64_t hash, int64_t movsize,
			      const char *imdb, const char *title);

void opensub_add_subtitles(struct prop *node, htsmsg_t *query);

#endif // OPENSUBTITLES_H__

