/*
 *  DVD SPU decoding
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

#ifndef VIDEO_DVDSPU_H
#define VIDEO_DVDSPU_H

#include "video_decoder.h"
#include <dvdnav/dvdnav.h>


TAILQ_HEAD(dvdspu_queue, dvdspu);

typedef struct dvdspu {

  TAILQ_ENTRY(dvdspu) d_link;

  uint8_t *d_data;
  size_t d_size;

  int d_cmdpos;
  int64_t d_pts;

  uint8_t d_palette[4];
  uint8_t d_alpha[4];
  
  int d_x1, d_y1;
  int d_x2, d_y2;

  uint8_t *d_bitmap;

  int d_destroyme;

} dvdspu_t;



/**
 *
 */
typedef struct dvdspu_decoder {

  struct dvdspu_queue dd_queue;

  uint32_t *dd_clut;
  
  hts_mutex_t dd_mutex;

  pci_t dd_pci;

  int dd_curbut;

  int dd_repaint;
  
} dvdspu_decoder_t;


void dvdspu_destroy(dvdspu_decoder_t *dd, dvdspu_t *d);

void dvdspu_decoder_dispatch(video_decoder_t *vd, media_buf_t *mb,
			     media_pipe_t *mp);

void dvdspu_decoder_destroy(dvdspu_decoder_t *dd);

int dvdspu_decode(dvdspu_t *d, int64_t pts);


#if 0

#include "media.h"

TAILQ_HEAD(gl_dvdspu_pic_head, gl_dvdspu_pic);

typedef struct hts_rect {
  int x, y, w, h;
} hts_rect_t;


typedef struct dvdspu_decoder {

  uint32_t *dd_clut;
  
  struct gl_dvdspu_pic_head dd_pics;

  hts_mutex_t dd_mutex;

  int dd_curbut;

  int dd_repaint;

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
#endif

#endif /* VIDEO_DVDSPU_H */
