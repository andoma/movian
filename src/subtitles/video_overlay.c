/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
#include "main.h"
#include "media/media.h"
#include "text/text.h"
#include "image/pixmap.h"
#include "misc/str.h"
#include "video_overlay.h"
#include "dvdspu.h"
#include "sub.h"
#include "subtitles/subtitles.h"

void
video_overlay_enqueue(media_pipe_t *mp, video_overlay_t *vo)
{
  hts_mutex_lock(&mp->mp_overlay_mutex);
  TAILQ_INSERT_TAIL(&mp->mp_overlay_queue, vo, vo_link);
  hts_mutex_unlock(&mp->mp_overlay_mutex);
}

#if ENABLE_LIBAV

/**
 * Decode subtitles from LAVC
 */
static void
video_subtitles_lavc(media_pipe_t *mp, media_buf_t *mb,
		     AVCodecContext *ctx)
{
  AVSubtitle sub;
  int got_sub = 0, i, x, y;
  video_overlay_t *vo;

  AVPacket avpkt;
  av_init_packet(&avpkt);
  avpkt.data = mb->mb_data;
  avpkt.size = mb->mb_size;

  vo = calloc(1, sizeof(video_overlay_t));
  vo->vo_type = VO_TIMED_FLUSH;
  vo->vo_start = mb->mb_pts;
  video_overlay_enqueue(mp, vo);

  if(avcodec_decode_subtitle2(ctx, &sub, &got_sub, &avpkt) < 1 || !got_sub)
    return;

  if(sub.num_rects == 0)
  {
    // Flush screen
    vo = calloc(1, sizeof(video_overlay_t));
    vo->vo_type = VO_TIMED_FLUSH;
    vo->vo_start = mb->mb_pts + sub.start_display_time * 1000;
    video_overlay_enqueue(mp, vo);
  }
  else
  {

    for(i = 0; i < sub.num_rects; i++) {
      AVSubtitleRect *r = sub.rects[i];

      switch(r->type) {

      case SUBTITLE_BITMAP:
		vo = calloc(1, sizeof(video_overlay_t));

		vo->vo_start = mb->mb_pts + sub.start_display_time * 1000;
		vo->vo_stop  = mb->mb_pts + sub.end_display_time * 1000;

		vo->vo_canvas_width  = ctx->width ? ctx->width : 720;
		vo->vo_canvas_height = ctx->height ? ctx->height : 576;

		vo->vo_x = r->x;
		vo->vo_y = r->y;

		vo->vo_pixmap = pixmap_create(r->w, r->h, PIXMAP_BGR32, 0);

		if(vo->vo_pixmap == NULL) {
		  free(vo);
		  break;
		}

		const uint8_t *src = r->pict.data[0];
		const uint32_t *clut = (uint32_t *)r->pict.data[1];

		for(y = 0; y < r->h; y++) {
		  uint32_t *dst = (uint32_t *)(vo->vo_pixmap->pm_data +
						   y * vo->vo_pixmap->pm_linesize);
		  for(x = 0; x < r->w; x++)
			*dst++ = clut[src[x]];

		  src += r->pict.linesize[0];
		}
		video_overlay_enqueue(mp, vo);
		break;

		  case SUBTITLE_ASS:
		sub_ass_render(mp, r->ass,
				   ctx->subtitle_header, ctx->subtitle_header_size,
				   mb->mb_font_context);
		break;

		  default:
		break;
      }
    }
  }
  avsubtitle_free(&sub);
}

#endif

extern char font_subs[];

/**
 *
 */
video_overlay_t *
video_overlay_render_cleartext(const char *txt, int64_t start, int64_t stop,
			       int tags, int fontdomain)
{
  uint32_t *uc;
  int len, txt_len;
  video_overlay_t *vo;

  txt_len = strlen(txt);

  if(txt_len == 0) {
    vo = calloc(1, sizeof(video_overlay_t));
  } else {

    uint32_t pfx[6];

    pfx[0] = TR_CODE_COLOR | subtitle_settings.color;
    pfx[1] = TR_CODE_SHADOW | subtitle_settings.shadow_displacement;
    pfx[2] = TR_CODE_SHADOW_COLOR | subtitle_settings.shadow_color;
    pfx[3] = TR_CODE_OUTLINE | subtitle_settings.outline_size;
    pfx[4] = TR_CODE_OUTLINE_COLOR | subtitle_settings.outline_color;
    int pfxlen = 5;

    if(font_subs[0])
      pfx[pfxlen++] = TR_CODE_FONT_FAMILY |
	freetype_family_id(font_subs, fontdomain);

    uc = text_parse(txt, &len, tags, pfx, pfxlen, fontdomain);
    if(uc == NULL)
      return NULL;

    vo = calloc(1, sizeof(video_overlay_t));
    vo->vo_type = VO_TEXT;
    vo->vo_text = uc;
    vo->vo_text_length = len;
    vo->vo_padding_left = -1;  // auto padding
  }

  if(stop == PTS_UNSET) {
    stop = start + calculate_subtitle_duration(txt_len) * 1000000;
    vo->vo_stop_estimated = 1;
  }

  vo->vo_start = start;
  vo->vo_stop = stop;
  return vo;
}

/**
 * Calculate the number of seconds a subtitle should be displayed.
 * Min 2 seconds, max 7 seconds.
 */
int
calculate_subtitle_duration(int txt_len)
{
  return 2 + (txt_len / 74.0F) * 5; //74 is the maximum amount of characters a subtitler may fit on 2 lines of text.
}


/**
 *
 */
void
video_overlay_decode(media_pipe_t *mp, media_buf_t *mb)
{
  media_codec_t *mc = mb->mb_cw;

  if(mc == NULL) {

    int offset = 0;
    char *str;

    if(mb->mb_codecid == AV_CODEC_ID_MOV_TEXT) {
      if(mb->mb_size < 2)
	return;
      offset = 2;
    }

    str = malloc(mb->mb_size + 1 - offset);
    memcpy(str, mb->mb_data + offset, mb->mb_size - offset);
    str[mb->mb_size - offset] = 0;

    video_overlay_t *vo;
    vo = video_overlay_render_cleartext(str, mb->mb_pts,
					mb->mb_duration ?
					mb->mb_pts + mb->mb_duration :
					PTS_UNSET,
                                        TEXT_PARSE_HTML_TAGS |
                                        TEXT_PARSE_HTML_ENTITIES |
                                        TEXT_PARSE_SLOPPY_TAGS,
					mb->mb_font_context);

    if(vo != NULL)
      video_overlay_enqueue(mp, vo);

    free(str);

  } else {

    if(mc->decode)
      mc->decode(mc, NULL, NULL, mb, 0);
#if ENABLE_LIBAV
    else
      video_subtitles_lavc(mp, mb, mc->ctx);
#endif
  }
}


/**
 *
 */
void
video_overlay_destroy(video_overlay_t *vo)
{
  if(vo->vo_pixmap != NULL)
    pixmap_release(vo->vo_pixmap);
  free(vo->vo_text);
  free(vo);
}


/**
 *
 */
video_overlay_t *
video_overlay_dup(video_overlay_t *src)
{
  video_overlay_t *dst = malloc(sizeof(video_overlay_t));
  memcpy(dst, src, sizeof(video_overlay_t));

  if(src->vo_pixmap)
    dst->vo_pixmap = pixmap_dup(src->vo_pixmap);

  if(src->vo_text) {
    dst->vo_text = malloc(src->vo_text_length * sizeof(uint32_t));
    memcpy(dst->vo_text, src->vo_text, src->vo_text_length * sizeof(uint32_t));
  }
  return dst;
}

/**
 *
 */
void
video_overlay_dequeue_destroy(media_pipe_t *mp, video_overlay_t *vo)
{
  TAILQ_REMOVE(&mp->mp_overlay_queue, vo, vo_link);
  video_overlay_destroy(vo);
}


/**
 *
 */
void
video_overlay_flush_locked(media_pipe_t *mp, int send)
{
  video_overlay_t *vo;

  while((vo = TAILQ_FIRST(&mp->mp_overlay_queue)) != NULL)
    video_overlay_dequeue_destroy(mp, vo);

  if(!send)
    return;

  vo = calloc(1, sizeof(video_overlay_t));
  vo->vo_type = VO_FLUSH;
  TAILQ_INSERT_TAIL(&mp->mp_overlay_queue, vo, vo_link);
}
