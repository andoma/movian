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

struct video_decoder;

typedef struct ext_subtitles {
  struct video_overlay_queue es_entries;
  video_overlay_t *es_cur;

  void (*es_dtor)(struct ext_subtitles *es);
  void (*es_picker)(struct ext_subtitles *es, int64_t pts,
		    struct video_decoder *vd);

} ext_subtitles_t;

void subtitles_destroy(ext_subtitles_t *sub);

ext_subtitles_t *subtitles_test(const char *fname);

ext_subtitles_t *subtitles_load(struct media_pipe *mp, const char *url);

ext_subtitles_t *load_ssa(const char *url, char *buf, size_t len);

void subtitles_pick(ext_subtitles_t *es, int64_t pts,
		    struct video_decoder *vd);
