/*
 *  Subtitles decoder / rendering
 *  Copyright (C) 2007 - 2010 Andreas Öman
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
#include "showtime.h"
#include "video_decoder.h"



/**
 * Decode subtitles from LAVC
 */
static void
video_subtitles_decode_lavc(video_decoder_t *vd, media_buf_t *mb,
			    AVCodecContext *ctx)
{
  AVSubtitle sub;
  int size = 0, i, x, y;
  subtitle_t *s;

  if(avcodec_decode_subtitle(ctx, &sub, &size, mb->mb_data, mb->mb_size) < 1 ||
     size < 1) 
    return;

  s = malloc(sizeof(subtitle_t) + sizeof(subtitle_rect_t) * sub.num_rects);

  s->s_active = 0;
  s->s_text = NULL;
  s->s_start = mb->mb_pts + sub.start_display_time * 1000;
  s->s_stop  = mb->mb_pts + sub.end_display_time * 1000;
  s->s_num_rects = sub.num_rects;

  for(i = 0; i < sub.num_rects; i++) {
    AVSubtitleRect *r = sub.rects[i];
    subtitle_rect_t *sr = &s->s_rects[i];

    switch(r->type) {

    case SUBTITLE_BITMAP:
      sr->x = r->x;
      sr->y = r->y;
      sr->w = r->w;
      sr->h = r->h;

      const uint8_t *src = r->pict.data[0];
      const uint32_t *clut = (uint32_t *)r->pict.data[1];

      sr->bitmap = malloc(4 * r->w * r->h);
      uint32_t *dst = (uint32_t *)sr->bitmap;
      
      for(y = 0; y < r->h; y++) {
	for(x = 0; x < r->w; x++) {
	  *dst++ = clut[src[x]];
	}
	src += r->pict.linesize[0];
      }
      break;

    default:
      sr->x = 0;
      sr->y = 0;
      sr->w = 0;
      sr->h = 0;
      sr->bitmap = NULL;
      break;
    }
  }

  hts_mutex_lock(&vd->vd_sub_mutex);
  TAILQ_INSERT_TAIL(&vd->vd_sub_queue, s, s_link);
  hts_mutex_unlock(&vd->vd_sub_mutex);
}



/**
 *
 */
static void
video_subtitles_cleartext(video_decoder_t *vd, media_buf_t *mb)
{
  subtitle_t *s = malloc(sizeof(subtitle_t));

  s->s_active = 0;
  s->s_text = strdup(mb->mb_data);
  s->s_start = mb->mb_pts;
  if(mb->mb_duration)
    s->s_stop = mb->mb_pts + mb->mb_duration;
  else
    s->s_stop = AV_NOPTS_VALUE;
  s->s_num_rects = 0;

  hts_mutex_lock(&vd->vd_sub_mutex);
  TAILQ_INSERT_TAIL(&vd->vd_sub_queue, s, s_link);
  hts_mutex_unlock(&vd->vd_sub_mutex);
}


/**
 *
 */
void 
video_subtitles_decode(video_decoder_t *vd, media_buf_t *mb)
{
  media_codec_t *cw = mb->mb_cw;

  if(cw != NULL)
    video_subtitles_decode_lavc(vd, mb, cw->codec_ctx);
  else
    video_subtitles_cleartext(vd, mb);
}

/**
 *
 */
void
video_subtitle_destroy(video_decoder_t *vd, subtitle_t *s)
{
  int i;

  TAILQ_REMOVE(&vd->vd_sub_queue, s, s_link);

  for(i = 0; i < s->s_num_rects; i++)
    free(s->s_rects[i].bitmap);

  free(s->s_text);
  free(s);
}


/**
 *
 */
void
video_subtitles_init(video_decoder_t *vd)
{
  TAILQ_INIT(&vd->vd_sub_queue);
  hts_mutex_init(&vd->vd_sub_mutex);
}


/**
 *
 */
void
video_subtitles_deinit(video_decoder_t *vd)
{
  subtitle_t *s;

  while((s = TAILQ_FIRST(&vd->vd_sub_queue)) != NULL)
    video_subtitle_destroy(vd, s);

  hts_mutex_destroy(&vd->vd_sub_mutex);
}
