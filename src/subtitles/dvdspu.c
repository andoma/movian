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

#include <inttypes.h>

#include "showtime.h"
#include "media.h"
#include "vobsub.h"
#include "dvdspu.h"

/**
 *
 */
static inline uint16_t
getbe16(const uint8_t *p)
{
  return (p[0] << 8) | p[1];
}


/**
 *
 */
static int
get_nibble(const uint8_t *buf, int nibble_offset)
{
  return (buf[nibble_offset >> 1] >> ((1 - (nibble_offset & 1)) << 2)) & 0xf;
}

/**
 *
 */
static int
decode_rle(uint8_t *bitmap, int linesize, int w, int h, 
	   const uint8_t *buf, int nibble_offset, int buf_size)
{
  unsigned int v;
  int x, y, len, color, nibble_end;
  uint8_t *d;

  nibble_end = buf_size * 2;
  x = 0;
  y = 0;
  d = bitmap;
  for(;;) {
    if (nibble_offset >= nibble_end)
      return -1;
    v = get_nibble(buf, nibble_offset++);
    if (v < 0x4) {
      v = (v << 4) | get_nibble(buf, nibble_offset++);
      if (v < 0x10) {
	v = (v << 4) | get_nibble(buf, nibble_offset++);
	if (v < 0x040) {
	  v = (v << 4) | get_nibble(buf, nibble_offset++);
	  if (v < 4) {
	    v |= (w - x) << 2;
	  }
	}
      }
    }
    len = v >> 2;
    if (len > (w - x))
      len = (w - x);
    color = v & 0x03;

    memset(d + x, color, len);
    x += len;
    if (x >= w) {
      y++;
      if (y >= h)
	break;
      d += linesize;
      x = 0;
      /* byte align */
      nibble_offset += (nibble_offset & 1);
    }
  }
  return 0;
}


/**
 *
 */
int
dvdspu_decode(dvdspu_t *d, int64_t pts)
{
  int retval = 0;
  uint8_t *buf = d->d_data;
  int date;
  int64_t picts;
  int stop;
  int pos, cmd, x1, y1, x2, y2, offset1, offset2, next_cmd_pos;


  if(d->d_cmdpos == -1)
    return TAILQ_NEXT(d, d_link) != NULL ? -1 : 0;
  
  while(d->d_cmdpos + 4 < d->d_size) {
    date = getbe16(buf + d->d_cmdpos);
    picts = d->d_pts + ((date << 10) / 90) * 1000;

    if(pts != PTS_UNSET && pts < picts)
      return retval;

    next_cmd_pos = getbe16(buf + d->d_cmdpos + 2);


    pos = d->d_cmdpos + 4;
    offset1 = -1;
    offset2 = -1;
    x1 = y1 = x2 = y2 = 0;
    stop = 0;

    while(!stop && pos < d->d_size) {
      cmd = buf[pos++];
      switch(cmd) {
      case 0x00:
	break;

      case 0x01:
	/* Start of picture */
	break;
      case 0x02:
	/* End of picture */
	return -1;

      case 0x03:
	/* set palette */
	if(d->d_size - pos < 2)
	  return -1;

	d->d_palette[3] = buf[pos] >> 4;
	d->d_palette[2] = buf[pos] & 0x0f;
	d->d_palette[1] = buf[pos + 1] >> 4;
	d->d_palette[0] = buf[pos + 1] & 0x0f;

	retval = 1;
	pos += 2;
	break;

      case 0x04:
	/* set alpha */
	if(d->d_size - pos < 2)
	  return -1;

	d->d_alpha[3] = buf[pos] >> 4;
	d->d_alpha[2] = buf[pos] & 0x0f;
	d->d_alpha[1] = buf[pos + 1] >> 4;
	d->d_alpha[0] = buf[pos + 1] & 0x0f;

	retval = 1;
	pos += 2;
	break;

      case 0x05:
	if(d->d_size - pos < 6)
	  return -1;

	x1 = (buf[pos] << 4) | (buf[pos + 1] >> 4);
	x2 = ((buf[pos + 1] & 0x0f) << 8) | buf[pos + 2];
	y1 = (buf[pos + 3] << 4) | (buf[pos + 4] >> 4);
	y2 = ((buf[pos + 4] & 0x0f) << 8) | buf[pos + 5];

	pos += 6;
	break;

      case 0x06:
	if(d->d_size - pos < 4)
	  return -1;

	offset1 = getbe16(buf + pos);
	offset2 = getbe16(buf + pos + 2);

	pos += 4;
	break;

      case 0x07:

	/* This is blindly reverse-engineered */

	d->d_alpha[3] = buf[pos + 10] >> 4;
	d->d_alpha[2] = buf[pos + 10] & 0x0f;
	d->d_alpha[1] = buf[pos + 11] >> 4;
	d->d_alpha[0] = buf[pos + 11] & 0x0f;

	retval = 1;
	stop = 1;
	break;
	      
      default:
	stop = 1;
	break;
      }
    }

    if(offset1 >= 0 && (x2 - x1 + 1) > 0 && (y2 - y1) > 0) {

      int width  = x2 - x1 + 1;
      int height = y2 - y1;

      d->d_x1 = x1;
      d->d_x2 = x2 + 1;
      d->d_y1 = y1;
      d->d_y2 = y2;
      
      if(d->d_bitmap != NULL)
	free(d->d_bitmap);

      d->d_bitmap = malloc(width * height);
      
      decode_rle(d->d_bitmap, width * 2, width, height / 2 + (height & 1),
		 buf, offset1 * 2, d->d_size);

      decode_rle(d->d_bitmap + width, width * 2, width, height / 2,
		 buf, offset2 * 2, d->d_size);
    }

    if(next_cmd_pos == d->d_cmdpos) {
      d->d_cmdpos = -1;
      break;
    }
    d->d_cmdpos = next_cmd_pos;
  }

  return retval;
}


/**
 *
 */
static uint32_t 
yuv_to_rgb(uint32_t u32)
{
  int C, D, E, Y, U, V, R, G, B;

  Y = (u32 >> 16) & 0xff;
  V = (u32 >> 8) & 0xff;
  U = (u32 >> 0) & 0xff;

  C = Y - 16;
  D = U - 128;
  E = V - 128;

  R = ( 298 * C           + 409 * E + 128) >> 8;
  G = ( 298 * C - 100 * D - 208 * E + 128) >> 8;
  B = ( 298 * C + 516 * D           + 128) >> 8;

#define clip256(x) ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))

  R = clip256(R);
  G = clip256(G);
  B = clip256(B);

  return R << 0 | G << 8 | B << 16;
}

/**
 *
 */
void
dvdspu_decode_clut(uint32_t *dst, const uint32_t *src)
{
  int i;

  for(i = 0; i < 16; i++)
    dst[i] = yuv_to_rgb(src[i]);
}


/**
 *
 */
void
dvdspu_enqueue(media_pipe_t *mp, const void *data, int size, 
	       const uint32_t *clut, int width, int height, int64_t pts)
{
  dvdspu_t *d;

  if(size < 4)
    return;

  d = calloc(1, sizeof(dvdspu_t) + size);
  memcpy(d->d_clut, clut, 16 * sizeof(uint32_t));
  memcpy(d->d_data, data, size);

  d->d_size = size;
  d->d_cmdpos = getbe16(d->d_data + 2);
  d->d_pts = pts;
  d->d_canvas_width  = width;
  d->d_canvas_height = height;

  hts_mutex_lock(&mp->mp_overlay_mutex);
  TAILQ_INSERT_TAIL(&mp->mp_spu_queue, d, d_link);
  hts_mutex_unlock(&mp->mp_overlay_mutex);
}

/**
 *
 */
void
dvdspu_destroy_one(media_pipe_t *mp, dvdspu_t *d)
{
  TAILQ_REMOVE(&mp->mp_spu_queue, d, d_link);
  free(d->d_bitmap);
  free(d);
}


/**
 *
 */
void
dvdspu_destroy_all(media_pipe_t *mp)
{
  dvdspu_t *d;

  while((d = TAILQ_FIRST(&mp->mp_spu_queue)) != NULL)
    dvdspu_destroy_one(mp, d);
}


/**
 *
 */
void
dvdspu_flush(media_pipe_t *mp)
{
  dvdspu_t *d;

  hts_mutex_lock(&mp->mp_overlay_mutex);

  TAILQ_FOREACH(d, &mp->mp_spu_queue, d_link)
    d->d_destroyme = 1;

  hts_mutex_unlock(&mp->mp_overlay_mutex);
}


/**
 *
 */
typedef struct {
  uint32_t clut[16];
  int w, h;
} dvdspu_codec_t;



/**
 *
 */
static void
dvdspu_codec_decode(struct media_codec *mc, struct video_decoder *vd,
		    struct media_queue *mq, struct media_buf *mb, int reqsize)
{
  dvdspu_codec_t *dc = mc->opaque;
  dvdspu_enqueue(mc->mp, mb->mb_data, mb->mb_size, dc->clut, dc->w, dc->h,
		 mb->mb_pts);
}


/**
 *
 */
static void
dvdspu_codec_close(struct media_codec *mc)
{
  dvdspu_codec_t *dc = mc->opaque;
  free(dc);
}



/**
 *
 */
static int
dvdspu_codec_create(media_codec_t *mc, int id,
		    const media_codec_params_t *mcp,
                    media_pipe_t *mp)
{
  if(id != CODEC_ID_DVD_SUBTITLE)
    return 1;

  if(mcp->extradata_size == 0)
    return 1;
  
  dvdspu_codec_t *dc = calloc(1, sizeof(dvdspu_codec_t));

  char *s = strdup((const char *)mcp->extradata);
  char *sf = s;
  int l;

  for(; l = strcspn(s, "\r\n"), *s; s += l+1+strspn(s+l+1, "\r\n")) {
    const char *p;
    s[l] = 0;
    if((p = mystrbegins(s, "palette:")) != NULL)
      vobsub_decode_palette(dc->clut, p);
    if((p = mystrbegins(s, "size:")) != NULL)
      vobsub_decode_size(&dc->w, &dc->h, p);
  }

  mc->opaque = dc;
  mc->decode = dvdspu_codec_decode;
  mc->close = dvdspu_codec_close;

  free(sf);

  return 0;
}


REGISTER_CODEC(NULL, dvdspu_codec_create, 50);
