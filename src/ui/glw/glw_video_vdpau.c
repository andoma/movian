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

/*
 * Replace the pixbuf magic with:
 * ftp://download.nvidia.com/XFree86/vdpau/GL_NV_vdpau_interop.txt
 *
 */

#define VDPAU_PIXMAP_WIDTH  1920
#define VDPAU_PIXMAP_HEIGHT 1080


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
static int64_t
vdpau_newframe(glw_video_t *gv, video_decoder_t *vd0, int flags)
{
  glw_root_t *gr = gv->w.glw_root;
  vdpau_dev_t *vd = gr->gr_be.gbr_vdpau_dev;
  media_pipe_t *mp = gv->gv_mp;
  VdpStatus st;
  glw_video_surface_t *s;
  int64_t pts = AV_NOPTS_VALUE;

  gv->gv_cmatrix_cur[0] = (gv->gv_cmatrix_cur[0] * 3.0f +
			   gv->gv_cmatrix_tgt[0]) / 4.0f;

  if(flags & GLW_REINITIALIZE_VDPAU) {

    int i;
    for(i = 0; i < GLW_VIDEO_MAX_SURFACES; i++)
      gv->gv_surfaces[i].gvs_vdpau_surface = VDP_INVALID_HANDLE;

    gv->gv_vdpau_pq = VDP_INVALID_HANDLE;
    gv->gv_vdpau_pqt = VDP_INVALID_HANDLE;
    
    gv->gv_engine = NULL;
    
    mp_send_cmd(mp, &mp->mp_video, MB_CTRL_REINITIALIZE);

    drain(gv, &gv->gv_displaying_queue);
    drain(gv, &gv->gv_decoded_queue);
    hts_cond_signal(&gv->gv_avail_queue_cond);
    
    return AV_NOPTS_VALUE;
  }

  /* Remove frames from displaying queue if they are idle and push
   * back to the decoder 
   */
  while((s = TAILQ_FIRST(&gv->gv_displaying_queue)) != NULL) {
    VdpPresentationQueueStatus qs;
    VdpTime t;

    gv->gv_vdpau_running = 1;

    st = vd->vdp_presentation_queue_query_surface_status(gv->gv_vdpau_pq,
							 s->gvs_vdpau_surface,
							 &qs, &t);
    if(st != VDP_STATUS_OK || qs == VDP_PRESENTATION_QUEUE_STATUS_IDLE) {
      TAILQ_REMOVE(&gv->gv_displaying_queue, s, gvs_link);
      TAILQ_INSERT_TAIL(&gv->gv_avail_queue, s, gvs_link);
      hts_cond_signal(&gv->gv_avail_queue_cond);
    } else {
      break;
    }
  }


  while((s = TAILQ_FIRST(&gv->gv_decoded_queue)) != NULL) {
    int64_t delta = gr->gr_frameduration * 2;
    int64_t aclock, d;
    int a_epoch;
    pts = s->gvs_pts;

    hts_mutex_lock(&mp->mp_clock_mutex);
    aclock = mp->mp_audio_clock + gr->gr_frame_start - 
      mp->mp_audio_clock_avtime + mp->mp_avdelta;
    a_epoch = mp->mp_audio_clock_epoch;

    hts_mutex_unlock(&mp->mp_clock_mutex);

    d = s->gvs_pts - aclock;

    if(s->gvs_pts == AV_NOPTS_VALUE || d < -5000000LL || d > 5000000LL)
      pts = gv->gv_nextpts;

    if(pts != AV_NOPTS_VALUE && (pts - delta) >= aclock &&
	a_epoch == s->gvs_epoch)
      break;

    st = vd->vdp_presentation_queue_display(gv->gv_vdpau_pq,
					    s->gvs_vdpau_surface, 
					    0, 0, 0);
    if(pts != AV_NOPTS_VALUE)
      gv->gv_nextpts = pts + s->gvs_duration;

    gv->gv_width  = s->gvs_width[0];
    gv->gv_height = s->gvs_height[0];

    TAILQ_REMOVE(&gv->gv_decoded_queue, s, gvs_link);
    TAILQ_INSERT_TAIL(&gv->gv_displaying_queue, s, gvs_link);
  }
  return gv->gv_nextpts;
}


static const float projmtx[16] = {
  2.414213,0.000000,0.000000,0.000000,
  0.000000,2.414213,0.000000,0.000000,
  0.000000,0.000000,1.033898,-1.000000,
  0.000000,0.000000,2.033898,0.000000
};

/**
 *
 */
static void
vdpau_render(glw_video_t *gv, glw_rctx_t *rc)
{
  glw_root_t *gr = gv->w.glw_root;
  vdpau_dev_t *vd = gr->gr_be.gbr_vdpau_dev;
  glw_backend_root_t *gbr = &gv->w.glw_root->gr_be;

  if(!gv->gv_vdpau_running)
    return;

  glDisable(GL_TEXTURE_2D);
  glEnable(GL_TEXTURE_RECTANGLE_ARB);

  if(gv->gv_vdpau_texture == 0) {
    glGenTextures(1, &gv->gv_vdpau_texture);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, gv->gv_vdpau_texture);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  } else {
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, gv->gv_vdpau_texture);
  }

  gr->gr_be.gbr_bind_tex_image(vd->vd_dpy, gv->gv_glx_pixmap,
			       GLX_FRONT_LEFT_EXT, NULL);
  
#if 0
  int w = gv->gv_rwidth  - 1;
  int h = gv->gv_rheight - 1;
#else
  int w = gv->gv_width  - 1;
  int h = gv->gv_height - 1;
#endif

  glMatrixMode(GL_PROJECTION);
  glLoadMatrixf(projmtx);
  glMatrixMode(GL_MODELVIEW);
  glLoadMatrixf(glw_mtx_get(rc->rc_mtx));

  glw_load_program(gbr, NULL);

  float c = gv->gv_cmatrix_cur[0];

  glColor4f(c, c, c, 1);

  glBegin(GL_QUADS);
  glTexCoord2f(0, h);
  glVertex3f(-1.0, -1.0, 0.0);

  glTexCoord2f(w, h);
  glVertex3f( 1.0, -1.0, 0.0);

  glTexCoord2f(w, 0);
  glVertex3f( 1.0,  1.0, 0.0);

  glTexCoord2f(0, 0);
  glVertex3f(-1.0,  1.0, 0.0);
  glEnd();

  gr->gr_be.gbr_release_tex_image(vd->vd_dpy, gv->gv_glx_pixmap,
				  GLX_FRONT_LEFT_EXT);

  glDisable(GL_TEXTURE_RECTANGLE_ARB);
  glEnable(GL_TEXTURE_2D);
}


/**
 *
 */
static void
surface_reset(vdpau_dev_t *vd, glw_video_t *gv, glw_video_surface_t *gvs)
{
  if(gvs->gvs_vdpau_surface != VDP_INVALID_HANDLE)
    vd->vdp_output_surface_destroy(gvs->gvs_vdpau_surface);
  gvs->gvs_vdpau_surface = VDP_INVALID_HANDLE;
}



typedef struct reap_task {
  glw_video_reap_task_t hdr;
  
  GLuint tex;
  Pixmap xpixmap;
  GLXPixmap glx_pixmap;
  

} reap_task_t;


static void
do_reap(glw_video_t *gv, reap_task_t *t)
{
  vdpau_dev_t *vd = gv->w.glw_root->gr_be.gbr_vdpau_dev;

  if(t->tex)
    glDeleteTextures(1, &t->tex);

  glXDestroyPixmap(vd->vd_dpy, t->glx_pixmap);
  XFreePixmap(vd->vd_dpy, t->xpixmap);
}

/**
 *
 */
static void
vdpau_reset(glw_video_t *gv)
{
  vdpau_dev_t *vd = gv->w.glw_root->gr_be.gbr_vdpau_dev;
  int i;
  reap_task_t *t = glw_video_add_reap_task(gv, sizeof(reap_task_t), do_reap);
  
  t->tex = gv->gv_vdpau_texture;
  gv->gv_vdpau_texture = 0;

  for(i = 0; i < GLW_VIDEO_MAX_SURFACES; i++)
    surface_reset(vd, gv, &gv->gv_surfaces[i]);

  vdpau_mixer_deinit(&gv->gv_vm);
  if(gv->gv_vdpau_pq != VDP_INVALID_HANDLE)
    vd->vdp_presentation_queue_destroy(gv->gv_vdpau_pq);
  if(gv->gv_vdpau_pqt != VDP_INVALID_HANDLE)
    vd->vdp_presentation_queue_target_destroy(gv->gv_vdpau_pqt);

  t->glx_pixmap = gv->gv_glx_pixmap;
  t->xpixmap = gv->gv_xpixmap;
  gv->gv_vdpau_running = 0;
  gv->gv_vdpau_clockdiff = 0;
}


/**
 *
 */
static int
surface_init(vdpau_dev_t *vd, glw_video_t *gv, glw_video_surface_t *gvs)
{
  VdpStatus r;

  r = vd->vdp_output_surface_create(vd->vd_dev, VDP_RGBA_FORMAT_B8G8R8A8,
				    VDPAU_PIXMAP_WIDTH, 
				    VDPAU_PIXMAP_HEIGHT, 
				    &gvs->gvs_vdpau_surface);

  if(r != VDP_STATUS_OK)
    return -1;

  TAILQ_INSERT_TAIL(&gv->gv_avail_queue, gvs, gvs_link);
  return 0;
}


static int pixmap_attribs[] = {
  GLX_RENDER_TYPE, 	        GLX_RGBA_BIT,
  GLX_RED_SIZE,		        8,
  GLX_GREEN_SIZE,		8,
  GLX_BLUE_SIZE,		8,
  GLX_ALPHA_SIZE,		8,
  GLX_DOUBLEBUFFER,	        1,
  GLX_BUFFER_SIZE,              32,
  GLX_DEPTH_SIZE,               24,
  GLX_BIND_TO_TEXTURE_RGB_EXT,  True,
  GLX_Y_INVERTED_EXT,           True,
  GLX_X_RENDERABLE,             True,
  None
};

int glx_pixmap_attribs[] = {
  GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_RECTANGLE_EXT,
  GLX_TEXTURE_FORMAT_EXT, GLX_TEXTURE_FORMAT_RGB_EXT,
  None 
};


/**
 *
 */
static int
vdpau_init(glw_video_t *gv)
{
  vdpau_dev_t *vd = gv->w.glw_root->gr_be.gbr_vdpau_dev;
  int i, nconfs;
  VdpStatus st;
  GLXFBConfig *fbconfig;

  for(i = 0; i < GLW_VIDEO_MAX_SURFACES; i++)
    gv->gv_surfaces[i].gvs_vdpau_surface = VDP_INVALID_HANDLE;

  gv->gv_vdpau_pq = VDP_INVALID_HANDLE;
  gv->gv_vdpau_pqt = VDP_INVALID_HANDLE;

  gv->gv_vdpau_initialized = 0;

  /* Pixmap */
  fbconfig = glXChooseFBConfig(vd->vd_dpy, 0, pixmap_attribs, &nconfs);
  if(nconfs < 1) {
    TRACE(TRACE_ERROR, "VDPAU", "Unable to find fbconfig for pixmap");
    return -1;
  }

  XWindowAttributes wndattribs;
  XGetWindowAttributes(vd->vd_dpy, DefaultRootWindow(vd->vd_dpy), &wndattribs);

  gv->gv_xpixmap = XCreatePixmap(vd->vd_dpy, glXGetCurrentDrawable(),
				 VDPAU_PIXMAP_WIDTH, VDPAU_PIXMAP_HEIGHT,
				 wndattribs.depth);

  gv->gv_glx_pixmap = glXCreatePixmap(vd->vd_dpy, *fbconfig, 
				      gv->gv_xpixmap, glx_pixmap_attribs);

  /* Presentation queue */
  st = vd->vdp_presentation_queue_target_create_x11(vd->vd_dev, gv->gv_xpixmap,
						    &gv->gv_vdpau_pqt);

  if(st != VDP_STATUS_OK) {
    TRACE(TRACE_ERROR, "VDPAU", "Unable to create pixmap target");
    return -1;
  }

  st = vd->vdp_presentation_queue_create(vd->vd_dev, gv->gv_vdpau_pqt,
					 &gv->gv_vdpau_pq);

  if(st != VDP_STATUS_OK) {
    TRACE(TRACE_ERROR, "VDPAU", "Unable to create presentation queue");
    return -1;
  }

  gv->gv_vm.vm_mixer = VDP_INVALID_HANDLE;

  /* Surfaces */
  for(i = 0; i < 4; i++) {
    if(surface_init(vd, gv, &gv->gv_surfaces[i])) {
      return -1;
    }
  }
  gv->gv_nextpts = AV_NOPTS_VALUE;
  return 0;
}


/**
 *
 */
static void
vdpau_blackout(glw_video_t *gv)
{
  gv->gv_cmatrix_tgt[0] = 0.0f;
}


static void vdpau_deliver(const frame_info_t *fi, glw_video_t *gv);

/**
 *
 */
static glw_video_engine_t glw_video_vdpau = {
  .gve_type = 'VDPA',
  .gve_newframe = vdpau_newframe,
  .gve_render = vdpau_render,
  .gve_reset = vdpau_reset,
  .gve_init = vdpau_init,
  .gve_deliver = vdpau_deliver,
  .gve_init_on_ui_thread = 1,
  .gve_blackout = vdpau_blackout,
};

GLW_REGISTER_GVE(glw_video_vdpau);

/**
 *
 */
static void
vdpau_deliver(const frame_info_t *fi, glw_video_t *gv)
{
  struct vdpau_render_state *rs = (struct vdpau_render_state *)fi->fi_data[0];
  vdpau_dev_t *vd = gv->w.glw_root->gr_be.gbr_vdpau_dev;
  vdpau_mixer_t *vm = &gv->gv_vm;
  glw_video_surface_t *s;
  VdpRect src_rect = { 0, 0, fi->fi_width, fi->fi_height };

#if 0
  VdpRect dst_rect = { 0, 0, 
		       gv->gv_rwidth ?: fi->fi_width,
		       gv->gv_rheight ?: fi->fi_height };
#else
  VdpRect dst_rect = { 0, 0, 
		       fi->fi_width,
		       fi->fi_height };
#endif

  if(glw_video_configure(gv, &glw_video_vdpau))
    return;

  gv->gv_cmatrix_tgt[0] = 1.0f;

  /* Video mixer */
  if(gv->gv_vm.vm_width  != fi->fi_width ||
     gv->gv_vm.vm_height != fi->fi_height) {
    
    VdpStatus st;

    vdpau_mixer_deinit(&gv->gv_vm);
    
    st = vdpau_mixer_create(vd, &gv->gv_vm, fi->fi_width, fi->fi_height);
    if(st != VDP_STATUS_OK) {
      TRACE(TRACE_ERROR, "VDPAU", "Unable to create video mixer");
      return;
    }
  }

  if((s = glw_video_get_surface(gv, NULL, NULL)) == NULL)
    return;

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

    vm->vm_surface_win[3] = vm->vm_surface_win[2];
    vm->vm_surface_win[2] = vm->vm_surface_win[1];
    vm->vm_surface_win[1] = vm->vm_surface_win[0];
    vm->vm_surface_win[0] = rs->surface;

    vd->vdp_video_mixer_render(vm->vm_mixer, VDP_INVALID_HANDLE, NULL,
			       !fi->fi_tff,
			       2, &vm->vm_surface_win[2],
			       vm->vm_surface_win[1],
			       1, &vm->vm_surface_win[0],
			       &src_rect,
			       s->gvs_vdpau_surface,
			       &dst_rect, &dst_rect,
			       0, NULL);

    glw_video_put_surface(gv, s, fi->fi_pts - duration,
			  fi->fi_epoch, duration, 0, 0);

    if((s = glw_video_get_surface(gv, NULL, NULL)) == NULL)
      return;

    s->gvs_width[0] = fi->fi_width;
    s->gvs_height[0] = fi->fi_height;

    vd->vdp_video_mixer_render(vm->vm_mixer, VDP_INVALID_HANDLE, NULL,
			       fi->fi_tff,
			       2, &vm->vm_surface_win[2],
			       vm->vm_surface_win[1],
			       1, &vm->vm_surface_win[0],
			       &src_rect, s->gvs_vdpau_surface,
			       &dst_rect, &dst_rect, 0, NULL);

  } else {
    vdpau_mixer_set_deinterlacer(vm, 0);

    vd->vdp_video_mixer_render(vm->vm_mixer, VDP_INVALID_HANDLE, NULL,
			       VDP_VIDEO_MIXER_PICTURE_STRUCTURE_FRAME,
			       0, NULL, rs->surface, 0, NULL,
			       &src_rect, s->gvs_vdpau_surface,
			       &dst_rect, &dst_rect, 0, NULL);
  }

  glw_video_put_surface(gv, s, fi->fi_pts, fi->fi_epoch, fi->fi_duration, 0, 0);
}
#endif
