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

#include "config.h"
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#if ENABLE_LIBXV
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>
#endif

#include "video/video_decoder.h"
#include "video/video_playback.h"

#include <sys/ipc.h>
#include <sys/shm.h>

#ifdef CONFIG_LIBXSS
#include <X11/extensions/scrnsaver.h>
#endif

#include "misc/callout.h"
#include "showtime.h"

#include "x11_common.h"

struct x11_screensaver_state {
  Display *dpy;
  callout_t callout;
};


/**
 *
 */
static void
reset_screensaver(callout_t *c, void *aux)
{
  struct x11_screensaver_state *s = aux;

  XResetScreenSaver(s->dpy);
  callout_arm(&s->callout, reset_screensaver, s, 30);
}


/**
 *
 */
struct x11_screensaver_state *
x11_screensaver_suspend(Display *dpy)
{
  struct x11_screensaver_state *s;

  s = calloc(1, sizeof(struct x11_screensaver_state));
  s->dpy = dpy;
  callout_arm(&s->callout, reset_screensaver, s, 1);

#ifdef CONFIG_LIBXSS
  XScreenSaverSuspend(dpy, 1);
  TRACE(TRACE_DEBUG, "X11", "Suspending screensaver");
#endif
  return s;
}


/**
 *
 */
void
x11_screensaver_resume(struct x11_screensaver_state *s)
{
#ifdef CONFIG_LIBXSS
  TRACE(TRACE_DEBUG, "X11", "Resuming screensaver");
  XScreenSaverSuspend(s->dpy, 0);
#endif

  callout_disarm(&s->callout);
  free(s);
}


#if ENABLE_LIBXV
/**
 *
 */
static void xv_video_frame_deliver(struct video_decoder *vd,
				   uint8_t * const data[],
				   const int pitch[],
				   int width,
				   int height,
				   int pix_fmt,
				   int64_t pts,
				   int epoch,
				   int duration,
				   int deinterlace,
				   int top_field_first);
#endif

/**
 *
 */
typedef struct video_output {

  video_decoder_t *vo_vd;
  media_pipe_t *vo_mp;
  video_playback_t *vo_vp;

  prop_sub_t *vo_sub_url;

  Display *vo_dpy;
  int vo_win;
  GC vo_gc;

#if ENABLE_LIBXV
  int vo_xv_port;
  XvImage *vo_xv_image;
#endif

  XShmSegmentInfo vo_shm;

  // Position and dimension in window
  int vo_x, vo_y, vo_w, vo_h;

} video_output_t;


/**
 *
 */
static void
vo_set_url(void *opaque, const char *url)
{
  video_output_t *vo = opaque;
  event_t *e;

  if(url == NULL)
    return;

  e = event_create_url(EVENT_PLAY_URL, url);
  mp_enqueue_event(vo->vo_mp, e);
  event_unref(e);
}

#if ENABLE_LIBXV
/**
 *
 */
static int
init_with_xv(video_output_t *vo)
{
  XvAdaptorInfo *ai;
  unsigned int num_ai;
  int i, j;

  if(XvQueryAdaptors(vo->vo_dpy, DefaultRootWindow(vo->vo_dpy),
		     &num_ai, &ai) != Success) {
    TRACE(TRACE_DEBUG, "X11", "No XV adaptors available");
    return 0;
  }

  for(i = 0; i < num_ai; i++) {
    
    if((ai[i].type & (XvImageMask | XvInputMask)) != 
       (XvImageMask | XvInputMask))
      continue;

    for(j = ai[i].base_id; j < ai[i].base_id + ai[i].num_ports; j++) {
      if(XvGrabPort(vo->vo_dpy, j, CurrentTime) == Success) {
	TRACE(TRACE_DEBUG, "X11", "XV: Allocated port %d on %s", j, ai[i].name);
	vo->vo_xv_port = j;
	return 1;
      }
    }
  }
  TRACE(TRACE_INFO, "X11", "XV: No available ports");
  return 0;
}
#endif

/**
 *
 */
struct video_output *
x11_vo_create(Display *dpy, int win, prop_courier_t *pc, prop_t *self,
	      char *errbuf, size_t errlen)
{
  XGCValues xgcv;
  video_output_t *vo;
  vd_frame_deliver_t *deliver_fn;
  
  if(!XShmQueryExtension(dpy)) {
    snprintf(errbuf, errlen, "No SHM Extension available");
    return NULL;
  }

  vo = calloc(1, sizeof(video_output_t));
  vo->vo_dpy = dpy;
  vo->vo_win = win;

#if ENABLE_LIBXV
  if(init_with_xv(vo)) {

    deliver_fn = xv_video_frame_deliver;

  } else
#endif
    {
    free(vo);
    snprintf(errbuf, errlen, "No suitable video display methods available");
    return NULL;
  }

  vo->vo_gc = XCreateGC(vo->vo_dpy, vo->vo_win, 0, &xgcv);

  vo->vo_mp = mp_create("Video decoder", "video", MP_VIDEO);
  vo->vo_vd = video_decoder_create(vo->vo_mp, deliver_fn, vo);
  vo->vo_vp = video_playback_create(vo->vo_mp);

  vo->vo_sub_url = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "url"),
		   PROP_TAG_CALLBACK_STRING, vo_set_url, vo,
		   PROP_TAG_COURIER, pc, 
		   PROP_TAG_NAMED_ROOT, self, "self",
		   NULL);
  return vo;
}


/**
 *
 */
void
x11_vo_destroy(struct video_output *vo)
{
  prop_unsubscribe(vo->vo_sub_url);

  video_playback_destroy(vo->vo_vp);
  video_decoder_stop(vo->vo_vd);
  mp_ref_dec(vo->vo_mp);
  video_decoder_destroy(vo->vo_vd);

  XShmDetach(vo->vo_dpy, &vo->vo_shm);
  shmdt(vo->vo_shm.shmaddr);

#if ENABLE_LIBXV
  if(vo->vo_xv_port)
    XvUngrabPort(vo->vo_dpy, vo->vo_xv_port, CurrentTime);

  if(vo->vo_xv_image)
    XFree(vo->vo_xv_image);
#endif

  XFreeGC(vo->vo_dpy, vo->vo_gc);

  free(vo);
}


/**
 *
 */
void
x11_vo_position(struct video_output *vo, int x, int y, int w, int h)
{
  vo->vo_x = x;
  vo->vo_y = y;
  vo->vo_w = w;
  vo->vo_h = h;
}


#if ENABLE_LIBXV
/**
 *
 */
static int
wait_for_aclock(media_pipe_t *mp, int64_t pts, int epoch)
{
  int64_t rt, aclock, diff, deadline;
  struct timespec ts;

  if(mp->mp_audio_clock_epoch != epoch) {
    /* Not the same clock epoch, can not sync */
    return 0;
  }

  hts_mutex_lock(&mp->mp_clock_mutex);
  rt = showtime_get_ts();
  aclock = mp->mp_audio_clock + rt - mp->mp_audio_clock_realtime;
  hts_mutex_unlock(&mp->mp_clock_mutex);

  aclock += mp->mp_avdelta;

  diff = pts - aclock;

  if(diff > 0 && diff < 10000000) {
    deadline = rt + diff;

    ts.tv_sec  =  deadline / 1000000;
    ts.tv_nsec = (deadline % 1000000) * 1000;

    clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &ts, NULL);
  }
  return 1;
}
#endif


#if ENABLE_LIBXV
/**
 *
 */
static void 
xv_video_frame_deliver(struct video_decoder *vd,
		       uint8_t * const data[],
		       const int linesize[],
		       int width,
		       int height,
		       int pix_fmt,
		       int64_t pts,
		       int epoch,
		       int duration,
		       int deinterlace,
		       int top_field_first)
{
  video_output_t *vo = vd->vd_opaque;
  int syncok;

  if(vo->vo_xv_image == NULL) {
    //    uint32_t xv_format = 0x32595559; // YV12
    //    uint32_t xv_format = 0x32315659; // YV12
    uint32_t xv_format = 0x30323449;

    vo->vo_xv_image = XvShmCreateImage(vo->vo_dpy, vo->vo_xv_port,
				       xv_format, NULL,
				       width, height,
				       &vo->vo_shm);

    vo->vo_shm.shmid = shmget(IPC_PRIVATE, vo->vo_xv_image->data_size, 
			      IPC_CREAT | 0777);

    vo->vo_shm.shmaddr = (char *)shmat(vo->vo_shm.shmid, 0, 0);
    vo->vo_shm.readOnly = False;

    vo->vo_xv_image->data = vo->vo_shm.shmaddr;

    XShmAttach(vo->vo_dpy, &vo->vo_shm);

    XSync(vo->vo_dpy, False);
    shmctl(vo->vo_shm.shmid, IPC_RMID, 0);
  }
  
  int i;
  for(i = 0; i < 3; i++) {
    int w = width;
    int h = height;

    if(i) {
      w = w / 2;
      h = h / 2;
    }

    const uint8_t *src = data[i];
    char *dst = vo->vo_xv_image->data + vo->vo_xv_image->offsets[i];
    int pitch = vo->vo_xv_image->pitches[i];
    
    while(h--) {
      memcpy(dst, src, w);
      dst += pitch;
      src += linesize[i];
    }
  }

  float a = vo->vo_w / (vo->vo_h * vd->vd_aspect);
  int x, y, w, h;

  if(a > 1) {
    w = vo->vo_w / a;
    h = vo->vo_h;
  } else {
    w = vo->vo_w;
    h = vo->vo_h * a;
  }

  x = vo->vo_x + (vo->vo_w - w) / 2;
  y = vo->vo_y + (vo->vo_h - h) / 2;

  syncok = wait_for_aclock(vd->vd_mp, pts, epoch);

  XvShmPutImage(vo->vo_dpy, vo->vo_xv_port, vo->vo_win, vo->vo_gc,
		vo->vo_xv_image, 0, 0, width, height, x, y, w, h, False);

  XFlush(vo->vo_dpy);
  XSync(vo->vo_dpy, False);

  if(!syncok)
    usleep(duration);
}
#endif
