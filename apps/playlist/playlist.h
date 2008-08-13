/*
 *  Application playlist
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

#ifndef PLAYLIST_H
#define PLAYLIST_H

#include "app.h"
#include "fileaccess/fa_tags.h"


/**
 * SubEvent used to signal that a new entry has been enqueued
 */ 
#define PLAYLIST_EVENT_NEWENTRY           1

/**
 * SubEvent used to signal that user wants to play given entry
 */ 
#define PLAYLIST_EVENT_PLAYENTRY          2


TAILQ_HEAD(playlist_entry_queue, playlist_entry);
LIST_HEAD(playlist_list, playlist);

/**
 * Struct describing a playlist
 */
typedef struct playlist {
  appi_t *pl_ai;

  glw_t *pl_widget;
  glw_t *pl_tab;
  glw_t *pl_list;

  char *pl_title;
  char *pl_backdrop;

  LIST_ENTRY(playlist) pl_link;

  struct playlist_entry_queue pl_entries;
  struct playlist_entry_queue pl_shuffle_entries;

  int pl_nentries;
  int pl_total_time;

  glw_prop_t *pl_prop_root;
  glw_prop_t *pl_prop_time_total;
  glw_prop_t *pl_prop_time_current;

  glw_prop_t *pl_prop_track_total;
  glw_prop_t *pl_prop_track_current;



} playlist_t;



/**
 * Struct describing a playlist entry (a track)
 */
typedef struct playlist_entry {
  TAILQ_ENTRY(playlist_entry) ple_link;
  TAILQ_ENTRY(playlist_entry) ple_shuffle_link;

  playlist_t *ple_pl;

  glw_t *ple_widget;                    /* Widget in tracklist */

  int ple_refcnt;                       /* protected by global 'reflock' */

  char *ple_url;

  int ple_duration;                     /* Track duration, we need it often
					   so avoid peeking in tags for this */

  int ple_time_offset;                  /* Time offset in entire playlist, 
					   i.e. sum of duration for all
					   previous entries */

  int ple_track;

  struct  filetag_list ple_ftags;

  glw_prop_t *ple_prop_root;
  glw_prop_t *ple_prop_playstatus;

} playlist_entry_t;

/**
 *
 */
typedef struct playlist_event {
  glw_event_t h;
  playlist_entry_t *ple;
  int type;
} playlist_event_t;



/**
 * Control struct for playback
 */
typedef struct playlist_player {
  int plp_mode;
  glw_event_queue_t plp_geq;
  media_pipe_t *plp_mp;

} playlist_player_t;


void playlist_init(void);

void playlist_enqueue(const char *url, struct filetag_list *ftags,
		      int playit);

playlist_entry_t *playlist_advance(playlist_entry_t *ple, int prev);

void *playlist_player(void *aux);

playlist_t *playlist_create(const char *title, int truncate);

extern hts_mutex_t playlistlock;

void playlist_entry_unref(playlist_entry_t *ple);

playlist_entry_t *playlist_enqueue0(playlist_t *pl, const char *url,
				    struct filetag_list *ftags);

void playlist_build_from_dir(const char *url);

void playlist_signal(playlist_entry_t *ple, int type);

void playlist_save(playlist_t *pl);

void playlist_set_backdrop(playlist_t *pl, const char *url);

#endif /* PLAYLIST_H */
