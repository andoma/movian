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
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#if ENABLE_LIBXV
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvlib.h>
#endif

#include <libswscale/swscale.h>

#include "video/video_decoder.h"
#include "video/video_playback.h"

#include <sys/ipc.h>
#include <sys/shm.h>

#ifdef CONFIG_LIBXSS
#include <X11/extensions/scrnsaver.h>
#endif


#if ENABLE_LIBXXF86VM
#include <X11/extensions/xf86vmode.h>
#endif

#include "misc/callout.h"
#include "main.h"

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


/**
 *
 */
int
x11_set_contrast(Display *dpy, int screen, int low, int high)
{
#if ENABLE_LIBXXF86VM

  unsigned short *ramp = malloc(sizeof(short) * 256);
  int i;

  for(i = 0; i < 256; i++)
    ramp[i] = low * 256 + ((i * (high - low) * 256) >> 8);

  XF86VidModeSetGammaRamp(dpy, screen, 256,
			  ramp, ramp, ramp);

  free(ramp);
  return 0;

#else
  return -1;
#endif
}



/**
 *
 */
typedef struct video_output {

  video_decoder_t *vo_vd;
  media_pipe_t *vo_mp;

  prop_sub_t *vo_sub_source;

  Display *vo_dpy;
  int vo_win;
  GC vo_gc;

#if ENABLE_LIBXV
  int vo_xv_port;
  XvImage *vo_xv_image;
#endif

  XVisualInfo vo_visualinfo;

  XImage *vo_ximage;
  int vo_depth;
  int vo_pix_fmt;

  XShmSegmentInfo vo_shm;

  // Dimension in window
  int vo_x, vo_y, vo_w, vo_h;

  struct SwsContext *vo_scaler;

  struct x11_screensaver_state *vo_xss;

  int vo_repaint;

} video_output_t;


/**
 *
 */
static void
vo_set_source(void *opaque, const char *url)
{
  video_output_t *vo = opaque;
  event_t *e;

  if(url == NULL)
    return;

  e = event_create_playurl(.url = url,
                           .primary = 1);

  mp_enqueue_event(vo->vo_mp, e);
  event_release(e);
}

#if ENABLE_LIBXV
/**
 *
 */
static int xv_video_frame_deliver(const frame_info_t *fi, void *opaque);

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
static int xi_video_frame_deliver(const frame_info_t *fi, void *opaque);

/**
 *
 */
static int
init_with_ximage(video_output_t *vo)
{
  XWindowAttributes attr;

  if(!XGetWindowAttributes(vo->vo_dpy, vo->vo_win, &attr)) {
    TRACE(TRACE_INFO, "X11", "Ximage: Unable to query window attributes");
    return 0;
  }

  if(!XMatchVisualInfo(vo->vo_dpy, DefaultScreen(vo->vo_dpy), attr.depth,
		       TrueColor, &vo->vo_visualinfo)) {
    TRACE(TRACE_INFO, "X11", "Ximage: Unable to find visual");
    return 0;
  }

  vo->vo_depth = attr.depth;

  TRACE(TRACE_DEBUG, "X11", "Ximage: Using visual 0x%lx (%d bpp)", 
	vo->vo_visualinfo.visualid, vo->vo_depth);

  return 1;
}


/**
 *
 */
struct video_output *
x11_vo_create(Display *dpy, int win, prop_courier_t *pc, prop_t *self,
	      char *errbuf, size_t errlen)
{
  XGCValues xgcv;
  video_output_t *vo;
  video_frame_deliver_t *deliver_fn;
  
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
    if(init_with_ximage(vo)) {
    
    deliver_fn = xi_video_frame_deliver;
    
  } else {
    free(vo);
    snprintf(errbuf, errlen, "No suitable video display methods available");
    return NULL;
  }

  vo->vo_gc = XCreateGC(vo->vo_dpy, vo->vo_win, 0, &xgcv);

  vo->vo_mp = mp_create("Video decoder", MP_VIDEO | MP_PRIMABLE);
  vo->vo_mp->mp_video_frame_deliver = deliver_fn;
  vo->vo_mp->mp_video_frame_opaque = vo;

  vo->vo_vd = video_decoder_create(vo->vo_mp);
  video_playback_create(vo->vo_mp);

  prop_link(vo->vo_mp->mp_prop_root, prop_create(self, "media"));

  vo->vo_sub_source = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "source"),
		   PROP_TAG_CALLBACK_STRING, vo_set_source, vo,
		   PROP_TAG_COURIER, pc, 
		   PROP_TAG_NAMED_ROOT, self, "self",
		   NULL);

  vo->vo_xss = x11_screensaver_suspend(vo->vo_dpy);
  return vo;
}


/**
 *
 */
void
x11_vo_destroy(struct video_output *vo)
{
  if(vo->vo_xss != NULL)
    x11_screensaver_resume(vo->vo_xss);

  prop_unsubscribe(vo->vo_sub_source);

  
  video_playback_destroy(vo->vo_mp);
  video_decoder_stop(vo->vo_vd);
  mp_destroy(vo->vo_mp);
  video_decoder_destroy(vo->vo_vd);

  if(vo->vo_shm.shmaddr != NULL)
    shmdt(vo->vo_shm.shmaddr);

  if(vo->vo_shm.shmid)
    XShmDetach(vo->vo_dpy, &vo->vo_shm);

#if ENABLE_LIBXV
  if(vo->vo_xv_port)
    XvUngrabPort(vo->vo_dpy, vo->vo_xv_port, CurrentTime);

  if(vo->vo_xv_image)
    XFree(vo->vo_xv_image);
#endif

  if(vo->vo_ximage)
    XFree(vo->vo_ximage);

  XFreeGC(vo->vo_dpy, vo->vo_gc);

  if(vo->vo_scaler)
    sws_freeContext(vo->vo_scaler);

  free(vo);
}


/**
 *
 */
void
x11_vo_dimension(struct video_output *vo, int x, int y, int w, int h)
{
  vo->vo_x = x;
  vo->vo_y = y;
  vo->vo_w = w;
  vo->vo_h = h;
  vo->vo_repaint = 1;

  mp_send_cmd_u32(vo->vo_mp, &vo->vo_mp->mp_video, MB_CTRL_REQ_OUTPUT_SIZE,
		  (w << 16) | h);
}

/**
 *
 */
void
x11_vo_exposed(struct video_output *vo)
{
  vo->vo_repaint = 1;
}


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
  rt = arch_get_avtime();
  aclock = mp->mp_audio_clock + rt - mp->mp_audio_clock_avtime;
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


/**
 *
 */
static void
compute_output_dimensions(video_output_t *vo, int dar_num, int dar_den,
			  int *w, int *h)
{
  float a = (float)(vo->vo_w * dar_den) / (float)(vo->vo_h * dar_num);

  if(a > 1) {
    *w = vo->vo_w / a;
    *h = vo->vo_h;
  } else {
    *w = vo->vo_w;
    *h = vo->vo_h * a;
  }
}


#if ENABLE_LIBXV
/**
 *
 */
static int
xv_video_frame_deliver(const frame_info_t *fi, void *opaque)
{
  video_output_t *vo = opaque;
  int syncok;
  int outw, outh;

  if(fi->fi_type != 'YUVP')
    return 1;

  if(vo->vo_w < 1 || vo->vo_h < 1 || fi == NULL)
    return 0;

  if(vo->vo_xv_image == NULL) {
    //    uint32_t xv_format = 0x32595559; // YV12
    //    uint32_t xv_format = 0x32315659; // YV12
    uint32_t xv_format = 0x30323449;

    vo->vo_xv_image = XvShmCreateImage(vo->vo_dpy, vo->vo_xv_port,
				       xv_format, NULL,
				       fi->fi_width, fi->fi_height,
				       &vo->vo_shm);

    vo->vo_shm.shmid = shmget(IPC_PRIVATE, vo->vo_xv_image->data_size, 
			      IPC_CREAT | 0777);

    vo->vo_shm.shmaddr = (char *)shmat(vo->vo_shm.shmid, 0, 0);
    vo->vo_shm.readOnly = False;

    vo->vo_xv_image->data = vo->vo_shm.shmaddr;

    XShmAttach(vo->vo_dpy, &vo->vo_shm);

    XSync(vo->vo_dpy, False);
    shmctl(vo->vo_shm.shmid, IPC_RMID, 0);
    vo->vo_repaint = 1;
  }
  
  if(vo->vo_repaint) {
    vo->vo_repaint = 0;
    XFillRectangle(vo->vo_dpy, vo->vo_win, vo->vo_gc, 0, 0, vo->vo_w, vo->vo_h);
  }

  int i;
  for(i = 0; i < 3; i++) {
    int w = fi->fi_width;
    int h = fi->fi_height;

    if(i) {
      w = w / 2;
      h = h / 2;
    }

    const uint8_t *src = fi->fi_data[i];
    char *dst = vo->vo_xv_image->data + vo->vo_xv_image->offsets[i];
    int pitch = vo->vo_xv_image->pitches[i];
    
    while(h--) {
      memcpy(dst, src, w);
      dst += pitch;
      src += fi->fi_pitch[i];
    }
  }

  compute_output_dimensions(vo, fi->fi_dar_num, fi->fi_dar_den, &outw, &outh);

  syncok = wait_for_aclock(vo->vo_mp, fi->fi_pts, fi->fi_epoch);

  XvShmPutImage(vo->vo_dpy, vo->vo_xv_port, vo->vo_win, vo->vo_gc,
		vo->vo_xv_image, 0, 0, fi->fi_width, fi->fi_height,
		vo->vo_x + (vo->vo_w - outw) / 2,
		vo->vo_y + (vo->vo_h - outh) / 2,
		outw, outh,
		False);

  XFlush(vo->vo_dpy);
  XSync(vo->vo_dpy, False);

  if(!syncok)
    usleep(fi->fi_duration);
  return 0;
}
#endif

const static struct {
  int rm, gm, bm, depth, pixfmt;
} visual2pixfmt[] = {
  {0x00ff0000, 0x0000ff00, 0x000000ff, 24, PIX_FMT_RGB32},
  {0x0000f800, 0x000007e0, 0x0000001f, 16, PIX_FMT_RGB565},
};

/**
 *
 */
static int
get_pix_fmt(video_output_t *vo)
{
  int rm = vo->vo_visualinfo.red_mask;
  int gm = vo->vo_visualinfo.green_mask;
  int bm = vo->vo_visualinfo.blue_mask;
  int i;

  for(i = 0; i < sizeof(visual2pixfmt) / sizeof(visual2pixfmt[0]); i++) {
    if(visual2pixfmt[i].rm == rm &&
       visual2pixfmt[i].gm == gm &&
       visual2pixfmt[i].bm == bm &&
       visual2pixfmt[i].depth == vo->vo_depth) {
      return visual2pixfmt[i].pixfmt;
    }
  }
  return -1;
}


/**
 *
 */
static int
xi_video_frame_deliver(const frame_info_t *fi, void *opaque)
{
  video_output_t *vo = opaque;
  uint8_t *dst[4] = {0,0,0,0};
  int dstpitch[4] = {0,0,0,0};
  int syncok;
  int outw, outh;

  if(fi->fi_type != 'YUVP')
    return 1;

  if(vo->vo_w < 1 || vo->vo_h < 1)
    return 0;

  if(fi->fi_prescaled) {
    outw = fi->fi_width;
    outh = fi->fi_height;
  } else {
    compute_output_dimensions(vo, fi->fi_dar_num, fi->fi_dar_den, &outw, &outh);
  }

  if(vo->vo_ximage != NULL && 
     (vo->vo_ximage->width  != outw ||
      vo->vo_ximage->height != outh)) {

    XShmDetach(vo->vo_dpy, &vo->vo_shm);
    shmdt(vo->vo_shm.shmaddr);

    XFree(vo->vo_ximage);
    vo->vo_ximage = NULL;
  }


  if(vo->vo_ximage == NULL) {
   
    vo->vo_ximage = XShmCreateImage(vo->vo_dpy, vo->vo_visualinfo.visual,
				    vo->vo_depth, ZPixmap, NULL,
				    &vo->vo_shm, outw, outh);

    if(vo->vo_ximage == NULL)
      return 0;

    vo->vo_shm.shmid = shmget(IPC_PRIVATE, 
			      vo->vo_ximage->bytes_per_line * 
			      vo->vo_ximage->height,
			      IPC_CREAT | 0777);

    vo->vo_shm.shmaddr = (char *)shmat(vo->vo_shm.shmid, 0, 0);
    vo->vo_shm.readOnly = False;

    vo->vo_ximage->data = vo->vo_shm.shmaddr;

    XShmAttach(vo->vo_dpy, &vo->vo_shm);

    XSync(vo->vo_dpy, False);
    shmctl(vo->vo_shm.shmid, IPC_RMID, 0);

    if((vo->vo_pix_fmt = get_pix_fmt(vo)) == -1)
      TRACE(TRACE_ERROR, "X11", 
	    "No pixel format for visual: %08lx %08lx %08lx %dbpp",
	    vo->vo_visualinfo.red_mask,
	    vo->vo_visualinfo.green_mask,
	    vo->vo_visualinfo.blue_mask,
	    vo->vo_depth);

    vo->vo_repaint = 1;
  }

  if(vo->vo_repaint) {
    vo->vo_repaint = 0;
    XFillRectangle(vo->vo_dpy, vo->vo_win, vo->vo_gc, 0, 0, vo->vo_w, vo->vo_h);
  }

  if(vo->vo_pix_fmt == -1)
    return 0;

  if(vo->vo_ximage->width          == fi->fi_width &&
     vo->vo_ximage->height         == fi->fi_height &&
     vo->vo_pix_fmt                == fi->fi_pix_fmt &&
     vo->vo_ximage->bytes_per_line == fi->fi_pitch[0]) {

    memcpy(vo->vo_ximage->data, fi->fi_data[0], 
	   vo->vo_ximage->bytes_per_line * vo->vo_ximage->height);

  } else {

    // Must do scaling and/or format conversion

    vo->vo_scaler = sws_getCachedContext(vo->vo_scaler,
					 fi->fi_width, fi->fi_height,
					 fi->fi_pix_fmt,
					 vo->vo_ximage->width,
					 vo->vo_ximage->height,
					 vo->vo_pix_fmt,
					 SWS_BICUBIC, NULL, NULL, NULL);
    
    dst[0] = (uint8_t *)vo->vo_ximage->data;
    dstpitch[0] = vo->vo_ximage->bytes_per_line;
    
    sws_scale(vo->vo_scaler, (void *)fi->fi_data, fi->fi_pitch, 0,
	      fi->fi_height, dst, dstpitch);
  }


  syncok = wait_for_aclock(vo->vo_mp, fi->fi_pts, fi->fi_epoch);

  XShmPutImage(vo->vo_dpy, vo->vo_win, vo->vo_gc, vo->vo_ximage,
	       0, 0,
	       vo->vo_x + (vo->vo_w - outw) / 2,
	       vo->vo_y + (vo->vo_h - outh) / 2,
	       outw, outh,
	       True);

  XFlush(vo->vo_dpy);
  XSync(vo->vo_dpy, False);

  if(!syncok)
    usleep(fi->fi_duration);

  return 0;
}
