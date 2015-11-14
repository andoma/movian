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
#include <assert.h>

#include <libavutil/pixdesc.h>

#include "main.h"
#include "media/media.h"
#include "video/video_playback.h"
#include "video/video_settings.h"

#include "glw.h"
#include "glw_video_common.h"
#include "glw_video_overlay.h"
#include "glw_renderer.h"
#include "glw_texture.h"

static int glw_video_input(const frame_info_t *info, void *opaque);
static int glw_set_video_codec(uint32_t type, media_codec_t *mc, void *opaque,
			       const frame_info_t *fi);


/**
 *
 */
static void
glw_video_reap(glw_video_t *gv)
{
  glw_video_reap_task_t *t;
  while((t = LIST_FIRST(&gv->gv_reaps)) != NULL) {
    LIST_REMOVE(t, link);
    t->fn(gv, t);
    free(t);
  }
}


/**
 *
 */
static void
glw_video_rctx_adjust(glw_rctx_t *rc, const glw_video_t *gv)
{
  const glw_root_t *gr = gv->w.glw_root;

  if(gr->gr_underscan_h || gr->gr_underscan_v) {
    glw_reposition(rc,
		   -gr->gr_underscan_h,
		   rc->rc_height + gr->gr_underscan_v,
		   rc->rc_width  + gr->gr_underscan_h,
		   -gr->gr_underscan_v);
  }

  float t_aspect = (float)gv->gv_dar_num / gv->gv_dar_den;

  if(gv->gv_fstretch)
    return;

  if(t_aspect * rc->rc_height < rc->rc_width) {

    if(gv->gv_hstretch)
      return;

    // Shrink X
    int border = rc->rc_width - t_aspect * rc->rc_height;
    int left  = (border + 1.0f) * 0.5f;
    int right = rc->rc_width - border * 0.5f;

    float s = (right - left) / (float)rc->rc_width;
    float t = -1.0f + (right + left) / (float)rc->rc_width;

    glw_Translatef(rc, t, 0.0f, 0.0f);
    glw_Scalef(rc, s, 1.0f, 1.0f);

    rc->rc_width = right - left;

  } else {
    // Shrink Y
    int border = rc->rc_height - rc->rc_width / t_aspect;
    int bottom  = (border + 1.0f) * 0.5f;
    int top     = rc->rc_height - border * 0.5f;

    float s = (top - bottom) / (float)rc->rc_height;
    float t = -1.0f + (top + bottom) / (float)rc->rc_height;

    glw_Translatef(rc, 0.0f, t, 0.0f);
    glw_Scalef(rc, 1.0f, s, 1.0f);
    rc->rc_height = top - bottom;
  }
}


/**
 *
 */
static int
glw_video_widget_event(glw_t *w, event_t *e)
{
  glw_video_t *gv = (glw_video_t *)w;
  media_pipe_t *mp = gv->gv_mp;

  // Intercept media events

  if(event_is_action(e, ACTION_PLAYPAUSE) ||
     event_is_action(e, ACTION_PLAY) ||
     event_is_action(e, ACTION_PAUSE) ||
     event_is_action(e, ACTION_SKIP_FORWARD) ||
     event_is_action(e, ACTION_SKIP_BACKWARD)) {
    mp_enqueue_event(mp, e);
    return 1;
  }

  // If we are in DVD menu, intercept those events as well

  if(gv->gv_spu_in_menu) {
    if(event_is_action(e, ACTION_UP) ||
       event_is_action(e, ACTION_DOWN) ||
       event_is_action(e, ACTION_LEFT) ||
       event_is_action(e, ACTION_RIGHT) ||
       event_is_action(e, ACTION_ACTIVATE)) {
      mp_enqueue_event(mp, e);
      return 1;
    }
  }

  return 0;
}


/**
 *
 */
static int
glw_video_compute_output_duration(glw_video_t *gv, int frame_duration)
{
  int delta;
  const int maxdiff = 5000;

  if(gv->gv_avdiff_x > 0) {
    delta = pow(gv->gv_avdiff_x * 1000.0f, 2);
    if(delta > maxdiff)
      delta = maxdiff;

  } else if(gv->gv_avdiff_x < 0) {
    delta = -pow(-gv->gv_avdiff_x * 1000.0f, 2);
    if(delta < -maxdiff)
      delta = -maxdiff;
  } else {
    delta = 0;
  }
  return frame_duration + delta;
}



// These are directly used in UI so renumbering is not possible
#define AVDIFF_LOCKED          0
#define AVDIFF_NO_LOCK         1
#define AVDIFF_INCORRECT_EPOCH 2
#define AVDIFF_HOLD            3
#define AVDIFF_CATCH_UP        4

const char *statustab[] = {
  [AVDIFF_LOCKED]          = "locked",
  [AVDIFF_NO_LOCK]         = "no lock",
  [AVDIFF_INCORRECT_EPOCH] = "incorrect epoch",
  [AVDIFF_HOLD]            = "holding",
  [AVDIFF_CATCH_UP]        = "catching up",
};

/**
 *
 */
static int
glw_video_compute_avdiff(glw_root_t *gr, media_pipe_t *mp,
			 int64_t pts, int epoch, glw_video_t *gv)
{
  int code;

  hts_mutex_lock(&mp->mp_clock_mutex);

  int64_t aclock = mp->mp_audio_clock + gr->gr_frame_start_avtime
    - mp->mp_audio_clock_avtime + mp->mp_avdelta;

  if(mp->mp_audio_clock_epoch != epoch) {
    /* Not the same clock epoch, can not sync */
    gv->gv_avdiff_x = 0;
    kalman_init(&gv->gv_avfilter);
    code = AVDIFF_INCORRECT_EPOCH;
    prop_set_int(mp->mp_prop_avdiff_error, code);

  } else {


    gv->gv_avdiff = aclock - pts;

    if(abs(gv->gv_avdiff) < 30000000) {

      gv->gv_avdiff_x = kalman_update(&gv->gv_avfilter,
				      (double)gv->gv_avdiff / 1000000);
      gv->gv_avdiff_x = MAX(MIN(gv->gv_avdiff_x, 30.0f), -30.0f);
      code = AVDIFF_LOCKED;

      if(gv->gv_avdiff_x > 0.1f)
	code = AVDIFF_CATCH_UP;
      if(gv->gv_avdiff_x < -0.1f)
	code = AVDIFF_HOLD;

    } else {
      code = AVDIFF_NO_LOCK;
    }

    if(!gv->gv_avdiff_update_thres) {
      prop_set_float(mp->mp_prop_avdiff, gv->gv_avdiff_x);
      prop_set_int(mp->mp_prop_avdiff_error, code);
      gv->gv_avdiff_update_thres = 5;
    } else {
      gv->gv_avdiff_update_thres--;
    }
  }

  hts_mutex_unlock(&mp->mp_clock_mutex);


  if(gconf.enable_detailed_avdiff) {
    static int64_t lastpts, lastaclock, lastclock;

    TRACE(TRACE_DEBUG, "AVDIFF", "VE:%d AE:%d %10f %10d %15"PRId64":a:%-8"PRId64" %15"PRId64":v:%-8"PRId64" %15"PRId64" %15"PRId64" %s %lld",
	  epoch,
	  mp->mp_audio_clock_epoch,
	  gv->gv_avdiff_x,
	  gv->gv_avdiff,
	  aclock,
	  aclock - lastaclock,
	  pts,
	  pts - lastpts,
	  mp->mp_audio_clock,
	  gr->gr_frame_start_avtime - lastclock,
	  statustab[code],
	  arch_get_avtime() - aclock);
    lastpts = pts;
    lastaclock = aclock;
    lastclock = gr->gr_frame_start_avtime;
  }
  return code;
}


/**
 *
 */
static int64_t
glw_video_compute_blend(glw_video_t *gv, glw_video_surface_t *sa,
			glw_video_surface_t *sb, int output_duration,
                        int interpolation)
{
  int64_t pts;

  if(!interpolation) {

    sa->gvs_duration -= output_duration;
    if(sa->gvs_pts != PTS_UNSET)
      sa->gvs_pts += output_duration;

    if(sa->gvs_duration < 0 && sb != NULL) {

      int spill = -sa->gvs_duration;
      sa->gvs_duration = 0;

      sb->gvs_duration -= spill;
      if(sb->gvs_pts != PTS_UNSET)
        sb->gvs_pts += spill;

      sa = sb;
    }

    gv->gv_sa = sa;
    gv->gv_sb = NULL;

    pts = sa->gvs_pts;

  } else if(sa->gvs_duration >= output_duration ) {

    gv->gv_sa = sa;
    gv->gv_sb = NULL;

    sa->gvs_duration -= output_duration;

    pts = sa->gvs_pts;
    if(sa->gvs_pts != PTS_UNSET)
      sa->gvs_pts += output_duration;

  } else if(sb != NULL) {

    gv->gv_sa = sa;
    gv->gv_sb = sb;
    gv->gv_blend = (float) sa->gvs_duration / (float)output_duration;

    if(sa->gvs_duration + sb->gvs_duration < output_duration) {

      pts = sb->gvs_pts;

    } else {
      pts = sa->gvs_pts;
      const int x = output_duration - sa->gvs_duration;
      sb->gvs_duration -= x;
      if(sb->gvs_pts != PTS_UNSET)
	sb->gvs_pts += x;
    }
    sa->gvs_duration = 0;

  } else {
    gv->gv_sa = sa;
    gv->gv_sb = NULL;
    if(sa->gvs_pts != PTS_UNSET)
      sa->gvs_pts += output_duration;

    pts = sa->gvs_pts;
  }

  return pts;
}


/**
 *
 */
int64_t
glw_video_newframe_blend(glw_video_t *gv, video_decoder_t *vd, int flags,
			 gv_surface_pixmap_release_t *release,
                         int interpolation)
{
  glw_root_t *gr = gv->w.glw_root;
  glw_video_surface_t *sa, *sb, *s;
  media_pipe_t *mp = gv->gv_mp;
  int64_t pts = PTS_UNSET;
  int frame_duration = gv->w.glw_root->gr_frameduration;

  interpolation &= gv->gv_vinterpolate;


  /* Find new surface to display */
  hts_mutex_assert(&gv->gv_surface_mutex);
 again:
  sa = TAILQ_FIRST(&gv->gv_decoded_queue);
  if(sa == NULL) {
    /* No frame available */
    sa = TAILQ_FIRST(&gv->gv_displaying_queue);
    if(sa != NULL) {
      /* Continue to display last frame */
      gv->gv_sa = sa;
      gv->gv_sa = NULL;
    } else {
      gv->gv_sa = NULL;
      gv->gv_sa = NULL;
    }

  } else {

    sb = TAILQ_NEXT(sa, gvs_link);

    if(!vd->vd_hold) {
      int output_duration;
      output_duration = glw_video_compute_output_duration(gv, frame_duration);

      pts = glw_video_compute_blend(gv, sa, sb, output_duration, interpolation);
      if(pts != PTS_UNSET) {
	pts -= frame_duration * 2;
	int code = glw_video_compute_avdiff(gr, mp, pts, sa->gvs_epoch, gv);

	if(code == AVDIFF_HOLD) {
	  kalman_init(&gv->gv_avfilter);
	  return pts;
	}

	if(code == AVDIFF_CATCH_UP && sb != NULL) {
	  gv->gv_sa = NULL;
	  release(gv, sa, &gv->gv_decoded_queue);
	  kalman_init(&gv->gv_avfilter);
	  goto again;
	}
      }
    }

    if(!vd->vd_hold || sb != NULL) {
      if(sa != NULL && sa->gvs_duration == 0)
	glw_video_enqueue_for_display(gv, sa, &gv->gv_decoded_queue);
    }
    if(sb != NULL && sb->gvs_duration == 0)
      glw_video_enqueue_for_display(gv, sb, &gv->gv_decoded_queue);


    /* There are frames available that we are going to display,
       push back old frames to decoder */
    while((s = TAILQ_FIRST(&gv->gv_displaying_queue)) != NULL) {
      if(s != gv->gv_sa && s != gv->gv_sb)
	release(gv, s, &gv->gv_displaying_queue);
      else
	break;
    }

  }
  return pts;
}


/**
 *
 */
static void
glw_video_play(glw_video_t *gv)
{
  event_t *e;

  if(gv->gv_freezed)
    return;

  if(!strcmp(gv->gv_current_url ?: "", gv->gv_pending_url ?: ""))
    return;

  mystrset(&gv->gv_current_url, gv->gv_pending_url ?: "");

  e = event_create_playurl(
      .url          = gv->gv_current_url,
      .primary      = !!(gv->gv_flags & GLW_VIDEO_PRIMARY),
      .priority     = gv->gv_priority,
      .no_audio     = !!(gv->gv_flags & GLW_VIDEO_NO_AUDIO),
      .item_model   = gv->gv_item_model,
      .how          = gv->gv_how,
      .parent_model = gv->gv_parent_model,
      .parent_url   = rstr_get(gv->gv_parent_url_x)
      );

  mp_enqueue_event(gv->gv_mp, e);
  event_release(e);
}


/**
 * We are going away, flush out all surfaces
 * and destroy zombie video decoder
 */
static void
glw_video_dtor(glw_t *w)
{
  glw_video_t *gv = (glw_video_t *)w;
  video_decoder_t *vd = gv->gv_vd;

  glw_renderer_free(&gv->gv_quad);

  prop_ref_dec(gv->gv_item_model);
  prop_ref_dec(gv->gv_parent_model);

#if ENABLE_MEDIA_SETTINGS
  prop_unsubscribe(gv->gv_vo_scaling_sub);
  prop_unsubscribe(gv->gv_vo_displace_x_sub);
  prop_unsubscribe(gv->gv_vo_displace_y_sub);
  prop_unsubscribe(gv->gv_vzoom_sub);
  prop_unsubscribe(gv->gv_pan_horizontal_sub);
  prop_unsubscribe(gv->gv_pan_vertical_sub);
  prop_unsubscribe(gv->gv_hstretch_sub);
  prop_unsubscribe(gv->gv_fstretch_sub);
  prop_unsubscribe(gv->gv_vo_on_video_sub);
  prop_unsubscribe(gv->gv_vinterpolate_sub);
#endif

  free(gv->gv_current_url);
  free(gv->gv_pending_url);
  free(gv->gv_how);
  rstr_release(gv->gv_parent_url_x);

  glw_video_overlay_deinit(gv);

  LIST_REMOVE(gv, gv_global_link);

  hts_mutex_lock(&gv->gv_surface_mutex);  /* Not strictly necessary
					     but keep asserts happy
					  */
  glw_video_surfaces_cleanup(gv);
  hts_mutex_unlock(&gv->gv_surface_mutex);

  video_decoder_destroy(vd);

  hts_cond_destroy(&gv->gv_avail_queue_cond);
  hts_cond_destroy(&gv->gv_init_cond);
  hts_mutex_destroy(&gv->gv_surface_mutex);

  mp_destroy(gv->gv_mp);
  gv->gv_mp = NULL;
}


/**
 *
 */
static void
glw_video_newframe(glw_t *w, int flags)
{
  glw_video_t *gv = (glw_video_t *)w;
  video_decoder_t *vd = gv->gv_vd;
  int64_t pts;

  hts_mutex_lock(&gv->gv_surface_mutex);

  glw_video_reap(gv);

  if(gv->gv_need_init) {
    gv->gv_engine->gve_init(gv);
    gv->gv_need_init = 0;
    hts_cond_signal(&gv->gv_init_cond);
  }

  if(gv->gv_engine != NULL)
    pts = gv->gv_engine->gve_newframe(gv, vd, flags);
  else
    pts = PTS_UNSET;

  hts_mutex_unlock(&gv->gv_surface_mutex);

  if(pts != PTS_UNSET)
    glw_video_overlay_set_pts(gv, pts);
}


/**
 *
 */
static void
glw_video_layout(glw_t *w, const glw_rctx_t *rc)
{
  glw_video_t *gv = (glw_video_t *)w;
  glw_rctx_t rc0;

  w->glw_root->gr_can_externalize = 0;

  rc0 = *rc;
  glw_video_rctx_adjust(&rc0, gv);
  glw_video_overlay_layout(gv, rc, &rc0);
}


/**
 *
 */
static int
glw_video_pointer_event(glw_t *w, const glw_pointer_event_t *gpe)
{
  glw_video_t *gv = (glw_video_t *)w;
  video_decoder_t *vd = gv->gv_vd;
  return glw_video_overlay_pointer_event(vd, gv->gv_width, gv->gv_height,
					 gpe, gv->gv_mp);

}

/**
 *
 */
static int
glw_video_widget_callback(glw_t *w, void *opaque, glw_signal_t signal,
			  void *extra)
{
  glw_video_t *gv = (glw_video_t *)w;
  video_decoder_t *vd = gv->gv_vd;

  switch(signal) {

  case GLW_SIGNAL_DESTROY:
    hts_mutex_lock(&gv->gv_surface_mutex);
    hts_cond_signal(&gv->gv_avail_queue_cond);
    hts_cond_signal(&gv->gv_init_cond);
    hts_mutex_unlock(&gv->gv_surface_mutex);
    video_playback_destroy(gv->gv_mp);
    video_decoder_stop(vd);
    return 0;

  default:
    return 0;
  }
}


/**
 *
 */
static void
glw_video_ctor(glw_t *w)
{
  glw_video_t *gv = (glw_video_t *)w;
  glw_root_t *gr = w->glw_root;

  kalman_init(&gv->gv_avfilter);

  //  gv->gv_cfg_req.gvc_engine = &glw_video_blank;
  //  gv->gv_cfg_cur.gvc_engine = &glw_video_blank;

  TAILQ_INIT(&gv->gv_avail_queue);
  TAILQ_INIT(&gv->gv_parked_queue);
  TAILQ_INIT(&gv->gv_displaying_queue);
  TAILQ_INIT(&gv->gv_decoded_queue);

  hts_mutex_init(&gv->gv_surface_mutex);
  hts_cond_init(&gv->gv_avail_queue_cond, &gv->gv_surface_mutex);
  hts_cond_init(&gv->gv_init_cond, &gv->gv_surface_mutex);

  gv->gv_mp = mp_create("Video decoder", MP_VIDEO | MP_PRIMABLE);
#if CONFIG_GLW_BACKEND_OPENGL
  if(video_settings.vdpau)
    gv->gv_mp->mp_vdpau_dev = gr->gr_be.gbr_vdpau_dev;
#endif

  LIST_INSERT_HEAD(&gr->gr_video_decoders, gv, gv_global_link);

  gv->gv_mp->mp_video_frame_deliver = glw_video_input;
  gv->gv_mp->mp_set_video_codec = glw_set_video_codec;
  gv->gv_mp->mp_video_frame_opaque = gv;

  gv->gv_vd = video_decoder_create(gv->gv_mp);
  video_playback_create(gv->gv_mp);

  gv->gv_vzoom = 100;

#if ENABLE_MEDIA_SETTINGS
  prop_t *c = gv->gv_mp->mp_prop_ctrl;

  gv->gv_vo_scaling_sub =
    prop_subscribe(0,
		   PROP_TAG_SET_FLOAT, &gv->gv_vo_scaling,
		   PROP_TAG_COURIER, w->glw_root->gr_courier,
		   PROP_TAG_ROOT, c,
                   PROP_TAG_NAME("ctrl", "subscale"),
		   NULL);

  gv->gv_vo_displace_y_sub =
    prop_subscribe(0,
		   PROP_TAG_SET_INT, &gv->gv_vo_displace_y,
		   PROP_TAG_COURIER, w->glw_root->gr_courier,
		   PROP_TAG_ROOT, c,
                   PROP_TAG_NAME("ctrl", "subvdisplace"),
		   NULL);

  gv->gv_vo_displace_x_sub =
    prop_subscribe(0,
		   PROP_TAG_SET_INT, &gv->gv_vo_displace_x,
		   PROP_TAG_COURIER, w->glw_root->gr_courier,
		   PROP_TAG_ROOT, c,
                   PROP_TAG_NAME("ctrl", "subhdisplace"),
		   NULL);

  gv->gv_vzoom_sub =
    prop_subscribe(0,
		   PROP_TAG_SET_INT, &gv->gv_vzoom,
		   PROP_TAG_COURIER, w->glw_root->gr_courier,
		   PROP_TAG_ROOT, c,
                   PROP_TAG_NAME("ctrl", "vzoom"),
		   NULL);

  gv->gv_pan_horizontal_sub =
    prop_subscribe(0,
		   PROP_TAG_SET_INT, &gv->gv_pan_horizontal,
		   PROP_TAG_COURIER, w->glw_root->gr_courier,
		   PROP_TAG_ROOT, c,
                   PROP_TAG_NAME("ctrl", "panhorizontal"),
		   NULL);

  gv->gv_pan_vertical_sub =
    prop_subscribe(0,
		   PROP_TAG_SET_INT, &gv->gv_pan_vertical,
		   PROP_TAG_COURIER, w->glw_root->gr_courier,
		   PROP_TAG_ROOT, c,
                   PROP_TAG_NAME("ctrl", "panvertical"),
		   NULL);

  gv->gv_hstretch_sub =
    prop_subscribe(0,
		   PROP_TAG_SET_INT, &gv->gv_hstretch,
		   PROP_TAG_COURIER, w->glw_root->gr_courier,
		   PROP_TAG_ROOT, c,
                   PROP_TAG_NAME("ctrl", "hstretch"),
		   NULL);

  gv->gv_fstretch_sub =
    prop_subscribe(0,
		   PROP_TAG_SET_INT, &gv->gv_fstretch,
		   PROP_TAG_COURIER, w->glw_root->gr_courier,
		   PROP_TAG_ROOT, c,
                   PROP_TAG_NAME("ctrl", "fstretch"),
		   NULL);

  gv->gv_vo_on_video_sub =
    prop_subscribe(0,
		   PROP_TAG_SET_INT, &gv->gv_vo_on_video,
		   PROP_TAG_COURIER, w->glw_root->gr_courier,
		   PROP_TAG_ROOT, c,
                   PROP_TAG_NAME("ctrl", "subalign"),
		   NULL);

  gv->gv_vinterpolate_sub =
    prop_subscribe(0,
		   PROP_TAG_SET_INT, &gv->gv_vinterpolate,
		   PROP_TAG_COURIER, w->glw_root->gr_courier,
		   PROP_TAG_ROOT, c,
                   PROP_TAG_NAME("ctrl", "vinterpolate"),
		   NULL);
#endif
}


/**
 *
 */
static void
mod_video_flags(glw_t *w, int set, int clr)
{
  glw_video_t *gv = (glw_video_t *)w;
  gv->gv_flags = (gv->gv_flags | set) & ~clr;
}


/**
 *
 */
static void
set_how(glw_t *w, const char *how)
{
  glw_video_t *gv = (glw_video_t *)w;

  if(how == NULL)
    return;

  mystrset(&gv->gv_how, how);
  glw_video_play(gv);
}



/**
 *
 */
static void
set_source(glw_t *w, rstr_t *url, glw_style_t *origin)
{
  glw_video_t *gv = (glw_video_t *)w;

  if(url == NULL)
    return;

  mystrset(&gv->gv_pending_url, rstr_get(url));
  glw_video_play(gv);
}


/**
 *
 */
static void
freeze(glw_t *w)
{
  glw_video_t *gv = (glw_video_t *)w;
  gv->gv_freezed = 1;
}


/**
 *
 */
static void
thaw(glw_t *w)
{
  glw_video_t *gv = (glw_video_t *)w;
  gv->gv_freezed = 0;
  glw_video_play(gv);
}


/**
 *
 */
static void
set_audio_volume(glw_video_t *gv, float v)
{
  media_pipe_t *mp = gv->gv_mp;

  if(mp->mp_vol_ui == v)
    return;

  hts_mutex_lock(&mp->mp_mutex);
  mp->mp_vol_ui = v;
  mp_send_volume_update_locked(mp);
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
static int
glw_video_set_float(glw_t *w, glw_attribute_t attrib, float value,
                    glw_style_t *origin)
{
  glw_video_t *gv = (glw_video_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_AUDIO_VOLUME:
    set_audio_volume(gv, value);
    return 0; // Setting audio volume does not need to repaint UI
  default:
    return -1;
  }
  return 1;
}


/**
 *
 */
static int
glw_video_set_int(glw_t *w, glw_attribute_t attrib, int value,
                  glw_style_t *origin)
{
  glw_video_t *gv = (glw_video_t *)w;
  event_t *e;

  switch(attrib) {
  case GLW_ATTRIB_PRIORITY:
    gv->gv_priority = value;

    e = event_create_int(EVENT_PLAYBACK_PRIORITY, value);
    mp_enqueue_event(gv->gv_mp, e);
    event_release(e);
    return 0;

  default:
    return -1;
  }
  return 1;
}


/**
 *
 */
static void
glw_video_set_roots(glw_t *w, prop_t *self, prop_t *parent, prop_t *clone)
{
  glw_video_t *gv = (glw_video_t *)w;
  prop_link(gv->gv_mp->mp_prop_root, prop_create(self, "media"));
}



/**
 *
 */
static int
glw_video_set_prop(glw_t *w, glw_attribute_t attrib, prop_t *p)
{
  glw_video_t *gv = (glw_video_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_PROP_ITEM_MODEL:
    prop_ref_dec(gv->gv_item_model);
    gv->gv_item_model = prop_ref_inc(p);
    return 0;

  case GLW_ATTRIB_PROP_PARENT_MODEL:
    prop_ref_dec(gv->gv_parent_model);
    gv->gv_parent_model = prop_ref_inc(p);
    return 0;

  default:
    return -1;
  }
  return 1;
}


/**
 *
 */
static int
glw_video_set_rstr(glw_t *w, glw_attribute_t attrib, rstr_t *rstr,
                   glw_style_t *origin)
{
  glw_video_t *gv = (glw_video_t *)w;

  switch(attrib) {
  case GLW_ATTRIB_PARENT_URL:
    rstr_set(&gv->gv_parent_url_x, rstr);
    return 0;

  default:
    return -1;
  }
  return 1;
}




/**
 *
 */
void
glw_video_render(glw_t *w, const glw_rctx_t *rc)
{
  const glw_root_t *gr = w->glw_root;
  glw_video_t *gv = (glw_video_t *)w;
  glw_rctx_t rc0 = *rc;

  glw_video_rctx_adjust(&rc0, gv);

  gv->gv_rwidth  = rc0.rc_width;
  gv->gv_rheight = rc0.rc_height;

  if(glw_is_focusable_or_clickable(w))
    glw_store_matrix(w, &rc0);

  glw_rctx_t rc1 = rc0;

  if(gv->gv_vzoom != 100) {
    float zoom = gv->gv_vzoom / 100.0f;
    glw_Scalef(&rc1, zoom, zoom, 1.0);
  }

  glw_Translatef(&rc1,
                 gv->gv_pan_horizontal / 50.0,
                 gv->gv_pan_vertical / 50.0,
                 0);

  glw_project(&gv->gv_rect, &rc1, gr);

  if(!glw_renderer_initialized(&gv->gv_quad)) {
    glw_renderer_init_quad(&gv->gv_quad);

    glw_renderer_vtx_pos(&gv->gv_quad, 0, -1, -1, 0);
    glw_renderer_vtx_pos(&gv->gv_quad, 1,  1, -1, 0);
    glw_renderer_vtx_pos(&gv->gv_quad, 2,  1,  1, 0);
    glw_renderer_vtx_pos(&gv->gv_quad, 3, -1,  1, 0);
  }

  hts_mutex_lock(&gv->gv_surface_mutex);

  if(gv->gv_engine != NULL)
    gv->gv_engine->gve_render(gv, &rc1);

  hts_mutex_unlock(&gv->gv_surface_mutex);

  glw_video_overlay_render(gv, rc, &rc0);
}


/**
 *
 */
static int
glw_video_set_int_unresolved(glw_t *w, const char *a, int value,
                             glw_style_t *gs)
{
  glw_video_t *gv = (glw_video_t *)w;

  if(!strcmp(a, "bottomOverlayDisplacement")) {
    if(gv->gv_bottom_overlay_displacement == value)
      return GLW_SET_NO_CHANGE;
    gv->gv_bottom_overlay_displacement = value;
    return GLW_SET_RERENDER_REQUIRED;
  }

  return GLW_SET_NOT_RESPONDING;
}


/**
 *
 */
static glw_class_t glw_video = {
  .gc_name = "video",
  .gc_instance_size = sizeof(glw_video_t),
  .gc_set_int = glw_video_set_int,
  .gc_set_float = glw_video_set_float,
  .gc_set_roots = glw_video_set_roots,
  .gc_set_prop = glw_video_set_prop,
  .gc_set_rstr = glw_video_set_rstr,
  .gc_ctor = glw_video_ctor,
  .gc_dtor = glw_video_dtor,
  .gc_layout = glw_video_layout,
  .gc_render = glw_video_render,
  .gc_newframe = glw_video_newframe,
  .gc_signal_handler = glw_video_widget_callback,
  .gc_mod_video_flags = mod_video_flags,
  .gc_set_source = set_source,
  .gc_set_how = set_how,
  .gc_freeze = freeze,
  .gc_thaw = thaw,
  .gc_send_event = glw_video_widget_event,
  .gc_pointer_event = glw_video_pointer_event,
  .gc_set_int_unresolved = glw_video_set_int_unresolved,
};

GLW_REGISTER_CLASS(glw_video);


/**
 *
 */
int
glw_video_configure(glw_video_t *gv, const glw_video_engine_t *engine)
{
  hts_mutex_assert(&gv->gv_surface_mutex);

  if(gv->gv_engine != engine) {

    if(gv->gv_engine != NULL)
      gv->gv_engine->gve_reset(gv);

    TAILQ_INIT(&gv->gv_avail_queue);
    TAILQ_INIT(&gv->gv_parked_queue);
    TAILQ_INIT(&gv->gv_displaying_queue);
    TAILQ_INIT(&gv->gv_decoded_queue);

    gv->gv_engine = engine;

    if(engine == NULL)
      return 0;

    if(engine->gve_init_on_ui_thread) {
      gv->gv_need_init = 1;
      while(1) {
	if(gv->w.glw_flags & GLW_DESTROYING)
	  return 1;
	if(!gv->gv_need_init)
	  break;
	hts_cond_wait(&gv->gv_init_cond, &gv->gv_surface_mutex);
      }
    } else {
      engine->gve_init(gv);
    }
  }
  return 0;
}


/**
 *
 */
void *
glw_video_add_reap_task(glw_video_t *gv, size_t s, void *fn)
{
  glw_video_reap_task_t *t = calloc(1, s);
  t->fn = fn;
  LIST_INSERT_HEAD(&gv->gv_reaps, t, link);
  hts_mutex_assert(&gv->gv_surface_mutex);
  return t;
}


/**
 *
 */
glw_video_surface_t *
glw_video_get_surface(glw_video_t *gv, const int *w, const int *h)
{
  glw_video_surface_t *gvs;
  hts_mutex_assert(&gv->gv_surface_mutex);

  while(1) {
    if(gv->w.glw_flags & GLW_DESTROYING)
      return NULL;

    if((gvs = TAILQ_FIRST(&gv->gv_avail_queue)) != NULL) {
      TAILQ_REMOVE(&gv->gv_avail_queue, gvs, gvs_link);

      if(w == NULL)
	return gvs;

      if(w[0] == gvs->gvs_width[0] &&
	 w[1] == gvs->gvs_width[1] &&
	 w[2] == gvs->gvs_width[2] &&
	 h[0] == gvs->gvs_height[0] &&
	 h[1] == gvs->gvs_height[1] &&
	 h[2] == gvs->gvs_height[2])
	return gvs;

      gvs->gvs_width[0]  = w[0];
      gvs->gvs_width[1]  = w[1];
      gvs->gvs_width[2]  = w[2];
      gvs->gvs_height[0] = h[0];
      gvs->gvs_height[1] = h[1];
      gvs->gvs_height[2] = h[2];

      if(gv->gv_engine != NULL && gv->gv_engine->gve_surface_init != NULL) {
	gv->gv_engine->gve_surface_init(gv, gvs);
	return gvs;
      }

      TAILQ_INSERT_TAIL(&gv->gv_parked_queue, gvs, gvs_link);
      continue;
    }
    hts_cond_wait(&gv->gv_avail_queue_cond, &gv->gv_surface_mutex);
  }
}


/**
 *
 */
void
glw_video_put_surface(glw_video_t *gv, glw_video_surface_t *s,
		      int64_t pts, int epoch, int duration,
		      int interlaced, int yshift)
{
  s->gvs_pts = pts;
  s->gvs_epoch = epoch;
  s->gvs_duration = duration;
  s->gvs_interlaced = interlaced;
  s->gvs_yshift = yshift;
  TAILQ_INSERT_TAIL(&gv->gv_decoded_queue, s, gvs_link);
  hts_mutex_assert(&gv->gv_surface_mutex);
}


static LIST_HEAD(, glw_video_engine) engines;

void
glw_register_video_engine(glw_video_engine_t *gve)
{
  LIST_INSERT_HEAD(&engines, gve, gve_link);
}


/**
 * Frame delivery from video decoder
 */
static int
glw_video_input(const frame_info_t *fi, void *opaque)
{
  glw_video_t *gv = opaque;
  glw_video_engine_t *gve;
  int rval;

  hts_mutex_lock(&gv->gv_surface_mutex);

  if(fi == NULL) {

    rval = 0;

    if(gv->gv_engine != NULL && gv->gv_engine->gve_blackout != NULL)
      gv->gv_engine->gve_blackout(gv);

  } else {

    rval = 1;

    gv->gv_dar_num = fi->fi_dar_num;
    gv->gv_dar_den = fi->fi_dar_den;
    gv->gv_vheight = fi->fi_height;

    LIST_FOREACH(gve, &engines, gve_link) {
      if(gve->gve_type == fi->fi_type) {
	rval = gve->gve_deliver(fi, gv, gve);
        break;
      }
    }
  }

  hts_mutex_unlock(&gv->gv_surface_mutex);
  return rval;
}


/**
 *
 */
static int
glw_set_video_codec(uint32_t type, media_codec_t *mc, void *opaque,
		    const frame_info_t *fi)
{
  glw_video_t *gv = opaque;
  glw_video_engine_t *gve;
  int r = -1;
  hts_mutex_lock(&gv->gv_surface_mutex);

  if(type == 'none') {
    glw_video_configure(gv, NULL);
    hts_mutex_unlock(&gv->gv_surface_mutex);
    return 0;
  }

  if(fi != NULL) {
    gv->gv_dar_num = fi->fi_dar_num;
    gv->gv_dar_den = fi->fi_dar_den;
    gv->gv_vheight = fi->fi_height;
  }

  LIST_FOREACH(gve, &engines, gve_link) {
    if(gve->gve_type == type) {
      r = gve->gve_set_codec(mc, gv, fi, gve);
      break;
    }
  }

  hts_mutex_unlock(&gv->gv_surface_mutex);
  return r;
}



/**
 *
 */
void
glw_video_surfaces_cleanup(glw_video_t *gv)
{
  if(gv->gv_engine != NULL)
    gv->gv_engine->gve_reset(gv);

  glw_video_reap(gv);
  TAILQ_INIT(&gv->gv_avail_queue);
  TAILQ_INIT(&gv->gv_parked_queue);
  TAILQ_INIT(&gv->gv_displaying_queue);
  TAILQ_INIT(&gv->gv_decoded_queue);
}





/**
 *
 */
void
glw_video_reset(glw_root_t *gr)
{
  glw_video_t *gv;

  LIST_FOREACH(gv, &gr->gr_video_decoders, gv_global_link) {

    hts_mutex_lock(&gv->gv_surface_mutex);

    glw_video_surfaces_cleanup(gv);
    gv->gv_engine = NULL;

    hts_mutex_unlock(&gv->gv_surface_mutex);
  }
}



#if ENABLE_LIBAV


static int
video_deliver_lavc(const frame_info_t *fi, glw_video_t *gv,
                   glw_video_engine_t *e)
{
  frame_info_t nfi = *fi;

  switch(fi->fi_pix_fmt) {
  case PIX_FMT_YUV420P:
  case PIX_FMT_YUV422P:
  case PIX_FMT_YUV444P:
  case PIX_FMT_YUV410P:
  case PIX_FMT_YUV411P:
  case PIX_FMT_YUV440P:
  case PIX_FMT_YUVJ420P:
  case PIX_FMT_YUVJ422P:
  case PIX_FMT_YUVJ444P:
  case PIX_FMT_YUVJ440P:
    av_pix_fmt_get_chroma_sub_sample(nfi.fi_pix_fmt, &nfi.fi_hshift,
                                     &nfi.fi_vshift);

    nfi.fi_type = 'YUVP';
    break;

  case AV_PIX_FMT_VDPAU:
    nfi.fi_type = 'VDPA';
    break;

  case AV_PIX_FMT_BGR24:
    nfi.fi_type = 'BGR';
    break;

  case AV_PIX_FMT_YUV420P10LE:
    nfi.fi_type = 'YUVp';
    break;

  case AV_PIX_FMT_XYZ12LE:
    nfi.fi_type = 'XYZ6';
    break;

  default:
    return 1;
  }
  glw_video_engine_t *gve;

  LIST_FOREACH(gve, &engines, gve_link)
    if(gve->gve_type == nfi.fi_type)
      return gve->gve_deliver(&nfi, gv, gve);

  return 1;
}


/**
 *
 */
static glw_video_engine_t glw_video_lavc = {
  .gve_type = 'LAVC',
  .gve_deliver = video_deliver_lavc,
};

GLW_REGISTER_GVE(glw_video_lavc);

#endif

