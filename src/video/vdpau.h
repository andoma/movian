/**
 *  X11 common code
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

#ifndef VDPAU_H__
#define VDPAU_H__

#include <vdpau/vdpau.h>
#include <vdpau/vdpau_x11.h>

#include <GL/glx.h>

#include <libavcodec/avcodec.h>
#include <libavcodec/vdpau.h>

#include "media.h"

TAILQ_HEAD(vdpau_video_surface_queue, vdpau_video_surface);
TAILQ_HEAD(vdpau_output_surface_queue, vdpau_output_surface);

struct frame_info;

typedef struct vdpau_dev {
  VdpDevice vd_dev;

  Display *vd_dpy;
  int vd_screen;

  void (*vd_preempted)(void *opaque);
  void *vd_opaque;

  hts_mutex_t vd_mutex;

  PFNGLXBINDTEXIMAGEEXTPROC vd_glXBindTexImage;
  
  VdpPreemptionCallbackRegister *vdp_preemption_callback_register;

  VdpGetErrorString *vdp_get_error_string;
  VdpVideoSurfaceCreate *vdp_video_surface_create;
  VdpVideoSurfaceDestroy *vdp_video_surface_destroy;

  VdpOutputSurfaceCreate *vdp_output_surface_create;
  VdpOutputSurfaceDestroy *vdp_output_surface_destroy;

  VdpDecoderCreate *vdp_decoder_create;
  VdpDecoderDestroy *vdp_decoder_destroy;
  VdpDecoderRender *vdp_decoder_render;
  VdpDecoderQueryCapabilities *vdp_decoder_query_caps;

  VdpPresentationQueueCreate *vdp_presentation_queue_create;
  VdpPresentationQueueDestroy *vdp_presentation_queue_destroy;
  VdpPresentationQueueGetTime *vdp_presentation_queue_get_time;
  VdpPresentationQueueTargetCreateX11 *vdp_presentation_queue_target_create_x11;
  VdpPresentationQueueTargetDestroy *vdp_presentation_queue_target_destroy;
  VdpPresentationQueueQuerySurfaceStatus  *vdp_presentation_queue_query_surface_status;
  VdpPresentationQueueDisplay *vdp_presentation_queue_display;
  VdpPresentationQueueBlockUntilSurfaceIdle *vdp_presentation_queue_block_until_surface_idle;

  VdpVideoMixerCreate *vdp_video_mixer_create;
  VdpVideoMixerDestroy *vdp_video_mixer_destroy;
  VdpVideoMixerRender *vdp_video_mixer_render;
  VdpVideoMixerSetFeatureEnables *vdp_video_mixer_set_feature_enables;
  VdpVideoMixerGetFeatureEnables *vdp_video_mixer_get_feature_enables;
  VdpVideoMixerGetFeatureSupport *vdp_video_mixer_get_feature_support;
  VdpVideoMixerSetAttributeValues *vdp_video_mixer_set_attribute_values;
  VdpVideoMixerQueryFeatureSupport *vdp_video_mixer_query_feature_support;
  VdpGenerateCSCMatrix *vdp_generate_csc_matrix;



} vdpau_dev_t;


/**
 *
 */
typedef struct vdpau_video_surface {
  TAILQ_ENTRY(vdpau_video_surface) vvs_link;

  struct vdpau_render_state vvs_rs;
  VdpVideoSurface vvs_surface;

  int vvs_idx;
  struct media_buf vvs_mb;
} vdpau_video_surface_t;


/**
 *
 */
typedef struct vdpau_codec {
  vdpau_dev_t *vc_vd;

  VdpDecoder vc_decoder;

  struct vdpau_video_surface_queue vc_vvs_alloc;
  struct vdpau_video_surface_queue vc_vvs_free;

  struct media_buf *vc_mb;

  int vc_b_age;
  int vc_age[2];

  int vc_width;
  int vc_height;
  int vc_refframes;

  VdpDecoderProfile vc_profile;

} vdpau_codec_t;

/**
 *
 */
typedef struct vdpau_mixer {
  vdpau_dev_t *vm_vd;

  VdpVideoMixer vm_mixer;
  VdpVideoSurface vm_surface_win[4];

  int vm_width;
  int vm_height;

  int vm_caps;
  int vm_enabled;

#define VDPAU_MIXER_DEINTERLACE_T   0x1
#define VDPAU_MIXER_DEINTERLACE_TS  0x2

  int vm_color_space;

} vdpau_mixer_t;

vdpau_dev_t *vdpau_init_x11(Display *dpy, int screen,
			    void (*preempted)(void *opaque), void *opaque);

VdpStatus vdpau_reinit_x11(vdpau_dev_t *vd);

int vdpau_get_buffer(struct AVCodecContext *c, AVFrame *pic);

void vdpau_release_buffer(struct AVCodecContext *c, AVFrame *pic);

void vdpau_draw_horiz_band(struct AVCodecContext *context, 
			   const AVFrame *frame, 
			   int offset[4], int y, int type, int h);

int vdpau_codec_create(media_codec_t *cw, enum CodecID id,
		       AVCodecContext *ctx, media_codec_params_t *mcp,
		       media_pipe_t *mp);

const char *vdpau_errstr(vdpau_dev_t *vd, VdpStatus st);

int vdpau_mixer_feat(vdpau_dev_t *vd, VdpVideoMixerFeature f);


int vdpau_mixer_create(vdpau_dev_t *vd, vdpau_mixer_t *vm,
		       int width, int height);

void vdpau_mixer_deinit(vdpau_mixer_t *vm);

void vdpau_mixer_set_deinterlacer(vdpau_mixer_t *vm, int on);

void vdpau_mixer_set_color_matrix(vdpau_mixer_t *vm, 
				  const struct frame_info *fi);

#endif // VDPAU_H__
