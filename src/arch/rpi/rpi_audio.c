#include <string.h>

#include "audio2/audio.h"
#include "omx.h"
#include "media.h"

typedef struct decoder {
  audio_decoder_t ad;
  omx_component_t *d_render;
  omx_tunnel_t *d_clock_tun;
  int d_bpf; // Bytes per frame
  int d_last_epoch;
} decoder_t;

/**
 *
 */
static int
rpi_audio_init(audio_decoder_t *ad)
{
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

  if(d->d_clock_tun)
    omx_tunnel_destroy(d->d_clock_tun);

  omx_wait_buffers(d->d_render);
  omx_set_state(d->d_render, OMX_StateIdle);
  omx_release_buffers(d->d_render, 100);

  omx_set_state(d->d_render, OMX_StateLoaded);
  omx_component_destroy(d->d_render);
}


/**
 *
 */
static int
rpi_audio_reconfig(audio_decoder_t *ad)
{
  media_pipe_t *mp = ad->ad_mp;
  decoder_t *d = (decoder_t *)ad;
  rpi_audio_fini(ad);

  omx_component_t *r;

  r = d->d_render = omx_component_create("OMX.broadcom.audio_render",
					 &ad->ad_mp->mp_mutex,
					 &ad->ad_mp->mp_audio.mq_avail);

  const char *device = "hdmi";

  OMX_CONFIG_BRCMAUDIODESTINATIONTYPE audioDest;
  OMX_INIT_STRUCTURE(audioDest);
  strncpy((char *)audioDest.sName, device, strlen(device));
  omxchk(OMX_SetConfig(d->d_render->oc_handle,
		       OMX_IndexConfigBrcmAudioDestination, &audioDest));


  if(mp->mp_extra)
    d->d_clock_tun = omx_tunnel_create(omx_get_clock(mp), 80, d->d_render, 101,
				       "clock -> audio");

  // Initialize audio render
  OMX_PARAM_PORTDEFINITIONTYPE param;
  OMX_AUDIO_PARAM_PCMMODETYPE pcm;

  // set the pcm parameters
  OMX_INIT_STRUCTURE(pcm);

  ad->ad_tile_size = 1024;

  ad->ad_out_sample_rate = MIN(48000, ad->ad_in_sample_rate);
  pcm.nSamplingRate = ad->ad_out_sample_rate;

  switch(ad->ad_in_sample_format) {
#if 0
  case AV_SAMPLE_FMT_S32P:
  case AV_SAMPLE_FMT_S32:
    ad->ad_out_sample_format = AV_SAMPLE_FMT_S32;
    pcm.nBitPerSample = 32;
    d->d_bpf = 8;
    break;
#endif

  default:
    ad->ad_out_sample_format = AV_SAMPLE_FMT_S16;
    pcm.nBitPerSample = 16;
    d->d_bpf = 4;
    break;
  }

  ad->ad_out_channel_layout = AV_CH_LAYOUT_STEREO;

  // set up the number/size of buffers
  OMX_INIT_STRUCTURE(param);
  param.nPortIndex = 100;

  omxchk(OMX_GetParameter(r->oc_handle, OMX_IndexParamPortDefinition, &param));
  param.nBufferSize = d->d_bpf * ad->ad_tile_size;
  param.nBufferCountActual = 4;

  omxchk(OMX_SetParameter(r->oc_handle, OMX_IndexParamPortDefinition, &param));

  pcm.nPortIndex = 100;
  pcm.nChannels = 2;
  pcm.eNumData = OMX_NumericalDataSigned;
  pcm.eEndian = OMX_EndianLittle;
  pcm.bInterleaved = OMX_TRUE;
  pcm.ePCMMode = OMX_AUDIO_PCMModeLinear;

  pcm.eChannelMapping[0] = OMX_AUDIO_ChannelLF;
  pcm.eChannelMapping[1] = OMX_AUDIO_ChannelRF;

  omxchk(OMX_SetParameter(r->oc_handle, OMX_IndexParamAudioPcm, &pcm));

  omx_set_state(r, OMX_StateIdle);

  omx_alloc_buffers(r, 100);
  omx_set_state(r, OMX_StateExecuting);
  return 0;
}


/**
 *
 */
static int
rpi_audio_deliver(audio_decoder_t *ad, int samples, int64_t pts, int epoch)
{
  decoder_t *d = (decoder_t *)ad;
  //  int bytes = samples * d->d_bpf;
  omx_component_t *oc = d->d_render;
  OMX_BUFFERHEADERTYPE *buf;

  if(ad->ad_discontinuity && pts == PTS_UNSET) {
    avresample_read(ad->ad_avr, NULL, samples);
    return 0;
  }

  if((buf = oc->oc_avail) == NULL)
    return 1;

  oc->oc_avail = buf->pAppPrivate;
  oc->oc_inflight_buffers++;

  uint8_t *data[8] = {0};
  data[0] = (uint8_t *)buf->pBuffer;
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
  if(d->d_render)
    omx_flush_port(d->d_render, 100);
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
};


audio_class_t *
audio_driver_init(void)
{
  return &rpi_audio_class;
}

