/*
 *  TV playback
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

#ifndef TV_PLAYBACK_H
#define TV_PLAYBACK_H

void tv_playback_init(tv_channel_t *ch);

void tv_playback_deinit(tv_channel_t *ch);

void tv_playback_clean(tv_channel_t *ch);

tv_channel_t *tv_channel_by_tag(tv_t *tv, uint32_t tag);

void tv_channel_stream_destroy(tv_channel_t *ch, tv_channel_stream_t *tcs,
			       int lock);

#endif /* TV_PLAYBACK_H */
