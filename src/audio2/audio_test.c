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
#include <math.h>
#include <unistd.h>

#include "audio.h"
#include "prop/prop.h"
#include "fileaccess/fileaccess.h"
#include "fileaccess/fa_libav.h"
#include "misc/minmax.h"

#include <libavformat/avformat.h>


typedef float (generator_t)(int samples);

static int signal_type;
static int test_channels[8];
static int sample_rate = 48000;

typedef struct pcm_sound {
  int16_t *data;
  int samples;
} pcm_sound_t;


/**
 *
 */
static float
gen_sinewave(int sample)
{
  int a = (sample * 4096LL * 440 / sample_rate) & 0xfff;
  return sin(a * 0.00153398f) * 0.25f;
}


/**
 *
 */
static float
gen_white_noise(int sample)
{
  static uint32_t seed;
  seed = seed * 196314165 + 907633515;
  
  union {
    uint32_t u32;
    float f;
  } u;

  u.u32 = (seed >> 9) | 0x40000000;
  return u.f - 3.0f;
}


/**
 *
 */
static float
gen_pink_noise(int sample)
{
  int k = __builtin_ctz(sample) & 0xf;
  static float octaves[16];
  static float x;

  float prev = octaves[k]; 

  while(1) {
    float r = gen_white_noise(0) * 0.5f;
    octaves[k] = r;
    r -= prev;
    x += r;
    if(x < -4.0f || x > 4.0f)
      x -= r;
    else
      break;
  }
  return (gen_white_noise(0) * 0.5f + x) * 0.125f; 
}


/**
 *
 */
static int
unpack_audio(const char *url, pcm_sound_t *out)
{
  AVFormatContext *fctx;
  AVCodecContext *ctx = NULL;
 
  char errbuf[256];
  fa_handle_t *fh = fa_open_ex(url, errbuf, sizeof(errbuf), 0, NULL);
  if(fh == NULL) {
  fail:
    TRACE(TRACE_ERROR, "audiotest", "Unable to open %s -- %s", url, errbuf);
    return -1;
  }
  AVIOContext *avio = fa_libav_reopen(fh, 0);

  if((fctx = fa_libav_open_format(avio, url, errbuf, sizeof(errbuf), NULL,
                                  0, -1, -1)) == NULL) {
    fa_libav_close(avio);
    goto fail;
  }

  int s;
  for(s = 0; s < fctx->nb_streams; s++) {
    ctx = fctx->streams[s]->codec;

    if(ctx->codec_type != AVMEDIA_TYPE_AUDIO)
      continue;

    const AVCodec *codec = avcodec_find_decoder(ctx->codec_id);
    if(codec == NULL)
      continue;

    if(avcodec_open2(ctx, codec, NULL) < 0) {
      TRACE(TRACE_ERROR, "audiotest", "Unable to codec");
      continue;
    }
    break;
  }

  AVFrame *frame = av_frame_alloc();

  out->samples = 0;
  out->data = NULL;

  while(1) {
    AVPacket pkt;
    int r;

    r = av_read_frame(fctx, &pkt);
    if(r == AVERROR(EAGAIN))
      continue;
    if(r)
      break;
    if(pkt.stream_index == s) {
      int got_frame;
      while(pkt.size) {
	r = avcodec_decode_audio4(ctx, frame, &got_frame, &pkt);
	if(r < 0)
	  break;
	if(got_frame) {
	  int ns = frame->nb_samples * 2;

	  out->data = realloc(out->data, sizeof(int16_t) * (out->samples + ns));

	  const int16_t *src = (const int16_t *)frame->data[0];

	  for(int i = 0; i < frame->nb_samples; i++) {
	    int16_t v = src[i];
	    out->data[out->samples + i * 2 + 0] = v;
	    out->data[out->samples + i * 2 + 1] = v;
	  }
	  out->samples += ns;
	}
	pkt.data += r;
	pkt.size -= r;
      }
    }
    av_free_packet(&pkt);
  }
  av_frame_free(&frame);
  avcodec_close(ctx);
  fa_libav_close_format(fctx, 0);
  return 0;
}




/**
 *
 */
static void
unpack_speaker_positions(pcm_sound_t v[8])
{
  unpack_audio("dataroot://res/speaker_positions/fl.mp3", &v[0]);
  unpack_audio("dataroot://res/speaker_positions/fr.mp3", &v[1]);
  unpack_audio("dataroot://res/speaker_positions/c.mp3", &v[2]);
  unpack_audio("dataroot://res/speaker_positions/lfe.mp3", &v[3]);
  unpack_audio("dataroot://res/speaker_positions/sl.mp3", &v[4]);
  unpack_audio("dataroot://res/speaker_positions/sr.mp3", &v[5]);
  unpack_audio("dataroot://res/speaker_positions/rl.mp3", &v[6]);
  unpack_audio("dataroot://res/speaker_positions/rr.mp3", &v[7]);
}

/**
 *
 */
static void *
test_generator_thread(void *aux)
{
  media_pipe_t *mp = aux;
  media_buf_t *mb = NULL;
  event_t *e;
  media_queue_t *mq = &mp->mp_audio;
  pcm_sound_t voices[8];
  AVFrame *frame = av_frame_alloc();
  AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AC3);
  AVCodecContext *ctx = avcodec_alloc_context3(codec);

  ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
  ctx->sample_rate = 48000;
  ctx->channel_layout = AV_CH_LAYOUT_5POINT1;

  if(avcodec_open2(ctx, codec, NULL) < 0) {
    TRACE(TRACE_ERROR, "audio", "Unable to open encoder");
    return NULL;
  }

  float *genbuf[8];
  for(int i = 0; i < 8; i++) {
    genbuf[i] = av_malloc(ctx->frame_size * sizeof(float));
    frame->data[i] = (void *)genbuf[i];
  }

  frame->nb_samples = ctx->frame_size;

  media_codec_t *mc = media_codec_create(AV_CODEC_ID_AC3, 0,
					 NULL, NULL, NULL, mp);

  mp->mp_audio.mq_stream = 0;
  mp_configure(mp, 0, MP_BUFFER_NONE, 0, "testsignal");
  mp_become_primary(mp);

  int sample = 0;

  unpack_speaker_positions(voices);

  while(1) {

    if(mb == NULL) {

      int got_packet;
      AVPacket pkt = {0};

      generator_t *g;

      switch(signal_type) {

      case 0:
	for(int c = 0; c < 8; c++) {
	  int z = ctx->frame_size;
	  if(test_channels[c]) {

	    int j = sample & 0xffff;
	    int to_copy = MIN(ctx->frame_size, (voices[c].samples - j));

	    if(to_copy < 0)
	      to_copy = 0;

	    for(int i = 0; i < to_copy; i++) {
	      genbuf[c][i] = voices[c].data[j+i] / 32767.0f;
	    }
	    z = ctx->frame_size - to_copy;
	  }
	  memset(genbuf[c] + ctx->frame_size - z, 0, sizeof(float) * z);
	}
	sample += ctx->frame_size;

	goto encode;
	
      default: g = &gen_pink_noise; break;
      case 2:  g = &gen_sinewave;   break;
      }
	
      for(int i = 0; i < ctx->frame_size; i++) {
	float x = g(sample);
	for(int c = 0; c < 8; c++) {
	  genbuf[c][i] = test_channels[c] ? x : 0;
	}
	sample++;
      }

    encode:
      av_init_packet(&pkt);
      int r = avcodec_encode_audio2(ctx, &pkt, frame, &got_packet);
      if(!r && got_packet) {
	mb = media_buf_from_avpkt_unlocked(mp, &pkt);
	av_free_packet(&pkt);
      } else {
	sleep(1);
      }

      mb->mb_cw = media_codec_ref(mc);

      mb->mb_data_type = MB_AUDIO;
      mb->mb_pts = PTS_UNSET;

    }

    if((e = mb_enqueue_with_events(mp, mq, mb)) == NULL) {
      mb = NULL; /* Enqueue succeeded */
      continue;
    }

    if(event_is_type(e, EVENT_EXIT)) {
      mp_flush(mp);
      break;
    }
    event_release(e);
  }  
  av_frame_free(&frame);

  for(int i = 0; i < 8; i++)
    av_freep(&genbuf[i]);

  for(int i = 0; i < 8; i++)
    free(voices[i].data);

  media_codec_deref(mc);

  return NULL;
}

static hts_thread_t generator_tid;
static media_pipe_t *gen_mp;

/**
 *
 */
static void
enable_test_thread(int on)
{
  if(!generator_tid == !on)
    return;

  if(on) {
    assert(gen_mp == NULL);

    gen_mp = mp_create("testsignal", MP_PRIMABLE);
    hts_thread_create_joinable("audiotest", &generator_tid,
			       test_generator_thread, gen_mp,
			       THREAD_PRIO_DEMUXER);
  } else {
    event_t *e = event_create_type(EVENT_EXIT);
    mp_enqueue_event(gen_mp, e);
    event_release(e);
    hts_thread_join(&generator_tid);
    mp_shutdown(gen_mp);
    mp_destroy(gen_mp);
    gen_mp = NULL;
    generator_tid = 0;
  }
}

/**
 *
 */
static void
enable_set_signal(void *opaque, int v)
{
  enable_test_thread(v);
}

/**
 *
 */
static void
add_ch_bool(prop_t *title, int id, int def, prop_t *asettings)
{
  setting_create(SETTING_BOOL, asettings, SETTINGS_INITIAL_UPDATE,
		 SETTING_TITLE(title),
		 SETTING_WRITE_BOOL(&test_channels[id]),
		 SETTING_VALUE(def),
		 NULL);

}

/**
 *
 */
void
audio_test_init(prop_t *asettings)
{
  settings_create_separator(asettings, _p("Audio test"));

  setting_create(SETTING_BOOL, asettings, SETTINGS_INITIAL_UPDATE,
		 SETTING_TITLE(_p("Play test signal")),
                 SETTING_CALLBACK(enable_set_signal, NULL),
		 NULL);

  setting_create(SETTING_MULTIOPT, asettings, SETTINGS_INITIAL_UPDATE,
		 SETTING_TITLE(_p("Test signal type")),
		 SETTING_WRITE_INT(&signal_type),
		 SETTING_OPTION("0", _p("Speaker position")),
		 SETTING_OPTION("1", _p("Pink noise")),
		 SETTING_OPTION("2", _p("440Hz sinewave -12dB")),
		 NULL);

  add_ch_bool(_p("Front Left"),  0, 1, asettings);
  add_ch_bool(_p("Center"),      2, 0, asettings);
  add_ch_bool(_p("Front Right"), 1, 1, asettings);
  add_ch_bool(_p("LFE"),         3, 0, asettings);
  add_ch_bool(_p("Surround Left"),   4, 0, asettings);
  add_ch_bool(_p("Surround Right"),  5, 0, asettings);
  add_ch_bool(_p("Rear Left"),   6, 0, asettings);
  add_ch_bool(_p("Rear Right"),  7, 0, asettings);

}
