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
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>

#include <libglw/glw.h>

#include "showtime.h"
#include "app.h"
#include "playlist.h"
#include "fileaccess/fa_probe.h"
#include <layout/layout.h>
#include <layout/layout_forms.h>
#include <layout/layout_support.h>

static struct playlist_list playlists;

static playlist_player_t plp;

static glw_t *playlists_list;

static glw_t *playlist_root;

static appi_t *playlist_appi;

/**
 * Global lock for reference counters and playlist/playlistentry relations
 */
pthread_mutex_t playlistlock = PTHREAD_MUTEX_INITIALIZER;


#define PL_EVENT_NEW_PLAYLIST      1
#define PL_EVENT_DELETE_PLAYLIST   2
#define PL_EVENT_PLE_CHANGED       3

static void playlist_entry_free(playlist_entry_t *ple);
static void playlist_unlink(playlist_t *pl);
static void playlist_destroy(playlist_t *pl);


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
playlist_t *
playlist_create(const char *title, int truncate)
{
  glw_t *e, *w;
  struct layout_form_entry_list lfelist;
  playlist_t *pl;

  pthread_mutex_lock(&playlistlock);

  LIST_FOREACH(pl, &playlists, pl_link) {
    if(!strcmp(title, pl->pl_title)) {
      pthread_mutex_unlock(&playlistlock);

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


  e = glw_create(GLW_MODEL,
		 GLW_ATTRIB_FILENAME, "playlist/playlist",
		 GLW_ATTRIB_SIGNAL_HANDLER, playlist_widget_callback, pl, 400,
		 NULL);

  pl->pl_widget = e;

  w = glw_find_by_id(e, "title", 0);
  if(w != NULL)
    glw_set(w, GLW_ATTRIB_CAPTION, title, NULL);

  w = layout_form_add_tab2(playlist_root, "playlists", e,
			   "track_list_container", "playlist/tracklist");
  pl->pl_tab = w;

  TAILQ_INIT(&lfelist);
  LFE_ADD_MONITOR_CHILD(&lfelist, "track_list", PL_EVENT_PLE_CHANGED);
  layout_form_initialize(&lfelist, w, &pl->pl_ai->ai_gfs,
			 &pl->pl_ai->ai_ic, 0);

  pl->pl_list = glw_find_by_id(w, "track_list", 0);

  pthread_mutex_unlock(&playlistlock);
  return pl;
}

/**
 * Set the supplied backdrop on the playlist
 */
void
playlist_set_backdrop(playlist_t *pl, const char *url)
{
  glw_t *w;

  free(pl->pl_backdrop);
  pl->pl_backdrop = strdup(url);

  if((w = glw_find_by_id(pl->pl_widget, "backdrop", 0)) == NULL)
    return;

  glw_set(w,
	  GLW_ATTRIB_FILENAME, url,
	  NULL);
}

/**
 * Destroy a playlist
 */
static void
playlist_destroy(playlist_t *pl)
{
  playlist_entry_t *ple, *next;

  playlist_unlink(pl);

  pthread_mutex_lock(&playlistlock);

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

  pthread_mutex_unlock(&playlistlock);

  glw_destroy(pl->pl_widget);
  glw_destroy(pl->pl_tab);

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
playlist_entry_t *
playlist_enqueue0(playlist_t *pl, const char *url, struct filetag_list *ftags)
{
  playlist_entry_t *ple, *ple2;
  struct filetag_list ftags0;
  int64_t i64;
  glw_t *w;
  const char *s;

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

  layout_update_str(w, "track_author",
		    filetag_get_str2(ftags, FTAG_AUTHOR));

  layout_update_str(w, "track_album",
		    filetag_get_str2(ftags, FTAG_ALBUM));

  layout_update_time(w, "track_duration",  ple->ple_duration);
  

  /**
   * Update playlist widget
   */
  layout_update_time(pl->pl_widget, "time_total",  pl->pl_total_time);
  layout_update_int(pl->pl_widget,  "track_total", pl->pl_nentries);

  filetag_freelist(ftags);
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
  playlist_t *pl = playlist_get_current();
  playlist_entry_t *ple;

  if(pl == NULL) 
    /* someone has deleted all playlists, but we're tougher than that! */
    pl = playlist_create("Default playlist", 0);

  ple = playlist_enqueue0(pl, url, ftags);


  /**
   * Inform player that a new entry has been enqueued
   *
   * If it is idle, it will start playing it directly
   */
  if(ple != NULL) {
    playlist_signal(ple, playit ? PLAYLIST_INPUTEVENT_PLAYENTRY : 
		    PLAYLIST_INPUTEVENT_NEWENTRY);
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

  if(pl != NULL) {
    TAILQ_REMOVE(&pl->pl_entries, ple, ple_link);
    TAILQ_REMOVE(&pl->pl_shuffle_entries, ple, ple_shuffle_link);
  }

  glw_destroy(ple->ple_widget);
  
  free(ple);
}


/**
 * Send signal to playlist player
 */ 
void
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
  playlist_entry_t *n = NULL;
  int shuffle = 0;

  pthread_mutex_lock(&playlistlock);

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
 *
 */
static void
playlist_new(appi_t *ai)
{
  struct layout_form_entry_list lfelist;
  glw_t *m;
  int r;
  char plname[64];

  TAILQ_INIT(&lfelist);

  m = glw_create(GLW_MODEL,
		 GLW_ATTRIB_PARENT, ai->ai_widget,
		 GLW_ATTRIB_FILENAME, "playlist/playlist-new",
		 NULL);

  plname[0] = 0;

  LFE_ADD_STR(&lfelist, "playlist_title", plname, sizeof(plname), 1);
  LFE_ADD_BTN(&lfelist, "ok",     1);
  LFE_ADD_BTN(&lfelist, "cancel", 2);
  r = layout_form_query(&lfelist, m, &ai->ai_gfs);

  if(r == 1 && plname[0])
    playlist_create(plname, 0);

  glw_detach(m);
}


/**
 *  Store playlist on disk
 */
void
playlist_save(playlist_t *pl)
{
  char buf[256];
  FILE *fp;
  struct stat st;
  playlist_entry_t *ple;

  if(settingsdir == NULL)
    return;

  snprintf(buf, sizeof(buf), "%s/playlists", settingsdir);
  if(stat(buf, &st) == 0 || mkdir(buf, 0700) == 0) {
    snprintf(buf, sizeof(buf), "%s/playlists/%s", settingsdir, 
	     pl->pl_title);

    fp = fopen(buf, "w+");
    if(fp != NULL) {

      fprintf(fp, 
	      "showtimeplaylist-v1\n"
	      "title=%s\n", pl->pl_title);
      if(pl->pl_backdrop != NULL)
	fprintf(fp, "backdrop=%s\n", pl->pl_backdrop);
      
      pthread_mutex_lock(&playlistlock);
      TAILQ_FOREACH(ple, &pl->pl_entries, ple_link) {
	fprintf(fp, "track=%s\n", ple->ple_url);
      }
      pthread_mutex_unlock(&playlistlock);
      fclose(fp);
    }
  }
}


/**
 *  Remove playlist from disk
 */
static void
playlist_unlink(playlist_t *pl)
{
  char buf[256];

  if(settingsdir == NULL)
    return;

  snprintf(buf, sizeof(buf), "%s/playlists/%s", settingsdir,  pl->pl_title);
  unlink(buf);
}







/**
 * Load a playlist
 */
static void
playlist_load(const char *path)
{
  FILE *fp;
  char line[300];
  int l;
  char *title = NULL;
  playlist_t *pl;

  fp = fopen(path, "r");
  if(fp != NULL) {
    if(fgets(line, sizeof(line), fp) != NULL) {
      if(!strcmp("showtimeplaylist-v1\n", line)) {
	if(fgets(line, sizeof(line), fp) != NULL) {
	  if(!strncmp("title=", line, 6)) {
	    l = strlen(line+6);

	    while(line[6+l] < 32 && l > 0)
	      line[6 + l--] = 0;

	    pl = playlist_create(line + 6, 0);

	    while(!feof(fp)) {
	      if(fgets(line, sizeof(line), fp) == NULL)
		break;
	
	      l = strlen(line);
	      while(line[l] < 32 && l > 0)
		line[l--] = 0;

	      if(!strncmp("track=", line, 6))
		playlist_enqueue0(pl, line + 6, NULL);

	      if(!strncmp("backdrop=", line, 9))
		playlist_set_backdrop(pl, line + 9);

	    }
	  }
	}
      }
    }
    fclose(fp);
  }
  free(title);
}




/**
 *  Scan (stored) playlists (on startup)
 */
static void
playlist_scan(void)
{
  char buf[256];
  char fullpath[256];
  struct dirent **namelist, *d;
  int n, i;

  if(settingsdir == NULL)
    return;

  snprintf(buf, sizeof(buf), "%s/playlists", settingsdir);

  n = scandir(buf, &namelist, NULL, NULL);
  if(n < 0)
    return;

  for(i = 0; i < n; i++) {
    d = namelist[i];
    if(d->d_name[0] == '.')
      continue;

    snprintf(fullpath, sizeof(fullpath), "%s/%s", buf, d->d_name);

    printf("Loading playlist %s\n", fullpath);
    playlist_load(fullpath);
  }
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

  playlist_appi = ai = appi_create("Playlist");

  ai->ai_widget =
    glw_create(GLW_CUBESTACK,
	       GLW_ATTRIB_SIGNAL_HANDLER, playlist_root_widget, ai, 500,
	       NULL);

  playlist_root = glw_create(GLW_MODEL,
			     GLW_ATTRIB_FILENAME, "playlist/root",
			     GLW_ATTRIB_PARENT, ai->ai_widget,
			     NULL);

  mini = glw_create(GLW_MODEL,
		    GLW_ATTRIB_FILENAME, "playlist/switcher-icon",
		    NULL);

  playlists_list = glw_find_by_id(playlist_root, "playlists", 0);


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
  playlist_scan();
  pl = playlist_create("Default playlist", 0);

  layout_switcher_appi_add(ai, mini);
  
  /**
   *
   */
  TAILQ_INIT(&lfelist);
  LFE_ADD(&lfelist, "playlists");
  LFE_ADD_BTN(&lfelist, "new_playlist", PL_EVENT_NEW_PLAYLIST);
  LFE_ADD_BTN(&lfelist, "delete_playlist", PL_EVENT_DELETE_PLAYLIST);
  layout_form_initialize(&lfelist, playlist_root, &ai->ai_gfs, &ai->ai_ic, 1);

  
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

      case PL_EVENT_NEW_PLAYLIST:
	playlist_new(ai);
	break;

      case PL_EVENT_DELETE_PLAYLIST:
	pl = playlist_get_current();
	if(pl != NULL)
	  playlist_destroy(pl);
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
