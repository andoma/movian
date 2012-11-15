#pragma once

#include <libavutil/samplefmt.h>
#include <libavresample/avresample.h>

#include "arch/threads.h"

struct media_pipe;
struct audio_decoder;

typedef struct audio_class {
  size_t ac_alloc_size;
  int (*ac_init)(struct audio_decoder *ad);
  void (*ac_fini)(struct audio_decoder *ad);
  int (*ac_reconfig)(struct audio_decoder *ad);
  int (*ac_deliver)(struct audio_decoder *ad, int samples,
                    int64_t pts, int epoch);

  void (*ac_pause)(struct audio_decoder *ad);
  void (*ac_play)(struct audio_decoder *ad);
  void (*ac_flush)(struct audio_decoder *ad);

} audio_class_t;


typedef struct audio_decoder {
  const audio_class_t *ad_ac;
  struct media_pipe *ad_mp;
  hts_thread_t ad_tid;

  int ad_num_samples; // Max number of samples to be delivered per round
  int ad_delay;       // Audio output delay in us

  int ad_in_sample_rate;
  enum AVSampleFormat ad_in_sample_format;
  int64_t ad_in_channel_layout;

  int ad_out_sample_rate;
  enum AVSampleFormat ad_out_sample_format;
  int64_t ad_out_channel_layout;

  AVAudioResampleContext *ad_avr;

} audio_decoder_t;


void audio_init(void);

void audio_fini(void);

audio_decoder_t *audio_decoder_create(struct media_pipe *mp);

void audio_decoder_destroy(audio_decoder_t *ad);

void audio_set_clock(struct media_pipe *mp, int64_t pts, int64_t delay,
                     int epoch);

struct AVFrame;

int audio_decoder_configure(audio_decoder_t *ad, const struct AVFrame *avf);

audio_class_t *audio_driver_init(void);

