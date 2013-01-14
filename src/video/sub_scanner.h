/*
 *  Scanning of external subtitles
 *  Copyright (C) 2013 Andreas Öman
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
#pragma once


/**
 *
 */
typedef struct sub_scanner {
  int ss_refcount;

  int ss_beflags;

  rstr_t *ss_title;          // Video title
  rstr_t *ss_imdbid;

  hts_mutex_t ss_mutex;  // Lock around ss_proproot
  prop_t *ss_proproot;   // property where to add subs

  char *ss_url;  // can be NULL 

  int ss_stop;   // set if we should stop working (video playback have stopped)

  int ss_opensub_hash_valid; // Set if hash is valid
  uint64_t ss_opensub_hash;  // opensubtitles hash
  uint64_t ss_fsize;         // Size of video file being played

  int ss_season;
  int ss_episode;

  int ss_duration;

} sub_scanner_t;

sub_scanner_t *sub_scanner_create(const char *url, int beflags, rstr_t *title,
				  prop_t *proproot, int opensub_hash_valid,
				  uint64_t opensub_hash, uint64_t fsize,
				  rstr_t *imdbid, int season, int episode,
				  int duration);

void sub_scanner_destroy(sub_scanner_t *ss); 

void sub_scanner_release(sub_scanner_t *ss);

void sub_scanner_retain(sub_scanner_t *ss);

