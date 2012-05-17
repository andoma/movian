/*
 *  Subtitling
 *  Copyright (C) 2007, 2010 Andreas Öman
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

#include <assert.h>
#include "arch/atomic.h"
#include "misc/redblack.h"
#include "video_overlay.h"

RB_HEAD(ext_subtitle_entry_tree, ext_subtitle_entry);

typedef struct ext_subtitle_entry {
  int64_t ese_start;
  int64_t ese_stop;
  RB_ENTRY(ext_subtitle_entry) ese_link;

  int ese_items;
  union {
    void *ese_data;
    void **ese_vec;
  };

} ext_subtitle_entry_t;


typedef struct ext_subtitles {
  struct ext_subtitle_entry_tree es_entries;
  ext_subtitle_entry_t *es_cur;
  void (*es_decode)(struct video_decoder *vd, struct ext_subtitles *es,
		    ext_subtitle_entry_t *ese);

  void (*es_free_entry_data)(void *data);

  void (*es_dtor)(struct ext_subtitles *es);

} ext_subtitles_t;

void subtitles_destroy(ext_subtitles_t *sub);

ext_subtitles_t *subtitles_test(const char *fname);

ext_subtitle_entry_t *subtitles_pick(ext_subtitles_t *sub, int64_t pts);

ext_subtitles_t *subtitles_load(const char *url);

ext_subtitles_t *load_ssa(const char *url, char *buf, size_t len);

void ese_insert(ext_subtitles_t *es, void *data, int64_t start, int64_t stop);

/**
 *
 */
static inline void **
ese_get_data(ext_subtitle_entry_t *ese)
{
  assert(ese->ese_items > 0);
  return ese->ese_items == 1 ? &ese->ese_data : ese->ese_vec;
}
