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

#include <assert.h>
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
#include "fileaccess/fa_probe.h"
#include <layout/layout.h>
#include <layout/layout_forms.h>
#include <layout/layout_support.h>

static playlist_player_t plp;

static glw_t *playlists_list;

/**
 * Global lock for reference counters and playlist/playlistentry relations
 */
pthread_mutex_t playlistlock = PTHREAD_MUTEX_INITIALIZER;


#define PL_EVENT_DELETE_PLAYLIST   2
#define PL_EVENT_PLE_CHANGED       3

static void playlist_signal(playlist_entry_t *ple, int type);


/**
 * Playlist widget callback
 */
static int
playlist_widget_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  return 0;
}

/**
 * Playlist widget callback
 */
static int
playlist_entry_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  switch(signal) {
  default:
    break;
  case GLW_SIGNAL_ENTER:
    playlist_signal(opaque, PLAYLIST_INPUTEVENT_PLAYENTRY);
    return 1;
  }
  return 0;
}


/**
 * Add a playlist
 */ 
static playlist_t *
playlist_add(appi_t *ai, const char *title)
{
  glw_t *e, *w;
  struct layout_form_entry_list lfelist;
  playlist_t *pl = calloc(1, sizeof(playlist_t));

  pl->pl_ai = ai;
  TAILQ_INIT(&pl->pl_entries);
  TAILQ_INIT(&pl->pl_shuffle_entries);


  e = glw_create(GLW_MODEL,
		 GLW_ATTRIB_FILENAME, "playlist/playlist",
		 GLW_ATTRIB_SIGNAL_HANDLER, playlist_widget_callback, pl, 400,
		 NULL);

  pl->pl_widget = e;

  w = glw_find_by_id(e, "title", 0);
  if(w != NULL)
    glw_set(w, GLW_ATTRIB_CAPTION, title, NULL);

  w = layout_form_add_tab2(ai->ai_widget, "playlists", e,
			   "track_list_container", "playlist/tracklist");

  TAILQ_INIT(&lfelist);
  LFE_ADD_MONITOR_CHILD(&lfelist, "track_list", PL_EVENT_PLE_CHANGED);
  layout_form_initialize(&lfelist, w, &ai->ai_gfs, &ai->ai_ic, 0);

  pl->pl_list = glw_find_by_id(w, "track_list", 0);
  return pl;
}

/**
 * Returns the current playlist
 */
static playlist_t *
playlist_get_current(void)
{
  glw_t *w;
  playlist_t *pl = NULL;

  glw_lock();

  w = playlists_list;
  if(w != NULL) {
    w = w->glw_selected;
    if(w != NULL) {
      pl = glw_get_opaque(w,  playlist_widget_callback);
    }
  }
  glw_unlock();
  return pl;
}


/**
 * Playlist enqueue
 *
 * Add URL to the current playlist.
 *
 * If 'ftags' != NULL, the tags will be used, otherwise, the playlist
 * will probe by itself
 *
 * (NOTE: ftags are not copied, this must be done by the caller)
 */
void
playlist_enqueue(const char *url, struct filetag_list *ftags, int playit)
{
  playlist_entry_t *ple, *ple2;
  playlist_t *pl = playlist_get_current();
  struct filetag_list ftags0;
  int64_t i64;
  glw_t *w;
  const char *s;

  if(pl == NULL) {
    if(ftags != NULL)
      filetag_freelist(ftags);
    return;
  }

  if(ftags == NULL) {
    TAILQ_INIT(&ftags0);
    if(fa_probe(&ftags0, url) == -1)
      return;
    ftags = &ftags0;
  }

  filetag_dumplist(ftags);

  if(filetag_get_int(ftags, FTAG_FILETYPE, &i64) < 0) {
    filetag_freelist(ftags);
    return;
  }

  if(i64 != FILETYPE_AUDIO) {
    /* We only accept audio */
    filetag_freelist(ftags);
    return;
  }

  ple = calloc(1, sizeof(playlist_entry_t));

  ple->ple_url = strdup(url);

  if(filetag_get_int(ftags, FTAG_DURATION, &i64) == 0)
    ple->ple_duration = i64;

  ple->ple_pl = pl;

  pthread_mutex_lock(&playlistlock);

  ple2 = TAILQ_LAST(&pl->pl_entries, playlist_entry_queue);
  if(ple2 != NULL) {
    ple->ple_time_offset = ple2->ple_time_offset + ple2->ple_duration;
    ple->ple_track       = ple2->ple_track       + 1;
  } else {
    ple->ple_track = 1;
  }

  TAILQ_INSERT_TAIL(&pl->pl_entries, ple, ple_link);
  TAILQ_INSERT_TAIL(&pl->pl_shuffle_entries, ple, ple_shuffle_link);
  pl->pl_nentries++;
  pl->pl_total_time += ple->ple_duration;

  pthread_mutex_unlock(&playlistlock);

  ple->ple_refcnt++; /* playlist linkage */


  /**
   * Create playlist entry model in tracklist
   */
  w = glw_create(GLW_MODEL,
		 GLW_ATTRIB_PARENT, pl->pl_list,
		 GLW_ATTRIB_SIGNAL_HANDLER, playlist_entry_callback, ple, 400,
		 GLW_ATTRIB_FILENAME, "playlist/track",
		 NULL);
  ple->ple_widget = w;

  if(filetag_get_str(ftags, FTAG_TITLE, &s) == 0) {
    layout_update_str(w, "track_title", s);
  } else {
    s = strrchr(url, '/');
    s = s ? s + 1 : url;
    layout_update_str(w, "track_title", s);
  }
  

  /**
   * Update playlist widget
   */
  layout_update_time(pl->pl_widget, "time_total",  pl->pl_total_time);
  layout_update_int(pl->pl_widget,  "track_total", pl->pl_nentries);


  /**
   * Inform player that a new entry has been enqueued
   *
   * If it is idle, it will start playing it directly
   */
  playlist_signal(ple, playit ? PLAYLIST_INPUTEVENT_PLAYENTRY : 
		  PLAYLIST_INPUTEVENT_NEWENTRY);
}

/**
 *
 */
static void
playlist_entry_free(playlist_entry_t *ple)
{
  playlist_t *pl = ple->ple_pl;
  free(ple->ple_url);

  if(pl != NULL) {
    TAILQ_REMOVE(&pl->pl_entries, ple, ple_link);
    TAILQ_REMOVE(&pl->pl_shuffle_entries, ple, ple_shuffle_link);
  }

 free(ple);
}


/**
 * Send signal to playlist player
 */ 
static void
playlist_signal(playlist_entry_t *ple, int type)
{
  inputevent_t ie;

  pthread_mutex_lock(&playlistlock);
  ple->ple_refcnt++;
  pthread_mutex_unlock(&playlistlock);

  ie.type = type;
  ie.u.ptr = ple;
  ie.freefunc = (void *)playlist_entry_unref;
  input_postevent(&plp.plp_ic, &ie);
}


/**
 * Get next (or previous) entry, optionally in shuffle mode
 */
playlist_entry_t *
playlist_advance(playlist_entry_t *ple, int prev)
{
  playlist_entry_t *n;
  int shuffle = 0;

  pthread_mutex_lock(&playlistlock);

  if(prev) {
    if(shuffle)
      n = TAILQ_PREV(ple, playlist_entry_queue, ple_shuffle_link);
    else
      n = TAILQ_PREV(ple, playlist_entry_queue, ple_link);
  } else {
    if(shuffle)
      n = TAILQ_NEXT(ple, ple_shuffle_link);
    else
      n = TAILQ_NEXT(ple, ple_link);
  }

  if(n != NULL)
    n->ple_refcnt++;
      
  pthread_mutex_unlock(&playlistlock);

  return n;
}


/**
 * Decrease refcount and free if reaching zero
 */
void
playlist_entry_unref(playlist_entry_t *ple)
{
  pthread_mutex_lock(&playlistlock);

  assert(ple->ple_refcnt > 0);
  ple->ple_refcnt--;
  if(ple->ple_refcnt == 0)
    playlist_entry_free(ple);

  pthread_mutex_unlock(&playlistlock);

}


/**
 *
 */
static int
playlist_root_widget(glw_t *w, void *opaque, glw_signal_t sig, ...)
{
  appi_t *ai = opaque;
  inputevent_t *ie;

  va_list ap;
  va_start(ap, sig);
  
  switch(sig) {
  default:
    break;

  case GLW_SIGNAL_INPUT_EVENT:
    ie = va_arg(ap, void *);
    input_postevent(&ai->ai_ic, ie);
    return 1;
  }
  va_end(ap);
  return 0;
}


/**
 * User interface playlist thread
 */
static void *
playlist_thread(void *aux)
{
  appi_t *ai;
  glw_t *mini;
  struct layout_form_entry_list lfelist;
  inputevent_t ie;
  playlist_t *pl;
  pthread_t playerthread;


  ai = appi_create("Playlist");

  ai->ai_widget =
    glw_create(GLW_MODEL,
	       GLW_ATTRIB_FILENAME, "playlist/root",
	       GLW_ATTRIB_SIGNAL_HANDLER, playlist_root_widget, ai, 500,
	       NULL);

  mini = glw_create(GLW_MODEL,
		    GLW_ATTRIB_FILENAME, "playlist/switcher-icon",
		    NULL);

  playlists_list = glw_find_by_id(ai->ai_widget, "playlists", 0);


  /**
   * Initialize and launch player thread
   */
  memset(&plp, 0, sizeof(plp));
  input_init(&plp.plp_ic);
  plp.plp_mp = &ai->ai_mp;
  pthread_create(&playerthread, NULL, playlist_player, &plp);


  /**
   *
   */
  pl = playlist_add(ai, "Default playlist");

  playlist_add(ai, "beta");
  playlist_add(ai, "gamma");

  layout_switcher_appi_add(ai, mini);
  
  /**
   *
   */
  TAILQ_INIT(&lfelist);
  LFE_ADD(&lfelist, "playlists");
  LFE_ADD_BTN(&lfelist, "delete_playlist", PL_EVENT_DELETE_PLAYLIST);
  layout_form_initialize(&lfelist, ai->ai_widget, &ai->ai_gfs, &ai->ai_ic, 1);

  
  while(1) {
    input_getevent(&ai->ai_ic, 1, &ie, NULL);
    
    switch(ie.type) {
    default:
      break;

    case INPUT_KEY:
      switch(ie.u.key) {
      default:
	break;

      case INPUT_KEY_SEEK_FAST_BACKWARD:
      case INPUT_KEY_SEEK_BACKWARD:
      case INPUT_KEY_SEEK_FAST_FORWARD:
      case INPUT_KEY_SEEK_FORWARD:
      case INPUT_KEY_PLAYPAUSE:
      case INPUT_KEY_PLAY:
      case INPUT_KEY_PAUSE:
      case INPUT_KEY_STOP:
      case INPUT_KEY_PREV:
      case INPUT_KEY_NEXT:
      case INPUT_KEY_RESTART_TRACK:
	input_postevent(&plp.plp_ic, &ie);
	break;
      }

    case INPUT_U32:
      
      switch(ie.u.u32) {
      case PL_EVENT_DELETE_PLAYLIST:
	printf("Delete playlist\n");
	break;
      }
    }
  }
}

void
playlist_init(void)
{
  pthread_t ptid;

  pthread_create(&ptid, NULL, playlist_thread, NULL);
}
