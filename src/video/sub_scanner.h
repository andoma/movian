/*
 *  Scanning of external subtitles
 *  Copyright (C) 2013 Andreas Öman
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

typedef struct sub_scanner sub_scanner_t;

sub_scanner_t *sub_scanner_create(const char *url, int beflags, rstr_t *title,
				  prop_t *proproot, int opensub_hash_valid,
				  uint64_t opensub_hash, uint64_t fsize,
				  rstr_t *imdbid, int season, int episode);

void sub_scanner_destroy(sub_scanner_t *ss); 

