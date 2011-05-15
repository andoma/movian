/*
 *  Subtitles decoder / rendering
 *  Copyright (C) 2007 - 2011 Andreas Öman
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
#include "text/text.h"
#include "misc/pixmap.h"
#include "misc/string.h"
#include "video_overlay.h"
#include "video_settings.h"
#include "sub.h"

void
video_overlay_enqueue(video_decoder_t *vd, video_overlay_t *vo)
{
  hts_mutex_lock(&vd->vd_overlay_mutex);
  TAILQ_INSERT_TAIL(&vd->vd_overlay_queue, vo, vo_link);
  hts_mutex_unlock(&vd->vd_overlay_mutex);
}


/**
 * Decode subtitles from LAVC
 */
static void
video_subtitles_lavc(video_decoder_t *vd, media_buf_t *mb,
		     AVCodecContext *ctx)
{
  AVSubtitle sub;
  int size = 0, i, x, y;
  video_overlay_t *vo;

  AVPacket avpkt;
  av_init_packet(&avpkt);
  avpkt.data = mb->mb_data;
  avpkt.size = mb->mb_size;

  if(avcodec_decode_subtitle2(ctx, &sub, &size, &avpkt) < 1 || size < 1) 
    return;

  for(i = 0; i < sub.num_rects; i++) {
    AVSubtitleRect *r = sub.rects[i];

    switch(r->type) {

    case SUBTITLE_BITMAP:
      vo = calloc(1, sizeof(video_overlay_t));

      vo->vo_start = mb->mb_pts + sub.start_display_time * 1000;
      vo->vo_stop  = mb->mb_pts + sub.end_display_time * 1000;
		  
      vo->vo_x = r->x;
      vo->vo_y = r->y;
      vo->vo_w = r->w;
      vo->vo_h = r->h;

      const uint8_t *src = r->pict.data[0];
      const uint32_t *clut = (uint32_t *)r->pict.data[1];

      vo->vo_bitmap = malloc(sizeof(uint32_t) * r->w * r->h);
      uint32_t *dst = (uint32_t *)vo->vo_bitmap;
      
      for(y = 0; y < r->h; y++) {
	for(x = 0; x < r->w; x++) {
	  *dst++ = clut[src[x]];
	}
	src += r->pict.linesize[0];
      }
      video_overlay_enqueue(vd, vo);
      break;

    case SUBTITLE_ASS:
      sub_ass_render(vd, r->ass,
		     ctx->subtitle_header, ctx->subtitle_header_size);
      break;

    default:
      break;
    }
  }
}


/**
 * This is crap. video_overlay contain pixmap instead
 */
video_overlay_t *
video_overlay_from_pixmap(const pixmap_t *pm)
{
  video_overlay_t *vo = calloc(1, sizeof(video_overlay_t));
  vo->vo_w = pm->pm_width;
  vo->vo_h = pm->pm_height;

  const uint8_t *src = pm->pm_pixels;
  vo->vo_bitmap = malloc(sizeof(uint32_t) * vo->vo_w * vo->vo_h);
  uint32_t *dst = (uint32_t *)vo->vo_bitmap;
  
  int x, y;

  switch(pm->pm_pixfmt) {
  case PIX_FMT_BGR32:
    
    for(y = 0; y < vo->vo_h; y++) {
      memcpy(dst, src, vo->vo_w * sizeof(uint32_t));
      dst += vo->vo_w;
      src += pm->pm_linesize;
    }
    break;

  case PIX_FMT_Y400A:
    for(y = 0; y < vo->vo_h; y++) {
      for(x = 0; x < vo->vo_w; x++) {
	uint8_t i = src[x * 2 + 0];
	uint8_t a = src[x * 2 + 1];
	*dst++ = i * 0x00010101 + (a << 24);
      }
      src += pm->pm_linesize;
    }
    break;

  case PIX_FMT_GRAY8:
    for(y = 0; y < vo->vo_h; y++) {
      for(x = 0; x < vo->vo_w; x++) {
	uint8_t i = src[x];
	*dst++ = i * 0x01010101;
      }
      src += pm->pm_linesize;
    }
    break;


  default:
    break;
  }
  return vo;
}


/**
 *
 */
static pixmap_t *
video_overlay_postprocess(pixmap_t *src)
{
  int shadow = 2;

  pixmap_t *alpha = pixmap_extract_channel(src, 3);

  pixmap_t *edges = pixmap_convolution_filter(alpha, PIXMAP_EDGE_DETECT);
  
  pixmap_t *blur = pixmap_convolution_filter(alpha, PIXMAP_BLUR);

  pixmap_t *dst = pixmap_create(src->pm_width  + shadow,
				src->pm_height + shadow,
				PIX_FMT_BGR32);
  
  pixmap_composite(dst, blur, shadow, shadow, 0, 0, 0, 208);
  pixmap_release(blur);

  pixmap_composite(dst, src, 0, 0, 255, 255, 255, 255);

  pixmap_composite(dst, edges, 0, 0, 0, 0, 0, 255);
  pixmap_release(edges);

  pixmap_release(src);

  pixmap_release(alpha);

  return dst;

}


/**
 *
 */
void
video_overlay_render_cleartext(video_decoder_t *vd, const char *txt,
			       int64_t start, int64_t stop, int tags)
{
  uint32_t *uc;
  int len;
  struct pixmap *pm;
  video_overlay_t *vo;
  const media_pipe_t *mp = vd->vd_mp;
  int vwidth  = mp->mp_video_width;
  int vheight = mp->mp_video_height;

  if(vwidth < 10 || vheight < 10)
    return;

  uc = text_parse(txt, &len, 
		  tags ? (TEXT_PARSE_TAGS | TEXT_PARSE_HTML_ENTETIES) : 0);
  if(uc == NULL)
    return;

  int margin_x = vwidth / 10;
  int maxwidth = vwidth - (margin_x * 2);
  int fontsize = vheight / 20;
  int flags = 0;

  if(subtitle_alignment == SUBTITLE_ALIGNMENT_CENTER)
    flags |= TR_RENDER_CENTERED;

  fontsize = fontsize * subtitle_scaling / 100;

  pm = text_render(uc, len, flags, fontsize, maxwidth, 10, NULL);

  pm = video_overlay_postprocess(pm);
  vo = video_overlay_from_pixmap(pm);

  vo->vo_start = start;
  vo->vo_stop = stop;


  switch(subtitle_alignment) {
  default:
    vo->vo_x = vwidth / 2 - pm->pm_width / 2;
    break;

  case SUBTITLE_ALIGNMENT_LEFT:
    vo->vo_x = margin_x;
    break;
 
  case SUBTITLE_ALIGNMENT_RIGHT:
    vo->vo_x = vwidth - pm->pm_width - margin_x;
    break;
  }

  vo->vo_y = vheight - pm->pm_height - (fontsize / 2);
  video_overlay_enqueue(vd, vo);
  pixmap_release(pm);
}


/**
 *
 */
void 
video_overlay_decode(video_decoder_t *vd, media_buf_t *mb)
{
  media_codec_t *cw = mb->mb_cw;
  
  if(cw == NULL) {

    int offset = 0;
    char *str;

    if(mb->mb_codecid == CODEC_ID_MOV_TEXT) {
      if(mb->mb_size < 2)
	return;
      offset = 2;
    }

    str = malloc(mb->mb_size + 1 - offset);
    memcpy(str, mb->mb_data + offset, mb->mb_size - offset);
    str[mb->mb_size - offset] = 0;

    video_overlay_render_cleartext(vd, str, mb->mb_pts,
				   mb->mb_duration ?
				   mb->mb_pts + mb->mb_duration :
				   AV_NOPTS_VALUE, 0);
  } else {
    video_subtitles_lavc(vd, mb, cw->codec_ctx);
  }
}


/**
 *
 */
void
video_overlay_destroy(video_decoder_t *vd, video_overlay_t *vo)
{
  TAILQ_REMOVE(&vd->vd_overlay_queue, vo, vo_link);
  free(vo->vo_bitmap);
  free(vo);
}


/**
 *
 */
void
video_overlay_flush(video_decoder_t *vd, int send)
{
  video_overlay_t *vo;

  while((vo = TAILQ_FIRST(&vd->vd_overlay_queue)) != NULL)
    video_overlay_destroy(vd, vo);

  if(!send)
    return;

  vo = calloc(1, sizeof(video_overlay_t));
  vo->vo_type = VO_FLUSH;
  video_overlay_enqueue(vd, vo);
}
