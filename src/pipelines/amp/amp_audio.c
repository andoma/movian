/*
 *  Copyright 2013 (C) Spotify AB
 */

#include <libavutil/mathematics.h> // XXX
#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#include "arch/threads.h"
#include "showtime.h"
#include "audio2/audio.h"

#include "amp.h"
#include "amp_sound_api.h"



#define MAX_AUDIO_STREAM_SIZE (16)
#define AUDIO_ES_BUFFER_SIZE  (32 * 1024)

typedef struct decoder {
  audio_decoder_t ad;

  int64_t audio_stream_pos;

  int running;

  HANDLE amp_sound;
  uint32_t amp_tunnel;

  AMP_COMPONENT amp_adec;
  AMP_COMPONENT amp_aren;

  AMP_BDCHAIN *audio_stream_queue;

  void *tmp;

  int64_t nextpts;

} decoder_t;


/**
 *
 */
static int
amp_audio_init(audio_decoder_t *ad)
{
  return 0;
}




static HRESULT
amp_adec_callback(CORBA_Object hCompObj,
                  AMP_PORT_IO ePortIo,
                  UINT32 uiPortIdx,
                  struct AMP_BD_ST *hBD,
                  AMP_IN void *pUserData)
{
  HRESULT ret;
  decoder_t *d = (decoder_t *)pUserData;

  if (ePortIo == AMP_PORT_INPUT) {
    ret = AMP_BDCHAIN_PushItem(d->audio_stream_queue, hBD);
    assert(ret == SUCCESS);
  }

  return SUCCESS;
}


/**
 *
 */
static void
amp_audio_stop(decoder_t *d)
{
  HRESULT ret;
  if(!d->running)
    return;
  d->running = 0;

  media_pipe_t *mp = d->ad.ad_mp;
  amp_extra_t *ae = mp->mp_extra;

  AMP_RPC(ret, AMP_ADEC_SetState, d->amp_adec, AMP_IDLE);
  assert(ret == SUCCESS);

  AMP_RPC(ret, AMP_AREN_SetState, d->amp_aren, AMP_IDLE);
  assert(ret == SUCCESS);

  ret = AMP_SND_RemoveInputTunnel(d->amp_sound, d->amp_tunnel);
  assert(ret == SUCCESS);

  ret = AMP_DisconnectComp(ae->amp_clk, 1, d->amp_aren, 0);
  assert(ret == SUCCESS);

  ret = AMP_DisconnectComp(d->amp_adec, 0, d->amp_aren, 1);
  assert(ret == SUCCESS);

  ret = AMP_DisconnectApp(d->amp_adec, AMP_PORT_INPUT, 0, amp_adec_callback);
  assert(ret == SUCCESS);

  AMP_RPC(ret, AMP_AREN_Close, d->amp_aren);
  assert(ret == SUCCESS);

  AMP_RPC(ret, AMP_ADEC_Close, d->amp_adec);
  assert(ret == SUCCESS);

  AMP_RPC(ret, AMP_AREN_Destroy, d->amp_aren);
  assert(ret == SUCCESS);

  AMP_RPC(ret, AMP_ADEC_Destroy, d->amp_adec);
  assert(ret == SUCCESS);

  // Drain buffers

  while (1) {

    UINT32 num_bd_allocated;

    ret = AMP_BDCHAIN_GetItemNum(d->audio_stream_queue, &num_bd_allocated);
    assert(ret == SUCCESS);

    if (num_bd_allocated < MAX_AUDIO_STREAM_SIZE) {
      printf("%3u of %3u audio buffer back, wait\n",
             num_bd_allocated, MAX_AUDIO_STREAM_SIZE);
      usleep(10000);
    } else {
      break;
    }
  }

  AMP_BD_HANDLE buf_desc;
  UINT32 num_bd_allocated;
  UINT32 i;
  AMP_BDTAG_MEMINFO *mem_info;
  AMP_SHM_HANDLE handle_share_mem;

  ret = AMP_BDCHAIN_GetItemNum(d->audio_stream_queue, &num_bd_allocated);
  assert(ret == SUCCESS);

  for (i = 0; i < num_bd_allocated; i++) {
    ret = AMP_BDCHAIN_PopItem(d->audio_stream_queue, &buf_desc);
    assert(ret == SUCCESS);

    ret = AMP_BDTAG_GetWithIndex(buf_desc, 0, (void **)&mem_info);
    assert(ret == SUCCESS);

    handle_share_mem = mem_info->uMemHandle;

    ret = AMP_BD_Free(buf_desc);
    assert(ret == SUCCESS);
  }

  ret = AMP_BDCHAIN_Destroy(d->audio_stream_queue);
  assert(ret == SUCCESS);

  ret = AMP_SHM_Release(handle_share_mem);
  assert(ret == SUCCESS);


  free(d->tmp);
}

/**
 *
 */
static void
amp_audio_fini(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  amp_audio_stop(d);
}


/**
 *
 */
static void
amp_audio_setup_buffers(decoder_t *d)
{
  HRESULT ret;
  AMP_BD_HANDLE buf_desc;
  AMP_SHM_HANDLE handle_share_mem;
  int i = 0;
  AMP_BDTAG_MEMINFO mem_info;
  AMP_BDTAG_UNITSTART unit_start;

  mem_info.Header.eType = AMP_BDTAG_ASSOCIATE_MEM_INFO;
  mem_info.Header.uLength = sizeof(AMP_BDTAG_MEMINFO);

  unit_start.Header.eType = AMP_BDTAG_BS_UNITSTART_CTRL;
  unit_start.Header.uLength = sizeof(AMP_BDTAG_UNITSTART);

  ret = AMP_BDCHAIN_Create(TRUE, &d->audio_stream_queue);
  assert(ret == SUCCESS);

  ret = AMP_SHM_Allocate(AMP_SHM_DYNAMIC,
                         AUDIO_ES_BUFFER_SIZE * MAX_AUDIO_STREAM_SIZE,
                         1024,
                         &handle_share_mem);
  assert(ret == SUCCESS);

  for (i = 0; i < MAX_AUDIO_STREAM_SIZE; i++) {

    ret = AMP_BD_Allocate(&buf_desc);
    assert(ret == SUCCESS);

    mem_info.uMemHandle = handle_share_mem;
    mem_info.uMemOffset = AUDIO_ES_BUFFER_SIZE * i;
    mem_info.uSize = AUDIO_ES_BUFFER_SIZE;

    unit_start.uPtsHigh = 0;
    unit_start.uPtsLow = 0;

    ret = AMP_BDTAG_Append(buf_desc, (UINT8 *)&mem_info, NULL, NULL);
    assert(ret == SUCCESS);

    ret = AMP_BDTAG_Append(buf_desc, (UINT8 *)&unit_start, NULL, NULL);
    assert(ret == SUCCESS);

    ret = AMP_BDCHAIN_PushItem(d->audio_stream_queue, buf_desc);
    assert(ret == SUCCESS);
  }
}


/**
 *
 */
static int
amp_audio_reconfig(audio_decoder_t *ad)
{
  decoder_t *d = (decoder_t *)ad;
  media_pipe_t *mp = ad->ad_mp;
  amp_extra_t *ae = mp->mp_extra;
  HRESULT ret;
  AMP_COMPONENT_CONFIG amp_config;

  d->nextpts = PTS_UNSET;

  amp_audio_stop(d);

  amp_audio_setup_buffers(d);

  ret = AMP_SND_Initialize(amp_factory, &d->amp_sound);
  assert(ret == SUCCESS);

  AMP_RPC(ret,
          AMP_FACTORY_CreateComponent,
          amp_factory,
          AMP_COMPONENT_ADEC,
          0,
          &d->amp_adec);
  assert(ret == SUCCESS);

  AMP_RPC(ret,
          AMP_FACTORY_CreateComponent,
          amp_factory,
          AMP_COMPONENT_AREN,
          0,
          &d->amp_aren);
  assert(ret == SUCCESS);

  AmpMemClear(&amp_config, sizeof(AMP_COMPONENT_CONFIG));
  amp_config._d = AMP_COMPONENT_ADEC;
  amp_config._u.pADEC.mode = AMP_TUNNEL;
  amp_config._u.pADEC.eAdecFmt = MEDIA_AES_PCM;
  amp_config._u.pADEC.uiInPortNum = 1;
  amp_config._u.pADEC.uiOutPortNum = 1;
  amp_config._u.pADEC.ucFrameIn = AMP_ADEC_FRAME;
  AMP_RPC(ret, AMP_ADEC_Open, d->amp_adec, &amp_config);
  assert(ret == SUCCESS);

  AMP_ADEC_PARAS amp_param;
  int i;

  AmpMemClear(&amp_param, sizeof(AMP_ADEC_PARAS));
  amp_param._d = AMP_ADEC_PARAIDX_RAWPCM;
  amp_param._u.PCM.uiPcmType = AMP_AUDIO_PCMBITS16_SINGED;
  amp_param._u.PCM.uiChanMap[0] = AMP_AUDIO_CHMASK_LEFT;
  amp_param._u.PCM.uiChanMap[1] = AMP_AUDIO_CHMASK_RGHT;

  for(i = 0; i < 8; i++) {
    amp_param._u.PCM.uiChanMask += amp_param._u.PCM.uiChanMap[i];
  }

  amp_param._u.PCM.uiSampleRate  = ad->ad_in_sample_rate;
  amp_param._u.PCM.unChanNr      = 2;
  amp_param._u.PCM.unBitDepth    = 16;
  amp_param._u.PCM.cLittleEndian = 0;
  amp_param._u.PCM.uiInThresh    = 0x2000;

  AMP_RPC(ret, AMP_ADEC_SetParameters, d->amp_adec, AMP_ADEC_PARAIDX_RAWPCM,
          &amp_param);
  assert(ret == SUCCESS);


  ad->ad_out_sample_format  = AV_SAMPLE_FMT_S16;
  ad->ad_out_channel_layout = AV_CH_LAYOUT_STEREO;
  ad->ad_out_sample_rate    = ad->ad_in_sample_rate;

  AmpMemClear(&amp_config, sizeof(AMP_COMPONENT_CONFIG));
  amp_config._d = AMP_COMPONENT_AREN;
  amp_config._u.pAREN.mode = AMP_TUNNEL;
  amp_config._u.pAREN.uiInClkPortNum = 1;
  amp_config._u.pAREN.uiInPcmPortNum  = 1;
  amp_config._u.pAREN.uiInSpdifPortNum = 0;
  amp_config._u.pAREN.uiInHdmiPortNum = 0;
  amp_config._u.pAREN.uiOutPcmPortNum = 1;
  amp_config._u.pAREN.uiOutSpdifPortNum = 0;
  amp_config._u.pAREN.uiOutHdmiPortNum = 0;
  AMP_RPC(ret, AMP_AREN_Open, d->amp_aren, &amp_config);
  assert(ret == SUCCESS);

  ret = AMP_ConnectApp(d->amp_adec, AMP_PORT_INPUT, 0, amp_adec_callback, d);
  assert(ret == SUCCESS);

  ret = AMP_ConnectComp(d->amp_adec, 0, d->amp_aren, 1);
  assert(ret == SUCCESS);

  ret = AMP_ConnectComp(ae->amp_clk, 1, d->amp_aren, 0);
  assert(ret == SUCCESS);

  ret = AMP_SND_SetupInputTunnel(d->amp_sound, d->amp_aren, 0, &d->amp_tunnel);
  assert(ret == SUCCESS);

  AMP_AREN_PARAST para_st;

  para_st._d = AMP_AREN_PARAIDX_PORTASSOCCLK;
  para_st._u.PORTASSOCCLK.uiAssocIdx = 0;

  AMP_RPC(ret, AMP_AREN_SetPortParameter, d->amp_aren, AMP_PORT_INPUT,
          1, AMP_AREN_PARAIDX_PORTASSOCCLK, &para_st);
  assert(ret == SUCCESS);

  d->running = 1;

  int channels = 2;
  d->tmp = malloc(sizeof(int16_t) * channels * 1024);

  AMP_RPC(ret, AMP_AREN_SetState, d->amp_aren, AMP_EXECUTING);
  assert(ret == SUCCESS);

  AMP_RPC(ret, AMP_ADEC_SetState, d->amp_adec, AMP_EXECUTING);
  assert(ret == SUCCESS);

  return 0;
}


/**
 *
 */
static int
usr_audio_callback(decoder_t *d, const void *data, int length,
                   uint64_t pts)
{
  HRESULT ret;
  UINT32 num_bd_remained;
  AMP_BD_HANDLE buf_desc;
  AMP_BDTAG_MEMINFO *mem_info;
  AMP_BDTAG_UNITSTART *unit_start;
  UINT8 *buf_mapped;

  ret = AMP_BDCHAIN_GetItemNum(d->audio_stream_queue,
                               &num_bd_remained);
  assert(ret == SUCCESS);

  if (num_bd_remained == 0)
    return -1;

  if (length  > AUDIO_ES_BUFFER_SIZE) {
    printf("Unexpected audio frame size %d\n", length);
    return 0;
  }

  ret = AMP_BDCHAIN_PopItem(d->audio_stream_queue, &buf_desc);
  assert(ret == SUCCESS);

  ret = AMP_BDTAG_GetWithIndex(buf_desc, 0, (void **)&mem_info);
  assert(ret == SUCCESS);

  ret = AMP_BDTAG_GetWithIndex(buf_desc, 1, (void **)&unit_start);
  assert(ret == SUCCESS);


  ret = AMP_SHM_GetVirtualAddress(mem_info->uMemHandle,
                                  mem_info->uMemOffset,
                                  (void **)&buf_mapped);
  assert(ret == SUCCESS);

  AmpMemcpy(buf_mapped, data, length);


  unit_start->uPtsHigh = 0x80000000 | (pts >> 32);
  unit_start->uPtsLow =  pts;
  unit_start->uDtsHigh = 0x80000000 | (pts >> 32);
  unit_start->uDtsLow =  pts;

  d->audio_stream_pos += length;

  mem_info->uSize = length;

  ret = AMP_SHM_CleanCache(mem_info->uMemHandle,
                           mem_info->uMemOffset,
                           AUDIO_ES_BUFFER_SIZE);
  assert(ret == SUCCESS);

  ret = AMP_SHM_InvalidateCache(mem_info->uMemHandle,
                                mem_info->uMemOffset,
                                AUDIO_ES_BUFFER_SIZE);
  assert(ret == SUCCESS);

  AMP_RPC(ret,
          AMP_ADEC_PushBD,
          d->amp_adec,
          AMP_PORT_INPUT,
          0,
          buf_desc);
  assert(ret == SUCCESS);
  return 0;
}

static UINT32 amp_audio_buffer_fullness(decoder_t *d)
{
  HRESULT ret;
  UINT32 num_bd_remained;

  ret = AMP_BDCHAIN_GetItemNum(d->audio_stream_queue, &num_bd_remained);
  assert(ret == SUCCESS);

  return (MAX_AUDIO_STREAM_SIZE - num_bd_remained);
}



/**
 *
 */
static int
amp_audio_deliver(audio_decoder_t *ad, int samples, int64_t pts, int epoch)
{
  const static AVRational mpeg_tc = {1, 90000};

  decoder_t *d = (decoder_t *)ad;

  while(amp_audio_buffer_fullness(d) >  MAX_AUDIO_STREAM_SIZE * 7 / 8) {
    // XXX BAD
    usleep(15000);
  }

  uint8_t *planes[8] = {0};
  planes[0] = d->tmp;

  int c = avresample_read(ad->ad_avr, planes, 1024);
  int duration = c * 90000 / ad->ad_out_sample_rate;

  if(pts != PTS_UNSET) {
    pts = av_rescale_q(pts, AV_TIME_BASE_Q, mpeg_tc);
  } else {
    pts = d->nextpts;
    if(pts == PTS_UNSET)
      return 0; // No clock yet, hang in there
  }

  d->nextpts = pts + duration;
  usr_audio_callback(d, d->tmp, c * 2 * sizeof(int16_t), pts);
  return 0;
}


/**
 *
 */
static audio_class_t amp_audio_class = {
  .ac_alloc_size     = sizeof(decoder_t),
  .ac_init           = amp_audio_init,
  .ac_fini           = amp_audio_fini,
  .ac_reconfig       = amp_audio_reconfig,
  .ac_deliver_unlocked = amp_audio_deliver,
};


/**
 *
 */
audio_class_t *
audio_driver_init(void)
{
  return &amp_audio_class;
}

