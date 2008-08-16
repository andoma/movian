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
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include "playlist.h"
#include "event.h"
#include <fileaccess/fa_probe.h>
#include <fileaccess/fileaccess.h>
#include <layout/layout.h>

static struct playlist_list playlists;

static playlist_player_t plp;

static glw_t *playlists_list;

static glw_t *playlist_root;

static appi_t *playlist_appi;

/**
 * Global lock for reference counters and playlist/playlistentry relations
 */
hts_mutex_t playlistlock;


static void playlist_entry_free(playlist_entry_t *ple);
static void playlist_unlink(playlist_t *pl);
static void playlist_destroy(playlist_t *pl);


/**
 * Playlist widget callback
 */
static int
pl_widget_callback(glw_t *w, void *opaque, glw_signal_t signal, 
		   void *extra)
{
  playlist_t *pl = opaque;
  playlist_entry_t *ple;
  glw_event_t *ge = extra;

  if(signal != GLW_SIGNAL_EVENT)
    return 0;

  switch(ge->ge_type) {
  default:
    break;
  case GEV_ENTER:
    pl = opaque;
    ple = TAILQ_FIRST(&pl->pl_entries);
    if(ple == NULL)
      break;
    playlist_signal(ple, PLAYLIST_EVENT_PLAYENTRY);
    return 1;
  }
  return 0;
}

/**
 * Playlist entry widget callback
 */
static int
ple_widget_callback(glw_t *w, void *opaque, glw_signal_t signal,
			void *extra)
{
  glw_event_t *ge = extra;
  if(signal != GLW_SIGNAL_EVENT)
    return 0;

  switch(ge->ge_type) {
  default:
    break;
  case GEV_ENTER:
    playlist_signal(opaque, PLAYLIST_EVENT_PLAYENTRY);
    return 1;
  }
  return 0;
}


/**
 * Add a playlist
 */ 
playlist_t *
playlist_create(const char *title, int truncate)
{
  glw_prop_t *p;
  playlist_t *pl;

  hts_mutex_lock(&playlistlock);

  LIST_FOREACH(pl, &playlists, pl_link) {
    if(!strcmp(title, pl->pl_title)) {
      hts_mutex_unlock(&playlistlock);

      if(truncate) {
	playlist_destroy(pl);
	break;
      }
      return pl;
    }
  }

  pl = calloc(1, sizeof(playlist_t));
  LIST_INSERT_HEAD(&playlists, pl, pl_link);

  pl->pl_title = strdup(title);
  
  pl->pl_ai = playlist_appi;
  TAILQ_INIT(&pl->pl_entries);
  TAILQ_INIT(&pl->pl_shuffle_entries);

  pl->pl_prop_root = glw_prop_create(NULL, "playlist", GLW_GP_DIRECTORY);

  pl->pl_prop_title = glw_prop_create(pl->pl_prop_root, "title", GLW_GP_STRING);
  glw_prop_set_string(pl->pl_prop_title, pl->pl_title);

  pl->pl_prop_backdrop = glw_prop_create(pl->pl_prop_root, "backdrop",
					 GLW_GP_STRING);

  glw_prop_set_string(pl->pl_prop_backdrop, "theme://images/sound.png"); // XXX

  pl->pl_prop_next_track_title = glw_prop_create(pl->pl_prop_root, "nexttrack",
						 GLW_GP_STRING);

  p = glw_prop_create(pl->pl_prop_root, "time", GLW_GP_DIRECTORY);
  pl->pl_prop_time_total   = glw_prop_create(p, "total", GLW_GP_TIME);
  pl->pl_prop_time_current = glw_prop_create(p, "current", GLW_GP_TIME);

  p = glw_prop_create(pl->pl_prop_root, "track", GLW_GP_DIRECTORY);
  pl->pl_prop_track_total   = glw_prop_create(p, "total", GLW_GP_FLOAT);
  pl->pl_prop_track_current = glw_prop_create(p, "current", GLW_GP_FLOAT);


  pl->pl_widget = glw_model_create("theme://playlist/playlist.model", NULL, 0,
				   prop_global, pl->pl_prop_root, NULL);
  
  glw_set(pl->pl_widget,
	  GLW_ATTRIB_SIGNAL_HANDLER, pl_widget_callback, pl, 400,
	  NULL);

  pl->pl_tab = glw_model_create("theme://playlist/tracklist.model", NULL, 0,
				   prop_global, pl->pl_prop_root, NULL);


  pl->pl_list = glw_find_by_id(pl->pl_tab, "track_container", 0);


  glw_add_tab(playlist_root,
	      "playlist_container", pl->pl_widget,
	      "tracklist_container", pl->pl_tab);

  hts_mutex_unlock(&playlistlock);
  return pl;
}

/**
 * Set the supplied backdrop on the playlist
 */
void
playlist_set_backdrop(playlist_t *pl, const char *url)
{
  free(pl->pl_backdrop);
  pl->pl_backdrop = strdup(url);

  glw_prop_set_string(pl->pl_prop_backdrop, url);
}

/**
 * Destroy a playlist
 */
static void
playlist_destroy(playlist_t *pl)
{
  playlist_entry_t *ple, *next;

  playlist_unlink(pl);

  hts_mutex_lock(&playlistlock);

  LIST_REMOVE(pl, pl_link);

  /**
   * Decouple all playlist entries from the playlist
   */
  for(ple = TAILQ_FIRST(&pl->pl_entries); ple != NULL; ple = next) {
    next = TAILQ_NEXT(ple, ple_link);

    ple->ple_pl = NULL;
    glw_set(ple->ple_widget, 
	    GLW_ATTRIB_PARENT, NULL,
	    NULL);

    assert(ple->ple_refcnt > 0);
    ple->ple_refcnt--;
    if(ple->ple_refcnt == 0)
      playlist_entry_free(ple);
  }

  hts_mutex_unlock(&playlistlock);

  glw_destroy(pl->pl_widget);
  glw_destroy(pl->pl_tab);

  glw_prop_destroy(pl->pl_prop_root);

  free(pl->pl_backdrop);
  free(pl->pl_title);
  free(pl);
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
      pl = glw_get_opaque(w,  pl_widget_callback);
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
playlist_entry_t *
playlist_enqueue0(playlist_t *pl, const char *url, struct filetag_list *ftags)
{
  playlist_entry_t *ple, *ple2;
  struct filetag_list ftags0;
  int64_t i64;
  glw_prop_t *p;

  if(ftags == NULL) {
    TAILQ_INIT(&ftags0);
    if(fa_probe(&ftags0, url) == -1)
      return NULL;
    ftags = &ftags0;
  }

  if(filetag_get_int(ftags, FTAG_FILETYPE, &i64) < 0) {
    filetag_freelist(ftags);
    return NULL;
  }

  if(i64 != FILETYPE_AUDIO) {
    /* We only accept audio */
    filetag_freelist(ftags);
    return NULL;
  }

  ple = calloc(1, sizeof(playlist_entry_t));

  ple->ple_url = strdup(url);

  if(filetag_get_int(ftags, FTAG_DURATION, &i64) == 0)
    ple->ple_duration = i64;

  ple->ple_pl = pl;

  hts_mutex_lock(&playlistlock);

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

  hts_mutex_unlock(&playlistlock);

  ple->ple_refcnt++; /* playlist linkage */

  /**
   * Create properties
   */
  ple->ple_prop_root = glw_prop_create(NULL, "media", GLW_GP_DIRECTORY);

  ple->ple_prop_playstatus = glw_prop_create(ple->ple_prop_root,
					     "playstatus", GLW_GP_STRING);

  media_fill_properties(ple->ple_prop_root, url, FA_FILE, ftags);

  p = glw_prop_create(ple->ple_prop_root, "time", GLW_GP_DIRECTORY);
  ple->ple_prop_time_current = glw_prop_create(p, "current", GLW_GP_TIME);


  /**
   * Create playlist entry model in tracklist
   */
  ple->ple_widget = glw_model_create("theme://playlist/track.model",
				     pl->pl_list, GLW_MODEL_CACHE,
				     ple->ple_prop_root,
				     pl->pl_prop_root, 
				     prop_global,
				     NULL);

  glw_set(ple->ple_widget,
	  GLW_ATTRIB_SIGNAL_HANDLER, ple_widget_callback, ple, 400,
	  NULL);

  /**
   * Update playlist widget
   */

  glw_prop_set_time(pl->pl_prop_time_total, pl->pl_total_time);
  glw_prop_set_float(pl->pl_prop_track_total, pl->pl_nentries);

  filetag_movelist(&ple->ple_ftags, ftags);
  return ple;
}


/**
 * External enqueue interface
 *
 * Will also save playlist
 */
void
playlist_enqueue(const char *url, struct filetag_list *ftags, int playit)
{
  playlist_t *pl;
  playlist_entry_t *ple;

  pl = playlist_create("Incoming", 0);

  ple = playlist_enqueue0(pl, url, ftags);


  /**
   * Inform player that a new entry has been enqueued
   *
   * If it is idle, it will start playing it directly
   */
  if(ple != NULL) {
    playlist_signal(ple, playit ? PLAYLIST_EVENT_PLAYENTRY : 
		    PLAYLIST_EVENT_NEWENTRY);
  }

  playlist_save(pl);
}

/**
 *
 */
static void
playlist_entry_free(playlist_entry_t *ple)
{
  playlist_t *pl = ple->ple_pl;

  free(ple->ple_url);

  filetag_freelist(&ple->ple_ftags);

  if(pl != NULL) {
    TAILQ_REMOVE(&pl->pl_entries, ple, ple_link);
    TAILQ_REMOVE(&pl->pl_shuffle_entries, ple, ple_shuffle_link);
  }

  glw_destroy(ple->ple_widget);
  
  glw_prop_destroy(ple->ple_prop_root);

  free(ple);
}

/**
 * Destroy a playlist entry signal
 */
static void
playlist_signal_dtor(glw_event_t *ge)
{
  playlist_event_t *pe = (void *)ge;
  playlist_entry_unref(pe->ple);
  free(pe);
}

/**
 * Send signal to playlist player
 */ 
void
playlist_signal(playlist_entry_t *ple, int type)
{
  playlist_event_t *pe;

  hts_mutex_lock(&playlistlock);
  ple->ple_refcnt++;
  hts_mutex_unlock(&playlistlock);

  pe = glw_event_create(EVENT_PLAYLIST, sizeof(playlist_event_t));
  pe->h.ge_dtor = playlist_signal_dtor;

  pe->ple = ple;
  pe->type = type;

  glw_event_enqueue(&plp.plp_geq, &pe->h);
}


/**
 * Get next (or previous) entry, optionally in shuffle mode
 */
playlist_entry_t *
playlist_advance(playlist_entry_t *ple, int prev)
{
  playlist_entry_t *n = NULL;
  int shuffle = 0;

  hts_mutex_lock(&playlistlock);

  /* ple_pl will be NULL if the playlist has been erased */

  if(ple->ple_pl != NULL) {
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
  }

  if(n != NULL)
    n->ple_refcnt++;
      
  hts_mutex_unlock(&playlistlock);

  return n;
}


/**
 * Decrease refcount and free if reaching zero
 */
void
playlist_entry_unref(playlist_entry_t *ple)
{
  hts_mutex_lock(&playlistlock);

  assert(ple->ple_refcnt > 0);
  ple->ple_refcnt--;
  if(ple->ple_refcnt == 0)
    playlist_entry_free(ple);

  hts_mutex_unlock(&playlistlock);

}


/**
 *
 */
static void
playlist_rename(playlist_t *pl, glw_t *parent)
{
  char buf[100];
  glw_t *m = glw_model_create("theme://playlist/playlist-rename.model", parent,
			      0,
			      prop_global, pl->pl_prop_root, NULL);

  if(!glw_wait_form_ok_cancel(m)) {
    glw_get_caption(m, "playlistname", buf, sizeof(buf));

    playlist_unlink(pl);

    free(pl->pl_title);
    pl->pl_title = strdup(buf);
    glw_prop_set_string(pl->pl_prop_title, pl->pl_title);
    playlist_save(pl);
  }

  glw_detach(m);
}


/**
 *
 */
static void
playlist_delete(playlist_t *pl, glw_t *parent)
{
  glw_t *m = glw_model_create("theme://playlist/playlist-delete.model", parent,
			      0,
			      prop_global, pl->pl_prop_root, NULL);

  if(!glw_wait_form_ok_cancel(m))
    playlist_destroy(pl);

  glw_detach(m);
}


/**
 *  Store playlist on disk
 */
void
playlist_save(playlist_t *pl)
{
  playlist_entry_t *ple;
  htsmsg_t *t, *m = htsmsg_create();

  htsmsg_add_str(m, "title", pl->pl_title);
  if(pl->pl_backdrop != NULL)
    htsmsg_add_str(m, "backdrop", pl->pl_backdrop);

  t = htsmsg_create();

  hts_mutex_lock(&playlistlock);
  TAILQ_FOREACH(ple, &pl->pl_entries, ple_link) {
    htsmsg_add_str(t, NULL, ple->ple_url);
  }
  hts_mutex_unlock(&playlistlock);

  htsmsg_add_array(m, "tracks", t);

  hts_settings_save(m, "playlists/%s", pl->pl_title);
  htsmsg_destroy(m);
}

/**
 *  Remove playlist from disk
 */
static void
playlist_unlink(playlist_t *pl)
{
  hts_settings_remove("playlists/%s", pl->pl_title);
}


/**
 * Load a playlist
 */
static void
playlist_load(htsmsg_t *list)
{
  htsmsg_t *tracks;
  const char *s;
  playlist_t *pl;
  htsmsg_field_t *f;

  if((s = htsmsg_get_str(list, "title")) == NULL)
    return;
  
  pl = playlist_create(s, 0);
  
  if((tracks = htsmsg_get_array(list, "tracks")) != NULL) {
    HTSMSG_FOREACH(f, tracks) {
      if(f->hmf_type == HMF_STR) {
	playlist_enqueue0(pl, f->hmf_str, NULL);
      }
    }
  }
  
  if((s = htsmsg_get_str(list, "backdrop")) != NULL)
    playlist_set_backdrop(pl, s);
}


/**
 *  Scan (stored) playlists (on startup)
 */
static void
playlist_scan(void)
{
  htsmsg_field_t *f;
  htsmsg_t *playlists, *list;

  if((playlists = hts_settings_load("playlists")) == NULL)
    return;

  HTSMSG_FOREACH(f, playlists) {
    if((list = htsmsg_get_msg_by_field(f)) != NULL)
      playlist_load(list);
  }
  htsmsg_destroy(playlists);
}
 



/**
 * User interface playlist thread
 */
static void *
playlist_thread(void *aux)
{
  appi_t *ai;
  glw_t *mini;
  playlist_t *pl;
  hts_thread_t playerthread;
  glw_t *form;
  glw_event_t *ge;
  glw_event_appmethod_t *gea;

  playlist_appi = ai = appi_create("Playlist");

  ai->ai_widget =
    glw_create(GLW_ZSTACK,
	       NULL);


  playlist_root = glw_model_create("theme://playlist/playlist-app.model",
				   ai->ai_widget, 0,
				   prop_global, NULL);

  form = glw_find_by_class(playlist_root, GLW_FORM);

  mini = glw_model_create("theme://playlist/playlist-miniature.model", NULL,
			  0, prop_global, NULL);

  playlists_list = glw_find_by_id(playlist_root, "playlist_container", 0);


  /**
   * Initialize and launch player thread
   */
  memset(&plp, 0, sizeof(plp));
  
  glw_event_initqueue(&plp.plp_geq);
  plp.plp_mp = ai->ai_mp;
  hts_thread_create(&playerthread, playlist_player, &plp);


  /**
   *
   */

  //  app_load_generic_config(ai, "playlist");

  playlist_scan();

  mainmenu_appi_add(ai, mini, 1);

  if(form != NULL) {
    glw_set(form,
	    GLW_ATTRIB_SIGNAL_HANDLER, glw_event_enqueuer, &ai->ai_geq, 1000, 
	    NULL);
  }
  
  while(1) {
    ge = glw_event_get(-1, &ai->ai_geq);

    switch(ge->ge_type) {
    default:
      break;

    case GEV_APPMETHOD:
      gea = (void *)ge;
      if(!strcmp(gea->method, "delete")) {
	if((pl = playlist_get_current()) != NULL)
	  playlist_delete(pl, ai->ai_widget);

      } else if(!strcmp(gea->method, "rename")) {
	if((pl = playlist_get_current()) != NULL)
	  playlist_rename(pl, ai->ai_widget);

      }
      break;
    }
    glw_event_unref(ge);
  }
}

void
playlist_init(void)
{
  hts_thread_t tid;

  hts_mutex_init(&playlistlock);
  hts_thread_create(&tid, playlist_thread, NULL);
}
