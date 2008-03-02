/*
 *  Functions for probing file contents
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

#ifndef FILEPROBE_H
#define FILEPROBE_H

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
  FTAG_STREAMINFO,
  FTAG_FILESIZE,
} ftag_t;


typedef enum {
  FILETYPE_PLAYLIST_PLS,
  FILETYPE_IMAGE,
  FILETYPE_ISO,
  FILETYPE_VIDEO,
  FILETYPE_AUDIO,
} ftag_filetype_t;



typedef struct filetag {
  TAILQ_ENTRY(filetag) ftag_link;

  ftag_t ftag_tag;

  int ftag_index;  /* In case of multple instances of the same type,
		      this should used as an index, otherwise 0 */


  const char *ftag_string;
  int64_t ftag_int;

} filetag_t;

filetag_t *filetag_find(struct filetag_list *list, ftag_t tag, int index,
			int create);

void filetag_freelist(struct filetag_list *list);

void filetag_set_str(struct filetag_list *list, ftag_t tag,
		     int index, const char *value);

void filetag_set_int(struct filetag_list *list, ftag_t tag,
		     int index, int64_t value);

int filetag_probe(struct filetag_list *list, const char *filename);

void filetag_dumplist(struct filetag_list *list);

int filetag_get_str(struct filetag_list *list, ftag_t tag,
		    int index, const char **valuep);

int filetag_get_int(struct filetag_list *list, ftag_t tag,
		     int index, int64_t *valuep);

#endif /* FILEPROBE_H */
