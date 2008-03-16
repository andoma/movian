/*
 *  Functions for tagging file contents
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

#ifndef FA_TAGS_H
#define FA_TAGS_H

TAILQ_HEAD(filetag_list, filetag);

typedef enum {
  FTAG_FILETYPE,
  FTAG_TITLE,
  FTAG_AUTHOR,
  FTAG_ALBUM,
  FTAG_ICON,
  FTAG_ORIGINAL_DATE,
  FTAG_TRACK,
  FTAG_NTRACKS,
  FTAG_DURATION,
  FTAG_FILESIZE,
  FTAG_AUDIOINFO,
  FTAG_VIDEOINFO,
  FTAG_MEDIAFORMAT,
} ftag_t;


typedef enum {
  FILETYPE_PLAYLIST_PLS,
  FILETYPE_IMAGE,
  FILETYPE_ISO,
  FILETYPE_VIDEO,
  FILETYPE_AUDIO,
  FILETYPE_NUMTAGS,
} ftag_filetype_t;



typedef struct filetag {
  TAILQ_ENTRY(filetag) ftag_link;

  ftag_t ftag_tag;
  const char *ftag_string;
  int64_t ftag_int;

} filetag_t;

filetag_t *filetag_find(struct filetag_list *list, ftag_t tag, int create);

void filetag_freelist(struct filetag_list *list);

void filetag_set_str(struct filetag_list *list, ftag_t tag,
		     const char *value);

void filetag_set_int(struct filetag_list *list, ftag_t tag,
		     int64_t value);

void filetag_dumplist(struct filetag_list *list);

int filetag_get_str(struct filetag_list *list, ftag_t tag,
		    const char **valuep);

const char *filetag_get_str2(struct filetag_list *list, ftag_t tag);

int filetag_get_int(struct filetag_list *list, ftag_t tag,
		    int64_t *valuep);

void filetag_movelist(struct filetag_list *dst, struct filetag_list *src);

void filetag_copylist(struct filetag_list *dst, struct filetag_list *src);

#endif /* FA_TAGS_H */
