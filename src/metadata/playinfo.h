/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2014 Lonelycoder AB
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

#pragma once

struct prop;

#define PLAYINFO_AUDIO_PLAY_THRESHOLD (10 * 1000000)

void playinfo_register_play(const char *url, int inc);

void playinfo_set_restartpos(const char *url, int64_t pos_ms,
			     int unimportant);

int64_t playinfo_get_restartpos(const char *url);

void playinfo_bind_url_to_prop(const char *url, struct prop *parent);

void playinfo_mark_urls_as(const char **urls, int num_urls, int seen);

