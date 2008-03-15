/*
 *  Playlist
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

#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include "playlist.h"
#include <layout/layout.h>
#include <layout/layout_forms.h>

TAILQ_HEAD(playlist_entry_queue, playlist_entry);

static glw_t *playlists_list;


typedef struct playlist {

  TAILQ_ENTRY(playlist) pl_link;

  struct playlist_entry_queue pl_entries;
  int pl_nentries;

} playlist_t;



typedef struct playlist_entry {

  TAILQ_ENTRY(playlist_entry) ple_link;


} playlist_entry_t;



/**
 * Add a playlist
 */ 
static int
playlist_widget_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  return 0;
}


/**
 * Add a playlist
 */ 
static void 
playlist_add(const char *title)
{
  glw_t *e, *w;

  playlist_t *pl = calloc(1, sizeof(playlist_t));

  TAILQ_INIT(&pl->pl_entries);
  

  e = glw_create(GLW_MODEL,
		 GLW_ATTRIB_PARENT, playlists_list,
		 GLW_ATTRIB_FILENAME, "playlist/playlist",
		 GLW_ATTRIB_SIGNAL_HANDLER, playlist_widget_callback, pl, 400,
		 NULL);

  w = glw_find_by_id(e, "title", 0);
  if(w != NULL)
    glw_set(w, GLW_ATTRIB_CAPTION, title, NULL);

}






static void *
playlist_thread(void *aux)
{
  appi_t *ai;
  glw_t *mini;
  struct layout_form_entry_list lfelist;
  
  TAILQ_INIT(&lfelist);

  ai = appi_create("Playlist");

  ai->ai_widget = glw_create(GLW_MODEL,
			     GLW_ATTRIB_FILENAME, "playlist/root",
			     NULL);
  mini = glw_create(GLW_MODEL,
		    GLW_ATTRIB_FILENAME, "playlist/switcher-icon",
		    NULL);

  playlists_list = glw_find_by_id(ai->ai_widget, "playlists", 0);
  

  playlist_add("alpha");
  playlist_add("beta");
  playlist_add("gamma");

  layout_switcher_appi_add(ai, mini);

  LFE_ADD(&lfelist, "playlists");
  LFE_ADD(&lfelist, "delete_playlist");

  layout_form_initialize(&lfelist, ai->ai_widget, &ai->ai_gfs, &ai->ai_ic);

  while(1) {
    sleep(1);
  }

}

void
playlist_init(void)
{
  pthread_t ptid;

  pthread_create(&ptid, NULL, playlist_thread, NULL);
}
