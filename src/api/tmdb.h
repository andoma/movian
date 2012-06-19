/*
 *  API interface to themoviedb.org
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

int64_t tmdb_query_by_title_and_year(void *db, const char *item_url, 
				     const char *title, int year,
				     int duration);

int64_t tmdb_query_by_imdb_id(void *db, const char *item_url, 
			      const char *imdb_id,
			      int duration);

void tmdb_init(void);

