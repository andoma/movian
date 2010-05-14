/*
 *  Playqueue
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

#ifndef PLAYQUEUE_H__
#define PLAYQUEUE_H__

/**
 *
 */
typedef struct playqueue_entry {

  int pqe_refcount;

  /**
   * Read only members
   */
  char *pqe_url;
  prop_t *pqe_psource;

  prop_t *pqe_node;
  prop_t *pqe_prop_url;

  /**
   * Entry is enqueued (ie, not from source list)
   */
  uint8_t pqe_enq;

  /**
   * Set if globally linked. Protected by playqueue_mutex
   */
  uint8_t pqe_linked;

  /**
   * Set if this entry is playable
   */
  uint8_t pqe_playable;

  /**
   * Set if this entry should be played ASAP
   */
  uint8_t pqe_startme;

  /**
   * Global link. Protected by playqueue_mutex
   */
  TAILQ_ENTRY(playqueue_entry) pqe_linear_link;
  TAILQ_ENTRY(playqueue_entry) pqe_shuffled_link;


  /**
   * Points back into node prop from source siblings
   * A ref is held on this prop when it's not NULL.
   */
  prop_t *pqe_originator;

  /**
   * Subscribes to source.url
   * Used to match entries from source into the currently played track
   */
  prop_sub_t *pqe_urlsub;


  /**
   * Subscribes to source.type
   * Used to find out if we should play the entry or just skip over it
   */
  prop_sub_t *pqe_typesub;

  /**
   * Maintains order from source list. Protected by playqueue_mutex
   */
  TAILQ_ENTRY(playqueue_entry) pqe_source_link;

} playqueue_entry_t;


/**
 *
 */
typedef struct playqueue_event {
  event_t h;
  playqueue_entry_t *pe_pqe;
} playqueue_event_t;


void playqueue_play(const char *url, prop_t *meta);

void playqueue_event_handler(event_t *e);

void playqueue_load_with_source(prop_t *track, prop_t *source, int mode);

#endif /* PLAYQUEUE_H__ */
