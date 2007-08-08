/*
 *  Functions for probing file contents
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

#ifndef MEDIAPROBE_H
#define MEDIAPROBE_H

typedef struct mediainfo {
  enum {
    MI_AUDIO,
    MI_VIDEO,
    MI_IMAGE,
    MI_PLAYLIST_PLS,
    MI_ISO,
  } mi_type;

  const char *mi_title;
  const char *mi_author;
  const char *mi_album;
  int mi_track;
  int mi_duration;

} mediainfo_t;


int mediaprobe(const char *filename, mediainfo_t *mi, int fast);

void mediaprobe_free(mediainfo_t *mi);

void mediaprobe_dup(mediainfo_t *dst, mediainfo_t *src);

#endif /* MEDIAPROBE_H */
