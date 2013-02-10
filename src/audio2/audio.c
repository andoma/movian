#include <assert.h>
#include <unistd.h>


#include "showtime.h"
#include "media.h"
#include "audio_ext.h"
#include "audio.h"
#include "libav.h"

#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>

static audio_class_t *audio_class;

static void *audio_decode_thread(void *aux);


/**
 *
 */
static void
save_matervol(void *opaque, float value)
{
  htsmsg_t *m = htsmsg_create_map();
  TRACE(TRACE_DEBUG, "audio", "Master volume set to %f dB", value);

  htsmsg_add_s32(m, "master-volume", value * 1000);
  htsmsg_store_save(m, "audiomixer");
  htsmsg_destroy(m);
}


/**
 *
 */
static void
audio_mastervol_init(void)
{
  htsmsg_t *m = htsmsg_store_load("audiomixer");
  int32_t i32;
  prop_t *pa, *mv, *mm;

  pa = prop_create(prop_get_global(), "audio");
  mv = prop_create(pa, "mastervolume");
  mm = prop_create(pa, "mastermute");

  prop_set_float_clipping_range(mv, -75, 12);

  if(m != NULL && !htsmsg_get_s32(m, "master-volume", &i32))
    prop_set_float(mv, (float)i32 / 1000);

  prop_set_int(mm, 0);
  
  htsmsg_destroy(m);

  prop_subscribe(PROP_SUB_NO_INITIAL_UPDATE,
		 PROP_TAG_CALLBACK_FLOAT, save_matervol, NULL,
		 PROP_TAG_ROOT, mv,
		 NULL);
}


/**
 *
 */
void 
audio_init(void)
{
  audio_mastervol_init();
  audio_class = audio_driver_init();
}


/**
 *
 */
void 
audio_fini(void)
{

}


/**
 *
 */
struct audio_decoder *
audio_decoder_create(struct media_pipe *mp)
{
  const audio_class_t *ac = audio_class;

  audio_decoder_t *ad = calloc(1, ac->ac_alloc_size);
  ad->ad_mp = mp;
  ad->ad_ac = ac;

  ad->ad_tile_size = 1024;
  ad->ad_frame = avcodec_alloc_frame();
  ad->ad_pts = AV_NOPTS_VALUE;
  ad->ad_epoch = 0;

  hts_thread_create_joinable("audio decoder", &ad->ad_tid,
                             audio_decode_thread, ad, THREAD_PRIO_HIGH);
  return ad;
}


/**
 *
 */
void
audio_decoder_destroy(struct audio_decoder *ad)
{
  mp_send_cmd(ad->ad_mp, &ad->ad_mp->mp_audio, MB_CTRL_EXIT);
  hts_thread_join(&ad->ad_tid);

  avcodec_free_frame(&ad->ad_frame);

  if(ad->ad_avr != NULL) {
    avresample_close(ad->ad_avr);
    avresample_free(&ad->ad_avr);
  }

  free(ad);
}



/**
 *
 */
static void
audio_process_audio(audio_decoder_t *ad, media_buf_t *mb)
{
  const audio_class_t *ac = ad->ad_ac;
  AVFrame *frame = ad->ad_frame;
  media_pipe_t *mp = ad->ad_mp;
  media_queue_t *mq = &mp->mp_audio;
  int r;
  int got_frame;
  AVPacket avpkt;
  int offset = 0;

  if(mb->mb_skip || mb->mb_stream != mq->mq_stream) 
    return;

  while(offset < mb->mb_size) {

    if(mb->mb_cw == NULL) {
      frame->sample_rate = mb->mb_rate;
      frame->format = AV_SAMPLE_FMT_S16;
      switch(mb->mb_channels) {
      case 1:
	frame->channel_layout = AV_CH_LAYOUT_MONO;
	frame->nb_samples = mb->mb_size / 2;
	break;
      case 2:
	frame->channel_layout = AV_CH_LAYOUT_STEREO;
	frame->nb_samples = mb->mb_size / 4;
	break;
      default:
	abort();
      }
      frame->data[0] = mb->mb_data;
      frame->linesize[0] = 0;
      r = mb->mb_size;
      got_frame = 1;

    } else {

      av_init_packet(&avpkt);
      avpkt.data = mb->mb_data + offset;
      avpkt.size = mb->mb_size - offset;
      
      r = avcodec_decode_audio4(mb->mb_cw->codec_ctx, frame,
				&got_frame, &avpkt);
      if(r < 0)
	return;

      if(frame->sample_rate == 0)
	frame->sample_rate = mb->mb_cw->codec_ctx->sample_rate;
    
      if(frame->sample_rate == 0)
	return;

      if(frame->channel_layout == 0) {
	switch(mb->mb_cw->codec_ctx->channels) {
	case 1:
	  frame->channel_layout = AV_CH_LAYOUT_MONO;
	  break;
	case 2:
	  frame->channel_layout = AV_CH_LAYOUT_STEREO;
	  break;
	default:
	  return;
	}
      }

      if(mp->mp_stats)
	mp_set_mq_meta(mq, mb->mb_cw->codec, mb->mb_cw->codec_ctx);

    }

    if(offset == 0 && mb->mb_pts != AV_NOPTS_VALUE) {
        
      int od = 0, id = 0;
          
      if(ad->ad_avr != NULL) {
	od = avresample_available(ad->ad_avr) *
	  1000000LL / ad->ad_out_sample_rate;
	id = avresample_get_delay(ad->ad_avr) *
	  1000000LL / frame->sample_rate;
      }
      ad->ad_pts = mb->mb_pts - od - id;
      ad->ad_epoch = mb->mb_epoch;
      //        printf("od=%-20d id=%-20d PTS=%-20ld oPTS=%-20ld\n",
      // od, id, mb->mb_pts, pts);
        
      if(mb->mb_drive_clock)
	mp_set_current_time(mp, mb->mb_pts - ad->ad_delay,
			    mb->mb_epoch, mb->mb_delta);
    }

    offset += r;

    if(got_frame) {

      if(frame->sample_rate    != ad->ad_in_sample_rate ||
	 frame->format         != ad->ad_in_sample_format ||
	 frame->channel_layout != ad->ad_in_channel_layout) {
          
	ad->ad_in_sample_rate    = frame->sample_rate;
	ad->ad_in_sample_format  = frame->format;
	ad->ad_in_channel_layout = frame->channel_layout;

	ac->ac_reconfig(ad);

	if(ad->ad_avr == NULL)
	  ad->ad_avr = avresample_alloc_context();
	else
	  avresample_close(ad->ad_avr);
          
	av_opt_set_int(ad->ad_avr, "in_sample_fmt",
		       ad->ad_in_sample_format, 0);
	av_opt_set_int(ad->ad_avr, "in_sample_rate", 
		       ad->ad_in_sample_rate, 0);
	av_opt_set_int(ad->ad_avr, "in_channel_layout",
		       ad->ad_in_channel_layout, 0);

	av_opt_set_int(ad->ad_avr, "out_sample_fmt",
		       ad->ad_out_sample_format, 0);
	av_opt_set_int(ad->ad_avr, "out_sample_rate",
		       ad->ad_out_sample_rate, 0);
	av_opt_set_int(ad->ad_avr, "out_channel_layout",
		       ad->ad_out_channel_layout, 0);
          
	char buf1[128];
	char buf2[128];

	av_get_channel_layout_string(buf1, sizeof(buf1), 
				     -1, ad->ad_in_channel_layout);
	av_get_channel_layout_string(buf2, sizeof(buf2), 
				     -1, ad->ad_out_channel_layout);

	TRACE(TRACE_DEBUG, "Audio",
	      "Converting from [%s %dHz %s] to [%s %dHz %s]",
	      buf1, ad->ad_in_sample_rate,
	      av_get_sample_fmt_name(ad->ad_in_sample_format),
	      buf2, ad->ad_out_sample_rate,
	      av_get_sample_fmt_name(ad->ad_out_sample_format));

	if(avresample_open(ad->ad_avr)) {
	  TRACE(TRACE_ERROR, "AudioQueue", "Unable to open resampler");
	  avresample_free(&ad->ad_avr);
	}
      }
      if(ad->ad_avr != NULL)
	avresample_convert(ad->ad_avr, NULL, 0, 0,
			   frame->data, frame->linesize[0],
			   frame->nb_samples);
    }
  }
}



/**
 *
 */
void *
audio_decode_thread(void *aux)
{
  audio_decoder_t *ad = aux;
  const audio_class_t *ac = ad->ad_ac;
  int run = 1;
  media_pipe_t *mp = ad->ad_mp;
  media_queue_t *mq = &mp->mp_audio;
  media_buf_t *mb;
  int blocked = 0;

  if(ac->ac_init != NULL)
    ac->ac_init(ad);

  hts_mutex_lock(&mp->mp_mutex);

  while(run) {

    int avail = ad->ad_avr != NULL ? avresample_available(ad->ad_avr) : 0;

    media_buf_t *data = TAILQ_FIRST(&mq->mq_q_data);
    media_buf_t *ctrl = TAILQ_FIRST(&mq->mq_q_ctrl);

    if(avail >= ad->ad_tile_size && blocked == 0 && !ad->ad_paused) {
      assert(avail != 0);
      assert(ad->ad_avr != NULL);

      int samples = MIN(ad->ad_tile_size, avail);
      int r;

      if(ac->ac_deliver_locked != NULL) {
        r = ac->ac_deliver_locked(ad, samples, ad->ad_pts, ad->ad_epoch);
        if(r) {
          hts_cond_wait(&mq->mq_avail, &mp->mp_mutex);
          continue;
        }
        ad->ad_pts = AV_NOPTS_VALUE;
        continue;
      } else {
        hts_mutex_unlock(&mp->mp_mutex);
        r = ac->ac_deliver_unlocked(ad, samples, ad->ad_pts, ad->ad_epoch);
        hts_mutex_lock(&mp->mp_mutex);
      }

      if(r) {
	blocked = 1;
	continue;
      } else {
	ad->ad_pts = AV_NOPTS_VALUE;
      }
      continue;
    }

    if(ctrl != NULL) {
      TAILQ_REMOVE(&mq->mq_q_ctrl, ctrl, mb_link);
      mb = ctrl;
    } else if(data != NULL && avail < ad->ad_tile_size) {
      TAILQ_REMOVE(&mq->mq_q_data, data, mb_link);
      mb = data;
    } else {
      hts_cond_wait(&mq->mq_avail, &mp->mp_mutex);
      continue;
    }

    mq->mq_packets_current--;
    mp->mp_buffer_current -= mb->mb_size;
    mq_update_stats(mp, mq);
    hts_cond_signal(&mp->mp_backpressure);

    if(mb->mb_data_type == MB_CTRL_UNBLOCK) {
      assert(blocked);
      blocked = 0;
    } else {

      hts_mutex_unlock(&mp->mp_mutex);

      switch(mb->mb_data_type) {
      case MB_AUDIO:
	audio_process_audio(ad, mb);
	break;

      case MB_CTRL_PAUSE:
	ad->ad_paused = 1;
	if(ac->ac_pause)
	  ac->ac_pause(ad);
	break;

      case MB_CTRL_PLAY:
	ad->ad_paused = 0;
	if(ac->ac_play)
	  ac->ac_play(ad);
	break;

      case MB_CTRL_FLUSH:
	if(ac->ac_flush)
	  ac->ac_flush(ad);
	ad->ad_pts = AV_NOPTS_VALUE;
	break;

      case MB_CTRL_EXIT:
	run = 0;
	break;

      default:
	abort();
      }

      hts_mutex_lock(&mp->mp_mutex);
    }
    media_buf_free_locked(mp, mb);
  }

  hts_mutex_unlock(&mp->mp_mutex);

  if(ac->ac_fini != NULL)
    ac->ac_fini(ad);
  return NULL;
}



#if 0
/**
 *
 */
static void *
dummy_audio_thread(void *aux)
{
  audio_decoder_t *ad = aux;
  media_pipe_t *mp = ad->ad_mp;
  media_queue_t *mq = &mp->mp_audio;
  media_buf_t *mb;
  int hold = 0;
  int run = 1;
  int64_t rt = 0;
  int64_t base = AV_NOPTS_VALUE;


  hts_mutex_lock(&mp->mp_mutex);

  while(run) {

    if((mb = TAILQ_FIRST(&mq->mq_q)) == NULL) {
      hts_cond_wait(&mq->mq_avail, &mp->mp_mutex);
      continue;
    }

    if(mb->mb_data_type == MB_AUDIO && hold && mb->mb_skip == 0) {
      hts_cond_wait(&mq->mq_avail, &mp->mp_mutex);
      continue;
    }

    TAILQ_REMOVE(&mq->mq_q, mb, mb_link);
    mq->mq_packets_current--;
    mp->mp_buffer_current -= mb->mb_size;
    mq_update_stats(mp, mq);
    hts_cond_signal(&mp->mp_backpressure);
    hts_mutex_unlock(&mp->mp_mutex);

    switch(mb->mb_data_type) {
    case MB_CTRL_EXIT:
      run = 0;
      break;

    case MB_CTRL_PAUSE:
      hold = 1;
      break;

    case MB_CTRL_PLAY:
      hold = 0;
      base = AV_NOPTS_VALUE;
      break;

    case MB_FLUSH:
      base = AV_NOPTS_VALUE;
      break;

    case MB_AUDIO:
      if(mb->mb_skip || mb->mb_stream != mq->mq_stream) 
	break;
      if(mb->mb_pts != AV_NOPTS_VALUE) {
        audio_set_clock(mp, mb->mb_pts, 0, mb->mb_epoch);

        if(base == AV_NOPTS_VALUE) {
          base = mb->mb_pts;
          rt = showtime_get_ts();
        } else {
          int64_t d = mb->mb_pts - base;
          if(d > 0) {
            int sleeptime = rt + d - showtime_get_ts();
	    if(sleeptime > 0)
	      usleep(sleeptime);
          }
        }
      }
      break;

    default:
      abort();
    }
    hts_mutex_lock(&mp->mp_mutex);
    media_buf_free_locked(mp, mb);
  }
  hts_mutex_unlock(&mp->mp_mutex);
  return NULL;
}

/**
 *
 */
static audio_class_t dummy_audio_class = {
  .ac_alloc_size = sizeof(audio_decoder_t),
  .ac_thread = dummy_audio_thread,
};
#endif
