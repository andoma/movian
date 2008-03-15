/*
 *  Playback of audio for playlist
 *  Copyright (C) 2007-2008 Andreas Öman
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

#include <sys/stat.h>
#include <sys/time.h>
#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <libgen.h>

#include <libavformat/avformat.h>
#include <libglw/glw.h>

#include "showtime.h"
#include "input.h"
#include "playlist.h"




  


static int
playlist_play(playlist_entry_t *ple, media_pipe_t *mp, ic_t *ic)
{
  AVFormatContext *fctx;
  AVCodecContext *ctx;
  AVPacket pkt;
  formatwrap_t *fw;
  int i, key;
  media_buf_t *mb;
  media_queue_t *mq;
  int64_t pts4seek = 0;
  int streams;
  int64_t cur_pos_pts = AV_NOPTS_VALUE;

  codecwrap_t *cw;

  if(av_open_input_file(&fctx, ple->ple_url, NULL, 0, NULL) != 0) {
    fprintf(stderr, "Unable to open input file\n");
    return -1;
  }

  if(av_find_stream_info(fctx) < 0) {
    av_close_input_file(fctx);
    fprintf(stderr, "Unable to find stream info\n");
    return INPUT_KEY_NEXT;
  }

  mp->mp_audio.mq_stream = -1;
  mp->mp_video.mq_stream = -1;

  fw = wrap_format_create(fctx, 1);

  cw = NULL;
  for(i = 0; i < fctx->nb_streams; i++) {
    ctx = fctx->streams[i]->codec;

    if(ctx->codec_type != CODEC_TYPE_AUDIO)
      continue;

    cw = wrap_codec_create(ctx->codec_id, ctx->codec_type, 0, fw, ctx);
    mp->mp_audio.mq_stream = i;
    break;
  }

  mp->mp_format = fctx;

  wrap_lock_all_codecs(fw);
  media_pipe_acquire_audio(mp);
  wrap_unlock_all_codecs(fw);

  while(1) {

 
    if(mp_is_paused(mp)) {
      key = input_getkey(ic, 1);
      media_pipe_acquire_audio(mp);
    } else {
      key = input_getkey(ic, 0);
    }

    switch(key) {

    case INPUT_KEY_SEEK_FAST_BACKWARD:
      av_seek_frame(fctx, -1, pts4seek - 60000000, AVSEEK_FLAG_BACKWARD);
    case INPUT_KEY_SEEK_BACKWARD:
      av_seek_frame(fctx, -1, pts4seek - 15000000, AVSEEK_FLAG_BACKWARD);
      goto seekflush;
    case INPUT_KEY_SEEK_FAST_FORWARD:
      av_seek_frame(fctx, -1, pts4seek + 60000000, 0);
      goto seekflush;
    case INPUT_KEY_SEEK_FORWARD:
      av_seek_frame(fctx, -1, pts4seek + 15000000, 0);
      goto seekflush;

    seekflush:
      mp_flush(mp);
      mp_auto_display(mp);
      input_flush_queue(ic);
      key = 0;
      break;

    case INPUT_KEY_PLAYPAUSE:
    case INPUT_KEY_PLAY:
    case INPUT_KEY_PAUSE:
      mp_playpause(mp, key);
      key = 0;
      break;

    default:
      key = 0;
      break;
    }
    
    if(key)
      break;

    if(mp_is_paused(mp))
      continue;

    mb = media_buf_alloc();

    i = av_read_frame(fctx, &pkt);

    if(i < 0) {
      mp_wait(mp, mp->mp_audio.mq_stream != -1, mp->mp_video.mq_stream != -1);
      key = 0;
      break;
    }

    mb->mb_data = NULL;
    mb->mb_size = pkt.size;

    if(pkt.pts != AV_NOPTS_VALUE) {
      mb->mb_pts = av_rescale_q(pkt.pts,
				fctx->streams[pkt.stream_index]->time_base,
				AV_TIME_BASE_Q);
      pts4seek = mb->mb_pts;
    } else {
      mb->mb_pts = AV_NOPTS_VALUE;
    }
    
    mb->mb_duration = av_rescale_q(pkt.duration,
				   fctx->streams[pkt.stream_index]->time_base,
				   AV_TIME_BASE_Q);

    if(pkt.stream_index == mp->mp_audio.mq_stream) {
      ctx = cw->codec_ctx;

      if(mb->mb_pts != AV_NOPTS_VALUE) {
	if(fctx->start_time != AV_NOPTS_VALUE)
	  cur_pos_pts = mb->mb_pts - fctx->start_time;
	else
	  cur_pos_pts = mb->mb_pts;
      }

      if(cur_pos_pts != AV_NOPTS_VALUE)
	mb->mb_time = cur_pos_pts / AV_TIME_BASE;
      else
	mb->mb_time = 0;

      mb->mb_data_type = MB_AUDIO;
      mb->mb_cw = wrap_codec_ref(cw);

      mb->mb_data = malloc(pkt.size +  FF_INPUT_BUFFER_PADDING_SIZE);
      memset(mb->mb_data + pkt.size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
 
      memcpy(mb->mb_data, pkt.data, pkt.size);
      mq = &mp->mp_audio;
    } else {
      mq = NULL;
    }

    if(mq != NULL) {
      mb_enqueue(mp, mq, mb);
      av_free_packet(&pkt);
      continue;
    }
    
    av_free_packet(&pkt);

    if(mb->mb_data != NULL)
      free(mb->mb_data);
    free(mb);

  }

  wrap_lock_all_codecs(fw);

  mp->mp_total_time = 0;

  streams = fctx->nb_streams;

  wrap_codec_deref(cw, 0);

  wrap_format_wait(fw);
  return key;
}





/**
 *
 */
void *
playlist_player(void *aux)
{
  playlist_player_t *plp = aux;
  media_pipe_t *mp = plp->plp_mp;

  playlist_entry_t *ple = NULL;
  inputevent_t ie;
  int key;

  while(1) {
 
    while(ple == NULL) {

      /* Got nothing to play, enter STOP mode */

      mp_set_playstatus(mp, MP_STOP);

      printf("Playlist waiting...\n");
      input_getevent(&plp->plp_ic, 1, &ie, NULL);

      switch(ie.type) {
      default:
	continue;

      case PLAYLIST_INPUTEVENT_NEWENTRY:
	/**
	 * A new entry has been enqueued, a reference is held for us
	 */
	ple = ie.u.ptr;
	printf("playlist got ple %s\n", ple->ple_url);
	break;
      }
    }
    mp_set_playstatus(mp, MP_PLAY);

    key = playlist_play(ple, mp, &plp->plp_ic);

    ple = playlist_advance(ple, 0, 0);
  }
}
