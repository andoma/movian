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
#include <AudioToolbox/AudioQueue.h>

#include <mach/mach_time.h>

#include "main.h"
#include "audio.h"
#include "media/media.h"

#define NUM_BUFS 8

typedef struct decoder {
  audio_decoder_t ad;
  AudioQueueRef aq;
  int framesize;
  int underrun;
  struct {
    AudioQueueBufferRef buf;
    int avail;
    int size;
  } buffers[NUM_BUFS];
} decoder_t;

static CFRunLoopRef audio_run_loop;
static mach_timebase_info_data_t timebase;


static void
do_nothing(CFRunLoopTimerRef timer, void *info)
{
}

/**
 *
 */
static void *
audio_thread(void *aux)
{
  audio_run_loop = CFRunLoopGetCurrent();
  __sync_synchronize();

  CFRunLoopTimerRef r =
    CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent(),
                         1000000.0f, 0, 0, do_nothing, NULL);
  CFRunLoopAddTimer(CFRunLoopGetCurrent(), r, kCFRunLoopCommonModes);
  while(1)
    CFRunLoopRun();
  return NULL;
}


/**
 *
 */
static void 
return_buf(void *aux, AudioQueueRef aq, AudioQueueBufferRef buf)
{
  decoder_t *d = aux;
  media_pipe_t *mp = d->ad.ad_mp;
  int i;
  hts_mutex_lock(&mp->mp_mutex);
  for(i = 0; i < NUM_BUFS; i++) {
    if(d->buffers[i].buf == buf) {
      d->buffers[i].avail = 1;
      hts_cond_signal(&mp->mp_audio.mq_avail);
      hts_mutex_unlock(&mp->mp_mutex);
      int tavail = 0;
      for(i = 0; i < NUM_BUFS; i++) {
        if(d->buffers[i].avail)
          tavail++;
      }
      if(tavail == NUM_BUFS)
        d->underrun = 1;
      return;
    }
  }
  abort();
}


/**
 *
 */
static AudioQueueBufferRef
allocbuf(decoder_t *d, int pos, int bytes)
{
  d->buffers[pos].size = bytes;
  AudioQueueAllocateBuffer(d->aq, bytes, &d->buffers[pos].buf);
  d->buffers[pos].avail = 0;
  return d->buffers[pos].buf;
}


/**
 *
 */
static AudioQueueBufferRef
getbuf(decoder_t *d, int bytes)
{
  int i;
  assert(d->aq != NULL);
  for(i = 0; i < NUM_BUFS; i++) {
    if(d->buffers[i].avail && d->buffers[i].size >= bytes) {
      d->buffers[i].avail = 0;
      return d->buffers[i].buf;
    }
  }

  for(i = 0; i < NUM_BUFS; i++)
    if(d->buffers[i].buf == NULL)
      return allocbuf(d, i, bytes);

  for(i = 0; i < NUM_BUFS; i++)
    if(d->buffers[i].avail) {
      AudioQueueFreeBuffer(d->aq, d->buffers[i].buf);
      return allocbuf(d, i, bytes);
    }
  return NULL;
}


/**
 *
 */
static void
mac_audio_fini(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  if(d->aq)
    AudioQueueDispose(d->aq, true);
}


/**
 *
 */
static int
mac_audio_reconfig(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  int i;
  OSStatus r;

  if(d->aq) {
    AudioQueueDispose(d->aq, false);
    d->aq = NULL;

    for(i = 0; i < NUM_BUFS; i++) {
      d->buffers[i].buf = NULL;
      d->buffers[i].avail = 0;
      d->buffers[i].size = 0;
    }
  }

  AudioStreamBasicDescription desc = {0};
  
  // Coding

  desc.mFormatID = kAudioFormatLinearPCM;

  // Rate

  ad->ad_out_sample_rate = ad->ad_in_sample_rate;
  desc.mSampleRate = ad->ad_out_sample_rate;

  // Channel configuration

  ad->ad_out_channel_layout = AV_CH_LAYOUT_STEREO;
  desc.mChannelsPerFrame = 2;

  // Sample format
  int sample_size;

  switch(ad->ad_in_sample_format) {
  case AV_SAMPLE_FMT_S32:
  case AV_SAMPLE_FMT_S32P:
    ad->ad_out_sample_format  = AV_SAMPLE_FMT_S32;
    desc.mFormatFlags = kAudioFormatFlagIsSignedInteger;
    sample_size = sizeof(int32_t);
    break;

  case AV_SAMPLE_FMT_S16:
  case AV_SAMPLE_FMT_S16P:
    if(ad->ad_in_channel_layout == AV_CH_LAYOUT_STEREO) {
      ad->ad_out_sample_format  = AV_SAMPLE_FMT_S16;
      desc.mFormatFlags = kAudioFormatFlagIsSignedInteger;
      sample_size = sizeof(int16_t);
      break;
    }
    // FALLTHRU
  default:
    ad->ad_out_sample_format  = AV_SAMPLE_FMT_FLT;
    desc.mFormatFlags = kAudioFormatFlagIsFloat;
    sample_size = sizeof(float);
    break;
  }

  desc.mBytesPerFrame = desc.mChannelsPerFrame * sample_size;
  desc.mBitsPerChannel = 8 * sample_size;
  desc.mFramesPerPacket = 1;
  desc.mBytesPerPacket = desc.mBytesPerFrame;

  d->framesize = desc.mBytesPerFrame;

  TRACE(TRACE_DEBUG, "AudioQueue", "Init %d Hz", ad->ad_out_sample_rate);

  r = AudioQueueNewOutput(&desc, return_buf, d, audio_run_loop,
                          kCFRunLoopCommonModes, 0, &d->aq);
  if(r) {
    d->aq = NULL;
    TRACE(TRACE_ERROR, "AudioQueue", "AudioQueueNewOutput() error %d", r);
    return 1;
  }

  r = AudioQueueStart(d->aq, NULL);
  if(r) {
    TRACE(TRACE_ERROR, "AudioQueue", "AudioQueueStart() error %d", r);
    AudioQueueDispose(d->aq, false);
    d->aq = NULL;
    return 1;
  }
  AudioQueueSetParameter(d->aq, kAudioQueueParam_Volume, ad->ad_vol_scale);
  return 0;
}


/**
 *
 */
static void
mac_audio_pause(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  if(d->aq)
    AudioQueuePause(d->aq);
}


/**
 *
 */
static void
mac_audio_set_volume(audio_decoder_t *ad, float level)
{
  decoder_t *d = (decoder_t *)ad;
  if(d->aq)
    AudioQueueSetParameter(d->aq, kAudioQueueParam_Volume, level);
}


/**
 *
 */
static void
mac_audio_play(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  if(d->aq)
    AudioQueueStart(d->aq, NULL);
}


/**
 *
 */
static void
mac_audio_flush(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  if(d->aq)
    AudioQueueReset(d->aq);
}


/**
 *
 */
static int
mac_audio_deliver(audio_decoder_t *ad, int samples,
                  int64_t pts, int epoch)
{
  decoder_t *d = (decoder_t *)ad;
  int bytes = samples * d->framesize;

  if(d->underrun) {
    d->underrun = 0;
    usleep(40000);
  }

  AudioQueueBufferRef b = getbuf(d, bytes);
  if(b == NULL)
    return 1;

  uint8_t *data[8] = {0};
  data[0] = (uint8_t *)b->mAudioData;
  avresample_read(ad->ad_avr, data, samples);
  b->mAudioDataByteSize = bytes;

  AudioTimeStamp ats;
  AudioQueueEnqueueBufferWithParameters(d->aq, b, 0, NULL,
                                        0, 0, 0, NULL, NULL, &ats);

  if(ats.mFlags & kAudioTimeStampHostTimeValid &&
     pts != AV_NOPTS_VALUE) {

    const int64_t now = mach_absolute_time();
    int64_t t = now * timebase.numer / (timebase.denom * 1000);

    ad->ad_delay = t - arch_get_avtime();

    media_pipe_t *mp = ad->ad_mp;

    hts_mutex_lock(&mp->mp_clock_mutex);
    mp->mp_audio_clock_avtime = t;
    mp->mp_audio_clock_epoch = epoch;
    mp->mp_audio_clock = pts;
    hts_mutex_unlock(&mp->mp_clock_mutex);
  }
  return 0;
}

/**
 *
 */
static audio_class_t mac_audio_class = {
  .ac_alloc_size = sizeof(decoder_t),
  
  .ac_fini = mac_audio_fini,
  .ac_reconfig = mac_audio_reconfig,
  .ac_deliver_locked = mac_audio_deliver,

  .ac_pause = mac_audio_pause,
  .ac_play  = mac_audio_play,
  .ac_flush = mac_audio_flush,
  .ac_set_volume = mac_audio_set_volume,
};

audio_class_t *
audio_driver_init(struct prop *asettings)
{
  mach_timebase_info(&timebase);
  hts_thread_create_detached("audioloop", audio_thread, NULL, 0);
  while(1) {

    __sync_synchronize();
    if(audio_run_loop != NULL)
      break;
    usleep(100);
  }

  return &mac_audio_class;
}

