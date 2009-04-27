/*
 *  Subtitling
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

#ifndef SUBTITLES_H_
#define SUBTITLES_H_


typedef enum {
  SUBTITLE_FORMAT_UNKNOWN,
  SUBTITLE_FORMAT_SRT,
  SUBTITLE_FORMAT_SUB,
} subtitle_format_t;

typedef struct subtitle_entry {
  const char *se_text;
  int64_t se_start;
  int64_t se_stop;
  TAILQ_ENTRY(subtitle_entry) se_link;
} subtitle_entry_t;


typedef struct subtitles {
  TAILQ_HEAD(, subtitle_entry) sub_entries;
  subtitle_entry_t **sub_vec;
  int sub_nentries;
  int sub_hint;

} subtitles_t;


void subtitles_free(subtitles_t *sub);

subtitles_t *subtitles_load(const char *filename);

int subtitles_index_by_pts(subtitles_t *sub, int64_t pts);

//glw_t *subtitles_make_widget(subtitles_t *sub, int index);

subtitle_format_t subtitle_probe_file(const char *filename);

#endif /* SUBTITLES_H_ */
