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
#include <unistd.h>

#include "main.h"
#include "audio2/audio.h"
#include "media/media.h"


typedef struct decoder {
  audio_decoder_t ad;
} decoder_t;


/**
 *
 */
static void
dummy_audio_fini(audio_decoder_t *ad)
{
}


/**
 *
 */
static int
dummy_audio_reconfig(audio_decoder_t *ad)
{

  dummy_audio_fini(ad);

  ad->ad_out_sample_format = AV_SAMPLE_FMT_S16;
  ad->ad_out_sample_rate = 48000;
  ad->ad_out_channel_layout = AV_CH_LAYOUT_STEREO;
  ad->ad_tile_size = 1024;

  return 0;
}


/**
 *
 */
static int
dummy_audio_deliver(audio_decoder_t *ad, int samples, int64_t pts, int epoch)
{
  int sleeptime = 1000000LL * samples / 48000;
  usleep(sleeptime);
  return 0;
}


/**
 *
 */
static void
dummy_audio_pause(audio_decoder_t *ad)
{
}


/**
 *
 */
static void
dummy_audio_play(audio_decoder_t *ad)
{
}


/**
 *
 */
static void
dummy_audio_flush(audio_decoder_t *ad)
{
}


/**
 *
 */
static audio_class_t dummy_audio_class = {
  .ac_alloc_size       = sizeof(decoder_t),
  .ac_fini             = dummy_audio_fini,
  .ac_reconfig         = dummy_audio_reconfig,
  .ac_deliver_unlocked = dummy_audio_deliver,
  .ac_pause            = dummy_audio_pause,
  .ac_play             = dummy_audio_play,
  .ac_flush            = dummy_audio_flush,
};



/**
 *
 */
audio_class_t *
audio_driver_init(struct prop *asettings)
{
  return &dummy_audio_class;
}

