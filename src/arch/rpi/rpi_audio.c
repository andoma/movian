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
#include <string.h>
#include <unistd.h>
#include <math.h>

#include <bcm_host.h>

#include "audio2/audio.h"
#include "omx.h"
#include "media/media.h"
#include "rpi.h"

//#include <interface/vmcs_host/vc_tvservice.h>

static char omx_enable_vorbis;
static char omx_enable_flac;


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

  char d_hdmi_8ch_pcm;
  char d_hdmi_ac3;
  char d_hdmi_eac3;
  char d_hdmi_dts;
  char d_hdmi_mlp;
  char d_hdmi_aac;

  char d_local_output;

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
static int local_output;
static int hdmi_ac3_mode;
static int hdmi_dts_mode;
static int hdmi_8ch_mode;




/**
 *
 */
static void
update_audio_clock(audio_decoder_t *ad, int epoch)
{
  media_pipe_t *mp = ad->ad_mp;
  omx_component_t *c = omx_get_clock(mp);
  if(c != NULL) {
    int64_t ts = omx_get_media_time(c);
    hts_mutex_lock(&mp->mp_clock_mutex);
    mp->mp_audio_clock_avtime = arch_get_avtime();
    mp->mp_audio_clock = ts;
    mp->mp_audio_clock_epoch = epoch;
    hts_mutex_unlock(&mp->mp_clock_mutex);
  }
}

/**
 *
 */
static int
check_mode(int detected, int configured)
{
  /**
   * Configured
   *   0 -> Autodetect
   *   1 -> Off
   *   2 -> On
   *
   * Hence,
   */
  return configured ? configured - 1 : detected;
}

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

  d->d_local_output = local_output;

  d->d_hdmi_8ch_pcm =
    !vc_tv_hdmi_audio_supported(EDID_AudioFormat_ePCM, 8,
                                EDID_AudioSampleRate_e48KHz,
                                EDID_AudioSampleSize_16bit);

  d->d_hdmi_ac3 =
    !vc_tv_hdmi_audio_supported(EDID_AudioFormat_eAC3, 2,
                                EDID_AudioSampleRate_e44KHz,
                                EDID_AudioSampleSize_16bit);

  d->d_hdmi_eac3 =
    !vc_tv_hdmi_audio_supported(EDID_AudioFormat_eEAC3, 2,
                                EDID_AudioSampleRate_e44KHz,
                                EDID_AudioSampleSize_16bit);

  d->d_hdmi_dts =
    !vc_tv_hdmi_audio_supported(EDID_AudioFormat_eDTS, 2,
                                EDID_AudioSampleRate_e44KHz,
                                EDID_AudioSampleSize_16bit);

  d->d_hdmi_mlp =
    !vc_tv_hdmi_audio_supported(EDID_AudioFormat_eMLP, 2,
                                EDID_AudioSampleRate_e44KHz,
                                EDID_AudioSampleSize_16bit);

  d->d_hdmi_aac =
    !vc_tv_hdmi_audio_supported(EDID_AudioFormat_eAAC, 2,
                                EDID_AudioSampleRate_e44KHz,
                                EDID_AudioSampleSize_16bit);

  TRACE(TRACE_DEBUG, "RPI", "Supported audio formats "
        "AC3:%s EAC3:%s DTS:%s MLP:%s AAC:%s 8ChPCM:%s",
        d->d_hdmi_ac3     ? "YES" : "NO",
        d->d_hdmi_eac3    ? "YES" : "NO",
        d->d_hdmi_dts     ? "YES" : "NO",
        d->d_hdmi_mlp     ? "YES" : "NO",
        d->d_hdmi_aac     ? "YES" : "NO",
        d->d_hdmi_8ch_pcm ? "YES" : "NO");

  if(!d->d_hdmi_8ch_pcm)
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

  omx_set_state(d->d_decoder, OMX_StateIdle);
  omx_wait_buffers(d->d_decoder);
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
  media_pipe_t *mp = d->ad.ad_mp;
  hts_mutex_lock(&mp->mp_mutex);
  media_buf_t *mb = media_buf_alloc_locked(mp, 0);
  mb->mb_data_type = MB_CTRL_RECONFIGURE;
  mb->mb_dtor = media_buf_dtor_frame_info;
  mb_enq(mp, &mp->mp_audio, mb);
  hts_mutex_unlock(&mp->mp_mutex);
}



static void
rpi_audio_port_reconfigure(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;

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

  d->d_local_output = local_output;

  const char *device = d->d_local_output ? "local" : "hdmi";

  TRACE(TRACE_DEBUG, "RPI", "Opening audio output %s", device);

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
get_out_sample_format(const decoder_t *d)
{
  switch(d->ad.ad_in_sample_format) {
  case AV_SAMPLE_FMT_FLTP:
  case AV_SAMPLE_FMT_FLT:
    return AV_SAMPLE_FMT_FLTP;

  default:
    return AV_SAMPLE_FMT_S16;
  }
}


/**
 *
 */
static int
get_out_channel_layout(const decoder_t *d)
{
  switch(d->ad.ad_in_channel_layout) {

  default:
    if(check_mode(d->d_hdmi_8ch_pcm && !d->d_local_output, hdmi_8ch_mode))
      return AV_CH_LAYOUT_7POINT1;

    // FALLTHRU

  case AV_CH_LAYOUT_MONO:
  case AV_CH_LAYOUT_STEREO:
    return AV_CH_LAYOUT_STEREO;
  }
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

  ad->ad_out_sample_format = get_out_sample_format(d);

  switch(ad->ad_out_sample_format) {
  case AV_SAMPLE_FMT_FLTP:
    wfe.Format.wFormatTag = 0x8000;
    bps = 32;
    break;

  case AV_SAMPLE_FMT_S16:
    wfe.Format.wFormatTag = WAVE_FORMAT_PCM;
    bps = 16;
    break;

  default:
    abort();
  }

  ad->ad_out_channel_layout = get_out_channel_layout(d);
  switch(ad->ad_out_channel_layout) {
  case AV_CH_LAYOUT_STEREO:
    d->d_channels = 2;
    break;
  case AV_CH_LAYOUT_7POINT1:
    d->d_channels = 8;
    break;
  }

  /**
   * Configure input port
   */
  ad->ad_tile_size = 1024;
  d->d_bpf = d->d_channels * (bps >> 3);
 
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

  int bytes_per_sec =  ad->ad_in_sample_rate * (bps >> 3) * d->d_channels;

  wfe.Samples.wSamplesPerBlock    = 0;
  wfe.Format.nChannels            = d->d_channels;
  wfe.Format.nBlockAlign          = d->d_channels * (bps >> 3);
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

  if(d->d_decoder != NULL) {
    int fmt = get_out_sample_format(d);
    int l   = get_out_channel_layout(d);
 
    if(fmt == ad->ad_out_sample_format &&
       l == ad->ad_out_channel_layout &&
       d->d_local_output == local_output) {
      return 0;
    }
  }

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
  oc->oc_avail_bytes -= buf->nAllocLen;

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

  if(pts != PTS_UNSET) {
    buf->nTimeStamp = omx_ticks_from_s64(pts);
  } else {
    buf->nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;
  }
  if(d->d_master_volume != master_volume || d->d_master_mute != master_mute) {
    d->d_master_volume = master_volume;
    d->d_master_mute = master_mute;
    set_mixer_matrix(d);
  }


  omxchk(OMX_EmptyThisBuffer(oc->oc_handle, buf));

  if(pts != PTS_UNSET)
    update_audio_clock(ad, epoch);

  hts_mutex_lock(&ad->ad_mp->mp_mutex);

  if(d->d_local_output != local_output)
    d->ad.ad_want_reconfig = 1;

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
rpi_get_mode(audio_decoder_t *ad, int codec,
	     const void *extradata, size_t extradata_size)
{
  decoder_t *d = (decoder_t *)ad;
  int encoding;

  switch(codec) {
  case AV_CODEC_ID_FLAC:
    if(!omx_enable_flac)
      return AUDIO_MODE_PCM;
    encoding = OMX_AUDIO_CodingFLAC;
    break;

  case AV_CODEC_ID_VORBIS:
    if(!omx_enable_vorbis)
      return AUDIO_MODE_PCM;
    encoding = OMX_AUDIO_CodingVORBIS;
    break;

  case AV_CODEC_ID_AC3:
    if(!check_mode(d->d_hdmi_ac3 && !d->d_local_output, hdmi_ac3_mode))
      return AUDIO_MODE_PCM;
    encoding = OMX_AUDIO_CodingDDP;
    break;

  case AV_CODEC_ID_DTS:
    if(!check_mode(d->d_hdmi_dts && !d->d_local_output, hdmi_dts_mode))
      return AUDIO_MODE_PCM;
    encoding = OMX_AUDIO_CodingDTS;
    break;

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


  if(extradata != NULL && extradata_size > 0) {

    OMX_BUFFERHEADERTYPE *buf = omx_get_buffer(d->d_decoder);
    buf->nOffset = 0;
    buf->nFilledLen = extradata_size;
    memset(buf->pBuffer, 0,  buf->nAllocLen);
    assert(buf->nFilledLen <= buf->nAllocLen);
    memcpy(buf->pBuffer, extradata, buf->nFilledLen);
    buf->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
    omxchk(OMX_EmptyThisBuffer(d->d_decoder->oc_handle, buf));
  }


  d->d_decoder->oc_port_settings_changed_cb = rpi_audio_port_settings_changed;
  d->d_decoder->oc_opaque = d;

  return AUDIO_MODE_CODED;
}


/**
 *
 */
static int
rpi_audio_deliver_coded(audio_decoder_t *ad, const void *data, size_t size,
			int64_t pts, int epoch)
{

  decoder_t *d = (decoder_t *)ad;
  //  int bytes = samples * d->d_bpf;
  omx_component_t *oc = d->d_decoder;
  OMX_BUFFERHEADERTYPE *buf;

  if(ad->ad_discontinuity && pts == PTS_UNSET && ad->ad_mp->mp_extra != NULL) {
    return 0;
  }

  if((buf = oc->oc_avail) == NULL)
    return 1;

  oc->oc_avail = buf->pAppPrivate;
  oc->oc_inflight_buffers++;
  oc->oc_avail_bytes -= buf->nAllocLen;

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

  if(pts != PTS_UNSET) {
    buf->nTimeStamp = omx_ticks_from_s64(pts);
  } else {
    buf->nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;
  }
  buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

  omxchk(OMX_EmptyThisBuffer(oc->oc_handle, buf));

  if(pts != PTS_UNSET)
    update_audio_clock(ad, epoch);

  hts_mutex_lock(&ad->ad_mp->mp_mutex);

  return 0;
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
  .ac_reconfigure = rpi_audio_port_reconfigure,
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
audio_driver_init(struct prop *asettings)
{
#if 0
  omx_enable_flac   = rpi_is_codec_enabled("FLAC");
  omx_enable_vorbis = rpi_is_codec_enabled("VORB");
#endif

  setting_create(SETTING_MULTIOPT, asettings, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("Audio output port")),
                 SETTING_STORE("audio2", "outputport"),
                 SETTING_WRITE_INT(&local_output),
                 SETTING_OPTION("0", _p("HDMI")),
                 SETTING_OPTION("1", _p("Analog")),
                 NULL);

  setting_create(SETTING_MULTIOPT, asettings, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("8 Channel PCM")),
                 SETTING_STORE("audio2", "8chmode"),
                 SETTING_WRITE_INT(&hdmi_8ch_mode),
                 SETTING_OPTION("0", _p("Autodetect")),
                 SETTING_OPTION("1", _p("Off")),
                 SETTING_OPTION("2", _p("On")),
                 NULL);

  setting_create(SETTING_MULTIOPT, asettings, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("AC3 Pass-Through")),
                 SETTING_STORE("audio2", "ac3mode"),
                 SETTING_WRITE_INT(&hdmi_ac3_mode),
                 SETTING_OPTION("0", _p("Autodetect")),
                 SETTING_OPTION("1", _p("Off")),
                 SETTING_OPTION("2", _p("On")),
                 NULL);

  setting_create(SETTING_MULTIOPT, asettings, SETTINGS_INITIAL_UPDATE,
                 SETTING_TITLE(_p("DTS Pass-Through")),
                 SETTING_STORE("audio2", "dtsmode"),
                 SETTING_WRITE_INT(&hdmi_dts_mode),
                 SETTING_OPTION("0", _p("Autodetect")),
                 SETTING_OPTION("1", _p("Off")),
                 SETTING_OPTION("2", _p("On")),
                 NULL);

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

