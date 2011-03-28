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

#ifndef SUBTITLES_H_
#define SUBTITLES_H_

#include "arch/atomic.h"
#include "misc/redblack.h"

struct media_buf;

RB_HEAD(subtitle_entry_tree, subtitle_entry);

typedef struct subtitle_entry {
  char *se_text;
  int64_t se_start;
  int64_t se_stop;
  RB_ENTRY(subtitle_entry) se_link;
} subtitle_entry_t;


typedef struct subtitles {
  struct subtitle_entry_tree s_entries;
  subtitle_entry_t *s_cur;
} subtitles_t;

subtitles_t *subtitles_create(const char *buf, size_t len);

void subtitles_destroy(subtitles_t *sub);

subtitles_t *subtitles_test(const char *fname);

subtitle_entry_t *subtitles_pick(subtitles_t *sub, int64_t pts);

subtitles_t *subtitles_load(const char *url);

struct media_buf *subtitles_ssa_decode_line(uint8_t *src, size_t len);

struct media_buf *subtitles_make_pkt(subtitle_entry_t *se);

#endif /* SUBTITLES_H_ */
