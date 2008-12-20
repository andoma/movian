/*
 *  DVD SPU decoding for GL surfaces
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

#ifndef GL_DVDSPU_H
#define GL_DVDSPU_H

#include "media.h"

TAILQ_HEAD(gl_dvdspu_pic_head, gl_dvdspu_pic);

typedef struct hts_rect {
  int x, y, w, h;
} hts_rect_t;


typedef struct gl_dvdspu {

  uint32_t *gd_clut;
  
  struct gl_dvdspu_pic_head gd_pics;

  hts_mutex_t gd_mutex;

  hts_rect_t gd_hilite;

  int gd_curbut;

  int gd_repaint;

} gl_dvdspu_t;


struct gl_dvdspu *gl_dvdspu_init(void);
void gl_dvdspu_deinit(gl_dvdspu_t *gd);

struct dvd_player;

void gl_dvdspu_dispatch(struct dvd_player *dp,
			gl_dvdspu_t *gd, media_buf_t *hmb);

void gl_dvdspu_flush(gl_dvdspu_t *gd);

void gl_dvdspu_layout(struct dvd_player *dp, struct gl_dvdspu *gd);

void gl_dvdspu_render(struct gl_dvdspu *gd, float xsize, float ysize,
		      float alpha);

#endif /* GL_DVDSPU_H */
