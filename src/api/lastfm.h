/*
 *  LastFM API
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

#ifndef LASTFM_H__
#define LASTFM_H__

#include "misc/rstr.h"

struct prop;

void lastfm_artistpics_init(struct prop *prop, rstr_t *artist);

void lastfm_albumart_init(struct prop *prop, rstr_t *artist, rstr_t *album);

void lastfm_init(void);

#endif /* LASTFM_H__ */
