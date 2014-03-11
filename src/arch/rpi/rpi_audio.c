/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
#include <string.h>
#include <unistd.h>
#include <math.h>

#include <bcm_host.h>

#include "audio2/audio.h"
#include "omx.h"
#include "media.h"

//#include <interface/vmcs_host/vc_tvservice.h>


typedef struct decoder {
  audio_decoder_t ad;
  omx_component_t *d_decoder;
  omx_component_t *d_mixer;
  omx_component_t *d_render;

  omx_tunnel_t *d_mixer_tun;
  omx_tunnel_t *d_render_tun;


  omx_tunnel_t *d_clock_tun;
  int d_bpf; // Bytes per frame
  int d_last_epoch;

  int d_channels;

  float d_gain;
  float d_master_volume;
  float d_master_mute;

  char d_can_8ch_pcm;

  double d_matrix[8][8];

} decoder_t;


typedef struct {
  uint32_t Data1;
  uint16_t Data2, Data3;
  uint8_t  Data4[8];
} __attribute__((__packed__)) GUID;


typedef struct tWAVEFORMATEX {
  uint16_t   wFormatTag;
  uint16_t   nChannels;
  uint32_t   nSamplesPerSec;
  uint32_t   nAvgBytesPerSec;
  uint16_t   nBlockAlign;
  uint16_t   wBitsPerSample;
  uint16_t   cbSize;
} __attribute__((__packed__)) WAVEFORMATEX;


typedef struct {
  WAVEFORMATEX Format;
  union {
    uint16_t wValidBitsPerSample;
    uint16_t wSamplesPerBlock;
    uint16_t wReserved;
  } Samples;
  uint32_t dwChannelMask;
  GUID SubFormat;
} __attribute__((__packed__)) WAVEFORMATEXTENSIBLE;


#define WAVE_FORMAT_PCM               0x0001

static const GUID KSDATAFORMAT_SUBTYPE_PCM = {
  WAVE_FORMAT_PCM,
  0x0000, 0x0010,
  {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}
};

static float master_volume = 1.0;
static int master_mute;

/**
 *
 */
static int
rpi_audio_init(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  d->d_gain = 1.0f;
  d->d_master_volume = master_volume;
  d->d_master_mute = master_mute;

  int x;

  x = vc_tv_hdmi_audio_supported(EDID_AudioFormat_ePCM, 8,
				 EDID_AudioSampleRate_e48KHz,
				 EDID_AudioSampleSize_16bit);
  
  d->d_can_8ch_pcm = !x;

  if(!d->d_can_8ch_pcm)
    ad->ad_stereo_downmix = 1;
  return 0;
}


/**
 *
 */
static void
rpi_audio_fini(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;

  if(d->d_render == NULL)
    return;

  if(d->d_render_tun != NULL) {
    omx_tunnel_destroy(d->d_render_tun);
    d->d_render_tun = NULL;
  }

  if(d->d_mixer_tun != NULL) {
    omx_tunnel_destroy(d->d_mixer_tun);
    d->d_mixer_tun = NULL;
  }

  if(d->d_clock_tun != NULL) {
    omx_tunnel_destroy(d->d_clock_tun);
    d->d_clock_tun = NULL;
  }
  omx_wait_buffers(d->d_decoder);
  omx_set_state(d->d_decoder, OMX_StateIdle);
  omx_release_buffers(d->d_decoder, 120);

  omx_set_state(d->d_decoder, OMX_StateLoaded);
  omx_component_destroy(d->d_decoder);

  if(d->d_mixer != NULL) {
    omx_set_state(d->d_mixer, OMX_StateIdle);
    omx_set_state(d->d_mixer, OMX_StateLoaded);
    omx_component_destroy(d->d_mixer);
  }

  d->d_mixer = NULL;

  omx_set_state(d->d_render, OMX_StateIdle);
  omx_set_state(d->d_render, OMX_StateLoaded);
  omx_component_destroy(d->d_render);
}


const static uint8_t channel_swizzle[8] = {0,1,3,2,4,5,6,7};
/**
 *
 */
static void
set_mixer_matrix(decoder_t *d)
{
  if(d->d_mixer == NULL)
    return;

  OMX_CONFIG_BRCMAUDIODOWNMIXCOEFFICIENTS8x8 mix;
  OMX_INIT_STRUCTURE(mix);

  float gain = d->d_master_mute ? 0.0f : (d->d_gain * d->d_master_volume);

  for(int r = 0; r < 8; r++) {
    int row = channel_swizzle[r];
    for(int c = 0; c < 8; c++) {
      double v = d->d_matrix[row][c] * gain;
      mix.coeff[r * 8 + c] = v * 65536.0;
    }
  }

  mix.nPortIndex = 232;
  omxchk(OMX_SetConfig(d->d_mixer->oc_handle,
		       OMX_IndexConfigBrcmAudioDownmixCoefficients8x8, &mix));
}

/**
 *
 */
static void
rpi_audio_port_settings_changed(omx_component_t *oc)
{
  decoder_t *d = oc->oc_opaque;

  if(d->d_render_tun != NULL) {
    omx_tunnel_destroy(d->d_render_tun);
    d->d_render_tun = NULL;
  }
 
  if(d->d_mixer_tun != NULL) {
    omx_tunnel_destroy(d->d_mixer_tun);
    d->d_mixer_tun = NULL;
  }

  omx_set_state(d->d_render, OMX_StateIdle);

  if(d->d_mixer != NULL) {

    omx_set_state(d->d_mixer, OMX_StateIdle);

    // Copy PCM settings 

    OMX_AUDIO_PARAM_PCMMODETYPE pcm;
    OMX_INIT_STRUCTURE(pcm);

    pcm.nPortIndex = 121;

    omxchk(OMX_GetParameter(d->d_decoder->oc_handle,
			    OMX_IndexParamAudioPcm, &pcm));

    pcm.nPortIndex = 231;
    omxchk(OMX_SetParameter(d->d_mixer->oc_handle,
			    OMX_IndexParamAudioPcm, &pcm));

    pcm.nPortIndex = 232;
    omxchk(OMX_SetParameter(d->d_mixer->oc_handle,
			    OMX_IndexParamAudioPcm, &pcm));

    memset(d->d_matrix, 0, sizeof(double) * 64);
    for(int i = 0; i < 8; i++)
      d->d_matrix[i][i] = 1.0f;

    set_mixer_matrix(d);

    d->d_mixer_tun =
      omx_tunnel_create(d->d_decoder, 121, d->d_mixer, 232,
			"adecoder -> mixer");

    d->d_render_tun =
      omx_tunnel_create(d->d_mixer, 231, d->d_render, 100,
			"amixer -> render");

  } else {

    d->d_render_tun =
      omx_tunnel_create(d->d_decoder, 121, d->d_render, 100,
			"decoder -> render");
  }

  if(d->d_mixer != NULL)
    omx_set_state(d->d_mixer, OMX_StateExecuting);

  omx_set_state(d->d_render, OMX_StateExecuting);
}


/**
 *
 */
static void
decoder_init(decoder_t *d, int with_mixer)
{
  audio_decoder_t *ad = &d->ad;
  media_pipe_t *mp = ad->ad_mp;

  rpi_audio_fini(ad);

  d->d_render = omx_component_create("OMX.broadcom.audio_render",
				     &ad->ad_mp->mp_mutex,
				     &ad->ad_mp->mp_audio.mq_avail);

  if(with_mixer) {
    d->d_mixer = omx_component_create("OMX.broadcom.audio_mixer",
				      &ad->ad_mp->mp_mutex,
				      &ad->ad_mp->mp_audio.mq_avail);
  }

  const char *device = "hdmi";

  OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
  OMX_INIT_STRUCTURE(audioDest);
  strncpy((char *)audioDest.sName, device, strlen(device));
  omxchk(OMX_SetConfig(d->d_render->oc_handle,
		       OMX_IndexConfigBrcmAudioDestination, &audioDest));


  if(mp->mp_extra)
    d->d_clock_tun = omx_tunnel_create(omx_get_clock(mp), 80, d->d_render, 101,
				       "clock -> audio");


  d->d_decoder = omx_component_create("OMX.broadcom.audio_decode",
				     &ad->ad_mp->mp_mutex,
				     &ad->ad_mp->mp_audio.mq_avail);

}


/**
 *
 */
static int
rpi_audio_config_pcm(decoder_t *d)
{
  WAVEFORMATEXTENSIBLE wfe = {};
  audio_decoder_t *ad = &d->ad;
  int bps;
  int channels;

  switch(ad->ad_in_sample_format) {
  case AV_SAMPLE_FMT_FLTP:
  case AV_SAMPLE_FMT_FLT:
    wfe.Format.wFormatTag = 0x8000;
    bps = 32;
    ad->ad_out_sample_format = AV_SAMPLE_FMT_FLTP;
    break;
#if 0
  case AV_SAMPLE_FMT_S32:
    wfe.Format.wFormatTag = WAVE_FORMAT_PCM;
    bps = 32;
    ad->ad_out_sample_format = AV_SAMPLE_FMT_S32;
    break;
#endif
  default:
    wfe.Format.wFormatTag = WAVE_FORMAT_PCM;
    bps = 16;
    ad->ad_out_sample_format = AV_SAMPLE_FMT_S16;
    break;
  }

  switch(ad->ad_in_channel_layout) {

  default:
    if(d->d_can_8ch_pcm) {
      TRACE(TRACE_DEBUG, "RPI", "8 Channel PCM support detected");
      ad->ad_out_channel_layout = AV_CH_LAYOUT_7POINT1;
      channels = 8;
      break;
    }

    // FALLTHRU

  case AV_CH_LAYOUT_MONO:
  case AV_CH_LAYOUT_STEREO:
    TRACE(TRACE_DEBUG, "RPI", "2 Channel PCM support detected");
    ad->ad_out_channel_layout = AV_CH_LAYOUT_STEREO;
    channels = 2;
    break;

  }

  d->d_channels = channels;

  /**
   * Configure input port
   */
  ad->ad_tile_size = 1024;
  d->d_bpf = channels * (bps >> 3);
 
  OMX_PARAM_PORTDEFINITIONTYPE portParam;
  OMX_INIT_STRUCTURE(portParam);
  portParam.nPortIndex = 120;

  omxchk(OMX_GetParameter(d->d_decoder->oc_handle, OMX_IndexParamPortDefinition,
			  &portParam));

  portParam.format.audio.eEncoding = OMX_AUDIO_CodingPCM;
  portParam.nBufferSize = d->d_bpf * ad->ad_tile_size;
  portParam.nBufferCountActual = 4;

  omxchk(OMX_SetParameter(d->d_decoder->oc_handle,
			  OMX_IndexParamPortDefinition, &portParam));
 
  /**
   * Configure input format
   */

  OMX_AUDIO_PARAM_PORTFORMATTYPE formatType;
  OMX_INIT_STRUCTURE(formatType);
  formatType.nPortIndex = 120;

  formatType.eEncoding =  OMX_AUDIO_CodingPCM;

  omxchk(OMX_SetParameter(d->d_decoder->oc_handle,
			  OMX_IndexParamAudioPortFormat, &formatType));
  
  omx_set_state(d->d_decoder, OMX_StateIdle);
  omx_alloc_buffers(d->d_decoder, 120);
  omx_set_state(d->d_decoder, OMX_StateExecuting);

  int bytes_per_sec =  ad->ad_in_sample_rate * (bps >> 3) * channels;

  wfe.Samples.wSamplesPerBlock    = 0;
  wfe.Format.nChannels            = channels;
  wfe.Format.nBlockAlign          = channels * (bps >> 3);
  wfe.Format.nSamplesPerSec       = ad->ad_in_sample_rate;
  wfe.Format.nAvgBytesPerSec      = bytes_per_sec;
  wfe.Format.wBitsPerSample       = bps;
  wfe.Samples.wValidBitsPerSample = bps;
  wfe.Format.cbSize               = 0;
  wfe.dwChannelMask               = ad->ad_out_channel_layout;
  wfe.SubFormat                   = KSDATAFORMAT_SUBTYPE_PCM;

  ad->ad_out_sample_rate = ad->ad_in_sample_rate;

  d->d_decoder->oc_port_settings_changed_cb = rpi_audio_port_settings_changed;
  d->d_decoder->oc_opaque = d;

  OMX_BUFFERHEADERTYPE *buf = omx_get_buffer(d->d_decoder);
  buf->nOffset = 0;
  buf->nFilledLen = sizeof(wfe);
  memcpy(buf->pBuffer, &wfe, buf->nFilledLen);
  buf->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
  omxchk(OMX_EmptyThisBuffer(d->d_decoder->oc_handle, buf));
  return 0;
}


/**
 *
 */
static int
rpi_audio_reconfig(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  decoder_init(d, 1);
  return rpi_audio_config_pcm(d);
}


/**
 *
 */
static int
rpi_audio_deliver(audio_decoder_t *ad, int samples, int64_t pts, int epoch)
{
  decoder_t *d = (decoder_t *)ad;
  //  int bytes = samples * d->d_bpf;
  omx_component_t *oc = d->d_decoder;
  OMX_BUFFERHEADERTYPE *buf;

  if(ad->ad_discontinuity && pts == PTS_UNSET && ad->ad_mp->mp_extra != NULL) {
    avresample_read(ad->ad_avr, NULL, samples);
    return 0;
  }

  if((buf = oc->oc_avail) == NULL)
    return 1;

  oc->oc_avail = buf->pAppPrivate;
  oc->oc_inflight_buffers++;

  assert(samples == ad->ad_tile_size);

  uint8_t *data[8] = {0};

  if(ad->ad_out_sample_format == AV_SAMPLE_FMT_FLTP) {
    for(int i = 0; i < d->d_channels;i++)
      data[i] = (uint8_t *)buf->pBuffer + samples * sizeof(float) * i;
  } else {
    data[0] = (uint8_t *)buf->pBuffer;
  }
  int r = avresample_read(ad->ad_avr, data, samples);

  hts_mutex_unlock(&ad->ad_mp->mp_mutex);

  if(d->d_last_epoch != epoch) {

    if(pts != PTS_UNSET)
      d->d_last_epoch = epoch;

    buf->nFlags |= OMX_BUFFERFLAG_DISCONTINUITY;
  }

  if(ad->ad_discontinuity) {
    buf->nFlags |= OMX_BUFFERFLAG_STARTTIME | OMX_BUFFERFLAG_DISCONTINUITY;
    ad->ad_discontinuity = 0;
  }

  buf->nOffset = 0;
  buf->nFilledLen = r * d->d_bpf;

  if(pts != PTS_UNSET)
    buf->nTimeStamp = omx_ticks_from_s64(pts);
  else
    buf->nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;

  if(d->d_master_volume != master_volume || d->d_master_mute != master_mute) {
    d->d_master_volume = master_volume;
    d->d_master_mute = master_mute;
    set_mixer_matrix(d);
  }


  omxchk(OMX_EmptyThisBuffer(oc->oc_handle, buf));
  hts_mutex_lock(&ad->ad_mp->mp_mutex);
  return 0;
}


/**
 *
 */
static void
rpi_audio_flush(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  if(d->d_decoder) {
    omx_flush_port(d->d_decoder, 121);
    omx_flush_port(d->d_decoder, 120);
  }

  if(d->d_mixer) {
    omx_flush_port(d->d_mixer, 231);
    omx_flush_port(d->d_mixer, 232);
  }

  if(d->d_render)
    omx_flush_port(d->d_render, 100);
}


/**
 *
 */
static void
rpi_audio_pause(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  if(d->d_render != NULL)
    omx_set_state(d->d_render, OMX_StatePause);
}


/**
 *
 */
static void
rpi_audio_play(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  if(d->d_render != NULL)
    omx_set_state(d->d_render, OMX_StateExecuting);
}


/**
 *
 */
static void
rpi_set_volume(audio_decoder_t *ad, float scale)
{
  decoder_t *d = (decoder_t *)ad;
  d->d_gain = scale;
  if(d->d_mixer != NULL)
    set_mixer_matrix(d);
}


/**
 *
 */
static int
rpi_get_mode(audio_decoder_t *ad, int codec)
{
  decoder_t *d = (decoder_t *)ad;
  int encoding = OMX_AUDIO_CodingDTS;

  switch(codec) {
  default:
    return AUDIO_MODE_PCM;
  }

  decoder_init(d, 0);

  OMX_CONFIG_BOOLEANTYPE boolType;
  OMX_INIT_STRUCTURE(boolType);
  boolType.bEnabled = OMX_TRUE;

  omxchk(OMX_SetParameter(d->d_decoder->oc_handle, 
			  OMX_IndexParamBrcmDecoderPassThrough, &boolType));

  // Input port

  OMX_PARAM_PORTDEFINITIONTYPE portParam;
  OMX_INIT_STRUCTURE(portParam);
  portParam.nPortIndex = 120;
  omxchk(OMX_GetParameter(d->d_decoder->oc_handle, OMX_IndexParamPortDefinition,
			  &portParam));

  portParam.format.audio.eEncoding = encoding;
  portParam.nBufferSize = 49152;
  portParam.nBufferCountActual = 4;


  omxchk(OMX_SetParameter(d->d_decoder->oc_handle,
			  OMX_IndexParamPortDefinition, &portParam));

  // Output port

  OMX_INIT_STRUCTURE(portParam);
  portParam.nPortIndex = 121;
  omxchk(OMX_GetParameter(d->d_decoder->oc_handle, OMX_IndexParamPortDefinition,
			  &portParam));


  omxchk(OMX_SetParameter(d->d_decoder->oc_handle,
			  OMX_IndexParamPortDefinition, &portParam));
  OMX_AUDIO_PARAM_PORTFORMATTYPE formatType;
  OMX_INIT_STRUCTURE(formatType);
  formatType.nPortIndex = 120;

  formatType.eEncoding =  encoding;

  omxchk(OMX_SetParameter(d->d_decoder->oc_handle,
			  OMX_IndexParamAudioPortFormat, &formatType));
  omx_set_state(d->d_decoder, OMX_StateIdle);
  omx_alloc_buffers(d->d_decoder, 120);
  omx_set_state(d->d_decoder, OMX_StateExecuting);

  d->d_decoder->oc_port_settings_changed_cb = rpi_audio_port_settings_changed;
  d->d_decoder->oc_opaque = d;

  return AUDIO_MODE_CODED;
}


/**
 *
 */
static void
rpi_audio_deliver_coded(audio_decoder_t *ad, const void *data, size_t size,
			int64_t pts, int epoch)
{

  decoder_t *d = (decoder_t *)ad;
  //  int bytes = samples * d->d_bpf;
  omx_component_t *oc = d->d_decoder;
  OMX_BUFFERHEADERTYPE *buf;

  if(ad->ad_discontinuity && pts == PTS_UNSET && ad->ad_mp->mp_extra != NULL) {
    return;
  }

  if((buf = oc->oc_avail) == NULL)
    return;

  oc->oc_avail = buf->pAppPrivate;
  oc->oc_inflight_buffers++;

  hts_mutex_unlock(&ad->ad_mp->mp_mutex);

  memcpy(buf->pBuffer, data, size);
  buf->nFilledLen = size;
  buf->nOffset = 0;

  if(d->d_last_epoch != epoch) {

    if(pts != PTS_UNSET)
      d->d_last_epoch = epoch;

    buf->nFlags |= OMX_BUFFERFLAG_DISCONTINUITY;
  }

  if(ad->ad_discontinuity) {
    buf->nFlags |= OMX_BUFFERFLAG_STARTTIME | OMX_BUFFERFLAG_DISCONTINUITY;
    ad->ad_discontinuity = 0;
  }

  if(pts != PTS_UNSET)
    buf->nTimeStamp = omx_ticks_from_s64(pts);
  else
    buf->nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;

  buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;


  omxchk(OMX_EmptyThisBuffer(oc->oc_handle, buf));
  hts_mutex_lock(&ad->ad_mp->mp_mutex);
}


/**
 *
 */
static audio_class_t rpi_audio_class = {
  .ac_alloc_size     = sizeof(decoder_t),
  .ac_init           = rpi_audio_init,
  .ac_fini           = rpi_audio_fini,
  .ac_reconfig       = rpi_audio_reconfig,
  .ac_deliver_locked = rpi_audio_deliver,
  .ac_flush          = rpi_audio_flush,
  .ac_pause          = rpi_audio_pause,
  .ac_play           = rpi_audio_play,
  .ac_set_volume     = rpi_set_volume,
  .ac_get_mode       = rpi_get_mode,
  .ac_deliver_coded_locked = rpi_audio_deliver_coded,
};

/**
 *
 */
static void
set_mastervol(void *opaque, float value)
{
  master_volume = pow(10, (value / 20));
}


/**
 *
 */
static void
set_mastermute(void *opaque, int value)
{
  master_mute = value;
}


audio_class_t *
audio_driver_init(void)
{
  prop_subscribe(0,
		 PROP_TAG_CALLBACK_FLOAT, set_mastervol, NULL,
		 PROP_TAG_NAME("global", "audio", "mastervolume"),
		 NULL);

  prop_subscribe(0,
		 PROP_TAG_CALLBACK_INT, set_mastermute, NULL,
		 PROP_TAG_NAME("global", "audio", "mastermute"),
		 NULL);

  return &rpi_audio_class;
}

