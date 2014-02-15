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

#include "misc/queue.h"

/**
 *
 */
typedef struct sub_scanner {
  int ss_refcount;

  int ss_beflags;

  rstr_t *ss_title;          // Video title
  rstr_t *ss_imdbid;

  hts_mutex_t ss_mutex;  // Lock around ss_proproot
  struct prop *ss_proproot;   // property where to add subs

  char *ss_url;  // can be NULL 

  int ss_stop;   // set if we should stop working (video playback have stopped)

  int ss_hash_valid; // Set if hash is valid
  uint64_t ss_opensub_hash;  // opensubtitles hash
  uint64_t ss_fsize;         // Size of video file being played
  uint8_t ss_subdbhash[16];

  int ss_year;
  int ss_season;
  int ss_episode;

  int ss_duration;

} sub_scanner_t;


/**
 *
 */
typedef struct subtitle_provider {
  TAILQ_ENTRY(subtitle_provider) sp_link;
  char *sp_id;
  int sp_enabled;
  int sp_autosel;
  int sp_prio;
  void (*sp_query)(struct subtitle_provider *sp, struct sub_scanner *ss,
                   int score, int autosel);
  void (*sp_retain)(struct subtitle_provider *sp);

  struct prop *sp_settings;

  struct setting *sp_setting_enabled;
  struct setting *sp_setting_autosel;

} subtitle_provider_t;


/**
 *
 */
struct subtitle_settings {

  struct setting *scaling_setting;
  struct setting *align_on_video_setting;
  struct setting *vertical_displacement_setting;
  struct setting *horizontal_displacement_setting;

  int alignment;   // LAYOUT_ALIGN_ from layout.h
  int style_override;
  int color;
  int shadow_color;
  int shadow_displacement;
  int outline_color;
  int outline_size;
};

extern struct subtitle_settings subtitle_settings;


struct video_args;
struct prop;
sub_scanner_t *sub_scanner_create(const char *url, struct prop *proproot,
				  const struct video_args *va, int duration);

void sub_scanner_destroy(sub_scanner_t *ss);

void sub_scanner_release(sub_scanner_t *ss);

void sub_scanner_retain(sub_scanner_t *ss);


void subtitles_init(void);

void subtitle_provider_register(subtitle_provider_t *sp,
                                const char *id, prop_t *title,
                                int default_prio, const char *subtype,
                                int default_enable, int default_autosel);

void subtitle_provider_unregister(subtitle_provider_t *sp);

int subtitles_embedded_score(void);

int subtitles_embedded_autosel(void);

const char *subtitles_probe(const char *url);
