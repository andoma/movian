#include <alsa/asoundlib.h>
#include <assert.h>

#include "showtime.h"
#include "audio.h"
#include "media.h"
#include "alsa.h"

#define CHANNELS 2
#define FRAMES   2048
#define BUFFER_SIZE (CHANNELS * FRAMES)


#define SLOTS 4

LIST_HEAD(decoder_list, decoder);

static hts_mutex_t mixer_mutex;
static struct decoder_list decoders;
static int mixer_sample_rate;
static int buffer_delay;


typedef struct decoder {
  audio_decoder_t ad;
  int running;

  LIST_ENTRY(decoder) link;

  int16_t *tmp;  // BUFFER_SIZE * SLOTS

  int rptr;
  int wptr;
  int paused;
  int underrun;
  int64_t system_time[SLOTS];
  int delay[SLOTS];

  hts_cond_t cond;

  int64_t last_ts;

} decoder_t;


/**
 *
 */
static void
alsa_audio_fini(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;

  if(!d->running)
    return;
  d->running = 0;
  hts_mutex_lock(&mixer_mutex);
  LIST_REMOVE(d, link);
  hts_mutex_unlock(&mixer_mutex);
  hts_cond_destroy(&d->cond);
  free(d->tmp);
  d->tmp = NULL;
}


/**
 *
 */
static int
alsa_audio_reconfig(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;


  alsa_audio_fini(ad);

  d->running = 1;

  hts_cond_init(&d->cond, &mixer_mutex);

  ad->ad_out_sample_format = AV_SAMPLE_FMT_S16;
  ad->ad_out_sample_rate = 48000;
  ad->ad_out_channel_layout = AV_CH_LAYOUT_STEREO;
  ad->ad_tile_size = FRAMES;

  hts_mutex_lock(&mixer_mutex);
  LIST_INSERT_HEAD(&decoders, d, link);
  hts_mutex_unlock(&mixer_mutex);

  d->tmp = malloc(sizeof(uint16_t) * CHANNELS * FRAMES * SLOTS);
  return 0;
}


/**
 *
 */
static int
alsa_audio_deliver(audio_decoder_t *ad, int samples, int64_t pts, int epoch)
{
  decoder_t *d = (decoder_t *)ad;
  media_pipe_t *mp = ad->ad_mp;
  int c;

  int64_t ts;
  int delay = 0;

  uint8_t *planes[8] = {0};

  hts_mutex_lock(&mixer_mutex);

  while(((d->wptr + 1) & (SLOTS-1)) == d->rptr)
    // FIFO is full, wait
    hts_cond_wait(&d->cond, &mixer_mutex);

  ts  = d->system_time[d->wptr];
  ts += buffer_delay;

  hts_mutex_unlock(&mixer_mutex);

  planes[0] = (void *) (d->tmp + (d->wptr * BUFFER_SIZE));

  c = avresample_read(ad->ad_avr, planes, FRAMES);

  assert(c == FRAMES);

  d->wptr = (d->wptr + 1) & (SLOTS - 1);

  if(ts && pts != AV_NOPTS_VALUE) {
    hts_mutex_lock(&mp->mp_clock_mutex);

    if(mp->mp_set_audio_clock != NULL)
      mp->mp_set_audio_clock(mp, pts - buffer_delay, epoch, 0);

    //    TRACE(TRACE_DEBUG, "MALSA", "%s: ts is %lld", mp->mp_name, ts);

    mp->mp_audio_clock_avtime = ts;
    mp->mp_audio_clock = pts;
    mp->mp_audio_clock_epoch = epoch;
    hts_mutex_unlock(&mp->mp_clock_mutex);

    ad->ad_delay = delay;
  }
  return 0;
}


/**
 *
 */
static void
alsa_audio_pause(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  hts_mutex_lock(&mixer_mutex);
  d->paused = 1;
  hts_mutex_unlock(&mixer_mutex);
}


/**
 *
 */
static void
alsa_audio_play(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  hts_mutex_lock(&mixer_mutex);
  d->paused = 0;
  hts_mutex_unlock(&mixer_mutex);
}


/**
 *
 */
static int
alsa_audio_flush(audio_decoder_t *ad, int lasting)
{
  decoder_t *d = (decoder_t *)ad;

  hts_mutex_lock(&mixer_mutex);
  d->wptr = 0;
  d->rptr = 0;
  hts_mutex_unlock(&mixer_mutex);
  return 0;
}


/**
 *
 */
static audio_class_t alsa_audio_class = {
  .ac_alloc_size       = sizeof(decoder_t),
  .ac_fini             = alsa_audio_fini,
  .ac_reconfig         = alsa_audio_reconfig,
  .ac_deliver_unlocked = alsa_audio_deliver,
  .ac_pause            = alsa_audio_pause,
  .ac_play             = alsa_audio_play,
  .ac_flush            = alsa_audio_flush,
  .ac_decoder_can_low_pri = 1,
};



/**
 *
 */
static void *
alsa_render_thread(void *aux)
{
  snd_pcm_t *h = aux;
  int c;
  int16_t buffer[BUFFER_SIZE];
  snd_pcm_status_t *status;
  int64_t samples = 0;

  snd_pcm_status_alloca(&status);

  while(1) {
    c = snd_pcm_wait(h, 100);
    if(c >= 0) {
      c = snd_pcm_avail_update(h);
    }
    if(c == -EPIPE) {
      snd_pcm_prepare(h);
      samples = 0;
      TRACE(TRACE_ERROR, "ALSA", "Audio underrun");
      continue;
    }

    int num = 0;
    decoder_t *d;

    hts_mutex_lock(&mixer_mutex);

    int err;

    int64_t ts = 0;

    if ((err = snd_pcm_status(h, status)) >= 0) {
      snd_htimestamp_t hts;
      snd_pcm_status_get_trigger_htstamp(status, &hts);
      ts = hts.tv_sec * 1000000LL + hts.tv_nsec / 1000;
      ts += samples * 1000000LL / mixer_sample_rate;
    }

    LIST_FOREACH(d, &decoders, link) {

      if(d->paused)
        continue;

      if(d->wptr == d->rptr) {
        d->underrun = 1;
        continue;
      }

      const int16_t *src = d->tmp + (d->rptr * BUFFER_SIZE);

      if(num == 0) {
        memcpy(buffer, src, BUFFER_SIZE * sizeof(int16_t));
      } else {
        for(int i = 0; i < BUFFER_SIZE; i++)
          buffer[i] += src[i]; // TODO: Vectorization and clipping
      }
      d->system_time[d->rptr] = ts;

      d->rptr = (d->rptr + 1) & (SLOTS - 1);
      hts_cond_signal(&d->cond);
      num++;
    }
    hts_mutex_unlock(&mixer_mutex);

    if(num == 0)
      memset(buffer, 0, BUFFER_SIZE * sizeof(int16_t));

    int r = snd_pcm_writei(h, buffer, FRAMES);
    if(r > 0)
      samples += r;
  }
  return NULL;
}


/**
 *
 */
static void
mixer_init(void)
{
  const char *dev = alsa_get_devicename();
  int r;
  snd_pcm_t *h;

  if((r = snd_pcm_open(&h, dev, SND_PCM_STREAM_PLAYBACK, 0) < 0)) {
    TRACE(TRACE_ERROR, "ALSA", "Unable to open %s -- %s", 
	  dev, snd_strerror(r));
    exit(1);
  }

  r = snd_pcm_set_params(h, SND_PCM_FORMAT_S16, SND_PCM_ACCESS_RW_INTERLEAVED,
			 2, 48000, 0, 100000);

  if(r < 0) {
    TRACE(TRACE_ERROR, "ALSA", "Unable to set params on %s -- %s", 
	  dev, snd_strerror(r));
    exit(1);
  }

  snd_pcm_hw_params_t *hwp;
  snd_pcm_hw_params_alloca(&hwp);

  snd_pcm_hw_params_current(h, hwp);

  unsigned int val;
  snd_pcm_uframes_t psize, bsize;

  snd_pcm_hw_params_get_rate(hwp, &val, 0);
  mixer_sample_rate = val;
  snd_pcm_hw_params_get_period_size(hwp, &psize, 0);
  snd_pcm_hw_params_get_buffer_size(hwp, &bsize);

  buffer_delay = FRAMES * SLOTS * 1000000LL / mixer_sample_rate;

  hts_thread_create_detached("ALSA output", alsa_render_thread, h,
                             THREAD_PRIO_AUDIO);
}


/**
 *
 */
audio_class_t *
audio_driver_init(void)
{
  mixer_init();

  return &alsa_audio_class;
}

