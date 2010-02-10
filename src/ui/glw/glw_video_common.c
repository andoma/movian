/*
 *  Video output on GL surfaces
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

#include "showtime.h"
#include "media.h"
#include "glw_video_opengl.h"
#include "glw_video_common.h"
#include "video/video_playback.h"
#if ENABLE_DVD
#include "video/video_dvdspu.h"
#endif



#if ENABLE_DVD
/**
 *
 */
int
glw_video_pointer_event(dvdspu_decoder_t *dd, int width, int height,
			glw_pointer_event_t *gpe, media_pipe_t *mp)
{
  pci_t *pci;
  int x, y;
  int32_t button, best, dist, d, mx, my, dx, dy;
  event_t *e;

  if(dd == NULL)
    return 0;

  pci = &dd->dd_pci;

  if(!pci->hli.hl_gi.hli_ss)
    return 1;
  
  x = (0.5 +  0.5 * gpe->x) * (float)width;
  y = (0.5 + -0.5 * gpe->y) * (float)height;

  best = 0;
  dist = 0x08000000; /* >> than  (720*720)+(567*567); */
  
  /* Loop through all buttons */
  for(button = 1; button <= pci->hli.hl_gi.btn_ns; button++) {
    btni_t *button_ptr = &(pci->hli.btnit[button-1]);

    if((x >= button_ptr->x_start) && (x <= button_ptr->x_end) &&
       (y >= button_ptr->y_start) && (y <= button_ptr->y_end)) {
      mx = (button_ptr->x_start + button_ptr->x_end)/2;
      my = (button_ptr->y_start + button_ptr->y_end)/2;
      dx = mx - x;
      dy = my - y;
      d = (dx*dx) + (dy*dy);
      /* If the mouse is within the button and the mouse is closer
       * to the center of this button then it is the best choice. */
      if(d < dist) {
        dist = d;
        best = button;
      }
    }
  }

  if(best == 0)
    return 1;

  switch(gpe->type) {
  case GLW_POINTER_CLICK:
    e = event_create(EVENT_DVD_ACTIVATE_BUTTON, sizeof(event_t) + 1);
    break;

  case GLW_POINTER_MOTION_UPDATE:
    if(dd->dd_curbut == best)
      return 1;

    e = event_create(EVENT_DVD_SELECT_BUTTON, sizeof(event_t) + 1);
    break;

  default:
    return 1;
  }

  e->e_payload[0] = best;
  mp_enqueue_event(mp, e);
  event_unref(e);
  return 1;
}
#endif


/**
 *
 */
void
glw_video_update_focusable(video_decoder_t *vd, glw_t *w)
{
  int want_focus = 0;
#if ENABLE_DVD
  dvdspu_decoder_t *dd = vd->vd_dvdspu;

  if(dd != NULL) {
    pci_t *pci = &dd->dd_pci;

    if(pci->hli.hl_gi.hli_ss)
      want_focus = 1;
  }
#endif
  
  glw_set_i(w, 
	    GLW_ATTRIB_FOCUS_WEIGHT, want_focus ? 1.0 : 0.0, 
	    NULL);
}


/**
 *
 */
int
glw_video_widget_event(event_t *e, media_pipe_t *mp, int in_menu)
{
  if(event_is_action(e, ACTION_PLAYPAUSE) ||
     event_is_action(e, ACTION_PLAY) ||
     event_is_action(e, ACTION_PAUSE) ||
     event_is_action(e, ACTION_ENTER)) {
    mp_enqueue_event(mp, e);
    return 1;
  }

  if(event_is_action(e, ACTION_UP) ||
     event_is_action(e, ACTION_DOWN) ||
     event_is_action(e, ACTION_LEFT) ||
     event_is_action(e, ACTION_RIGHT)) {
    
    if(in_menu) {
      mp_enqueue_event(mp, e);
      return 1;
    }
  }

  return 0;
}


/**
 *
 */
int
glw_video_compute_output_duration(video_decoder_t *vd, int frame_duration)
{
  int delta;
  const int maxdiff = 5000;

  if(vd->vd_avdiff_x > 0) {
    delta = pow(vd->vd_avdiff_x * 1000.0f, 2);
    if(delta > maxdiff)
      delta = maxdiff;

  } else if(vd->vd_avdiff_x < 0) {
    delta = -pow(-vd->vd_avdiff_x * 1000.0f, 2);
    if(delta < -maxdiff)
      delta = -maxdiff;
  } else {
    delta = 0;
  }
  return frame_duration + delta;
}


/**
 *
 */
void
glw_video_compute_avdiff(video_decoder_t *vd, media_pipe_t *mp, 
			 int64_t pts, int epoch)
{
  int64_t rt;
  int64_t aclock;

  if(mp->mp_audio_clock_epoch != epoch) {
    /* Not the same clock epoch, can not sync */
    vd->vd_avdiff_x = 0;
    kalman_init(&vd->vd_avfilter);
    return;
  }

  if(vd->vd_compensate_thres > 0) {
    vd->vd_compensate_thres--;
    vd->vd_avdiff_x = 0;
    kalman_init(&vd->vd_avfilter);
    return;
  }
  

  hts_mutex_lock(&mp->mp_clock_mutex);

  rt = showtime_get_ts();

  aclock = mp->mp_audio_clock + rt - mp->mp_audio_clock_realtime;

  hts_mutex_unlock(&mp->mp_clock_mutex);

  aclock += mp->mp_avdelta;

  vd->vd_avdiff = aclock - (pts - 16666) - vd->vd_avd_delta;

  if(abs(vd->vd_avdiff) < 10000000) {

    vd->vd_avdiff_x = kalman(&vd->vd_avfilter, (float)vd->vd_avdiff / 1000000);
    if(vd->vd_avdiff_x > 10.0f)
      vd->vd_avdiff_x = 10.0f;
    
    if(vd->vd_avdiff_x < -10.0f)
      vd->vd_avdiff_x = -10.0f;
  }

  if(mp->mp_stats) {
    if(!vd->vd_may_update_avdiff) {
      prop_set_float(mp->mp_prop_avdiff, vd->vd_avdiff_x);
      vd->vd_may_update_avdiff = 5;
    } else {
      vd->vd_may_update_avdiff--;
    }
  }

#if 0
 {
   static int64_t lastpts, lastaclock;
   
  printf("%s: AVDIFF = %10f %10d %15lld %15lld %15lld %15lld %15lld\n", 
	 mp->mp_name, vd->vd_avdiff_x, vd->vd_avdiff,
	 aclock, aclock - lastaclock, pts, pts - lastpts,
	 mp->mp_audio_clock);
  lastpts = pts;
  lastaclock = aclock;
 }
#endif
}
