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

#include "config.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include "showtime.h"
#include "fa_tags.h"

const char *filetag_tagnames[] = {
  [FTAG_FILETYPE]       = "Filetype",
  [FTAG_TITLE]          = "Title",
  [FTAG_AUTHOR]         = "Author",
  [FTAG_ALBUM]          = "Album",
  [FTAG_ICON]           = "Icon",
  [FTAG_ORIGINAL_DATE]  = "Original date",
  [FTAG_TRACK]          = "Track",
  [FTAG_NTRACKS]        = "Tracks",
  [FTAG_DURATION]       = "Duration",
  [FTAG_AUDIOINFO]      = "Audioinfo",
  [FTAG_VIDEOINFO]      = "Videoinfo",
  [FTAG_MEDIAFORMAT]    = "Mediaformat",
  [FTAG_FILESIZE]       = "Filesize"
};


/**
 *
 */
void 
filetag_dumplist(struct filetag_list *list)
{
  filetag_t *ft;

  TAILQ_FOREACH(ft, list, ftag_link) {
    printf("%20s: ",
	   filetag_tagnames[ft->ftag_tag]);
    if(ft->ftag_string)
      printf("%s\n", ft->ftag_string);
    else
      printf("%lld\n", ft->ftag_int);

  }
}




/**
 *
 */
void 
filetag_freelist(struct filetag_list *list)
{
  filetag_t *ft;

  while((ft = TAILQ_FIRST(list)) != NULL) {
    TAILQ_REMOVE(list, ft, ftag_link);
    free((void *)ft->ftag_string);
    free(ft);
  }
}

/**
 *
 */
filetag_t *
filetag_find(struct filetag_list *list, ftag_t tag, int create)
{
  filetag_t *ft;

  TAILQ_FOREACH(ft, list, ftag_link)
    if(ft->ftag_tag == tag)
      return ft;
  if(!create)
    return NULL;
  ft = calloc(1, sizeof(filetag_t));
  ft->ftag_tag   = tag;
  TAILQ_INSERT_TAIL(list, ft, ftag_link);
  return ft;
}


/**
 *
 */
void 
filetag_set_str(struct filetag_list *list, ftag_t tag, const char *value)
{
  filetag_t *ft = filetag_find(list, tag, 1);
  free((void *)ft->ftag_string);
  ft->ftag_string = strdup(value);
}


/**
 *
 */
void
filetag_set_int(struct filetag_list *list, ftag_t tag, int64_t value)
{
  filetag_t *ft = filetag_find(list, tag, 1);
  ft->ftag_int = value;
}


/**
 *
 */
int
filetag_get_str(struct filetag_list *list, ftag_t tag, const char **valuep)
{
  filetag_t *ft = filetag_find(list, tag, 0);
  if(ft == NULL)
    return -1;
  *valuep = ft->ftag_string;
  return 0;
}


/**
 *
 */
const char *
filetag_get_str2(struct filetag_list *list, ftag_t tag)
{
  filetag_t *ft = filetag_find(list, tag, 0);
  return ft ? ft->ftag_string : NULL;
}


/**
 *
 */
int
filetag_get_int(struct filetag_list *list, ftag_t tag, int64_t *valuep)
{
  filetag_t *ft = filetag_find(list, tag, 0);
  if(ft == NULL)
    return -1;
  *valuep = ft->ftag_int;
  return 0;
}

/**
 *
 */
void 
filetag_movelist(struct filetag_list *dst, struct filetag_list *src)
{
  filetag_t *ft;

  TAILQ_INIT(dst);

  while((ft = TAILQ_FIRST(src)) != NULL) {
    TAILQ_REMOVE(src, ft, ftag_link);
    TAILQ_INSERT_TAIL(dst, ft, ftag_link);
  }
}

/**
 *
 */
void 
filetag_copylist(struct filetag_list *dst, struct filetag_list *src)
{
  filetag_t *ft, *n;

  TAILQ_INIT(dst);

  TAILQ_FOREACH(ft, src, ftag_link) {
    n = calloc(1, sizeof(filetag_t));
    n->ftag_tag    = ft->ftag_tag;
    n->ftag_int    = ft->ftag_int;
    n->ftag_string = ft->ftag_string ? strdup(ft->ftag_string) : NULL;
    TAILQ_INSERT_TAIL(dst, ft, ftag_link);
  }
}
