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
#include "config.h"

#if ENABLE_VDPAU

#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>
#include <time.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include "showtime.h"
#include "glw_video_common.h"

#include "GL/glx.h"
#include "video/video_settings.h"


typedef struct glw_video_vdpau {
  vdpau_mixer_t gvv_vm;
} glw_video_vdpau_t;

/**
 * GL_NV_vdpau_interop is documented here:
 *
 * ftp://download.nvidia.com/XFree86/vdpau/GL_NV_vdpau_interop.txt
 */



static void
drain(glw_video_t *gv, struct glw_video_surface_queue *q)
{
  glw_video_surface_t *s;
  while((s = TAILQ_FIRST(q)) != NULL) {
    TAILQ_REMOVE(q, s, gvs_link);
    TAILQ_INSERT_TAIL(&gv->gv_avail_queue, s, gvs_link);
  }
}



/**
 *
 */
static void
gv_surface_pixmap_release(glw_video_t *gv, glw_video_surface_t *gvs,
			  struct glw_video_surface_queue *fromqueue)
{
  glw_root_t *gr = gv->w.glw_root;
  glw_backend_root_t *gbr = &gr->gr_be;

  assert(gvs != gv->gv_sa);
  assert(gvs != gv->gv_sb);

  TAILQ_REMOVE(fromqueue, gvs, gvs_link);

  if(gvs->gvs_mapped) {
    gvs->gvs_mapped = 0;
    gbr->gbr_glVDPAUUnmapSurfacesNV(1, &gvs->gvs_gl_surface);
  }

  TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvs, gvs_link);
  hts_cond_signal(&gv->gv_avail_queue_cond);
}


/**
 *
 */
static void
upload_texture(glw_video_t *gv, glw_video_surface_t *gvs)
{
  glw_root_t *gr = gv->w.glw_root;
  glw_backend_root_t *gbr = &gr->gr_be;

  if(gvs->gvs_mapped)
    return;

  gvs->gvs_mapped = 1;
  gbr->gbr_glVDPAUMapSurfacesNV(1, &gvs->gvs_gl_surface);
}




/**
 *
 */
static int
surface_init(glw_video_t *gv, glw_video_surface_t *gvs)
{
  VdpStatus r;
  glw_root_t *gr = gv->w.glw_root;
  glw_backend_root_t *gbr = &gr->gr_be;
  vdpau_dev_t *vd = gbr->gbr_vdpau_dev;


  r = vd->vdp_output_surface_create(vd->vd_dev, VDP_RGBA_FORMAT_B8G8R8A8,
                                    gvs->gvs_width[0],
                                    gvs->gvs_height[0],
				    &gvs->gvs_vdpau_surface);

  if(r != VDP_STATUS_OK)
    return -1;

  glGenTextures(1, &gvs->gvs_texture);

  gvs->gvs_gl_surface =
    gbr->gbr_glVDPAURegisterOutputSurfaceNV((void *)(uintptr_t)
                                            gvs->gvs_vdpau_surface,
                                            GL_TEXTURE_2D, 1,
                                            &gvs->gvs_texture);

  TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvs, gvs_link);
  hts_cond_signal(&gv->gv_avail_queue_cond);
  return 0;
}


/**
 *
 */
static int64_t
gvv_newframe(glw_video_t *gv, video_decoder_t *vd0, int flags)
{
  video_decoder_t *vd = gv->gv_vd;
  media_pipe_t *mp = gv->gv_mp;

  gv->gv_cmatrix_cur[0] = (gv->gv_cmatrix_cur[0] * 3.0f +
			   gv->gv_cmatrix_tgt[0]) / 4.0f;

  if(flags & GLW_REINITIALIZE_VDPAU) {

    int i;
    for(i = 0; i < GLW_VIDEO_MAX_SURFACES; i++)
      gv->gv_surfaces[i].gvs_vdpau_surface = VDP_INVALID_HANDLE;

    gv->gv_engine = NULL;

    mp_send_cmd(mp, &mp->mp_video, MB_CTRL_REINITIALIZE);

    drain(gv, &gv->gv_displaying_queue);
    drain(gv, &gv->gv_decoded_queue);
    hts_cond_signal(&gv->gv_avail_queue_cond);

    return AV_NOPTS_VALUE;
  }

  glw_video_surface_t *gvs;

  while((gvs = TAILQ_FIRST(&gv->gv_parked_queue)) != NULL) {
    TAILQ_REMOVE(&gv->gv_parked_queue, gvs, gvs_link);
    surface_init(gv, gvs);
  }

  glw_need_refresh(gv->w.glw_root, 0);
  return glw_video_newframe_blend(gv, vd, flags, &gv_surface_pixmap_release, 1);
}



static const float projection[16] = {
  2.414213,0.000000,0.000000,0.000000,
  0.000000,2.414213,0.000000,0.000000,
  0.000000,0.000000,1.033898,-1.000000,
  0.000000,0.000000,2.033898,0.000000
};


/**
 *
 */
static void
gvv_render(glw_video_t *gv, glw_rctx_t *rc)
{
  glw_video_surface_t *sa = gv->gv_sa;
  glw_video_surface_t *sb = gv->gv_sa;

  if(sa == NULL)
    return;

  gv->gv_width  = sa->gvs_width[0];
  gv->gv_height = sa->gvs_height[0];

  upload_texture(gv, sa);
  if(sb != NULL)
    upload_texture(gv, sb);

  if(rc->rc_alpha > 0.98f)
    glDisable(GL_BLEND);
  else
    glEnable(GL_BLEND);

  glw_backend_root_t *gbr = &gv->w.glw_root->gr_be;
  glw_program_t *gp;

  if(sb != NULL) {
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, sb->gvs_texture);
    glActiveTexture(GL_TEXTURE0);
    gp = gbr->gbr_rgb2rgb_2f;
  } else {
    gp = gbr->gbr_rgb2rgb_1f;
  }
  glBindTexture(GL_TEXTURE_2D, sa->gvs_texture);

  glw_render_video_quad(0, 0, sa->gvs_width[0], sa->gvs_height[0],
                        0, 0, gbr, gp, gv, rc);

  glEnable(GL_BLEND);
}



typedef struct reap_task {
  glw_video_reap_task_t hdr;

  GLvdpauSurfaceNV gl_surface;
  VdpOutputSurface vdpau_surface;

} reap_task_t;


/**
 *
 */
static void
do_reap(glw_video_t *gv, reap_task_t *t)
{
  glw_root_t *gr = gv->w.glw_root;
  glw_backend_root_t *gbr = &gr->gr_be;
  vdpau_dev_t *vd = gbr->gbr_vdpau_dev;

  gbr->gbr_glVDPAUUnregisterSurfaceNV(t->gl_surface);
  vd->vdp_output_surface_destroy(t->vdpau_surface);

}


/**
 *
 */
static void
surface_reset(vdpau_dev_t *vd, glw_video_t *gv, glw_video_surface_t *gvs)
{
  reap_task_t *t = glw_video_add_reap_task(gv, sizeof(reap_task_t), do_reap);
  t->gl_surface    = gvs->gvs_gl_surface;
  t->vdpau_surface = gvs->gvs_vdpau_surface;
}


/**
 *
 */
static void
gvv_reset(glw_video_t *gv)
{
  vdpau_dev_t *vd = gv->w.glw_root->gr_be.gbr_vdpau_dev;
  int i;
  glw_video_vdpau_t *gvv = gv->gv_aux;

  for(i = 0; i < GLW_VIDEO_MAX_SURFACES; i++)
    surface_reset(vd, gv, &gv->gv_surfaces[i]);

  vdpau_mixer_deinit(&gvv->gvv_vm);
}


/**
 *
 */
static int
gvv_init(glw_video_t *gv)
{
  int i;

  for(i = 0; i < GLW_VIDEO_MAX_SURFACES; i++)
    gv->gv_surfaces[i].gvs_vdpau_surface = VDP_INVALID_HANDLE;

  /* Surfaces */
  for(i = 0; i < 4; i++) {
    glw_video_surface_t *gvs = &gv->gv_surfaces[i];
    TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvs, gvs_link);
  }

  glw_video_vdpau_t *gvv = calloc(1, sizeof(glw_video_vdpau_t));

  gv->gv_aux = gvv;

  gvv->gvv_vm.vm_mixer = VDP_INVALID_HANDLE;

  return 0;
}


/**
 *
 */
static void
gvv_blackout(glw_video_t *gv)
{
  gv->gv_cmatrix_tgt[0] = 0.0f;
}



/**
 *
 */
static VdpVideoSurface
frame_to_surface(AVFrame *frame)
{
  if(frame == NULL)
    return VDP_INVALID_HANDLE;
  return (uintptr_t)(void *)frame->data[0];
}


/**
 *
 */
static int
gvv_deliver(const frame_info_t *fi, glw_video_t *gv, glw_video_engine_t *gve)
{
  vdpau_dev_t *vd = gv->w.glw_root->gr_be.gbr_vdpau_dev;
  glw_video_surface_t *s;
  VdpRect src_rect = { 0, 0, fi->fi_width, fi->fi_height };

  int hvec[3], wvec[3];

  wvec[0] = fi->fi_width;
  wvec[1] = 0;
  wvec[2] = 0;
  hvec[0] = fi->fi_height;
  hvec[1] = 0;
  hvec[2] = 0;

  VdpRect dst_rect = { 0, 0,
		       fi->fi_width,
		       fi->fi_height };

  if(glw_video_configure(gv, gve))
    return -1;

  glw_video_vdpau_t *gvv = gv->gv_aux;

  vdpau_mixer_t *vm = &gvv->gvv_vm;

  gv->gv_cmatrix_tgt[0] = 1.0f;

  /* Video mixer */
  if(vm->vm_width  != fi->fi_width || vm->vm_height != fi->fi_height) {

    VdpStatus st;

    vdpau_mixer_deinit(vm);

    st = vdpau_mixer_create(vd, vm, fi->fi_width, fi->fi_height);
    if(st != VDP_STATUS_OK) {
      TRACE(TRACE_ERROR, "VDPAU", "Unable to create video mixer");
      return -1;
    }
  }

  if((s = glw_video_get_surface(gv, wvec, hvec)) == NULL)
    return -1;

  s->gvs_width[0]  = fi->fi_width;
  s->gvs_height[0] = fi->fi_height;

  vdpau_mixer_set_color_matrix(vm, fi);

  if(fi->fi_interlaced) {
    int duration = fi->fi_duration >> 1;

    if(video_settings.vdpau_deinterlace_resolution_limit > 0 && 
       fi->fi_height > video_settings.vdpau_deinterlace_resolution_limit)
      vdpau_mixer_set_deinterlacer(vm, 0);
    else
      vdpau_mixer_set_deinterlacer(vm, video_settings.vdpau_deinterlace);

    if(vm->vm_surfaces[3] != NULL)
      av_frame_unref(vm->vm_surfaces[3]);
    vm->vm_surfaces[3] = vm->vm_surfaces[2];
    vm->vm_surfaces[2] = vm->vm_surfaces[1];
    vm->vm_surfaces[1] = vm->vm_surfaces[0];
    vm->vm_surfaces[0] = av_frame_clone(fi->fi_avframe);

    VdpVideoSurface past[2], present, future;

    past[1] = frame_to_surface(vm->vm_surfaces[3]);
    past[0] = frame_to_surface(vm->vm_surfaces[2]);
    present = frame_to_surface(vm->vm_surfaces[1]);
    future  = frame_to_surface(vm->vm_surfaces[0]);

    vd->vdp_video_mixer_render(vm->vm_mixer, VDP_INVALID_HANDLE, NULL,
			       !fi->fi_tff,
			       2, past,
                               present,
			       1, &future,
			       &src_rect,
			       s->gvs_vdpau_surface,
			       &dst_rect, &dst_rect,
			       0, NULL);

    glw_video_put_surface(gv, s, fi->fi_pts - duration,
			  fi->fi_epoch, duration, 0, 0);

    if((s = glw_video_get_surface(gv, wvec, hvec)) == NULL)
      return -1;

    s->gvs_width[0] = fi->fi_width;
    s->gvs_height[0] = fi->fi_height;

    vd->vdp_video_mixer_render(vm->vm_mixer, VDP_INVALID_HANDLE, NULL,
			       fi->fi_tff,
			       2, past,
                               present,
			       1, &future,
			       &src_rect, s->gvs_vdpau_surface,
			       &dst_rect, &dst_rect, 0, NULL);

  } else {
    vdpau_mixer_set_deinterlacer(vm, 0);

    VdpVideoSurface surface = frame_to_surface(fi->fi_avframe);

    vd->vdp_video_mixer_render(vm->vm_mixer, VDP_INVALID_HANDLE, NULL,
                               VDP_VIDEO_MIXER_PICTURE_STRUCTURE_FRAME,
                               0, NULL, surface, 0, NULL,
                               &src_rect, s->gvs_vdpau_surface,
                               &dst_rect, &dst_rect, 0, NULL);
  }

  glw_video_put_surface(gv, s, fi->fi_pts, fi->fi_epoch, fi->fi_duration, 0, 0);
  return 0;
}


/**
 *
 */
static glw_video_engine_t glw_video_vdpau = {
  .gve_type = 'VDPA',
  .gve_newframe = gvv_newframe,
  .gve_render   = gvv_render,
  .gve_reset    = gvv_reset,
  .gve_init     = gvv_init,
  .gve_deliver  = gvv_deliver,
  .gve_blackout = gvv_blackout,
  .gve_init_on_ui_thread = 1,
};

GLW_REGISTER_GVE(glw_video_vdpau);

#endif
