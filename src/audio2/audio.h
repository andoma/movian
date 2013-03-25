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
  int (*ac_deliver_unlocked)(struct audio_decoder *ad, int samples,
			     int64_t pts, int epoch);
  int (*ac_deliver_locked)(struct audio_decoder *ad, int samples,
                           int64_t pts, int epoch);

  void (*ac_pause)(struct audio_decoder *ad);
  void (*ac_play)(struct audio_decoder *ad);
  void (*ac_flush)(struct audio_decoder *ad);

} audio_class_t;


typedef struct audio_decoder {
  const audio_class_t *ad_ac;
  struct media_pipe *ad_mp;
  hts_thread_t ad_tid;

  struct AVFrame *ad_frame;
  int64_t ad_pts;
  int ad_epoch;

  int ad_tile_size;   // Number of samples to be delivered per round
  int ad_delay;       // Audio output delay in us

  int ad_paused;

  int ad_in_sample_rate;
  enum AVSampleFormat ad_in_sample_format;
  int64_t ad_in_channel_layout;

  int ad_out_sample_rate;
  enum AVSampleFormat ad_out_sample_format;
  int64_t ad_out_channel_layout;

  int ad_stereo_downmix; /* We can only output stereo so ask for downmix
			    as early as codec initialization */

  AVAudioResampleContext *ad_avr;

} audio_decoder_t;

audio_class_t *audio_driver_init(void);


