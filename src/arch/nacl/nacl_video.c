/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2014 Lonelycoder AB
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


#include "ppapi/c/pp_errors.h"
#include "ppapi/c/pp_module.h"
#include "ppapi/c/pp_var.h"
#include "ppapi/c/pp_codecs.h"
#include "ppapi/c/ppb.h"
#include "ppapi/c/ppb_core.h"
#include "ppapi/c/ppb_graphics_3d.h"
#include "ppapi/c/ppb_message_loop.h"
#include "ppapi/c/ppb_video_decoder.h"

#include "nacl.h"
#include "video/h264_annexb.h"
#include "media/media.h"
#include "video/video_decoder.h"
#include "misc/minmax.h"

extern const PPB_Core *ppb_core;
extern const PPB_VideoDecoder *ppb_videodecoder;
extern const PPB_MessageLoop *ppb_messageloop;

extern PP_Resource nacl_3d_context;
extern PP_Instance g_Instance;


#define PICINFO_SIZE 128

TAILQ_HEAD(nacl_video_texture_queue, nacl_video_texture);


/**
 *
 */
typedef struct nacl_video_codec {
  PP_Resource nvc_decoder;
  h264_annexb_ctx_t nvc_annexb;
  media_pipe_t *nvc_mp;
  int nvc_run;

  int nvc_picinfo_ptr;
  media_buf_meta_t nvc_picinfo[PICINFO_SIZE];

  PP_Resource nvc_message_loop;
  pthread_t nvc_thread;

  struct nacl_video_texture_queue nvc_textures;

  // --

  hts_cond_t nvc_cond;
  const void *nvc_data;
  size_t nvc_size;
  int nvc_cur_picinfo;


} nacl_video_codec_t;


/**
 *
 */
typedef struct nacl_video_texture {
  PP_Resource decoder;
  struct PP_VideoPicture pic;
  nacl_video_codec_t *nvc;
  TAILQ_ENTRY(nacl_video_texture) link;
} nacl_video_texture_t;


static void picture_get(nacl_video_codec_t *nvc);

/**
 *
 */
static void
picture_release(void *aux)
{
  nacl_video_texture_t *nvt = aux;
  // nvt->nvc may be NULL here
  ppb_videodecoder->RecyclePicture(nvt->decoder, &nvt->pic);
  ppb_core->ReleaseResource(nvt->decoder);
  free(nvt);
}


/**
 *
 */
static void
got_picture(void *aux, int32_t retval)
{
  nacl_video_texture_t *nvt = aux;
  nacl_video_codec_t *nvc = nvt->nvc;
  media_pipe_t *mp = nvc->nvc_mp;
  nvt->nvc = NULL;

  if(retval) {
    TRACE(TRACE_DEBUG, "NACLVIDEO", "Failed to get picture -- %s",
          pepper_errmsg(retval));
    free(nvt);

  } else {

    nvt->decoder = nvc->nvc_decoder;
    ppb_core->AddRefResource(nvt->decoder);

    hts_mutex_lock(&mp->mp_mutex);
    TAILQ_INSERT_TAIL(&nvc->nvc_textures, nvt, link);
    hts_cond_signal(&nvc->nvc_cond);
    hts_mutex_unlock(&mp->mp_mutex);
  }

  picture_get(nvc);
}


/**
 *
 */
static void
picture_get(nacl_video_codec_t *nvc)
{
  nacl_video_texture_t *nvt = calloc(1, sizeof(nacl_video_texture_t));
  nvt->nvc = nvc;
  int r = ppb_videodecoder->GetPicture(nvc->nvc_decoder, &nvt->pic,
                                       (const struct PP_CompletionCallback) {
                                         &got_picture, nvt});
  if(!r)
    return;

  if(r != PP_OK_COMPLETIONPENDING) {
    TRACE(TRACE_DEBUG, "NACL", "GetPicture failed - %d", r);
    free(nvt);
  }
}


/**
 *
 */
static void *
nvc_thread(void *aux)
{
  nacl_video_codec_t *nvc = aux;

  ppb_messageloop->AttachToCurrentThread(nvc->nvc_message_loop);

  picture_get(nvc);
  ppb_messageloop->Run(nvc->nvc_message_loop);
  TRACE(TRACE_DEBUG, "NACLVIDEO", "Thread exiting");
  return NULL;
}


/**
 *
 */
static void
deliver_frames(nacl_video_codec_t *nvc, video_decoder_t *vd)
{
  media_pipe_t *mp = nvc->nvc_mp;
  nacl_video_texture_t *nvt;

  while((nvt = TAILQ_FIRST(&nvc->nvc_textures)) != NULL) {
    TAILQ_REMOVE(&nvc->nvc_textures, nvt, link);

    hts_mutex_unlock(&mp->mp_mutex);

    const media_buf_meta_t *mbm = &nvc->nvc_picinfo[nvt->pic.decode_id];
    frame_info_t fi = {};

    TRACE(TRACE_DEBUG, "NACL", "Video frame %d x %d  target:%d id:%d",
          nvt->pic.texture_size.width,
          nvt->pic.texture_size.height,
          nvt->pic.texture_target,
          nvt->pic.texture_id);

    fi.fi_width  = nvt->pic.texture_size.width;
    fi.fi_height = nvt->pic.texture_size.height;

    fi.fi_pts         = mbm->mbm_pts;
    fi.fi_epoch       = mbm->mbm_epoch;
    fi.fi_delta       = mbm->mbm_delta;
    fi.fi_drive_clock = mbm->mbm_drive_clock;
    fi.fi_type        = 'tex';

    fi.fi_u32[0]      = nvt->pic.texture_target;
    fi.fi_u32[1]      = nvt->pic.texture_id;

    fi.fi_ref_release = picture_release;
    fi.fi_ref_aux     = nvt;

    fi.fi_dar_num = fi.fi_width;
    fi.fi_dar_den = fi.fi_height;

    fi.fi_duration = 41666;

    int r = video_deliver_frame(vd, &fi);
    TRACE(TRACE_DEBUG, "NACL", "Deliver frame = %d", r);
    if(r)
      picture_release(nvt);
    hts_mutex_lock(&mp->mp_mutex);
  }
}


/**
 *
 */
static void
discard_frames(nacl_video_codec_t *nvc)
{
  nacl_video_texture_t *nvt;

  while((nvt = TAILQ_FIRST(&nvc->nvc_textures)) != NULL) {
    TAILQ_REMOVE(&nvc->nvc_textures, nvt, link);
    picture_release(nvt);
  }
}


/**
 *
 */
static void
frame_decoded(void *aux, int32_t retval)
{
  nacl_video_codec_t *nvc = aux;
  media_pipe_t *mp = nvc->nvc_mp;

  TRACE(TRACE_DEBUG, "NACL", "Frame decoded retval=%d", retval);

  hts_mutex_lock(&mp->mp_mutex);
  nvc->nvc_data = NULL;
  hts_cond_signal(&nvc->nvc_cond);
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
static void
frame_decode(void *aux, int32_t retval)
{
  nacl_video_codec_t *nvc = aux;
  media_pipe_t *mp = nvc->nvc_mp;

  TRACE(TRACE_DEBUG, "NACL", "About to deliver %d", nvc->nvc_cur_picinfo);

  int r = ppb_videodecoder->Decode(nvc->nvc_decoder, nvc->nvc_cur_picinfo,
                                   nvc->nvc_size, nvc->nvc_data,
                                   (const struct PP_CompletionCallback) {
                                     &frame_decoded, nvc});

  if(!r)
    return;

  if(r != PP_OK_COMPLETIONPENDING) {
    TRACE(TRACE_DEBUG, "NACL", "Unable to decode frame = %d", r);
    hts_mutex_lock(&mp->mp_mutex);
    nvc->nvc_data = NULL;
    hts_cond_signal(&nvc->nvc_cond);
    hts_mutex_unlock(&mp->mp_mutex);
  }
}

/**
 *
 */
static void
submit_au(nacl_video_codec_t *nvc, const void *data, size_t size,
          const media_buf_t *mb, video_decoder_t *vd)
{
  media_pipe_t *mp = nvc->nvc_mp;
  copy_mbm_from_mb(&nvc->nvc_picinfo[nvc->nvc_picinfo_ptr], mb);

  nvc->nvc_data = data;
  nvc->nvc_size = size;
  nvc->nvc_cur_picinfo = nvc->nvc_picinfo_ptr;
  nvc->nvc_picinfo_ptr = (nvc->nvc_picinfo_ptr + 1) & (PICINFO_SIZE - 1);

  ppb_messageloop->PostWork(nvc->nvc_message_loop,
                            (const struct PP_CompletionCallback) {
                              &frame_decode, nvc}, 0);

  TRACE(TRACE_DEBUG, "NACL", "Work posted for %d", nvc->nvc_cur_picinfo);

  deliver_frames(nvc, vd);

  while(nvc->nvc_data != NULL) {
    pthread_cond_wait(&nvc->nvc_cond, &mp->mp_mutex);
    deliver_frames(nvc, vd);
  }
  TRACE(TRACE_DEBUG, "NACL", "AU submitted");
}


/**
 *
 */
static int
nacl_codec_decode(struct media_codec *mc, struct video_decoder *vd,
                  struct media_queue *mq, struct media_buf *mb)
{
  nacl_video_codec_t *nvc = mc->opaque;

  if(nvc->nvc_annexb.extradata != NULL &&
     nvc->nvc_annexb.extradata_injected == 0) {

    submit_au(nvc, nvc->nvc_annexb.extradata, nvc->nvc_annexb.extradata_size,
              mb, vd);
    nvc->nvc_annexb.extradata_injected = 1;
  }

  uint8_t *data = mb->mb_data;
  size_t size = mb->mb_size;

  h264_to_annexb(&nvc->nvc_annexb, &data, &size);
  submit_au(nvc, data, size, mb, vd);
  return 0;
}


/**
 *
 */
static void
nacl_codec_flush(struct media_codec *mc, struct video_decoder *vd)
{
  nacl_video_codec_t *nvc = mc->opaque;
  nvc->nvc_annexb.extradata_injected = 0;
  ppb_videodecoder->Reset(nvc->nvc_decoder, PP_BlockUntilComplete());
}


/**
 *
 */
static void
nacl_codec_close(struct media_codec *mc)
{
  nacl_video_codec_t *nvc = mc->opaque;
  nvc->nvc_run = 0;
  TRACE(TRACE_DEBUG, "NACL", "Codec close");
  ppb_videodecoder->Reset(nvc->nvc_decoder, PP_BlockUntilComplete());
  TRACE(TRACE_DEBUG, "NACL", "Reset done");
  ppb_messageloop->PostQuit(nvc->nvc_message_loop, 0);
  TRACE(TRACE_DEBUG, "NACL", "Quit done");
  pthread_join(nvc->nvc_thread, NULL);
  TRACE(TRACE_DEBUG, "NACL", "Thread joined");
  discard_frames(nvc);
  TRACE(TRACE_DEBUG, "NACL", "Frames discarded");
  ppb_core->ReleaseResource(nvc->nvc_decoder);
  TRACE(TRACE_DEBUG, "NACL", "Resource released");
  h264_to_annexb_cleanup(&nvc->nvc_annexb);
  free(nvc);
  TRACE(TRACE_DEBUG, "NACL", "Cleanup done");
}


/**
 *
 */
static int
nacl_codec_create(media_codec_t *mc, const media_codec_params_t *mcp,
                  media_pipe_t *mp)
{
  return 1;

  if(ppb_videodecoder == NULL)
    return 1;

  int profile;

  switch(mc->codec_id) {
  case AV_CODEC_ID_H264:
    profile = PP_VIDEOPROFILE_H264HIGH;
    break;

  default:
    return 1;
  }

  PP_Resource vd = ppb_videodecoder->Create(g_Instance);

  int r = ppb_videodecoder->Initialize(vd, nacl_3d_context, profile, 1,
                                       PP_BlockUntilComplete());

  if(r) {
    TRACE(TRACE_DEBUG, "NACL", "Unable to init video decoder -- %s",
          pepper_errmsg(r));
    ppb_core->ReleaseResource(vd);
    return 1;
  }

  nacl_video_codec_t *nvc = calloc(1, sizeof(nacl_video_codec_t));
  TAILQ_INIT(&nvc->nvc_textures);
  nvc->nvc_decoder = vd;

  hts_cond_init(&nvc->nvc_cond, &mp->mp_mutex);

  if(mc->codec_id == AV_CODEC_ID_H264 && mcp != NULL && mcp->extradata_size)
    h264_to_annexb_init(&nvc->nvc_annexb, mcp->extradata, mcp->extradata_size);

  nvc->nvc_mp = mp;

  nvc->nvc_message_loop = ppb_messageloop->Create(g_Instance);

  pthread_create(&nvc->nvc_thread, NULL, nvc_thread, nvc);
  mc->opaque = nvc;
  mc->close  = nacl_codec_close;
  mc->decode_locked = nacl_codec_decode;
  mc->flush  = nacl_codec_flush;
  return 0;
}

REGISTER_CODEC(NULL, nacl_codec_create, 100);
