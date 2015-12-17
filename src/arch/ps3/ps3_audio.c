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
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <altivec.h>

#include <audio/audio.h>
#include <psl1ght/lv2/timer.h>
#include <sysutil/audio.h>

#include <libavutil/avutil.h>

#include "main.h"
#include "media/media.h"
#include "audio2/audio.h"

static int max_pcm;
static int max_dts;
static int max_ac3;

/**
 *
 */
typedef struct decoder {
  audio_decoder_t ad;
  u32 port_num;
  sys_event_queue_t snd_queue;
  AudioPortConfig config;
  u64 snd_queue_key;
  int channels;
  int write_ptr;
  int audio_blocks;
} decoder_t;

#define INVALID_PORT 0xffffffff



/**
 *
 */
int64_t
arch_get_avtime(void)
{
  return arch_get_ts();
}


/**
 *
 */
static void
ps3_audio_destroy_port(decoder_t *d)
{
  if(d->port_num != INVALID_PORT) {
    audioPortStop(d->port_num);
    audioRemoveNotifyEventQueue(d->snd_queue_key);
    audioPortClose(d->port_num);
    sys_event_queue_destroy(d->snd_queue, 0);
    d->port_num = INVALID_PORT;
  }
}


/**
 *
 */
static int
ps3_audio_init(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  d->audio_blocks = 16;
  d->port_num = INVALID_PORT;
  ad->ad_tile_size = AUDIO_BLOCK_SAMPLES;
  return 0;
}


/**
 *
 */
static void
ps3_audio_fini(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  ps3_audio_destroy_port(d);
}


/**
 *
 */
static int
ps3_audio_reconfig(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;

  ps3_audio_destroy_port(d);

  ad->ad_out_sample_rate = 48000;
  ad->ad_out_sample_format = AV_SAMPLE_FMT_FLT;
 
  AudioOutConfiguration conf;
  memset(&conf, 0, sizeof(conf));

  if(ad->ad_in_channel_layout == AV_CH_LAYOUT_MONO ||
     ad->ad_in_channel_layout == AV_CH_LAYOUT_STEREO) {
    ad->ad_out_channel_layout = AV_CH_LAYOUT_STEREO;
    d->channels = 2;
    conf.channel = 2;
    conf.encoder = AUDIO_OUT_CODING_TYPE_LPCM;
  } else if(ad->ad_in_channel_layout &
	    (AV_CH_BACK_LEFT |
	     AV_CH_BACK_RIGHT |
	     AV_CH_BACK_CENTER)) {

    d->channels = 8;
    ad->ad_out_channel_layout = AV_CH_LAYOUT_7POINT1;
    if(max_pcm == 8) {
      conf.channel = 8;
      conf.encoder = AUDIO_OUT_CODING_TYPE_LPCM;
    } else if(max_ac3 == 6) {
      conf.channel = 6;
      conf.encoder = AUDIO_OUT_CODING_TYPE_AC3;
      conf.down_mixer = AUDIO_OUT_DOWNMIXER_TYPE_B;
    } else if(max_dts == 6) {
      conf.channel = 6;
      conf.encoder = AUDIO_OUT_CODING_TYPE_DTS;
      conf.down_mixer = AUDIO_OUT_DOWNMIXER_TYPE_B;
    } else {
      d->channels = 2;
      conf.channel = 2;
      conf.encoder = AUDIO_OUT_CODING_TYPE_LPCM;
      conf.down_mixer = AUDIO_OUT_DOWNMIXER_TYPE_A;
      ad->ad_out_channel_layout = AV_CH_LAYOUT_STEREO;
    } 
  } else {


    d->channels = 8;
    ad->ad_out_channel_layout = AV_CH_LAYOUT_7POINT1;
    if(max_pcm >= 6) {
      conf.channel = 6;
      conf.encoder = AUDIO_OUT_CODING_TYPE_LPCM;
    } else if(max_ac3 == 6) {
      conf.channel = 6;
      conf.encoder = AUDIO_OUT_CODING_TYPE_AC3;
    } else if(max_dts == 6) {
      conf.channel = 6;
      conf.encoder = AUDIO_OUT_CODING_TYPE_DTS;
    } else {
      conf.channel = 2;
      conf.encoder = AUDIO_OUT_CODING_TYPE_LPCM;
      conf.down_mixer = AUDIO_OUT_DOWNMIXER_TYPE_A;
      ad->ad_out_channel_layout = AV_CH_LAYOUT_STEREO;
      d->channels = 2;
    }    
  }
	     
  int r;
  r = audioOutConfigure(AUDIO_OUT_PRIMARY, &conf, NULL, 1);
  if(r == 0) {
    int i;
    for(i = 0; i < 100;i++) {
      AudioOutState state;
      r = audioOutGetState(AUDIO_OUT_PRIMARY, 0, &state);
      if(r != 0)
	break;
      TRACE(TRACE_DEBUG, "AUDIO", "The state is %d", state.state);
      if(state.state == 2)
	continue;
      usleep(100);
      break;
    }
  }

  AudioPortParam params;

  params.numChannels = d->channels;
  params.numBlocks = d->audio_blocks;
  params.attr = 0;
  params.level = 1;
	
  audioPortOpen(&params, &d->port_num);

  TRACE(TRACE_DEBUG, "AUDIO", 
	"PS3 audio port %d opened (%d channels)",
	d->port_num, d->channels);
	

  audioGetPortConfig(d->port_num, &d->config);
  audioCreateNotifyEventQueue(&d->snd_queue, &d->snd_queue_key);
  audioSetNotifyEventQueue(d->snd_queue_key);
  sys_event_queue_drain(d->snd_queue);
  audioPortStart(d->port_num);
	
  return 0;
}


/**
 *
 */
static int
ps3_audio_deliver(audio_decoder_t *ad, int samples, int64_t pts, int epoch)
{
  int i;
  decoder_t *d = (decoder_t *)ad;

  assert(samples >= AUDIO_BLOCK_SAMPLES);

  sys_event_t event;
  int ret = sys_event_queue_receive(d->snd_queue, &event, 20 * 1000);
  if(ret)
    TRACE(TRACE_ERROR, "PS3AUDIO", "Audio queue timeout");

  float *buf = (float *)(intptr_t)d->config.audioDataStart;
  int current_block = *(uint64_t *)(intptr_t)d->config.readIndex;

  int bi;

  if(d->write_ptr != -1)
    bi = d->write_ptr;
  else
    bi = (current_block + 1) & 7;

  while(bi != current_block &&
	avresample_available(ad->ad_avr) >= AUDIO_BLOCK_SAMPLES) {

    float *dst = buf + d->channels * AUDIO_BLOCK_SAMPLES * bi;
    uint8_t *planes[8] = {0};

    float s = audio_master_mute ? 0 : audio_master_volume * ad->ad_vol_scale;

    vector float m = vec_splats(s);
    vector float z = vec_splats(0.0f);

    switch(ad->ad_out_channel_layout) {
    case AV_CH_LAYOUT_STEREO:
      planes[0] = (uint8_t *)dst;
      avresample_read(ad->ad_avr, planes, AUDIO_BLOCK_SAMPLES);

      for(i = 0; i < AUDIO_BLOCK_SAMPLES / 2; i++) {
	vec_st(vec_madd(vec_ld(0, dst), m, z), 0, dst);
	dst += 4;
      }
      break;

    case AV_CH_LAYOUT_7POINT1:
      planes[0] = (uint8_t *)dst;
      avresample_read(ad->ad_avr, planes, AUDIO_BLOCK_SAMPLES);

      // Swap Side-channels with Rear-channels as the channel
      // order differs between PS3 and libav

      for(i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {

	vector float v1 = vec_ld(0,  dst);
	vector float v2 = vec_ld(16, dst);
	
	v2 = vec_perm(v2, v2, (const vector unsigned char) {
	    0x8,0x9,0xa,0xb,
	      0xc,0xd,0xe,0xf,
	      0x0,0x1,0x2,0x3,
	      0x4,0x5,0x6,0x7});
			  
	v1 = vec_madd(v1, m, z);
	v2 = vec_madd(v2, m, z);

	vec_st(v1, 0, dst);
	vec_st(v2, 16, dst);
	dst += 8;
      }
      break;

    default:
      break;
    }

    bi = (bi + 1) & (d->audio_blocks - 1);

    if(pts != AV_NOPTS_VALUE) {

      pts -= 1000000LL * (AUDIO_BLOCK_SAMPLES * (d->audio_blocks - 1)) / 48000;

      media_pipe_t *mp = ad->ad_mp;

      hts_mutex_lock(&mp->mp_clock_mutex);
      mp->mp_audio_clock = pts;
      mp->mp_audio_clock_avtime = arch_get_avtime();
      mp->mp_audio_clock_epoch = epoch;
      hts_mutex_unlock(&mp->mp_clock_mutex);
      pts = AV_NOPTS_VALUE;
    }
  }
  d->write_ptr = bi;
  return 0;
}


/**
 *
 */
static audio_class_t ps3_audio_class = {
  .ac_alloc_size = sizeof(decoder_t),
  .ac_init = ps3_audio_init,
  .ac_fini = ps3_audio_fini,
  .ac_reconfig = ps3_audio_reconfig,
  .ac_deliver_unlocked = ps3_audio_deliver,
};


/**
 *
 */
audio_class_t *
audio_driver_init(struct prop *asettings)
{

  max_pcm = audioOutGetSoundAvailability(AUDIO_OUT_PRIMARY,
					 AUDIO_OUT_CODING_TYPE_LPCM,
					 AUDIO_OUT_FS_48KHZ,
					 0);
  
  max_dts = audioOutGetSoundAvailability(AUDIO_OUT_PRIMARY,
					 AUDIO_OUT_CODING_TYPE_DTS,
					 AUDIO_OUT_FS_48KHZ,
					 0);

  max_ac3 = audioOutGetSoundAvailability(AUDIO_OUT_PRIMARY,
					 AUDIO_OUT_CODING_TYPE_AC3,
					 AUDIO_OUT_FS_48KHZ,
					 0);

  audioInit();

  audioOutSetCopyControl(AUDIO_OUT_PRIMARY, AUDIO_OUT_COPY_CONTROL_FREE);

  return &ps3_audio_class;
}

