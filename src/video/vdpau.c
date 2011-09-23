/**
 *  X11 VDPAU common code
 *  Copyright (C) 2010 Andreas Öman
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
 */

#include <stdlib.h>
#include <assert.h>
#include "showtime.h"
#include "media.h"
#include "vdpau.h"
#include "video/video_decoder.h"

static VdpStatus mixer_setup(vdpau_dev_t *vd, vdpau_mixer_t *vm);

#define vproc(id, ptr) do { \
    VdpStatus r;						\
    if((r = gp(vd->vd_dev, id, (void *)&vd->ptr)) != VDP_STATUS_OK)	\
      return r;							\
  }while(0)


/**
 *
 */
static VdpStatus
resolve_funcs(vdpau_dev_t *vd, VdpGetProcAddress *gp)
{
  vproc(VDP_FUNC_ID_GET_ERROR_STRING,
	vdp_get_error_string);
  vproc(VDP_FUNC_ID_PREEMPTION_CALLBACK_REGISTER,
       vdp_preemption_callback_register);

  vproc(VDP_FUNC_ID_VIDEO_SURFACE_CREATE,
	vdp_video_surface_create);
  vproc(VDP_FUNC_ID_VIDEO_SURFACE_DESTROY,
	vdp_video_surface_destroy);

  vproc(VDP_FUNC_ID_OUTPUT_SURFACE_CREATE,
	vdp_output_surface_create);
  vproc(VDP_FUNC_ID_OUTPUT_SURFACE_DESTROY,
	vdp_output_surface_destroy);

  vproc(VDP_FUNC_ID_DECODER_CREATE,
	vdp_decoder_create);
  vproc(VDP_FUNC_ID_DECODER_DESTROY,
	vdp_decoder_destroy);
  vproc(VDP_FUNC_ID_DECODER_RENDER,
	vdp_decoder_render);
  vproc(VDP_FUNC_ID_DECODER_QUERY_CAPABILITIES,
	vdp_decoder_query_caps);

  vproc(VDP_FUNC_ID_PRESENTATION_QUEUE_CREATE,
	vdp_presentation_queue_create);
  vproc(VDP_FUNC_ID_PRESENTATION_QUEUE_DESTROY,
	vdp_presentation_queue_destroy);
  vproc(VDP_FUNC_ID_PRESENTATION_QUEUE_TARGET_CREATE_X11,
	vdp_presentation_queue_target_create_x11);
  vproc(VDP_FUNC_ID_PRESENTATION_QUEUE_TARGET_DESTROY,
	vdp_presentation_queue_target_destroy);
  vproc(VDP_FUNC_ID_PRESENTATION_QUEUE_QUERY_SURFACE_STATUS,
	vdp_presentation_queue_query_surface_status);
  vproc(VDP_FUNC_ID_PRESENTATION_QUEUE_DISPLAY,
	vdp_presentation_queue_display);
  vproc(VDP_FUNC_ID_PRESENTATION_QUEUE_GET_TIME,
	vdp_presentation_queue_get_time);
  vproc(VDP_FUNC_ID_PRESENTATION_QUEUE_BLOCK_UNTIL_SURFACE_IDLE,
	vdp_presentation_queue_block_until_surface_idle);

  vproc(VDP_FUNC_ID_VIDEO_MIXER_CREATE,
	vdp_video_mixer_create);
  vproc(VDP_FUNC_ID_VIDEO_MIXER_DESTROY,
	vdp_video_mixer_destroy);
  vproc(VDP_FUNC_ID_VIDEO_MIXER_RENDER,
	vdp_video_mixer_render);
  vproc(VDP_FUNC_ID_VIDEO_MIXER_SET_FEATURE_ENABLES,
	vdp_video_mixer_set_feature_enables);
  vproc(VDP_FUNC_ID_VIDEO_MIXER_GET_FEATURE_ENABLES,
	vdp_video_mixer_get_feature_enables);
  vproc(VDP_FUNC_ID_VIDEO_MIXER_GET_FEATURE_SUPPORT,
	vdp_video_mixer_get_feature_support);
  vproc(VDP_FUNC_ID_VIDEO_MIXER_QUERY_FEATURE_SUPPORT,
	vdp_video_mixer_query_feature_support);
  vproc(VDP_FUNC_ID_VIDEO_MIXER_SET_ATTRIBUTE_VALUES,
	vdp_video_mixer_set_attribute_values);

  vproc(VDP_FUNC_ID_GENERATE_CSC_MATRIX,
	vdp_generate_csc_matrix);

  return VDP_STATUS_OK;
}


/**
 *
 */
const char *
vdpau_errstr(vdpau_dev_t *vd, VdpStatus st)
{
  if(st == VDP_STATUS_OK)
    return "OK";

  if(vd->vdp_get_error_string == NULL)
    return "Untranslated error";
  return vd->vdp_get_error_string(st);
}



static const struct {
  int id;
  const char *name;
} vdpauprofiles[] = {
  { VDP_DECODER_PROFILE_MPEG1,                   "MPEG1"},
  { VDP_DECODER_PROFILE_MPEG2_MAIN,              "MPEG2 Main"},
  { VDP_DECODER_PROFILE_MPEG2_SIMPLE,            "MPEG2 Simple"},

  { VDP_DECODER_PROFILE_H264_BASELINE,           "H264 Baseline"},
  { VDP_DECODER_PROFILE_H264_HIGH,               "H264 High"},
  { VDP_DECODER_PROFILE_H264_MAIN,               "H264 Main"},

  { VDP_DECODER_PROFILE_VC1_ADVANCED,            "VC-1 Advanced"},
  { VDP_DECODER_PROFILE_VC1_MAIN,                "VC-1 Main"},
  { VDP_DECODER_PROFILE_VC1_SIMPLE,              "VC-1 Simple"},

  { VDP_DECODER_PROFILE_MPEG4_PART2_ASP,         "MPEG4 Part2 ASP"},
  { VDP_DECODER_PROFILE_MPEG4_PART2_SP,          "MPEG4 Part2 SP"},
  { VDP_DECODER_PROFILE_MPEG4_PART2_SP,          "MPEG4 Part2 SP"},
  
  { VDP_DECODER_PROFILE_DIVX4_HD_1080P,          "DIVX4 HD 1080P"},
  { VDP_DECODER_PROFILE_DIVX4_HOME_THEATER,      "DIVX4 Home Theater"},
  { VDP_DECODER_PROFILE_DIVX4_MOBILE,            "DIVX4 Mobile"},
  { VDP_DECODER_PROFILE_DIVX4_QMOBILE,           "DIVX4 QMobile"},
  { VDP_DECODER_PROFILE_DIVX5_HD_1080P,          "DIVX5 Home Theater"},
  { VDP_DECODER_PROFILE_DIVX5_MOBILE,            "DIVX5 Mobile"},
  { VDP_DECODER_PROFILE_DIVX5_QMOBILE,           "DIVX5 QMobile"},
};

/**
 *
 */
static void
vdpau_info(vdpau_dev_t *vd)
{
  int i;
  VdpStatus r;

  TRACE(TRACE_DEBUG, "VDPAU", "VDPAU decoder supported profiles");
  TRACE(TRACE_DEBUG, "VDPAU", "%-20s  %-20s %-10s %-10s %-10s",
	"Profile", "Level", "MB/frame", "Width", "Height");
	

  for(i = 0; i < sizeof(vdpauprofiles) / sizeof(vdpauprofiles[0]); i++) {
    uint32_t max_level, max_mb, max_width, max_height;
    VdpBool is_supported;

    r = vd->vdp_decoder_query_caps(vd->vd_dev,
				   vdpauprofiles[i].id,
				   &is_supported,
				   &max_level,
				   &max_mb,
				   &max_width,
				   &max_height);
    
    if(r != VDP_STATUS_OK)
      is_supported = 0;

    if(!is_supported)
      TRACE(TRACE_DEBUG, "VDPAU","%-20s  Not supported",
	    vdpauprofiles[i].name);
    else
      TRACE(TRACE_DEBUG,"VDPAU", "%-20s  %-20d %-10d %-10d %-10d",
			vdpauprofiles[i].name,
			max_level,
			max_mb,
			max_width,
			max_height);
  }
}


/**
 *
 */
static void
preempt(VdpDevice device, void *context)
{
  vdpau_dev_t *vd = context;
  TRACE(TRACE_DEBUG, "VDPAU", "VDPAU preempted");
  vd->vd_preempted(vd->vd_opaque);
}



/**
 *
 */
static VdpStatus
vdpau_setup_x11(vdpau_dev_t *vd)
{
  VdpStatus r;
  VdpGetProcAddress *getproc;

  r = vdp_device_create_x11(vd->vd_dpy, vd->vd_screen, &vd->vd_dev, &getproc);
  if(r != VDP_STATUS_OK) {
    TRACE(TRACE_DEBUG, "VDPAU", "Unable to create VDPAU device");
    return r;
  }

  if((r = resolve_funcs(vd, getproc)) != VDP_STATUS_OK)
    return r;

  vd->vdp_preemption_callback_register(vd->vd_dev, preempt, vd);
  return VDP_STATUS_OK;
}



/**
 *
 */
vdpau_dev_t *
vdpau_init_x11(Display *dpy, int screen,
	       void (*preempted)(void *opaque), void *opaque)
{
  vdpau_dev_t *vd = calloc(1, sizeof(vdpau_dev_t));

  vd->vd_dpy = dpy;
  vd->vd_screen = screen;
  vd->vd_preempted = preempted;
  vd->vd_opaque = opaque;

  hts_mutex_init(&vd->vd_mutex);

  if(vdpau_setup_x11(vd)) {
    TRACE(TRACE_DEBUG, "VDPAU", "Unable to create VDPAU device");
    free(vd);
    return NULL;
  }
  vdpau_info(vd);
  return vd;
}


/**
 *
 */
VdpStatus
vdpau_reinit_x11(vdpau_dev_t *vd)
{
  return vdpau_setup_x11(vd) != VDP_STATUS_OK;
}


/**
 *
 */
int
vdpau_get_buffer(struct AVCodecContext *ctx, AVFrame *pic)
{
  media_codec_t *mc = ctx->opaque;
  vdpau_codec_t *vc = mc->opaque;
  vdpau_video_surface_t *vvs;
  media_buf_t *mb = vc->vc_mb;

  if((vvs = TAILQ_FIRST(&vc->vc_vvs_free)) == NULL)
    return -1;

  TAILQ_REMOVE(&vc->vc_vvs_free, vvs, vvs_link);
  TAILQ_INSERT_TAIL(&vc->vc_vvs_alloc, vvs, vvs_link);

  pic->data[0] = (uint8_t *)&vvs->vvs_rs;
  pic->data[1] =  pic->data[2] = NULL;
  pic->linesize[0] = pic->linesize[1] =  pic->linesize[2] = 0;
  pic->opaque = vvs;
  pic->type = FF_BUFFER_TYPE_USER;
  pic->age = 256 * 256 * 256 * 64;

  memcpy(&vvs->vvs_mb, mb, sizeof(media_buf_t));
  return 0;
}


/**
 *
 */
static VdpStatus
vdpau_create_buffers(vdpau_codec_t *vc, int width, int height, int buffers)
{
  int i;
  vdpau_video_surface_t *vvs;
  vdpau_dev_t *vd = vc->vc_vd;
  VdpStatus r;

  for(i = 0; i < buffers; i++) {
    vvs = calloc(1, sizeof(vdpau_video_surface_t));
    r = vd->vdp_video_surface_create(vd->vd_dev, VDP_CHROMA_TYPE_420,
				     width, height, &vvs->vvs_surface);
    if(r != VDP_STATUS_OK) {
      free(vvs);
      return r;
    }
    vvs->vvs_idx = i;
    vvs->vvs_rs.surface = vvs->vvs_surface;
    TAILQ_INSERT_TAIL(&vc->vc_vvs_free, vvs, vvs_link);
  }
  return VDP_STATUS_OK;
}


/**
 *
 */
void
vdpau_release_buffer(struct AVCodecContext *ctx, AVFrame *pic)
{
  vdpau_video_surface_t *vvs = (vdpau_video_surface_t *)pic->opaque;
  media_codec_t *mc = ctx->opaque;
  vdpau_codec_t *vc = mc->opaque;

  vvs->vvs_rs.state &= ~FF_VDPAU_STATE_USED_FOR_REFERENCE;
  TAILQ_REMOVE(&vc->vc_vvs_alloc, vvs, vvs_link);
  TAILQ_INSERT_TAIL(&vc->vc_vvs_free, vvs, vvs_link);
  pic->data[0] = NULL;
  pic->opaque = NULL;
}


/**
 *
 */
void
vdpau_draw_horiz_band(struct AVCodecContext *ctx, 
		      const AVFrame *frame, 
		      int offset[4], int y, int type, int h)
{
  media_codec_t *mc = ctx->opaque;
  vdpau_codec_t *vc = mc->opaque;
  vdpau_dev_t *vd = vc->vc_vd;
  struct vdpau_render_state *rs = (struct vdpau_render_state *)frame->data[0];

  vd->vdp_decoder_render(vc->vc_decoder, rs->surface, 
			 (VdpPictureInfo const *)&rs->info,
			 rs->bitstream_buffers_used, rs->bitstream_buffers);
}


/**
 *
 */
static void
vdpau_decode(struct media_codec *mc, struct video_decoder *vd,
	     struct media_queue *mq, struct media_buf *mb, int reqsize)
{
  media_codec_t *cw = mb->mb_cw;
  AVCodecContext *ctx = cw->codec_ctx;
  vdpau_codec_t *vc = mc->opaque;
  media_pipe_t *mp = vd->vd_mp;
  vdpau_video_surface_t *vvs;
  int got_pic = 0;
  AVFrame *frame = vd->vd_frame;
  
  if(vd->vd_do_flush) {
    AVPacket avpkt;
    av_init_packet(&avpkt);
    avpkt.data = NULL;
    avpkt.size = 0;
    do {
      avcodec_decode_video2(ctx, frame, &got_pic, &avpkt);
    } while(got_pic);

    vd->vd_do_flush = 0;
    vd->vd_prevpts = AV_NOPTS_VALUE;
    vd->vd_nextpts = AV_NOPTS_VALUE;
    vd->vd_estimated_duration = 0;
    avcodec_flush_buffers(ctx);
    vd->vd_compensate_thres = 5;
  }

  ctx->skip_frame = mb->mb_skip == 1 ? AVDISCARD_NONREF : AVDISCARD_NONE;
  if(mb->mb_skip == 2)
    vd->vd_skip = 1;

  vc->vc_mb = mb;

  AVPacket avpkt;
  av_init_packet(&avpkt);
  avpkt.data = mb->mb_data;
  avpkt.size = mb->mb_size;

  avcodec_decode_video2(ctx, frame, &got_pic, &avpkt);

  if(mp->mp_stats)
    mp_set_mq_meta(mq, cw->codec, cw->codec_ctx);

  if(!got_pic || mb->mb_skip == 1)
    return;

  vd->vd_skip = 0;
  vvs = frame->opaque;
  video_deliver_frame(vd, vd->vd_mp, mq, ctx, frame, &vvs->vvs_mb, 0);
}



/**
 *
 */
static void
vc_cleanup(vdpau_codec_t *vc)
{
  vdpau_dev_t *vd = vc->vc_vd;
  vdpau_video_surface_t *vvs;

  if(vc->vc_decoder != VDP_INVALID_HANDLE) {
    vd->vdp_decoder_destroy(vc->vc_decoder);
    vc->vc_decoder = VDP_INVALID_HANDLE;
  }

  while((vvs = TAILQ_FIRST(&vc->vc_vvs_free)) != NULL) {
    TAILQ_REMOVE(&vc->vc_vvs_free, vvs, vvs_link);
    vd->vdp_video_surface_destroy(vvs->vvs_surface);
    free(vvs);
  }
}


/**
 *
 */
static void
vc_destroy(vdpau_codec_t *vc)
{
  vc_cleanup(vc);
  assert(TAILQ_FIRST(&vc->vc_vvs_alloc) == NULL);
  free(vc);
}


/**
 *
 */
static void
vdpau_codec_close(struct media_codec *mc)
{
  vdpau_codec_t *vc = mc->opaque;
  vc_destroy(vc);
}


/**
 *
 */
static enum PixelFormat 
vdpau_get_pixfmt(struct AVCodecContext *s, const enum PixelFormat *fmt)
{
  return fmt[0];
}


/**
 *
 */
static void
vdpau_codec_reinit(media_codec_t *mc)
{
  vdpau_codec_t *vc = mc->opaque;
  vdpau_dev_t *vd = vc->vc_vd;
  vdpau_video_surface_t *vvs;
  VdpStatus r;

  r = vd->vdp_decoder_create(vd->vd_dev, vc->vc_profile, 
			     vc->vc_width, vc->vc_height,
			     vc->vc_refframes, &vc->vc_decoder);

  if(r != VDP_STATUS_OK) {
    TRACE(TRACE_INFO, "VDPAU", "Unable to reinit decoder: %s",
	  vdpau_errstr(vd, r));
    return;
  }

  TAILQ_FOREACH(vvs, &vc->vc_vvs_free, vvs_link)
    vd->vdp_video_surface_create(vd->vd_dev, VDP_CHROMA_TYPE_420,
				 vc->vc_width, vc->vc_height,
				 &vvs->vvs_surface);
  
  TAILQ_FOREACH(vvs, &vc->vc_vvs_alloc, vvs_link)
    vd->vdp_video_surface_create(vd->vd_dev, VDP_CHROMA_TYPE_420,
				 vc->vc_width, vc->vc_height,
				 &vvs->vvs_surface);
  
}


/**
 *
 */
int
vdpau_codec_create(media_codec_t *mc, enum CodecID id,
		   AVCodecContext *ctx, media_codec_params_t *mcp,
		   media_pipe_t *mp)
{
  VdpDecoderProfile profile;
  vdpau_dev_t *vd = mp->mp_vdpau_dev;
  VdpStatus r;
  int refframes;

  if(vd == NULL)
    return 1;

  if(mcp->width == 0 || mcp->height == 0)
    return 1;

  switch(id) {

  case CODEC_ID_MPEG1VIDEO:
    profile = VDP_DECODER_PROFILE_MPEG1; 
    mc->codec = avcodec_find_decoder_by_name("mpegvideo_vdpau");
    refframes = 2;
    break;

  case CODEC_ID_MPEG2VIDEO:
    profile = VDP_DECODER_PROFILE_MPEG2_MAIN; 
    mc->codec = avcodec_find_decoder_by_name("mpegvideo_vdpau");
    refframes = 2;
    break;

  case CODEC_ID_H264:
    profile = VDP_DECODER_PROFILE_H264_HIGH; 
    mc->codec = avcodec_find_decoder_by_name("h264_vdpau");
    refframes = 16;
    break;
#if 0 // Seems broken
  case CODEC_ID_VC1:
    profile = VDP_DECODER_PROFILE_VC1_ADVANCED; 
    mc->codec = avcodec_find_decoder_by_name("vc1_vdpau");
    refframes = 16;
    break;

  case CODEC_ID_WMV3:
    profile = VDP_DECODER_PROFILE_VC1_MAIN;
    mc->codec = avcodec_find_decoder_by_name("wmv3_vdpau");
    refframes = 16;
    break;
#endif
  default:
    return 1;
  }

  if(mc->codec == NULL)
    return -1;

  vdpau_codec_t *vc = calloc(1, sizeof(vdpau_codec_t));
  TAILQ_INIT(&vc->vc_vvs_alloc);
  TAILQ_INIT(&vc->vc_vvs_free);
  vc->vc_vd = vd;
  vc->vc_width = mcp->width;
  vc->vc_height = mcp->height;
  vc->vc_profile = profile;
  vc->vc_refframes = refframes;

  r = vd->vdp_decoder_create(vd->vd_dev, vc->vc_profile, 
			     vc->vc_width, vc->vc_height,
			     vc->vc_refframes, &vc->vc_decoder);

  if(r != VDP_STATUS_OK) {
    TRACE(TRACE_INFO, "VDPAU", "Unable to create decoder: %s",
	  vdpau_errstr(vd, r));
    vc_destroy(vc);
    return -1;
  }

  
  r = vdpau_create_buffers(vc, vc->vc_width, vc->vc_height,
			   vc->vc_refframes + 5);

  if(r != VDP_STATUS_OK) {
    TRACE(TRACE_INFO, "VDPAU", "Unable to allocate decoding buffers");
    vc_destroy(vc);
    return -1;
  }

  TRACE(TRACE_DEBUG, "VDPAU", "Decoder initialized");
	  
  mc->codec_ctx = ctx ?: avcodec_alloc_context();
  mc->codec_ctx->codec_id   = mc->codec->id;
  mc->codec_ctx->codec_type = mc->codec->type;

  if(avcodec_open(mc->codec_ctx, mc->codec) < 0) {
    if(ctx == NULL)
      free(mc->codec_ctx);
    mc->codec = NULL;
    vc_destroy(vc);
    return -1;
  }

  mc->codec_ctx->get_buffer      = vdpau_get_buffer;
  mc->codec_ctx->release_buffer  = vdpau_release_buffer;
  mc->codec_ctx->draw_horiz_band = vdpau_draw_horiz_band;
  mc->codec_ctx->get_format      = vdpau_get_pixfmt;

  mc->codec_ctx->slice_flags = SLICE_FLAG_CODED_ORDER | SLICE_FLAG_ALLOW_FIELD;

  mc->codec_ctx->opaque = mc;
  mc->opaque = vc;
  mc->decode = vdpau_decode;
  mc->close  = vdpau_codec_close;
  mc->reinit = vdpau_codec_reinit;
  return 0;
}


/**
 *
 */
int
vdpau_mixer_feat(vdpau_dev_t *vd, VdpVideoMixerFeature f)
{
  VdpStatus st;
  VdpBool b;
  
  st = vd->vdp_video_mixer_query_feature_support(vd->vd_dev, f, &b);
  return st == VDP_STATUS_OK ? b : 0;
}



const static VdpVideoMixerFeature mixer_features[] = {
  VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL,
  VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL_SPATIAL,
};

const static VdpVideoMixerParameter mixer_params[] = {
  VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_WIDTH,
  VDP_VIDEO_MIXER_PARAMETER_VIDEO_SURFACE_HEIGHT,
};

#define MIXER_FEATURES (sizeof(mixer_features) / sizeof(VdpVideoMixerFeature))


static VdpStatus
mixer_setup(vdpau_dev_t *vd, vdpau_mixer_t *vm)
{
  VdpStatus st;
  int i;

  for(i = 0; i < 4; i++)
    vm->vm_surface_win[i] = VDP_INVALID_HANDLE;

  void const *mixer_values[] = {&vm->vm_width, &vm->vm_height};

  st = vd->vdp_video_mixer_create(vd->vd_dev,
				  MIXER_FEATURES, mixer_features,
				  2, mixer_params, mixer_values,
				  &vm->vm_mixer);

  return st;
}


/**
 *
 */
int
vdpau_mixer_create(vdpau_dev_t *vd, vdpau_mixer_t *vm, int width, int height)
{
  VdpStatus st;
  VdpBool supported[MIXER_FEATURES];

  vm->vm_color_space = -1;
  vm->vm_vd = vd;
  vm->vm_width = width;
  vm->vm_height = height;

  st = mixer_setup(vd, vm);

  if(st != VDP_STATUS_OK) {
    TRACE(TRACE_ERROR, "VDPAU", "Unable to create video mixer");
    return st;
  }

  st = vd->vdp_video_mixer_get_feature_support(vm->vm_mixer,
					       MIXER_FEATURES, mixer_features,
					       supported);
  if(st != VDP_STATUS_OK) {
    TRACE(TRACE_ERROR, "VDPAU", "Unable to quiery video mixer features");
    return st;
  }

  vm->vm_caps = 0;

  if(supported[0])
    vm->vm_caps |= VDPAU_MIXER_DEINTERLACE_T;
  if(supported[1])
    vm->vm_caps |= VDPAU_MIXER_DEINTERLACE_TS;

  vm->vm_enabled = 0;

  return VDP_STATUS_OK;
}


/**
 *
 */
void
vdpau_mixer_deinit(vdpau_mixer_t *vm)
{
  if(vm->vm_mixer != VDP_INVALID_HANDLE)
    vm->vm_vd->vdp_video_mixer_destroy(vm->vm_mixer);
}


/**
 *
 */
void
vdpau_mixer_set_deinterlacer(vdpau_mixer_t *vm, int on)
{
  int best;
  VdpVideoMixerFeature f;
  const char *type;
  VdpVideoMixerFeature features[1];
  VdpBool values[1];
  VdpStatus st;

  if(vm->vm_caps & VDPAU_MIXER_DEINTERLACE_TS) {
    best = VDPAU_MIXER_DEINTERLACE_TS;
    f = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL_SPATIAL;
    type = "Temporal/Spatial";
  } else if(vm->vm_caps & VDPAU_MIXER_DEINTERLACE_T) {
    best = VDPAU_MIXER_DEINTERLACE_T;
    f = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL;
    type = "Temporal";
  } else
    return;

  if(on) {
    if(vm->vm_enabled & best)
      return;
    vm->vm_enabled |= best;
  } else {
    if(!(vm->vm_enabled & best))
      return;
    vm->vm_enabled &= ~best;
  }


  features[0] = f;

  values[0]= !!on;

  st = vm->vm_vd->vdp_video_mixer_set_feature_enables(vm->vm_mixer,
						      1, features, values);
  if(st != VDP_STATUS_OK) {
    TRACE(TRACE_ERROR, "VDPAU", "Unable to %s %s deinterlacer",
	  on ? "enable" : "disable", type);
  } else {
    TRACE(TRACE_DEBUG, "VDPAU", "%s %s deinterlacer",
	  on ? "Enabled" : "Disabled", type);
	  
	  
  }
}


/**
 *
 */
void
vdpau_mixer_set_color_matrix(vdpau_mixer_t *vm, const struct frame_info *fi)
{
  int cs;
  VdpCSCMatrix matrix;
  VdpVideoMixerAttribute attributes[] = {VDP_VIDEO_MIXER_ATTRIBUTE_CSC_MATRIX};

  switch(fi->color_space) {
  case AVCOL_SPC_BT709:
    cs = VDP_COLOR_STANDARD_ITUR_BT_709;
    break;

  case AVCOL_SPC_BT470BG:
  case AVCOL_SPC_SMPTE170M:
    cs = VDP_COLOR_STANDARD_ITUR_BT_601;
    break;

  case AVCOL_SPC_SMPTE240M:
    cs = VDP_COLOR_STANDARD_SMPTE_240M;
    break;

  default:
    cs = fi->height < 720 ? VDP_COLOR_STANDARD_ITUR_BT_601 : VDP_COLOR_STANDARD_ITUR_BT_709;
    break;
  }

  if(vm->vm_color_space == cs)
    return;

  vm->vm_color_space = cs;

  if(vm->vm_vd->vdp_generate_csc_matrix(NULL, cs, &matrix) != VDP_STATUS_OK)
    return;

  void const *values[] = { &matrix };

  vm->vm_vd->vdp_video_mixer_set_attribute_values(vm->vm_mixer, 1, 
						  attributes, values);
}
