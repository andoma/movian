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
#ifndef GLW_VIDEO_COMMON_H
#define GLW_VIDEO_COMMON_H

#include "glw.h"
#include "glw_renderer.h"
#include "media/media.h"
#include "video/video_playback.h"
#include "video/video_decoder.h"
#include "misc/kalman.h"

#if ENABLE_VDPAU
#include "video/vdpau.h"
#endif

LIST_HEAD(glw_video_overlay_list, glw_video_overlay);
TAILQ_HEAD(glw_video_surface_queue, glw_video_surface);
LIST_HEAD(glw_video_reap_task_list, glw_video_reap_task);

/**
 *
 */
typedef struct glw_video_reap_task {
  LIST_ENTRY(glw_video_reap_task) link;
  void (*fn)(struct glw_video *gv, void *ptr);
} glw_video_reap_task_t;


/**
 *
 */
typedef struct glw_video_surface {

  TAILQ_ENTRY(glw_video_surface) gvs_link;

  int gvs_duration;
  uint64_t gvs_pts;
  int gvs_epoch;

  void *gvs_data[3];

  int gvs_width[3];
  int gvs_height[3];

  int gvs_interlaced;
  int gvs_yshift;

  int gvs_id;

  void *gvs_opaque;
  int gvs_format;

  glw_backend_texture_t gvs_texture;
  int gvs_uploaded;

#if CONFIG_GLW_BACKEND_OPENGL
  GLuint gvs_pbo[3];
  int gvs_size[3];
#endif

#if CONFIG_GLW_BACKEND_RSX
  realityTexture gvs_tex[3];
  int gvs_size;
  int gvs_offset;
#endif

#if ENABLE_VDPAU
  int gvs_mapped;
  VdpOutputSurface gvs_vdpau_surface;
  GLvdpauSurfaceNV gvs_gl_surface;
#endif

  struct AVFrame *gvs_frame;
  float gvs_tex_width;

  void *gvs_ref_aux;
  void (*gvs_ref_release)(void *aux);

} glw_video_surface_t;


#define GLW_VIDEO_MAX_SURFACES 10

/**
 *
 */
typedef struct glw_video {

  glw_t w;

  glw_renderer_t gv_quad;
  glw_program_args_t gv_gpa;

  int gv_width;
  int gv_height;

  int gv_dar_num;
  int gv_dar_den;

  int gv_vheight;

  int gv_rwidth;
  int gv_rheight;

  int gv_fwidth;
  int gv_fheight;

  char *gv_current_url;
  char *gv_pending_url;

  char *gv_how;
  rstr_t *gv_parent_url_x;

  int gv_flags;
  int gv_priority;
  prop_t *gv_item_model;
  prop_t *gv_parent_model;
  char gv_freezed;

  /**
   * AV Diff handling
   */

  int gv_avdiff_update_thres; // Avoid updating user facing avdiff too often
  kalman_t gv_avfilter;
  float gv_avdiff_x;
  int gv_avdiff;

  video_decoder_t *gv_vd;
  media_pipe_t *gv_mp;

  glw_video_surface_t *gv_sa, *gv_sb;
  float gv_blend;

  LIST_ENTRY(glw_video) gv_global_link;

  struct glw_video_overlay_list gv_overlays;
  int gv_bottom_overlay_displacement;

  float gv_cmatrix_cur[16];
  float gv_cmatrix_tgt[16];
  int gv_planes;
  int gv_tex_internal_format;
  int gv_tex_format;
  int gv_tex_type;
  int gv_tex_bytes_per_pixel;

  hts_mutex_t gv_surface_mutex;

  struct glw_video_reap_task_list gv_reaps;

  const struct glw_video_engine *gv_engine;
  int gv_need_init;
  hts_cond_t gv_init_cond;
  glw_video_surface_t gv_surfaces[GLW_VIDEO_MAX_SURFACES];


  /**
   * Frames that needs to be prepared before they can be used
   */
  struct glw_video_surface_queue gv_parked_queue;

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

  int64_t gv_nextpts;
  int gv_nextpts_epoch;

  void *gv_aux;


  /**
   * Settings that originate from media_pipe. However, we subscribe
   * on them to get updates
   */
#if ENABLE_MEDIA_SETTINGS
  prop_sub_t *gv_vo_scaling_sub;
  prop_sub_t *gv_vo_displace_y_sub;
  prop_sub_t *gv_vo_displace_x_sub;
  prop_sub_t *gv_vo_on_video_sub;
  prop_sub_t *gv_vzoom_sub;
  prop_sub_t *gv_pan_horizontal_sub;
  prop_sub_t *gv_pan_vertical_sub;
  prop_sub_t *gv_hstretch_sub;
  prop_sub_t *gv_fstretch_sub;
  prop_sub_t *gv_vinterpolate_sub;
#endif
  float gv_vo_scaling;
  int gv_vo_displace_y;
  int gv_vo_displace_x;
  int gv_vo_on_video;
  int gv_vzoom;
  int gv_pan_horizontal;
  int gv_pan_vertical;
  int gv_hstretch;
  int gv_fstretch;
  int gv_vinterpolate;

  // DVD SPU stuff

  int gv_spu_in_menu;

  // 2D cordinates on screen

  glw_rect_t gv_rect;

  int gv_invisible;


  // Log supression
  int gv_logged_pixfmt;

} glw_video_t;


/**
 *
 */
typedef struct glw_video_engine {
  uint32_t gve_type;

  int gve_init_on_ui_thread;

  int (*gve_deliver)(const frame_info_t *fi, glw_video_t *gv,
                     struct glw_video_engine *gve);

  int (*gve_set_codec)(media_codec_t *mc, glw_video_t *gv,
		       const frame_info_t *fi,
                       struct glw_video_engine *gve);

  void (*gve_blackout)(glw_video_t *gv);

  void (*gve_render)(glw_video_t *gv, glw_rctx_t *rc);

  int64_t (*gve_newframe)(glw_video_t *gv, video_decoder_t *vd, int flags);

  void (*gve_reset)(glw_video_t *gv);

  int (*gve_init)(glw_video_t *gv);

  void (*gve_surface_init)(glw_video_t *gv, glw_video_surface_t *gvs);

  LIST_ENTRY(glw_video_engine) gve_link;

} glw_video_engine_t;


void glw_register_video_engine(glw_video_engine_t *gve);

#define GLW_REGISTER_GVE(n) INITIALIZER(gveinit ## n) { \
 glw_register_video_engine(&n);}


typedef void (gv_surface_pixmap_release_t)(glw_video_t *gv,
					   glw_video_surface_t *gvs,
					   struct glw_video_surface_queue *fq);

int64_t glw_video_newframe_blend(glw_video_t *gv, video_decoder_t *vd,
				 int flags, gv_surface_pixmap_release_t *r,
                                 int interpolation);

void glw_video_render(glw_t *w, const glw_rctx_t *rc);

void glw_video_reset(glw_root_t *gr);

/**
 *
 */
static __inline void
glw_video_enqueue_for_display(glw_video_t *gv, glw_video_surface_t *gvs,
			      struct glw_video_surface_queue *fromqueue)
{
  TAILQ_REMOVE(fromqueue, gvs, gvs_link);
  TAILQ_INSERT_TAIL(&gv->gv_displaying_queue, gvs, gvs_link);
}


/**
 *
 */
void glw_video_surfaces_cleanup(glw_video_t *gv);

glw_video_surface_t *glw_video_get_surface(glw_video_t *gv,
					   const int *w, const int *h);

void glw_video_put_surface(glw_video_t *gv, glw_video_surface_t *s,
			   int64_t pts, int epoch, int duration,
			   int interlaced, int yshift);

int glw_video_configure(glw_video_t *gv, const glw_video_engine_t *engine);

void *glw_video_add_reap_task(glw_video_t *gv, size_t s, void *fn);

/**
 *
 */
void glw_video_opengl_load_uniforms(glw_root_t *gr, glw_program_t *gp,
                                    void *args, const glw_render_job_t *rj);

#endif /* GLW_VIDEO_COMMON_H */

