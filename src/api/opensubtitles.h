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
#pragma once

#include "htsmsg/htsmsg.h"
#include "arch/threads.h"

struct prop;
struct fa_handle;

int opensub_compute_hash(struct fa_handle *fh, uint64_t *hashp);

void opensub_query(struct prop *p, hts_mutex_t *mtx, uint64_t hash,
		   uint64_t size, const char *title, const char *imdb,
		   int season, int episode);


