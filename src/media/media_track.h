/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
 *
 *  This program is also available under a commercial proprietary license.
 *  For more information, contact andreas@lonelycoder.com
 */

#pragma once

TAILQ_HEAD(media_track_queue, media_track);

/**
 * Media pipe
 */
typedef struct media_track_mgr {

  prop_sub_t *mtm_node_sub;
  prop_sub_t *mtm_current_sub;
  prop_sub_t *mtm_url_sub;
  struct media_track_queue mtm_tracks;
  struct media_track *mtm_suggested_track;
  struct media_track *mtm_current;
  struct media_pipe *mtm_mp;

  enum {
    MEDIA_TRACK_MANAGER_AUDIO,
    MEDIA_TRACK_MANAGER_SUBTITLES,
  } mtm_type;

  int mtm_user_set; /* If set by user, and if so, we should not suggest
		       anything */

  char *mtm_current_url;
  char *mtm_canonical_url;
  rstr_t *mtm_user_pref;  // Configured by user

} media_track_mgr_t;



void mp_track_mgr_init(struct media_pipe *mp, media_track_mgr_t *mtm,
                       prop_t *root, int type, prop_t *current);

void mp_track_mgr_destroy(media_track_mgr_t *mtm);

void mp_track_mgr_next_track(media_track_mgr_t *mtm);

int mp_track_mgr_select_track(media_track_mgr_t *mtm,
                              event_select_track_t *est);


void mp_add_track(prop_t *parent,
		  const char *title,
		  const char *url,
		  const char *format,
		  const char *longformat,
		  const char *isolang,
		  const char *source,
		  prop_t *sourcep,
		  int score,
                  int autosel);

void mp_add_trackr(prop_t *parent,
		   rstr_t *title,
		   const char *url,
		   rstr_t *format,
		   rstr_t *longformat,
		   rstr_t *isolang,
		   rstr_t *source,
		   prop_t *sourcep,
		   int score,
                   int autosel);

void mp_add_track_off(prop_t *tracks, const char *title);
