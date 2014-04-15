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

#include <stdlib.h>
#include <assert.h>
#include "showtime.h"
#include "media.h"
#include "vdpau.h"
#include "video/video_decoder.h"
#include "libav.h"

/**
 *
 */
typedef struct vdpau_codec {
  int vc_refcount;

  vdpau_dev_t *vc_vd;

  int vc_width;
  int vc_height;
  //  int vc_refframes;

  VdpDecoderProfile vc_profile;

#define VC_SURFACE_CACHE_SIZE 16
  VdpVideoSurface vc_surface_cache[VC_SURFACE_CACHE_SIZE];
  int vc_surface_cache_depth;

  hts_mutex_t vc_surface_cache_mutex;

} vdpau_codec_t;





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

  r = vdp_device_create_x11(vd->vd_dpy, vd->vd_screen, &vd->vd_dev,
                            &vd->vd_getproc);
  if(r != VDP_STATUS_OK) {
    TRACE(TRACE_DEBUG, "VDPAU", "Unable to create VDPAU device");
    return r;
  }

  if((r = resolve_funcs(vd, vd->vd_getproc)) != VDP_STATUS_OK)
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
static void
vdpau_release_surfaces(vdpau_dev_t *vd, vdpau_codec_t *vc)
{
  for(int i = 0; i < vc->vc_surface_cache_depth; i++)
    vd->vdp_video_surface_destroy(vc->vc_surface_cache[i]);
  vc->vc_surface_cache_depth = 0;
}



/**
 *
 */
static void
vdpau_codec_release(vdpau_codec_t *vc)
{
  if(atomic_add(&vc->vc_refcount, -1) != 1)
    return;

  vdpau_release_surfaces(vc->vc_vd, vc);
  hts_mutex_destroy(&vc->vc_surface_cache_mutex);
  free(vc);
}


/**
 *
 */
static void
vdpau_release_buffer(void *opaque, uint8_t *data)
{
  VdpVideoSurface surface = (uintptr_t)(void *)data;
  vdpau_codec_t *vc = opaque;

  hts_mutex_lock(&vc->vc_surface_cache_mutex);

  if(vc->vc_surface_cache_depth < VC_SURFACE_CACHE_SIZE) {
    vc->vc_surface_cache[vc->vc_surface_cache_depth++] = surface;
  } else {
    vc->vc_vd->vdp_video_surface_destroy(surface);
  }

  hts_mutex_unlock(&vc->vc_surface_cache_mutex);

  vdpau_codec_release(vc);
}


/**
 *
 */
static int
vdpau_get_buffer(struct AVCodecContext *ctx, AVFrame *frame, int flags)
{
  media_codec_t *mc = ctx->opaque;
  vdpau_codec_t *vc = mc->opaque;
  vdpau_dev_t *vd = vc->vc_vd;

  VdpStatus r;

  VdpVideoSurface surface;

  hts_mutex_lock(&vc->vc_surface_cache_mutex);

  if(vc->vc_surface_cache_depth > 0) {

    surface = vc->vc_surface_cache[--vc->vc_surface_cache_depth];

  } else {

    r = vd->vdp_video_surface_create(vd->vd_dev,
                                     VDP_CHROMA_TYPE_420,
                                     vc->vc_width, vc->vc_height, &surface);

    if(r != VDP_STATUS_OK) {
      TRACE(TRACE_INFO, "VDPAU", "Unable to create surface: %s",
            vdpau_errstr(vd, r));
      return -1;
    }
  }

  hts_mutex_unlock(&vc->vc_surface_cache_mutex);

  atomic_add(&vc->vc_refcount, 1);

  frame->data[3] = frame->data[0] = (void *)(uintptr_t)surface;
  frame->buf[0] = av_buffer_create(frame->data[0], 0, vdpau_release_buffer,
                                   vc, 0);
  return 0;
}


/**
 *
 */
static void
vdpau_codec_hw_close(struct media_codec *mc)
{
  vdpau_codec_t *vc = mc->opaque;
  vdpau_dev_t *vd = vc->vc_vd;
  AVCodecContext *ctx = mc->ctx;
  AVVDPAUContext *vctx = ctx->hwaccel_context;

  hts_mutex_lock(&vc->vc_surface_cache_mutex);
  vdpau_release_surfaces(vd, vc);
  hts_mutex_unlock(&vc->vc_surface_cache_mutex);

  vd->vdp_decoder_destroy(vctx->decoder);

  av_freep(&ctx->hwaccel_context);

  vdpau_codec_release(vc);

  mc->opaque = NULL;
}


/**
 *
 */
int
vdpau_init_libav_decode(media_codec_t *mc, AVCodecContext *ctx)
{
  media_pipe_t *mp = mc->mp;
  vdpau_dev_t *vd = mp->mp_vdpau_dev;

  if(vd == NULL)
    return 1;  // VDPAU not initialized

  VdpDecoderProfile vdp_profile;
  int refframes = 2;

  if(av_vdpau_get_profile(ctx, &vdp_profile)) {
    TRACE(TRACE_DEBUG, "VDPAU", "Can't decode %s profile %d",
          ctx->codec->name, ctx->profile);
    return 1;
  }

  if(ctx->codec_id == AV_CODEC_ID_H264)
    refframes = 16;

  int width  = (ctx->coded_width  + 1) & ~1;
  int height = (ctx->coded_height + 3) & ~3;

  if(height == 1088)
    height = 1080;

  VdpStatus r;

  AVVDPAUContext *vctx = av_vdpau_alloc_context();
  vctx->decoder = VDP_INVALID_HANDLE;
  vctx->render = vd->vdp_decoder_render;

  r = vd->vdp_decoder_create(vd->vd_dev, vdp_profile, width, height,
                             refframes, &vctx->decoder);
  if(r) {
    TRACE(TRACE_DEBUG, "VDPAU", "Unable to create decoder: %s",
          vdpau_errstr(vd, r));
    av_freep(&vctx);
    return 1;
  }

  ctx->hwaccel_context = vctx;

  vdpau_codec_t *vc = calloc(1, sizeof(vdpau_codec_t));
  vc->vc_refcount = 1;
  hts_mutex_init(&vc->vc_surface_cache_mutex);
  vc->vc_vd = vd;
  vc->vc_width = width;
  vc->vc_height = height;
  vc->vc_profile = vdp_profile;

  assert(mc->opaque == NULL);

  mc->opaque = vc;
  mc->close = vdpau_codec_hw_close;

  mc->get_buffer2 = vdpau_get_buffer;

  TRACE(TRACE_DEBUG, "VDPAU",
        "Created accelerated decoder for %s %d x %d profile:%d",
        ctx->codec->name, width, height, ctx->profile);

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

  for(int i = 0; i < 4; i++)
    vm->vm_surfaces[i] = NULL;

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
    TRACE(TRACE_ERROR, "VDPAU", "Unable to query video mixer features");
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

  for(int i = 0; i < 4; i++) {
    if(vm->vm_surfaces[i] != NULL) {
      av_frame_unref(vm->vm_surfaces[i]);
      vm->vm_surfaces[i] = NULL;
    }
  }
}


/**
 *
 */
void
vdpau_mixer_set_deinterlacer(vdpau_mixer_t *vm, int mode)
{
  int best;
  VdpVideoMixerFeature f;
  const char *type;
  VdpVideoMixerFeature features[1];
  VdpBool values[1];
  VdpStatus st;

  if(mode & vm->vm_caps & VDPAU_MIXER_DEINTERLACE_TS) {
    best = VDPAU_MIXER_DEINTERLACE_TS;
    f = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL_SPATIAL;
    type = "Temporal/Spatial";
  } else if(mode & vm->vm_caps & VDPAU_MIXER_DEINTERLACE_T) {
    best = VDPAU_MIXER_DEINTERLACE_T;
    f = VDP_VIDEO_MIXER_FEATURE_DEINTERLACE_TEMPORAL;
    type = "Temporal";
  } else
    return;

  if(mode) {
    if(vm->vm_enabled & best)
      return;
    vm->vm_enabled |= best;
  } else {
    if(!(vm->vm_enabled & best))
      return;
    vm->vm_enabled &= ~best;
  }


  features[0] = f;

  values[0]= !!(mode > 0);

  st = vm->vm_vd->vdp_video_mixer_set_feature_enables(vm->vm_mixer,
						      1, features, values);
  if(st != VDP_STATUS_OK) {
    TRACE(TRACE_ERROR, "VDPAU", "Unable to %s %s deinterlacer",
	  mode ? "enable" : "disable", type);
  } else {
    TRACE(TRACE_DEBUG, "VDPAU", "%s %s deinterlacer",
	  mode ? "Enabled" : "Disabled", type);
	  
	  
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

  switch(fi->fi_color_space) {
  case COLOR_SPACE_BT_709:
    cs = VDP_COLOR_STANDARD_ITUR_BT_709;
    break;

  case COLOR_SPACE_BT_601:
    cs = VDP_COLOR_STANDARD_ITUR_BT_601;
    break;

  case COLOR_SPACE_SMPTE_240M:
    cs = VDP_COLOR_STANDARD_SMPTE_240M;
    break;

  default:
    cs = fi->fi_height < 720 ? VDP_COLOR_STANDARD_ITUR_BT_601 :
    VDP_COLOR_STANDARD_ITUR_BT_709;
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

