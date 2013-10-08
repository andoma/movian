/*
 *  Copyright 2013 (C) Spotify AB
 */

#include <libavutil/mathematics.h> // XXX
#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#include "arch/threads.h"
#include "showtime.h"
#include "video/video_decoder.h"
#include "amp.h"
//#include "video/h264_parser.h"

// We want to merge this with Showtime's own buffers probably
#define MAX_VIDEO_STREAM_SIZE (4)
#define VIDEO_ES_BUFFER_SIZE  (512 * 1024)



static HRESULT
amp_vdec_callback(CORBA_Object hCompObj, AMP_PORT_IO ePortIo,
                  UINT32 uiPortIdx, struct AMP_BD_ST *hBD,
                  AMP_IN void *pUserData)
{
  HRESULT ret;
  amp_video_t *av = pUserData;

  if(ePortIo == AMP_PORT_INPUT) {
    ret = AMP_BDCHAIN_PushItem(av->video_stream_queue, hBD);
    assert(ret == SUCCESS);
  }

  return SUCCESS;
}


/**
 *
 */
static void
amp_video_alloc_buffer(amp_video_t *av)
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

  ret = AMP_BDCHAIN_Create(TRUE, &av->video_stream_queue);
  assert(ret == SUCCESS);

  ret = AMP_SHM_Allocate(AMP_SHM_DYNAMIC,
                         VIDEO_ES_BUFFER_SIZE * MAX_VIDEO_STREAM_SIZE,
                         1024,
                         &handle_share_mem);
  assert(ret == SUCCESS);

  for (i = 0; i < MAX_VIDEO_STREAM_SIZE; i++) {

    ret = AMP_BD_Allocate(&buf_desc);
    assert(ret == SUCCESS);

    mem_info.uMemHandle = handle_share_mem;
    mem_info.uMemOffset = VIDEO_ES_BUFFER_SIZE * i;
    mem_info.uSize = VIDEO_ES_BUFFER_SIZE;

    unit_start.uPtsHigh = 0;
    unit_start.uPtsLow = 0;

    ret = AMP_BDTAG_Append(buf_desc, (UINT8 *)&mem_info, NULL, NULL);
    assert(ret == SUCCESS);

    ret = AMP_BDTAG_Append(buf_desc, (UINT8 *)&unit_start, NULL, NULL);
    assert(ret == SUCCESS);

    ret = AMP_BDCHAIN_PushItem(av->video_stream_queue, buf_desc);
    assert(ret == SUCCESS);
  }
}


/**
 *
 */
static UINT32
amp_video_buffer_fullness(amp_video_t *av)
{
  HRESULT ret;
  UINT32 num_bd_remained;

  ret = AMP_BDCHAIN_GetItemNum(av->video_stream_queue, &num_bd_remained);
  assert(ret == SUCCESS);
  return MAX_VIDEO_STREAM_SIZE - num_bd_remained;
}


/**
 *
 */
static void
amp_video_flush(struct media_codec *mc, struct video_decoder *vd)
{
  amp_video_t *av = mc->opaque;
  av->annexb.extradata_injected = 0;
  av->av_configured = 0;
}


/**
 *
 */
static void
amp_video_close(struct media_codec *mc)
{
  HRESULT ret;

  amp_video_t *av = mc->opaque;
  const amp_extra_t *ae = mc->mp->mp_extra;

  AMP_RPC(ret, AMP_VDEC_SetState, av->amp_vdec, AMP_IDLE);
  assert(ret == SUCCESS);

  ret = AMP_DisconnectApp(av->amp_vdec, AMP_PORT_INPUT, 0, amp_vdec_callback);
  assert(ret == SUCCESS);

  AMP_RPC(ret, AMP_VDEC_Close, av->amp_vdec);
  assert(ret == SUCCESS);

  AMP_RPC(ret, AMP_VDEC_Destroy, av->amp_vdec);
  assert(ret == SUCCESS);

  while(1) {
    UINT32 num_bd_allocated;

    ret = AMP_BDCHAIN_GetItemNum(av->video_stream_queue, &num_bd_allocated);
    assert(ret == SUCCESS);

    if(num_bd_allocated < MAX_VIDEO_STREAM_SIZE) {
      usleep(50000);
    } else {
      break;
    }
  }

  AMP_BD_HANDLE buf_desc;
  UINT32 num_bd_allocated;
  UINT32 i;
  AMP_BDTAG_MEMINFO *mem_info;
  AMP_SHM_HANDLE handle_share_mem;

  ret = AMP_BDCHAIN_GetItemNum(av->video_stream_queue, &num_bd_allocated);
  assert(ret == SUCCESS);

  for (i = 0; i < num_bd_allocated; i++) {
    ret = AMP_BDCHAIN_PopItem(av->video_stream_queue, &buf_desc);
    assert(ret == SUCCESS);

    ret = AMP_BDTAG_GetWithIndex(buf_desc, 0, (void **)&mem_info);
    assert(ret == SUCCESS);

    handle_share_mem = mem_info->uMemHandle;

    ret = AMP_BD_Free(buf_desc);
    assert(ret == SUCCESS);
  }

  ret = AMP_BDCHAIN_Destroy(av->video_stream_queue);
  assert(ret == SUCCESS);

  ret = AMP_SHM_Release(handle_share_mem);
  assert(ret == SUCCESS);


  h264_to_annexb_cleanup(&av->annexb);
  free(av);
}


/**
 *
 */
static void
video_packet_enqueue(amp_video_t *av, const void *data, int length,
                     uint64_t pts, uint64_t dts)
{
  HRESULT ret;
  UINT32 num_bd_remained;
  AMP_BD_HANDLE buf_desc;
  AMP_BDTAG_MEMINFO *mem_info;
  AMP_BDTAG_UNITSTART *unit_start;
  UINT8 *buf_mapped;
  UINT32 padding_size = 0;

  ret = AMP_BDCHAIN_GetItemNum(av->video_stream_queue,
                               &num_bd_remained);
  assert(ret == SUCCESS);

  if(num_bd_remained == 0) {
    TRACE(TRACE_DEBUG, "AMP", "Video buffer overflow\n");
    return;
  }

  if(length > VIDEO_ES_BUFFER_SIZE) {
    TRACE(TRACE_DEBUG, "AMP", "Unexpected video frame size %d\n", length);
    return;
  }

  ret = AMP_BDCHAIN_PopItem(av->video_stream_queue, &buf_desc);
  assert(ret == SUCCESS);

  ret = AMP_BDTAG_GetWithIndex(buf_desc, 0, (void **)&mem_info);
  assert(ret == SUCCESS);

  ret = AMP_BDTAG_GetWithIndex(buf_desc, 1, (void **)&unit_start);
  assert(ret == SUCCESS);

  ret = AMP_SHM_GetVirtualAddress(mem_info->uMemHandle,
                                  mem_info->uMemOffset,
                                  (void **)&buf_mapped);
  assert(ret == SUCCESS);


  memcpy(buf_mapped, data, length);

  padding_size = length > 65536 ? 32 - length % 32 : 65536 - length;

  if(padding_size)
    AmpMemSet(buf_mapped + length, 0x88, padding_size);

  if(pts == PTS_UNSET) {
    unit_start->uPtsHigh = 0;
    unit_start->uPtsLow =  0;
  } else {
    unit_start->uPtsHigh = 0x80000000 | (pts >> 32);
    unit_start->uPtsLow =  pts;
  }

  if(dts == PTS_UNSET) {
    unit_start->uDtsHigh = 0;
    unit_start->uDtsLow =  0;
  } else {
    unit_start->uDtsHigh = 0x80000000 | (dts >> 32);
    unit_start->uDtsLow =  dts;
  }

  mem_info->uSize = length + padding_size;

  ret = AMP_SHM_CleanCache(mem_info->uMemHandle,
                           mem_info->uMemOffset,
                           VIDEO_ES_BUFFER_SIZE);
  assert(ret == SUCCESS);

  ret = AMP_SHM_InvalidateCache(mem_info->uMemHandle,
                                mem_info->uMemOffset,
                                VIDEO_ES_BUFFER_SIZE);
  assert(ret == SUCCESS);

  AMP_RPC(ret, AMP_VDEC_PushBD, av->amp_vdec, AMP_PORT_INPUT, 0, buf_desc);
  assert(ret == SUCCESS);
}


/**
 *
 */
static void
submit_au(amp_video_t *av, void *data, size_t len, int64_t pts, int64_t dts)
{
  const static AVRational mpeg_tc = {1, 90000};

  pts = pts != PTS_UNSET ? av_rescale_q(pts, AV_TIME_BASE_Q, mpeg_tc) : pts;
  dts = dts != PTS_UNSET ? av_rescale_q(dts, AV_TIME_BASE_Q, mpeg_tc) : dts;

  video_packet_enqueue(av, data, len, pts, dts);
}


/**
 *
 */
static void
amp_video_decode(struct media_codec *mc, struct video_decoder *vd,
                 struct media_queue *mq, struct media_buf *mb, int reqsize)
{
  media_pipe_t *mp = mq->mq_mp;
  amp_video_t *av = mc->opaque;

  while(1) {

    int fullness = amp_video_buffer_fullness(av);

    // XXX This is crap, real events should be used here instead

    TRACE(TRACE_DEBUG, "AMP", "video fullness: %d", fullness);
    if(fullness >  MAX_VIDEO_STREAM_SIZE * 7 / 8) {
      usleep(10000);
      continue;
    }
    break;
  }

  if(av->annexb.extradata != NULL && !av->annexb.extradata_injected) {
    submit_au(av, av->annexb.extradata, av->annexb.extradata_size,
              mb->mb_pts, mb->mb_dts);
    av->annexb.extradata_injected = 1;
  }

  uint8_t *data = mb->mb_data;
  size_t size   = mb->mb_size;

  h264_to_annexb(&av->annexb, &data, &size);
  submit_au(av, data, size, mb->mb_pts, mb->mb_dts);

  TRACE(TRACE_DEBUG, "AMP", "Enqueued video frame");

  if(!av->av_configured) {
    av->av_configured = 1;
    mp->mp_set_video_codec('amp', mc, mp->mp_video_frame_opaque);
  }
}


/**
 *
 */
static int
amp_codec_create(media_codec_t *mc, const media_codec_params_t *mcp,
                 media_pipe_t *mp)
{
  HRESULT ret;
  AMP_COMPONENT_CONFIG amp_config;
  amp_extra_t *ae = mp->mp_extra;

  if(mc->codec_id != CODEC_ID_H264)
    return 1;

  amp_video_t *av = calloc(1, sizeof(amp_video_t));

  mc->opaque = av;

  if(mcp != NULL && mcp->extradata_size)
    h264_to_annexb_init(&av->annexb, mcp->extradata, mcp->extradata_size);

  int64_t t0 = showtime_get_ts();

  amp_video_alloc_buffer(av);

  AMP_RPC(ret, AMP_FACTORY_CreateComponent, amp_factory,
          AMP_COMPONENT_VDEC, 0, &av->amp_vdec);
  assert(ret == SUCCESS);

  AmpMemClear(&amp_config, sizeof(AMP_COMPONENT_CONFIG));
  amp_config._d = AMP_COMPONENT_VDEC;
  amp_config._u.pVDEC.mode = AMP_TUNNEL;
  amp_config._u.pVDEC.uiType = MEDIA_VES_AVC;
  amp_config._u.pVDEC.uiFlag |= 1 << 9;
  AMP_RPC(ret, AMP_VDEC_Open, av->amp_vdec, &amp_config);
  assert(ret == SUCCESS);

  ret = AMP_ConnectApp(av->amp_vdec, AMP_PORT_INPUT, 0, amp_vdec_callback, av);
  assert(ret == SUCCESS);

  AMP_RPC(ret, AMP_VDEC_SetState, av->amp_vdec, AMP_EXECUTING);
  assert(ret == SUCCESS);

  TRACE(TRACE_DEBUG, "AMP", "Video codec setup time: %d",
        (int)(showtime_get_ts() - t0));

  mc->decode = amp_video_decode;
  mc->flush  = amp_video_flush;
  mc->close  = amp_video_close;
  return 0;
}


REGISTER_CODEC(NULL, amp_codec_create, 100);
