/*
 *  Video decoder
 *  Copyright (C) 2007 - 2010 Andreas Öman
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

#ifndef GLW_VIDEO_COMMON_H
#define GLW_VIDEO_COMMON_H

#include "glw.h"
#include "glw_renderer.h"
#include "media.h"
#include "video/video_playback.h"
#include "video/video_decoder.h"
#include "misc/kalman.h"

#if ENABLE_VDPAU
#include "video/vdpau.h"
#endif

LIST_HEAD(glw_video_overlay_list, glw_video_overlay);
TAILQ_HEAD(glw_video_surface_queue, glw_video_surface);

/**
 *
 */
typedef struct {

  const struct glw_video_engine *gvc_engine;

  int gvc_width[3];
  int gvc_height[3];

  int gvc_nsurfaces;

  int gvc_flags;
#define GVC_YHALF     0x1
#define GVC_CUTBORDER 0x2

  int gvc_valid;

} glw_video_config_t;


/**
 *
 */
typedef struct glw_video_surface {

  TAILQ_ENTRY(glw_video_surface) gvs_link;

  int gvs_duration;
  uint64_t gvs_pts;
  int gvs_epoch;

  void *gvs_data[3];

  int gvs_width;
  int gvs_height;
  int gvs_yshift;

#if CONFIG_GLW_BACKEND_OPENGL
  GLuint gvs_pbo[3];
  void *gvs_pbo_ptr[3];

  int gvs_uploaded;

  GLuint gvs_textures[3];

  unsigned int gvs_frame_buffer;

#endif


#if CONFIG_GLW_BACKEND_GX
  GXTexObj gvs_obj[3];
  void *gvs_mem[3];
  int gvs_size[3];
#endif

#if CONFIG_GLW_BACKEND_RSX
  realityTexture gvs_tex[3];
  int gvs_size[3];
  int gvs_offset[3];   // Position in RSX memory
#endif

#if ENABLE_VDPAU
  VdpVideoSurface gvs_vdpau_surface;
#endif

} glw_video_surface_t;


#define GLW_VIDEO_MAX_SURFACES 6

/**
 *
 */
typedef struct glw_video {

  glw_t w;

  AVRational gv_dar;
  int gv_vheight;

  int gv_rwidth;
  int gv_rheight;

  int gv_fwidth;
  int gv_fheight;

  char *gv_current_url;
  char *gv_pending_url;

  int gv_flags;
  int gv_priority;
  char gv_freezed;

  video_decoder_t *gv_vd;
  media_pipe_t *gv_mp;

  glw_video_surface_t *gv_sa, *gv_sb;
  float gv_blend;

  LIST_ENTRY(glw_video) gv_global_link;

  struct glw_video_overlay_list gv_overlays;

  float gv_cmatrix_cur[16];
  float gv_cmatrix_tgt[16];
  
  glw_video_config_t gv_cfg_cur;
  glw_video_config_t gv_cfg_req;
  hts_cond_t gv_reconf_cond;

  glw_video_surface_t gv_surfaces[GLW_VIDEO_MAX_SURFACES];
  
  hts_mutex_t gv_surface_mutex;

  /**
   * Frames available for decoder
   * Once we push frames here we also notify via gv_avail_queue_cond
   */
  struct glw_video_surface_queue gv_avail_queue;
  hts_cond_t gv_avail_queue_cond;

  /**
   * Frames currently being displayed sits here
   */
  struct glw_video_surface_queue gv_displaying_queue;

  /**
   * Freshly decoded surfaces are enqueued here
   */
  struct glw_video_surface_queue gv_decoded_queue;



  /**
   * VDPAU specifics
   */
#if ENABLE_VDPAU
  int gv_vdpau_initialized;
  int gv_vdpau_running;
  Pixmap gv_xpixmap;
  GLXPixmap gv_glx_pixmap;
  VdpPresentationQueue gv_vdpau_pq;
  VdpPresentationQueueTarget gv_vdpau_pqt;
  GLuint gv_vdpau_texture;
  int64_t gv_vdpau_clockdiff;

  int64_t gv_nextpts;
  vdpau_mixer_t gv_vm;
#endif

  // 
  prop_sub_t *gv_vo_scaling_sub;
  float gv_vo_scaling;

  prop_sub_t *gv_vo_on_video_sub;
  int gv_vo_on_video;

  prop_sub_t *gv_vzoom_sub;
  int gv_vzoom;

} glw_video_t;


/**
 *
 */
typedef struct glw_video_engine {
  const char *gve_name;
  void (*gve_render)(glw_video_t *gv, glw_rctx_t *rc);

  int64_t (*gve_newframe)(glw_video_t *gv, video_decoder_t *vd, int flags);

  void (*gve_reset)(glw_video_t *gv);

  int (*gve_init)(glw_video_t *gv);

} glw_video_engine_t;



int glw_video_compute_output_duration(video_decoder_t *vd, int frame_duration);

void glw_video_compute_avdiff(glw_root_t *gr,
			      video_decoder_t *vd, media_pipe_t *mp, 
			      int64_t pts, int epoch);
void glw_video_render(glw_t *w, glw_rctx_t *rc);

void glw_video_reset(glw_root_t *gr);

/**
 *
 */
static inline void
glw_video_enqueue_for_display(glw_video_t *gv, glw_video_surface_t *gvs,
			      struct glw_video_surface_queue *fromqueue)
{
  TAILQ_REMOVE(fromqueue, gvs, gvs_link);
  TAILQ_INSERT_TAIL(&gv->gv_displaying_queue, gvs, gvs_link);
}


/**
 *
 */

void glw_video_surface_reconfigure(glw_video_t *gv);

void glw_video_surfaces_cleanup(glw_video_t *gv);

glw_video_surface_t *glw_video_get_surface(glw_video_t *gv);

void glw_video_put_surface(glw_video_t *gv, glw_video_surface_t *s,
			   int64_t pts, int epoch, int duration, int yshift);

int glw_video_configure(glw_video_t *gv,
			const glw_video_engine_t *engine,
			const int *wvec, const int *hvec,
			int surfaces, int flags);


/**
 *
 */
void glw_video_input_yuvp(glw_video_t *gv,
			  uint8_t * const data[], const int pitch[],
			  const frame_info_t *fi);


void glw_video_input_vdpau(glw_video_t *gv,
			   uint8_t * const data[], const int pitch[],
			   const frame_info_t *fi);

#endif /* GLW_VIDEO_COMMON_H */

