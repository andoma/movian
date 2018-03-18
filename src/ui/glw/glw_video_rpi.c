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
#include "config.h"

#include <malloc.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include <interface/vmcs_host/vc_dispmanx.h>

#include "main.h"
#include "glw_video_common.h"

#include "video/video_settings.h"
#include "arch/rpi/omx.h"
#include "arch/rpi/rpi_video.h"


typedef struct omx_video_display {
  omx_component_t *ovd_vrender;
  omx_component_t *ovd_vsched;
  omx_component_t *ovd_imgfx;

  omx_tunnel_t *ovd_tun_clock_vsched;

  // Pipeline order is  decoder -> (imgfx) -> vsched -> render
  omx_tunnel_t *ovd_tun_vdecoder_output;
  omx_tunnel_t *ovd_tun_imgfx_output;

  omx_tunnel_t *ovd_tun_vsched_vrender;

  int ovd_reconfigure;
  int64_t ovd_pts;
  int64_t ovd_last_pts;

  int ovd_estimated_duration;

  glw_video_t *ovd_gv;

  media_codec_t *ovd_mc; // Current media codec

  glw_rect_t ovd_pos;
  float ovd_alpha;
  hts_mutex_t ovd_mutex;

} omx_video_display_t;


/**
 *
 */
static int
ovd_init(glw_video_t *gv)
{
  omx_video_display_t *ovd = calloc(1, sizeof(omx_video_display_t));
  ovd->ovd_pts = PTS_UNSET;
  ovd->ovd_alpha = -1000;
  ovd->ovd_gv = gv;
  gv->gv_aux = ovd;
  hts_mutex_init(&ovd->ovd_mutex);
  return 0;
}


/**
 *
 */
static int64_t
ovd_newframe(glw_video_t *gv, video_decoder_t *vd, int flags)
{
  omx_video_display_t *ovd = gv->gv_aux;

  if(ovd->ovd_vsched && ovd->ovd_reconfigure) {
    ovd->ovd_reconfigure = 0;

    if(ovd->ovd_tun_vsched_vrender)
      omx_tunnel_destroy(ovd->ovd_tun_vsched_vrender);
    
    ovd->ovd_tun_vsched_vrender =
      omx_tunnel_create(ovd->ovd_vsched, 11, ovd->ovd_vrender, 90,
			"vsched -> vrender");

    omx_set_state(ovd->ovd_vrender, OMX_StateExecuting);


    OMX_CONFIG_DISPLAYREGIONTYPE dr;
    OMX_INIT_STRUCTURE(dr);
    dr.nPortIndex = 90;
    dr.set = OMX_DISPLAY_SET_LAYER;
    dr.layer = 3;
    omxchk(OMX_SetConfig(ovd->ovd_vrender->oc_handle,
			 OMX_IndexConfigDisplayRegion, &dr));
  }
  return ovd->ovd_pts;
}


/**
 *
 */
static void
buffer_mark(omx_component_t *oc, void *ptr)
{
  if(ptr == NULL)
    return;

  omx_video_display_t *ovd = oc->oc_opaque;
  glw_video_t *gv = ovd->ovd_gv;
  media_pipe_t *mp = gv->gv_mp;
  video_decoder_t *vd = gv->gv_vd;
  media_buf_meta_t *mbm = ptr;

  ovd->ovd_last_pts = ovd->ovd_pts;

  ovd->ovd_pts = mbm->mbm_pts;

  if(mbm->mbm_duration == 0) {
    if(ovd->ovd_last_pts != PTS_UNSET && ovd->ovd_pts != PTS_UNSET)
      ovd->ovd_estimated_duration = ovd->ovd_pts - ovd->ovd_last_pts;
    mbm->mbm_duration = ovd->ovd_estimated_duration;
  }

  hts_mutex_lock(&mp->mp_mutex);
  vd->vd_reorder_current = mbm;
  hts_cond_signal(&mp->mp_video.mq_avail);
  hts_mutex_unlock(&mp->mp_mutex);
}

/**
 *
 */
static void
vsched_port_settings_changed(omx_component_t *oc)
{
  omx_video_display_t *ovd = oc->oc_opaque;
  ovd->ovd_reconfigure = 1;
}


/**
 *
 */
static void
ovd_reset(glw_video_t *gv)
{
  omx_video_display_t *ovd = gv->gv_aux;

  omx_tunnel_destroy(ovd->ovd_tun_clock_vsched);

  omx_flush_port(ovd->ovd_vsched, 10);
  omx_flush_port(ovd->ovd_vsched, 11);

  omx_flush_port(ovd->ovd_vrender, 90);

  if(ovd->ovd_tun_vsched_vrender != NULL)
    omx_tunnel_destroy(ovd->ovd_tun_vsched_vrender);

  if(ovd->ovd_tun_imgfx_output != NULL)
    omx_tunnel_destroy(ovd->ovd_tun_imgfx_output);

  if(ovd->ovd_tun_vdecoder_output != NULL)
    omx_tunnel_destroy(ovd->ovd_tun_vdecoder_output);

  omx_set_state(ovd->ovd_vrender,  OMX_StateIdle);
  omx_set_state(ovd->ovd_vsched,   OMX_StateIdle);
  if(ovd->ovd_imgfx)
    omx_set_state(ovd->ovd_imgfx,  OMX_StateIdle);

  omx_set_state(ovd->ovd_vrender,  OMX_StateLoaded);
  omx_set_state(ovd->ovd_vsched,   OMX_StateLoaded);
  if(ovd->ovd_imgfx)
    omx_set_state(ovd->ovd_imgfx,  OMX_StateLoaded);

  omx_component_destroy(ovd->ovd_vrender);
  omx_component_destroy(ovd->ovd_vsched);
  if(ovd->ovd_imgfx)
    omx_component_destroy(ovd->ovd_imgfx);

  if(ovd->ovd_mc != NULL)
    media_codec_deref(ovd->ovd_mc);

  hts_mutex_destroy(&ovd->ovd_mutex);
  free(ovd);
}


/**
 *
 */
static void
ovd_render(glw_video_t *gv, glw_rctx_t *rc)
{
  omx_video_display_t *ovd = gv->gv_aux;
  OMX_CONFIG_DISPLAYREGIONTYPE conf;

  if(memcmp(&ovd->ovd_pos, &gv->gv_rect, sizeof(glw_rect_t))) {
    ovd->ovd_pos = gv->gv_rect;

    OMX_INIT_STRUCTURE(conf);
    conf.nPortIndex = 90;

    conf.fullscreen = OMX_FALSE;
    conf.noaspect   = OMX_TRUE;
    conf.set =
      OMX_DISPLAY_SET_DEST_RECT |
      OMX_DISPLAY_SET_FULLSCREEN |
      OMX_DISPLAY_SET_NOASPECT;

    conf.dest_rect.x_offset = ovd->ovd_pos.x1;
    conf.dest_rect.y_offset = ovd->ovd_pos.y1;
    conf.dest_rect.width    = ovd->ovd_pos.x2 - ovd->ovd_pos.x1;
    conf.dest_rect.height   = ovd->ovd_pos.y2 - ovd->ovd_pos.y1;

    omxchk(OMX_SetConfig(ovd->ovd_vrender->oc_handle,
                         OMX_IndexConfigDisplayRegion, &conf));
  }


  if(ovd->ovd_alpha != rc->rc_alpha) {
    ovd->ovd_alpha = rc->rc_alpha;

    OMX_INIT_STRUCTURE(conf);
    conf.nPortIndex = 90;

    conf.alpha = rc->rc_alpha * 255;
    if(conf.alpha < 5)
      conf.alpha = 0;
    conf.set = OMX_DISPLAY_SET_ALPHA;
    omxchk(OMX_SetConfig(ovd->ovd_vrender->oc_handle,
                         OMX_IndexConfigDisplayRegion, &conf));

  }
}


/**
 *
 */
static void
ovd_blackout(glw_video_t *gv)
{
  omx_video_display_t *ovd = gv->gv_aux;
  if(ovd->ovd_imgfx != NULL) {
    omx_flush_port(ovd->ovd_imgfx, 190);
    omx_flush_port(ovd->ovd_imgfx, 191);
  }
  omx_flush_port(ovd->ovd_vsched, 10);
  omx_flush_port(ovd->ovd_vsched, 11);
  omx_flush_port(ovd->ovd_vrender, 90);
}


/**
 *
 */
static int
ovd_set_codec(media_codec_t *mc, glw_video_t *gv, const frame_info_t *fi,
              struct glw_video_engine *gve)
{
  media_pipe_t *mp = gv->gv_mp;

  glw_video_configure(gv, gve);

  gv->gv_width = fi->fi_width;
  gv->gv_height = fi->fi_height;

  omx_video_display_t *ovd = gv->gv_aux;
  rpi_video_codec_t *rvc = mc->opaque;

  if(ovd->ovd_vrender == NULL) {

    ovd->ovd_vrender = omx_component_create("OMX.broadcom.video_render",
					    &ovd->ovd_mutex, NULL);
    ovd->ovd_vsched  = omx_component_create("OMX.broadcom.video_scheduler",
                                            &ovd->ovd_mutex, NULL);

    ovd->ovd_vsched->oc_opaque = ovd;
    ovd->ovd_vrender->oc_opaque = ovd;

    gv->gv_vd->vd_render_component = ovd->ovd_vrender;

    omx_enable_buffer_marks(ovd->ovd_vrender);

    ovd->ovd_tun_clock_vsched =
      omx_tunnel_create(omx_get_clock(mp), 81, ovd->ovd_vsched, 12,
			"clock -> vsched");

    ovd->ovd_vsched->oc_port_settings_changed_cb =
      vsched_port_settings_changed;

    ovd->ovd_vrender->oc_event_mark_cb = buffer_mark;

  }
  omx_set_state(ovd->ovd_vrender, OMX_StateIdle);

  if(ovd->ovd_tun_vdecoder_output != NULL)
    omx_tunnel_destroy(ovd->ovd_tun_vdecoder_output);

  if(ovd->ovd_tun_imgfx_output != NULL)
    omx_tunnel_destroy(ovd->ovd_tun_imgfx_output);

  if(ovd->ovd_mc != NULL)
    media_codec_deref(ovd->ovd_mc);

  ovd->ovd_mc = media_codec_ref(mc);

  if(fi->fi_interlaced) {

    if(ovd->ovd_imgfx == NULL) {
      ovd->ovd_imgfx = omx_component_create("OMX.broadcom.image_fx",
                                            &ovd->ovd_mutex, NULL);
      ovd->ovd_imgfx->oc_opaque = ovd;

      // add extra buffers for Advanced Deinterlace
      OMX_PARAM_U32TYPE extra_buffers;
      OMX_INIT_STRUCTURE(extra_buffers);
      extra_buffers.nU32 = 6;
      extra_buffers.nPortIndex = 130;
      omxchk(OMX_SetParameter(ovd->ovd_imgfx->oc_handle,
                              OMX_IndexParamBrcmExtraBuffers, &extra_buffers));

      OMX_CONFIG_IMAGEFILTERPARAMSTYPE image_filter;
      OMX_INIT_STRUCTURE(image_filter);
      image_filter.nPortIndex = 191;

      image_filter.nNumParams = 4;
      image_filter.nParams[0] = 6;
      image_filter.nParams[1] = 0; // default frame interval
      image_filter.nParams[2] = 0; // half framerate
      image_filter.nParams[3] = 1; // use qpus

      if(0) {
        image_filter.eImageFilter = OMX_ImageFilterDeInterlaceFast;
      } else {
        image_filter.eImageFilter = OMX_ImageFilterDeInterlaceAdvanced;
      }

      omxchk(OMX_SetConfig(ovd->ovd_imgfx->oc_handle,
                           OMX_IndexConfigCommonImageFilterParameters,
                           &image_filter));

    }

    ovd->ovd_tun_vdecoder_output =
      omx_tunnel_create(rvc->rvc_decoder, 131, ovd->ovd_imgfx, 190,
                        "vdecoder -> imgfx");

    ovd->ovd_tun_imgfx_output =
      omx_tunnel_create(ovd->ovd_imgfx, 191, ovd->ovd_vsched, 10,
                        "imgfx -> vsched");

    omx_set_state(ovd->ovd_imgfx,  OMX_StateExecuting);

  } else {

    if(ovd->ovd_imgfx != NULL) {
      omx_component_destroy(ovd->ovd_imgfx);
      ovd->ovd_imgfx = NULL;
    }

    ovd->ovd_tun_vdecoder_output =
      omx_tunnel_create(rvc->rvc_decoder, 131, ovd->ovd_vsched, 10,
                        "vdecoder -> vsched");
  }

  omx_set_state(ovd->ovd_vsched,  OMX_StateExecuting);
  return 0;
}


/**
 * Tunneled OMX
 */
static glw_video_engine_t glw_video_ovd = {
  .gve_type = 'omx',
  .gve_newframe = ovd_newframe,
  .gve_render   = ovd_render,
  .gve_reset    = ovd_reset,
  .gve_init     = ovd_init,
  .gve_set_codec= ovd_set_codec,
  .gve_blackout = ovd_blackout,
};

GLW_REGISTER_GVE(glw_video_ovd);

#define DISPMANX_VIDEO_SURFACES 4

/**
 *
 */
typedef struct dispmanx_video {

  DISPMANX_ELEMENT_HANDLE_T dv_handle;

} dispmanx_video_t;


/**
 *
 */
static void
dispmanx_yuvp_surface_reset(glw_video_t *gv, glw_video_surface_t *gvs)
{
  if(gvs->gvs_id != -1) {
    vc_dispmanx_resource_delete(gvs->gvs_id);
    gvs->gvs_id = -1;
  }
}


/**
 *
 */
static void
make_surfaces_available(glw_video_t *gv)
{
  for(int i = 0; i < DISPMANX_VIDEO_SURFACES; i++) {
    glw_video_surface_t *gvs = &gv->gv_surfaces[i];
    TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvs, gvs_link);
    gvs->gvs_id = -1;
  }
}


/**
 *
 */
static int
dispmanx_yuvp_init(glw_video_t *gv)
{
  make_surfaces_available(gv);

  dispmanx_video_t *dv = calloc(1, sizeof(dispmanx_video_t));
  gv->gv_aux = dv;
  dv->dv_handle = -1;

  return 0;
}

extern DISPMANX_DISPLAY_HANDLE_T dispman_display;

static void
copy_from_displaying(glw_video_t *gv)
{
  glw_video_surface_t *s;
  while((s = TAILQ_FIRST(&gv->gv_displaying_queue)) != NULL) {
    vc_dispmanx_resource_delete(s->gvs_id);
    s->gvs_id = -1;
    TAILQ_REMOVE(&gv->gv_displaying_queue, s, gvs_link);
    TAILQ_INSERT_TAIL(&gv->gv_avail_queue, s, gvs_link);
    hts_cond_signal(&gv->gv_avail_queue_cond);
  }
}


/**
 *
 */
static int64_t
dispmanx_yuvp_newframe(glw_video_t *gv, video_decoder_t *vd0, int flags)
{
  glw_root_t *gr = gv->w.glw_root;
  int64_t pts = AV_NOPTS_VALUE;
  glw_video_surface_t *s;
  dispmanx_video_t *dv = gv->gv_aux;
  media_pipe_t *mp = gv->gv_mp;

  DISPMANX_UPDATE_HANDLE_T update;
  VC_RECT_T src_rect;
  VC_RECT_T dst_rect;
  VC_DISPMANX_ALPHA_T alpha = {DISPMANX_FLAGS_ALPHA_FIXED_ALL_PIXELS, 255, 0};


  copy_from_displaying(gv);

  int64_t aclock;
  int aepoch;
  hts_mutex_lock(&mp->mp_clock_mutex);
  aclock = mp->mp_audio_clock + gr->gr_frame_start_avtime - 
    mp->mp_audio_clock_avtime + mp->mp_avdelta;

  const int aclock_valid = !!mp->mp_audio_clock_epoch;

  aepoch = mp->mp_audio_clock_epoch;

  hts_mutex_unlock(&mp->mp_clock_mutex);

  while((s = TAILQ_FIRST(&gv->gv_decoded_queue)) != NULL) {
    int64_t delta = gr->gr_frameduration * 2;
    int64_t d;
    pts = s->gvs_pts;
    int epoch = s->gvs_epoch;

    d = s->gvs_pts - aclock;

    if(gv->gv_nextpts_epoch == epoch &&
       (pts == AV_NOPTS_VALUE || d < -5000000LL || d > 5000000LL)) {
      pts = gv->gv_nextpts;
    }

    if(pts != AV_NOPTS_VALUE && (pts - delta) >= aclock && aclock_valid) {

      if(gconf.enable_detailed_avdiff)
        TRACE(TRACE_DEBUG, "AVDIFF",
              "%s: Not sending frame %d:%lld %d:%lld diff:%lld\n",
              mp->mp_name,
              s->gvs_epoch,
              (pts - delta), aepoch, aclock,
              (pts - delta) - aclock);

      break;
    } else {
      if(gconf.enable_detailed_avdiff)
        TRACE(TRACE_DEBUG, "AVDIFF",
              "%s:     Sending frame %d:%lld %d:%lld\n",
             mp->mp_name, s->gvs_epoch,
             (pts - delta), aepoch, aclock);
    }


    update = vc_dispmanx_update_start(10);

    vc_dispmanx_rect_set(&src_rect, 0, 0,
                         s->gvs_width[0]  << 16,
                         s->gvs_height[0] << 16 );

    vc_dispmanx_rect_set(&dst_rect,
                         gv->gv_rect.x1,
                         gv->gv_rect.y1,
                         gv->gv_rect.x2 - gv->gv_rect.x1,
                         gv->gv_rect.y2 - gv->gv_rect.y1);

    if(dv->dv_handle != -1)
      vc_dispmanx_element_remove(update, dv->dv_handle);

    dv->dv_handle =
      vc_dispmanx_element_add(update,
                              dispman_display,
                              4,
                              &dst_rect,
                              s->gvs_id,
                              &src_rect,
                              DISPMANX_PROTECTION_NONE,
                              &alpha,
                              NULL,
                              VC_IMAGE_ROT0 );

    vc_dispmanx_update_submit_sync(update);

    TAILQ_REMOVE(&gv->gv_decoded_queue, s, gvs_link);
    TAILQ_INSERT_TAIL(&gv->gv_displaying_queue, s, gvs_link);
  }

  return pts;
}


/**
 *
 */
static void
dispmanx_yuvp_render(glw_video_t *gv, glw_rctx_t *rc)
{
}



/**
 *
 */
static void
dispmanx_yuvp_reset(glw_video_t *gv)
{
  dispmanx_video_t *dv = gv->gv_aux;

  for(int i = 0; i < DISPMANX_VIDEO_SURFACES; i++) {
    glw_video_surface_t *gvs = &gv->gv_surfaces[i];
    dispmanx_yuvp_surface_reset(gv, gvs);
  }

  DISPMANX_UPDATE_HANDLE_T update;

  update = vc_dispmanx_update_start(10);

  if(dv->dv_handle != -1)
    vc_dispmanx_element_remove(update, dv->dv_handle);

  vc_dispmanx_update_submit_sync(update);

  free(dv);
}




static int
dispmanx_yuvp_deliver(const frame_info_t *fi, glw_video_t *gv,
                     glw_video_engine_t *gve)
{
  glw_video_surface_t *gvs;
  if(glw_video_configure(gv, gve))
    return -1;
  if((gvs = glw_video_get_surface(gv, NULL, NULL)) == NULL)
    return -1;

  VC_RECT_T       dst_rect;

  int pitch = fi->fi_pitch[0];

  const int aligned_height = (fi->fi_height + 15) & ~15;

  gvs->gvs_width[0]  = fi->fi_width;
  gvs->gvs_height[0] = fi->fi_height;

  vc_dispmanx_rect_set(&dst_rect,
                       0, 0, gvs->gvs_width[0],
                       (3*aligned_height)/2);

  uint32_t image_ptr; // This is not used inside the API used AFAIK
  assert(gvs->gvs_id == -1);
  int lumasize   = pitch * aligned_height;
  int chromasize = pitch * aligned_height / 4;

  int bufsize = lumasize + chromasize * 2;

  void *tmp = memalign(32, bufsize);
  void *p1 = tmp;
  void *p2 = tmp + lumasize;
  void *p3 = p2 + chromasize;

  memcpy(p1, fi->fi_data[0], fi->fi_pitch[0] * fi->fi_height);
  memcpy(p2, fi->fi_data[1], fi->fi_pitch[1] * fi->fi_height / 2);
  memcpy(p3, fi->fi_data[2], fi->fi_pitch[2] * fi->fi_height / 2);

  gvs->gvs_id = vc_dispmanx_resource_create(VC_IMAGE_YUV420,
                                            gvs->gvs_width[0],
                                            aligned_height,
                                            &image_ptr);

  vc_dispmanx_resource_write_data(gvs->gvs_id,
                                  VC_IMAGE_YUV420,
                                  fi->fi_pitch[0],
                                  tmp,
                                  &dst_rect);
  free(tmp);
  glw_video_put_surface(gv, gvs, fi->fi_pts, fi->fi_epoch,
                        fi->fi_duration, 0, 0);
  return 0;
}


/**
 *
 */
static glw_video_engine_t glw_video_dispmanx = {
  .gve_type              = 'YUVP',
  .gve_newframe          = dispmanx_yuvp_newframe,
  .gve_render            = dispmanx_yuvp_render,
  .gve_reset             = dispmanx_yuvp_reset,
  .gve_init              = dispmanx_yuvp_init,
  .gve_deliver           = dispmanx_yuvp_deliver,
};

GLW_REGISTER_GVE(glw_video_dispmanx);

