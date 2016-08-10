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
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <libavutil/mem.h>
#include <libswscale/swscale.h>

#include "main.h"
#include "video_decoder.h"
#include "event.h"
#include "media/media.h"
#include "misc/sha.h"
#include "libav.h"

#include "subtitles/ext_subtitles.h"
#include "subtitles/video_overlay.h"
#include "subtitles/dvdspu.h"



static void
vd_init_timings(video_decoder_t *vd)
{
  vd->vd_prevpts = AV_NOPTS_VALUE;
  vd->vd_nextpts = AV_NOPTS_VALUE;
  vd->vd_estimated_duration = 0;

  for(int i = 0; i < VD_FRAME_SIZE_LEN; i++)
    vd->vd_fps_array[i] = PTS_UNSET;

  vd->vd_fps_delta = 0;
}


/**
 *
 */
int64_t 
video_decoder_infer_pts(const media_buf_meta_t *mbm,
			video_decoder_t *vd,
			int is_bframe)
{
  if(is_bframe)
    vd->vd_seen_bframe = 100;

  if(vd->vd_seen_bframe)
    vd->vd_seen_bframe--;
    
  if(mbm->mbm_pts == PTS_UNSET && mbm->mbm_dts != PTS_UNSET &&
     (!vd->vd_seen_bframe || is_bframe))
    return mbm->mbm_dts;

  return mbm->mbm_pts;
}


/**
 *
 */
void
video_decoder_set_current_time(video_decoder_t *vd, int64_t user_time,
			       int epoch, int64_t pts, int drive_mode)
{
  int64_t lastpts = vd->vd_fps_array[vd->vd_fps_ptr];
  vd->vd_fps_array[vd->vd_fps_ptr] = pts;

  vd->vd_fps_ptr = (vd->vd_fps_ptr + 1) & (ARRAYSIZE(vd->vd_fps_array) - 1);

  if(pts != PTS_UNSET && lastpts != PTS_UNSET) {
    int64_t delta = pts - lastpts;

    if(delta < 10000000LL && delta > 10000LL) {
      if(vd->vd_fps_delta)
        vd->vd_fps_delta += (delta - vd->vd_fps_delta) >> 4;
      else
        vd->vd_fps_delta = delta;
      int64_t tmp = 100000000LL * VD_FRAME_SIZE_LEN / vd->vd_fps_delta;
      float fps = tmp / 100.0;

      if(fps != vd->vd_fps) {
        vd->vd_fps = fps;
        prop_set_float(vd->vd_mp->mp_prop_fps, vd->vd_fps);
      }
    }
  }


  if(drive_mode == 2) {
    if(pts == PTS_UNSET || user_time == PTS_UNSET)
      return;
    user_time = pts - user_time;
  } else {
    if(user_time == PTS_UNSET)
      return;
  }

  mp_set_current_time(vd->vd_mp, user_time, epoch, 0);

  if(pts == PTS_UNSET)
    return;

  vd->vd_subpts_user_time = user_time - vd->vd_mp->mp_svdelta;
  vd->vd_subpts_ts = pts - vd->vd_mp->mp_svdelta;

  pts -= vd->vd_mp->mp_svdelta;

  if(vd->vd_ext_subtitles != NULL)
    subtitles_pick(vd->vd_ext_subtitles, vd->vd_subpts_user_time,
                   pts, vd->vd_mp);
}


/**
 *
 */
int
video_deliver_frame(video_decoder_t *vd, const frame_info_t *info)
{
  int r = vd->vd_mp->mp_video_frame_deliver(info,
                                            vd->vd_mp->mp_video_frame_opaque);

  if(info->fi_drive_clock && !r)
    video_decoder_set_current_time(vd, info->fi_user_time, info->fi_epoch,
                                   info->fi_pts, info->fi_drive_clock);

  return r;
}


/**
 *
 */
static void
update_vbitrate(media_pipe_t *mp, media_queue_t *mq, 
		media_buf_t *mb, video_decoder_t *vd)
{
  int i;
  int64_t sum;

  int size = mb->mb_size;
  int duration = mb->mb_duration;
  if(duration == 0)
    duration = vd->vd_estimated_duration;
  vd->vd_frame_size[vd->vd_frame_size_ptr] = size;
  vd->vd_frame_size_ptr = (vd->vd_frame_size_ptr + 1) & VD_FRAME_SIZE_MASK;

  if(duration == 0 || (vd->vd_frame_size_ptr & 7) != 0)
    return;

  sum = 0;
  for(i = 0; i < VD_FRAME_SIZE_LEN; i++)
    sum += vd->vd_frame_size[i];

  sum = 8000000LL * sum / VD_FRAME_SIZE_LEN / duration;
  prop_set_int(mq->mq_prop_bitrate, sum / 1000);
}

/**
 * Video decoder thread
 */
static void *
vd_thread(void *aux)
{
  video_decoder_t *vd = aux;
  media_pipe_t *mp = vd->vd_mp;
  media_queue_t *mq = &mp->mp_video;
  media_buf_t *mb;
  media_buf_t *cur = NULL;
  media_codec_t *mc, *mc_current = NULL;
  int run = 1;
  int reqsize = -1;
  int reinit = 0;

  const media_buf_meta_t *mbm = NULL;

  vd->vd_frame = av_frame_alloc();

  hts_mutex_lock(&mp->mp_mutex);

  while(run) {

    if(mbm != vd->vd_reorder_current) {
      mbm = vd->vd_reorder_current;
      hts_mutex_unlock(&mp->mp_mutex);

      vd->vd_estimated_duration = mbm->mbm_duration;

      if(mbm->mbm_drive_clock)
        video_decoder_set_current_time(vd, mbm->mbm_user_time,
                                       mbm->mbm_epoch, mbm->mbm_pts,
                                       mbm->mbm_drive_clock);
      hts_mutex_lock(&mp->mp_mutex);
      continue;
    }

    media_buf_t *ctrl = TAILQ_FIRST(&mq->mq_q_ctrl);
    media_buf_t *data = TAILQ_FIRST(&mq->mq_q_data);
    media_buf_t *aux  = TAILQ_FIRST(&mq->mq_q_aux);

    if(ctrl != NULL) {
      TAILQ_REMOVE(&mq->mq_q_ctrl, ctrl, mb_link);
      mb = ctrl;

    } else if(aux != NULL && aux->mb_pts < vd->vd_subpts_ts + 1000000LL) {

      if(vd->vd_hold) {
	hts_cond_wait(&mq->mq_avail, &mp->mp_mutex);
	continue;
      }

      TAILQ_REMOVE(&mq->mq_q_aux, aux, mb_link);
      mb = aux;

    } else if(cur != NULL) {

      if(vd->vd_hold) {
	hts_cond_wait(&mq->mq_avail, &mp->mp_mutex);
	continue;
      }

      mb = cur;
      goto retry_current;
    } else if(data != NULL) {

      if(vd->vd_hold) {
	hts_cond_wait(&mq->mq_avail, &mp->mp_mutex);
	continue;
      }

      TAILQ_REMOVE(&mq->mq_q_data, data, mb_link);
      mp_check_underrun(mp);
      mb = data;
      if(mb->mb_dts != PTS_UNSET)
        mq->mq_last_deq_dts = mb->mb_dts;

    } else {
      hts_cond_wait(&mq->mq_avail, &mp->mp_mutex);
      continue;
    }


    mq->mq_packets_current--;
    mp->mp_buffer_current -= mb_buffered_size(mb);
    mq_update_stats(mp, mq, 1);

    hts_cond_signal(&mp->mp_backpressure);

  retry_current:
    mc = mb->mb_cw;

    if(mb->mb_data_type == MB_VIDEO && mc->decode_locked != NULL) {

      if(mc != mc_current) {
        hts_mutex_unlock(&mp->mp_mutex);
	if(mc_current != NULL) {
	  media_codec_deref(mc_current);
        }
	mc_current = media_codec_ref(mc);
	prop_set_int(mq->mq_prop_too_slow, 0);
        hts_mutex_lock(&mp->mp_mutex);
      }

      mq->mq_no_data_interest = 1;
      if(mc->decode_locked(mc, vd, mq, mb)) {
        cur = mb;
 	hts_cond_wait(&mq->mq_avail, &mp->mp_mutex);
        continue;
      }
      mq->mq_no_data_interest = 0;
      cur = NULL;

      update_vbitrate(mp, mq, mb, vd);
      media_buf_free_locked(mp, mb);
      continue;
    }

    hts_mutex_unlock(&mp->mp_mutex);


    switch(mb->mb_data_type) {
    case MB_CTRL_EXIT:
      run = 0;
      break;

    case MB_CTRL_PAUSE:
      vd->vd_hold = 1;
      break;

    case MB_CTRL_PLAY:
      vd->vd_hold = 0;
      break;

    case MB_CTRL_FLUSH:
      if(cur != NULL) {
        media_buf_free_unlocked(mp, cur);
        mq->mq_no_data_interest = 0;
        cur = NULL;
      }
      vd_init_timings(vd);
      vd->vd_interlaced = 0;

      hts_mutex_lock(&mp->mp_overlay_mutex);
      video_overlay_flush_locked(mp, 1);
      dvdspu_flush_locked(mp);
      hts_mutex_unlock(&mp->mp_overlay_mutex);

      mp->mp_video_frame_deliver(NULL, mp->mp_video_frame_opaque);

      if(mc_current != NULL)
        mc_current->flush(mc_current, vd);

      if(mb->mb_data32) {

        // Final flush, sent from mp_shutdown()
        // Release codec if we hang on to it

        if(mc_current != NULL) {

          // Need to release here because during end of video playback.
          // The only thing holding reference to the codec is this
          // 'mc_current'. So when the demuxer layer have released
          // its references and the pipe is empty and a flush is sent
          // we should also release the codec (causing it to close).
          // Otherwise we will keeping a reference and another stream
          // opening a new instance will fail to allocate resources.
          // see #2495
          media_codec_deref(mc_current);
          mc_current = NULL;
        }

        mp->mp_set_video_codec('none', NULL, mp->mp_video_frame_opaque, NULL);
        mp->mp_video_frame_deliver(NULL, mp->mp_video_frame_opaque);
      }

      if(mp->mp_seek_video_done != NULL)
	mp->mp_seek_video_done(mp);
      break;

    case MB_VIDEO:
      if(mc != mc_current) {
	if(mc_current != NULL) {
	  media_codec_deref(mc_current);
          mp->mp_set_video_codec('none', NULL, mp->mp_video_frame_opaque, NULL);
        }

	mc_current = media_codec_ref(mc);
	prop_set_int(mq->mq_prop_too_slow, 0);
      }

      if(reinit) {
	if(mc->reinit != NULL)
	  mc->reinit(mc);
	reinit = 0;
      }

      media_discontinuity_debug(&vd->vd_debug_discont_in,
                                mb->mb_dts,
                                mb->mb_pts,
                                mp->mp_epoch,
                                mb->mb_skip,
                                "VDEC");

      mc->decode(mc, vd, mq, mb, reqsize);
      update_vbitrate(mp, mq, mb, vd);
      reqsize = -1;
      break;

    case MB_CTRL_REQ_OUTPUT_SIZE:
      reqsize = mb->mb_data32;
      break;

    case MB_CTRL_REINITIALIZE:
      reinit = 1;
      break;

    case MB_CTRL_RECONFIGURE:
      mb->mb_cw->reconfigure(mc, mb->mb_frame_info);
      break;

#if ENABLE_DVD
    case MB_DVD_RESET_SPU:
      hts_mutex_lock(&mp->mp_overlay_mutex);
      vd->vd_spu_curbut = 1;
      dvdspu_flush_locked(mp);
      hts_mutex_unlock(&mp->mp_overlay_mutex);
      break;

    case MB_CTRL_DVD_HILITE:
      vd->vd_spu_curbut = mb->mb_data32;
      vd->vd_spu_repaint = 1;
      break;

    case MB_DVD_PCI:
      memcpy(&vd->vd_pci, mb->mb_data, sizeof(pci_t));
      vd->vd_spu_repaint = 1;
      event_payload_t *ep =
        event_create(EVENT_DVD_PCI, sizeof(event_t) + sizeof(pci_t));
      memcpy(ep->payload, mb->mb_data, sizeof(pci_t));
      mp_enqueue_event(mp, &ep->h);
      event_release(&ep->h);
      break;

    case MB_DVD_CLUT:
      dvdspu_decode_clut(vd->vd_dvd_clut, (void *)mb->mb_data);
      break;

    case MB_DVD_SPU:
      dvdspu_enqueue(mp, mb->mb_data, mb->mb_size, 
		     vd->vd_dvd_clut, 0, 0, mb->mb_pts);
      break;
#endif

    case MB_CTRL_DVD_SPU2:
      dvdspu_enqueue(mp, mb->mb_data+72, mb->mb_size-72,
		     (void *)mb->mb_data,
		     ((const uint32_t *)mb->mb_data)[16],
		     ((const uint32_t *)mb->mb_data)[17],
		     mb->mb_pts);
      break;
      


    case MB_SUBTITLE:
      if(vd->vd_ext_subtitles == NULL && mb->mb_stream == mq->mq_stream2)
	video_overlay_decode(mp, mb);
      break;

    case MB_CTRL_FLUSH_SUBTITLES:
      hts_mutex_lock(&mp->mp_overlay_mutex);
      video_overlay_flush_locked(mp, 1);
      hts_mutex_unlock(&mp->mp_overlay_mutex);
      break;

    case MB_CTRL_EXT_SUBTITLE:
      if(vd->vd_ext_subtitles != NULL)
         subtitles_destroy(vd->vd_ext_subtitles);

      // Steal subtitle from the media_buf
      vd->vd_ext_subtitles = (void *)mb->mb_data;
      mb->mb_data = NULL;
      hts_mutex_lock(&mp->mp_overlay_mutex);
      video_overlay_flush_locked(mp, 1);
      hts_mutex_unlock(&mp->mp_overlay_mutex);
      break;

    default:
      abort();
    }

    hts_mutex_lock(&mp->mp_mutex);
    media_buf_free_locked(mp, mb);
  }

  if(cur != NULL)
    media_buf_free_locked(mp, cur);

  mq->mq_no_data_interest = 0;

  hts_mutex_unlock(&mp->mp_mutex);

  if(mc_current != NULL) {
    media_codec_deref(mc_current);
    mp->mp_set_video_codec('none', NULL, mp->mp_video_frame_opaque, NULL);
  }

  if(vd->vd_ext_subtitles != NULL)
    subtitles_destroy(vd->vd_ext_subtitles);

  av_frame_free(&vd->vd_frame);
  return NULL;
}




video_decoder_t *
video_decoder_create(media_pipe_t *mp)
{
  video_decoder_t *vd = calloc(1, sizeof(video_decoder_t));

  vd->vd_mp = mp_retain(mp);

  vd_init_timings(vd);

  hts_thread_create_joinable("video decoder", 
			     &vd->vd_decoder_thread, vd_thread, vd,
			     THREAD_PRIO_VIDEO);
  
  return vd;
}


/**
 *
 */
void
video_decoder_stop(video_decoder_t *vd)
{
  media_pipe_t *mp = vd->vd_mp;

  mp_send_cmd(mp, &mp->mp_video, MB_CTRL_EXIT);

  hts_thread_join(&vd->vd_decoder_thread);
  mp_release(vd->vd_mp);
  vd->vd_mp = NULL;
}


/**
 *
 */
void
video_decoder_destroy(video_decoder_t *vd)
{
  sws_freeContext(vd->vd_sws);
  avpicture_free(&vd->vd_convert);
  free(vd);
}
