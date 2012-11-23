#include <assert.h>
#include <AudioToolbox/AudioQueue.h>
#include <CoreAudio/HostTime.h>

#include "showtime.h"
#include "audio.h"
#include "media.h"

#define NUM_BUFS 3

typedef struct decoder {
  audio_decoder_t ad;
  AudioQueueRef aq;
  int framesize;
  struct {
    AudioQueueBufferRef buf;
    int avail;
    int size;
  } buffers[NUM_BUFS];
} decoder_t;


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
        printf("Underrun!\n");
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

  r = AudioQueueNewOutput(&desc, return_buf, d, CFRunLoopGetMain(),
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
    int64_t t = AudioConvertHostTimeToNanos(ats.mHostTime) / 1000LL;
    ad->ad_delay = t - showtime_get_avtime();

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
};

audio_class_t *
audio_driver_init(void)
{
  return &mac_audio_class;
}

