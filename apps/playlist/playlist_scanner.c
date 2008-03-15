/*
 *  Code for recursive scan of directores to populate playlist
 *  Copyright (C) 2007-2008 Andreas Öman
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

#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "input.h"
#include "playlist.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_probe.h"


typedef struct playlist_scanner {
  char *ps_url;

  playlist_t *ps_pl;

  char *ps_image_url;
  int   ps_image_size;  /* the bigger, the better */

  int   ps_started;

} playlist_scanner_t;

/**
 *
 */
static void
playlist_scandir_dofile(playlist_scanner_t *ps, const char *url)
{
  playlist_entry_t *ple;
  struct filetag_list ftags;
  int64_t i64;
  int size;

  TAILQ_INIT(&ftags);
  
  if(fa_probe(&ftags, url) == -1)
    return; /* Can not identify file */

  if(filetag_get_int(&ftags, FTAG_FILETYPE, &i64) < 0) {
    filetag_freelist(&ftags);
    return;
  }
  
  switch(i64) {
  case FILETYPE_AUDIO:
    ple = playlist_enqueue0(ps->ps_pl, url, &ftags);

    if(ps->ps_started == 0 && ple != NULL) {
      ps->ps_started = 1;
      playlist_signal(ple, PLAYLIST_INPUTEVENT_PLAYENTRY);
    }
    return;

  case FILETYPE_IMAGE:
    size = fileaccess_size(url);
    if(size > ps->ps_image_size) {
      free(ps->ps_image_url);
      ps->ps_image_url = strdup(url);
    }
    break;
  }

  filetag_freelist(&ftags);
}


/**
 * Callback from scandir
 */
static void
playlist_scandir_callback(void *arg, const char *url, const char *filename,
			  int type)
{
  playlist_scanner_t *ps = arg;
  
  switch(type) {
  case FA_FILE:
    playlist_scandir_dofile(ps, url);
    break;

  case FA_DIR:
    fileaccess_scandir(url, playlist_scandir_callback, ps);
    break;
  }
}

 /**
 * Directory scanner thread
 */
static void *
playlist_scanner_thread(void *arg)
{
  playlist_scanner_t *ps = arg;
  const char *name;

  /**
   * Use last component of url as name for playlist
   */
  name = strrchr(ps->ps_url, '/');
  name = name ? name + 1 : ps->ps_url;
  ps->ps_pl = playlist_create(name, 1);

  fileaccess_scandir(ps->ps_url, playlist_scandir_callback, ps);

  if(ps->ps_image_url != NULL)
    playlist_set_backdrop(ps->ps_pl, ps->ps_image_url);

  playlist_save(ps->ps_pl);

  free(ps->ps_url);
  free(ps->ps_image_url);
  free(ps);

  return NULL;
}



/**
 * Based on the supplied URL we will construct a playlist with all
 * its contents.
 *
 * Note: Url should point to a directory or not much will happen
 *
 * Scanning will take place in a separate thread, this function will
 * return immediately
 */
void
playlist_build_from_dir(const char *url)
{
  playlist_scanner_t *ps;
  pthread_t ptid;
  pthread_attr_t attr;

  ps = calloc(1, sizeof(playlist_scanner_t));
  ps->ps_url = strdup(url);

  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  pthread_create(&ptid, &attr, playlist_scanner_thread, ps);
}
