/*
 *  Playlist
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

#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <ffmpeg/avcodec.h>
#include <ffmpeg/avformat.h>
#include <libhts/htscfg.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "input.h"
#include "layout/layout.h"
#include "menu.h"
#include "apps/playlist/playlist.h"
#include "play_file.h"
#include "mediaprobe.h"
#include "miw.h"

app_t app_pl;

TAILQ_HEAD(play_list_entry_queue, play_list_entry);


typedef struct play_list_entry {
  const char *ple_path;
  glw_t *ple_widget;
  glw_t *ple_curicon;

  TAILQ_ENTRY(play_list_entry) ple_link;
  TAILQ_ENTRY(play_list_entry) ple_shuffle_link;

  mediainfo_t ple_mi;

  int ple_refcount;

  int ple_slot;

} play_list_entry_t;



typedef struct play_list {
  pthread_mutex_t pl_mutex;
  pthread_cond_t pl_cond;
  glw_t *pl_list;
  glw_t *pl_widget;

  play_list_entry_t *pl_cur;
  int pl_entries;

  struct play_list_entry_queue pl_queue;
  struct play_list_entry_queue pl_shuffle_queue;

  enum {
    PL_PLAYMODE_NORMAL,
    PL_PLAYMODE_SHUFFLE,
  } pl_playmode;

  appi_t *pl_ai;

  ic_t pl_ic;

  int pl_moved;

  glw_t *pl_waiting_widget;
  
  int pl_stopped;

} play_list_t;


static void pl_flush(play_list_t *pl, play_list_entry_t *notme);
static glw_t *playlist_menu_setup(glw_t *p, play_list_t *pl);

static int 
playlist_entry_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  return 0;
}


/*
 * .pls -playlist format loader
 */

static void
playlist_enqueue_playlist_pls(const char *path)
{
  FILE *fp;
  char line[300];
  int l;
  char *fname;


  fp = fopen(path, "r");
  if(fp == NULL)
    return;


  while(!feof(fp)) {
    if(fgets(line, sizeof(line) - 1, fp) == NULL)
      break;

    l = strlen(line);
    while(line[l] < ' ')
      line[l--] = 0;
    
    if(strncmp(line, "File", 4))
      continue;

    if(!isdigit(line[4]))
      continue;
    
    fname = strchr(line, '=');
    if(fname == NULL)
      continue;
    fname++;
    playlist_enqueue(fname, NULL, 0);
  }
  fclose(fp);
}


/*
 *
 */

void
playlist_eventstrike(inputevent_t *ie)
{
  appi_t *ai;

  ai = appi_find(&app_pl, 0, 0);
  if(ai != NULL)
    input_postevent(&ai->ai_ic, ie);
}
/*
 *
 */

void
playlist_flush(void)
{
  appi_t *ai;
  play_list_t *pl;

  ai = appi_find(&app_pl, 0, 0);
  if(ai == NULL)
    return;

  pl = ai->ai_play_list;

  pthread_mutex_lock(&pl->pl_mutex);
  pl_flush(pl, NULL);
  pthread_mutex_unlock(&pl->pl_mutex);
}


/*
 *
 */

int
playlist_enqueue(const char *path, mediainfo_t *mi, int flush)
{
  play_list_t *pl;
  play_list_entry_t *ple, *prev;
  glw_t *w;
  int o, slot = 1;
  char tmp[40];
  appi_t *ai;

  ai = appi_find(&app_pl, 0, 1);

  pl = ai->ai_play_list;

  if(mi != NULL && mi->mi_type == MI_PLAYLIST_PLS) {
    playlist_enqueue_playlist_pls(path);
    return 0;
  }

  ple = malloc(sizeof(play_list_entry_t));

  if(mi != NULL) {
    mediaprobe_dup(&ple->ple_mi, mi);
  } else if(mediaprobe(path, &ple->ple_mi, 0)) {
    free(ple);
    return 0;
  }

  mi = &ple->ple_mi;
  ple->ple_path = strdup(path);


  w = glw_create(GLW_BITMAP,
		 GLW_ATTRIB_SIGNAL_HANDLER, playlist_entry_callback, ple, 0,
		 GLW_ATTRIB_PARENT, pl->pl_list,
		 GLW_ATTRIB_FILENAME, "icon://plate-wide.png",
		 GLW_ATTRIB_FLAGS, GLW_NOASPECT,
		 NULL);

  ple->ple_widget = w;

  w = glw_create(GLW_CONTAINER_X, 
		 GLW_ATTRIB_PARENT, w,
		 NULL);

  ple->ple_curicon = 
    glw_create(GLW_BITMAP, GLW_ATTRIB_PARENT, w,
	       GLW_ATTRIB_FILENAME, "icon://media-playback-start.png",
	       GLW_ATTRIB_WEIGHT, 0.5,
	       GLW_ATTRIB_ALPHA, 0.0,
	       NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_CAPTION, mi->mi_title,
	     GLW_ATTRIB_TEXT_FLAGS, GLW_TEXT_UTF8,
	     GLW_ATTRIB_WEIGHT, 10.0f,
	     GLW_ATTRIB_PARENT, w,
	     NULL);


  snprintf(tmp, sizeof(tmp), "%d:%02d",
	   mi->mi_duration / 60, mi->mi_duration % 60);


  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_RIGHT,
	     GLW_ATTRIB_CAPTION, tmp,
	     GLW_ATTRIB_WEIGHT, 1.0f,
	     GLW_ATTRIB_PARENT, w,
	     NULL);

  pthread_mutex_lock(&pl->pl_mutex);

  if(flush)
    pl_flush(pl, NULL);

  prev = TAILQ_LAST(&pl->pl_queue, play_list_entry_queue);
  if(prev != 0)
    slot = prev->ple_slot + 1;
  
  ple->ple_slot = slot;
  

  TAILQ_INSERT_TAIL(&pl->pl_queue, ple, ple_link);

  prev = TAILQ_FIRST(&pl->pl_shuffle_queue);

  o = pl->pl_entries > 0 ? rand() % pl->pl_entries : 0;

  while(o-- && prev != NULL)
    prev = TAILQ_NEXT(prev, ple_shuffle_link);

  if(prev != NULL)
    TAILQ_INSERT_AFTER(&pl->pl_shuffle_queue, prev, ple, ple_shuffle_link);
  else
    TAILQ_INSERT_TAIL(&pl->pl_shuffle_queue, ple, ple_shuffle_link);

  pl->pl_entries++;
  pl->pl_stopped = 0;

  pthread_cond_signal(&pl->pl_cond);
  pthread_mutex_unlock(&pl->pl_mutex);
  return 1;
}


/*
 *
*/

static play_list_entry_t *
play_list_prev(play_list_t *pl)
{
  play_list_entry_t *ple;

  switch(pl->pl_playmode) {
  case PL_PLAYMODE_NORMAL:
    ple = TAILQ_PREV(pl->pl_cur, play_list_entry_queue, ple_link);
    if(ple == NULL)
      ple = TAILQ_LAST(&pl->pl_queue, play_list_entry_queue);
    break;

  case PL_PLAYMODE_SHUFFLE:
    ple = TAILQ_PREV(pl->pl_cur, play_list_entry_queue, ple_shuffle_link);
    if(ple == NULL)
      ple = TAILQ_LAST(&pl->pl_shuffle_queue, play_list_entry_queue);
    break;
  default:
    ple = NULL;
    break;
  }
  return ple;
}


/*
 *
 */

static play_list_entry_t *
play_list_next(play_list_t *pl, int wrap)
{
  play_list_entry_t *ple;

  switch(pl->pl_playmode) {
  case PL_PLAYMODE_NORMAL:
    ple = TAILQ_NEXT(pl->pl_cur, ple_link);
    if(ple == NULL && wrap)
      ple = TAILQ_FIRST(&pl->pl_queue);
    break;

  case PL_PLAYMODE_SHUFFLE:
    ple = TAILQ_NEXT(pl->pl_cur, ple_shuffle_link);
    if(ple == NULL && wrap)
      ple = TAILQ_FIRST(&pl->pl_shuffle_queue);
    break;
  default:
    ple = NULL;
    break;
  }
  return ple;
}

/*
 *
 */

static void
pl_renumberate(play_list_t *pl)
{
  play_list_entry_t *ple;
  int i = 0;

  TAILQ_FOREACH(ple, &pl->pl_queue, ple_link)
    ple->ple_slot = ++i;
}

/*
 *
 */

static void
ple_destroy(play_list_t *pl, play_list_entry_t *ple)
{
  if(pl->pl_cur == ple) {
    if(pl->pl_stopped == 0) {
      /* This song is currently playing, post a DELETE and let player
	 thread take care of it */
      input_keystrike(&pl->pl_ic, INPUT_KEY_DELETE);
      return;
    } else {
      pl->pl_cur = NULL;
    }
  }

  mediaprobe_free(&ple->ple_mi);
  glw_destroy(ple->ple_widget);
  free((void *)ple->ple_path);
  TAILQ_REMOVE(&pl->pl_queue, ple, ple_link);
  TAILQ_REMOVE(&pl->pl_shuffle_queue, ple, ple_shuffle_link);
  free(ple);
  pl->pl_entries--;
}

/*
 *
 */

static void
pl_flush(play_list_t *pl, play_list_entry_t *notme)
{
  play_list_entry_t *ple, *n;

  for(ple = TAILQ_FIRST(&pl->pl_queue); ple != NULL; ple = n) {
    n = TAILQ_NEXT(ple, ple_link);

    if(ple == notme)
      continue;

    ple_destroy(pl, ple);
  }

  if(notme != NULL)
    notme->ple_slot = 1;

}

/*
 *
 */


static int 
plextra_callback(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  play_list_t *pl = opaque;
  play_list_entry_t *ple = pl->pl_cur;
  char tmp[30];

  va_list ap;
  va_start(ap, signal);

  switch(signal) {
  case GLW_SIGNAL_PREPARE:

    if(ple == NULL) {
      glw_set(w, GLW_ATTRIB_CAPTION, "", NULL);
      return 0;
    }
    snprintf(tmp, sizeof(tmp), "%d / %d", ple->ple_slot, pl->pl_entries);
    glw_set(w, GLW_ATTRIB_CAPTION, tmp, NULL);
    return 0;

  default:
    break;
  }
  va_end(ap);
  return 0;
}



static glw_t *
plxtra(play_list_t *pl)
{
  glw_t *y;

  y = glw_create(GLW_CONTAINER_Y,
		 NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_CAPTION, "Playlist",
	     NULL);

  glw_create(GLW_RULER, 
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_WEIGHT, 0.1,
	     NULL);

  glw_create(GLW_TEXT_BITMAP,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_CAPTION, "",
	     GLW_ATTRIB_SIGNAL_HANDLER, plextra_callback, pl, 0,
	     NULL);

  return y;
}


/*
 *
 */

static void *
play_list_play_thread(void *aux)
{
  play_list_t *pl = aux;
  play_list_entry_t *ple;
  const char *path;
  int key;

  pthread_mutex_lock(&pl->pl_mutex);

  while(1) {

    if(pl->pl_stopped) {
      pthread_cond_wait(&pl->pl_cond, &pl->pl_mutex);
      input_flush_queue(&pl->pl_ic);
      continue;
    }

    if(pl->pl_cur == NULL) {
      ple = TAILQ_FIRST(&pl->pl_queue);
      if(ple == NULL) {
	pthread_cond_wait(&pl->pl_cond, &pl->pl_mutex);
	continue;
      }

      pl->pl_cur = ple;
    }

    ple = pl->pl_cur;
    path = ple->ple_path;
    glw_set(ple->ple_curicon, GLW_ATTRIB_ALPHA, 1.0f, NULL);
    pl->pl_moved = 0;

    pthread_mutex_unlock(&pl->pl_mutex);
    key = play_file(path, pl->pl_ai, &pl->pl_ic, &ple->ple_mi, plxtra(pl),
		    NULL);
    pthread_mutex_lock(&pl->pl_mutex);
 
    glw_set(ple->ple_curicon, GLW_ATTRIB_ALPHA, 0.0f, NULL);

    switch(key) {
    case 0:
      /* Nothing pressed, track just ended */

      pl->pl_cur = play_list_next(pl, 0);
      if(pl->pl_cur == NULL)
	pl->pl_stopped = 1;

      break;
    case INPUT_KEY_STOP:
      pl->pl_stopped = 1;
      break;

    case INPUT_KEY_DELETE:
      pl->pl_cur = play_list_next(pl, 0);
      if(pl->pl_cur == ple)
	pl->pl_cur = NULL;

      ple_destroy(pl, ple);
      pl_renumberate(pl);
      pl->pl_moved = 0;
      break;

    case INPUT_KEY_ENTER:
      pl->pl_cur = glw_get_opaque(pl->pl_list->glw_selected,
				  playlist_entry_callback);
      break;
    case INPUT_KEY_NEXT:
      pl->pl_cur = play_list_next(pl, 1);
      break;
    case INPUT_KEY_PREV:
      pl->pl_cur = play_list_prev(pl);
      break;
    }

    if(pl->pl_moved == 0 && pl->pl_cur != NULL) {
      /* Playlist hilite didnt moved during entire song,
	 lets update it do point to us */
      pl->pl_list->glw_extra = 0;
      pl->pl_list->glw_selected = pl->pl_cur->ple_widget;
    }
  }
}


/*
 *
 */

static void
pl_save(play_list_t *pl)
{
  glw_t *w, *y, *z;
  char str[40];
  int p, key;
  FILE *fp;
  char filename[400];
  play_list_entry_t *ple;
  
  w = glw_create(GLW_CONTAINER,
		 GLW_ATTRIB_PARENT, pl->pl_widget,
		 NULL);

  y = glw_create(GLW_CONTAINER_Y,
		 GLW_ATTRIB_PARENT, w,
		 NULL);

  glw_create(GLW_TEXT_VECTOR,
	     GLW_ATTRIB_PARENT, y,
	     GLW_ATTRIB_CAPTION, "Enter filename:",
	     NULL);

  z = glw_create(GLW_TEXT_VECTOR,
		 GLW_ATTRIB_PARENT, y,
		 GLW_ATTRIB_CAPTION, "",
		 NULL);
  p = 0;
  memset(str, 0, sizeof(str));

  do {
    pthread_mutex_unlock(&pl->pl_mutex);
    key = input_getkey(&pl->pl_ai->ai_ic, 1);
    pthread_mutex_lock(&pl->pl_mutex);
    switch(key) {

    case INPUT_KEY_CLOSE:
      goto nosave;

    case 'a' ... 'z':
    case 'A' ... 'Z':
    case '0' ... '9':
      str[p++] = key;
      break;

    case INPUT_KEY_BACK:
      if(p > 0)
	str[--p] = 0;
      break;

    default:
      break;
    }
    glw_set(z, GLW_ATTRIB_CAPTION, str, NULL);
  } while(key != INPUT_KEY_ENTER);

  snprintf(filename, sizeof(filename), "%s/%s.pls", 
	   config_get_str("playlistpath", "."), str);

  fp = fopen(filename, "w+");
  if(fp != NULL) {

    p = 1;
    fprintf(fp, "[playlist]\n");
    fprintf(fp, "NumberOfEntries=%d\n", pl->pl_entries);
    TAILQ_FOREACH(ple, &pl->pl_queue, ple_link) {
      fprintf(fp, "File%d=%s\n", p++, ple->ple_path);
    }

    fclose(fp);
  }


 nosave:
  glw_destroy(w);
}



/*
 *
 */

static void *
play_list_main(appi_t *ai, play_list_t *pl)
{
  inputevent_t ie;

  pthread_mutex_lock(&pl->pl_mutex);

  ai->ai_visible = 1;

  while(1) {
    pthread_mutex_unlock(&pl->pl_mutex);
    input_getevent(&ai->ai_ic, 1, &ie, NULL);
    pthread_mutex_lock(&pl->pl_mutex);

    switch(ie.type) {
    default:
      break;
#if 0
    case INPUT_PAD:
      pad_nav_slist(pl->pl_list, &ie);
      break;
#endif
    case INPUT_KEY:
      switch(ie.u.key) {
      case 0:
	break;
      case INPUT_KEY_BACK:
	layout_hide(pl->pl_ai);
	break;
      case INPUT_KEY_UP:
	pl->pl_moved = 1;
	glw_nav_signal(pl->pl_list, GLW_SIGNAL_UP);
	break;
      case INPUT_KEY_DOWN:
	pl->pl_moved = 1;
	glw_nav_signal(pl->pl_list, GLW_SIGNAL_DOWN);
	break;
      case INPUT_KEY_CLEAR:
      case INPUT_KEY_EJECT:
	pl_flush(pl, NULL);
	break;
      case INPUT_KEY_CLEAR_BUT_CURRENT:
	pl_flush(pl, pl->pl_cur);
	break;
      case INPUT_KEY_SAVE:
	pl_save(pl);
	break;

      default:
	pl->pl_stopped = 0;
	pthread_cond_signal(&pl->pl_cond);
	input_keystrike(&pl->pl_ic, ie.u.key);
	break;
      }
      break;
    }
  }
  return NULL;
}


/*
 *
 */


const char *playlistmodes[] = {
  [PL_PLAYMODE_NORMAL] = "Normal",
  [PL_PLAYMODE_SHUFFLE] = "Shuffle",
};



/*
 *
 */


static void *
playlist_start(void *aux)
{
  pthread_t ptid;
  play_list_t *pl = aux;
  appi_t *ai = pl->pl_ai;

  pthread_create(&ptid, NULL, play_list_play_thread, pl);
  play_list_main(ai, pl);
  return NULL;
}



static void
playlist_spawn(appi_t *ai)
{
  play_list_t *pl = calloc(1, sizeof(play_list_t));

  input_init(&pl->pl_ic); /* Input keys forwarded to playback thread */

  pthread_mutex_init(&pl->pl_mutex, NULL);
  pthread_cond_init(&pl->pl_cond, NULL);

  pl->pl_ai = ai;
  ai->ai_play_list = pl;

  TAILQ_INIT(&pl->pl_queue);
  TAILQ_INIT(&pl->pl_shuffle_queue);

  pl->pl_list = 
    glw_create(GLW_ARRAY, 
	       GLW_ATTRIB_X_SLICES, 1,
	       GLW_ATTRIB_Y_SLICES, 13,
	       GLW_ATTRIB_SIDEKICK, bar_title("Playlist"),
	       GLW_ATTRIB_SIGNAL_HANDLER, appi_widget_post_key, ai, 0,
	       NULL);

  ai->ai_widget = pl->pl_list;

  playlist_menu_setup(appi_menu_top(ai), pl);

  pthread_create(&ai->ai_tid, NULL, playlist_start, pl);
}


app_t app_pl = {
  .app_name = "Playlist",
  .app_icon = "icon://playlist.png",
  .app_spawn = playlist_spawn
};



/******************************************************************************
 *
 * Menus
 *
 */

static void
playlist_mode_switch(play_list_t *pl)
{
  if(pl->pl_playmode == PL_PLAYMODE_NORMAL)
    pl->pl_playmode = PL_PLAYMODE_SHUFFLE;
  else
    pl->pl_playmode = PL_PLAYMODE_NORMAL;
}

static int 
playlist_menu_mode(glw_t *w, void *opaque, glw_signal_t signal, ...)
{
  play_list_t *pl = opaque;
  char buf[50];
  switch(signal) {
  case GLW_SIGNAL_PREPARE:
    snprintf(buf, sizeof(buf), "Playmode: %s", playlistmodes[pl->pl_playmode]);

    w = glw_find_by_class(w, GLW_TEXT_BITMAP);
    if(w != NULL)
      glw_set(w, GLW_ATTRIB_CAPTION, buf, NULL);
    return 0;

  case GLW_SIGNAL_CLICK:
    playlist_mode_switch(pl);
    return 1;
    
  default:
    return 0;
  }
}


static glw_t *
playlist_menu_setup(glw_t *p, play_list_t *pl)
{
  glw_t *v;

  v = menu_create_submenu(p, "icon://playlist.png", "Playlist control", 0);
  
  menu_create_item(v, "icon://clear.png", "Clear all",
		   menu_post_key_pop_and_hide, pl->pl_ai, 
		   INPUT_KEY_CLEAR, 0);

  menu_create_item(v, "icon://clear.png", "Clear but current",
		   menu_post_key_pop_and_hide, pl->pl_ai,
		   INPUT_KEY_CLEAR_BUT_CURRENT, 0);

  menu_create_item(v, "icon://playlist.png", "Playmode",
		   playlist_menu_mode, pl, 0, 0);

  return v;
}
