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
#include "glw_texture.h"
#include "video/video_playback.h"

#if ENABLE_DVD
/**
 *
 */
int
glw_video_pointer_event(video_decoder_t *vd, int width, int height,
			glw_pointer_event_t *gpe, media_pipe_t *mp)
{
  pci_t *pci;
  int x, y;
  int32_t button, best, dist, d, mx, my, dx, dy;
  event_t *e;

  pci = &vd->vd_pci;
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
  case GLW_POINTER_LEFT_PRESS:
    e = event_create(EVENT_DVD_ACTIVATE_BUTTON, sizeof(event_t) + 1);
    break;

  case GLW_POINTER_MOTION_UPDATE:
    if(vd->vd_spu_curbut == best)
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
  if(vd->vd_pci.hli.hl_gi.hli_ss)
    want_focus = 1;
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


/**
 * 
 */
void
glw_video_purge_queues(video_decoder_t *vd,
		       void (*purge)(video_decoder_t *vd,
				     video_decoder_frame_t *vdf))
{
  video_decoder_frame_t *vdf;

  while((vdf = TAILQ_FIRST(&vd->vd_avail_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_avail_queue, vdf, vdf_link);
    purge(vd, vdf);
  }

  while((vdf = TAILQ_FIRST(&vd->vd_bufalloc_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_bufalloc_queue, vdf, vdf_link);
    purge(vd, vdf);
  }

  while((vdf = TAILQ_FIRST(&vd->vd_bufalloced_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_bufalloced_queue, vdf, vdf_link);
    purge(vd, vdf);
  }

  while((vdf = TAILQ_FIRST(&vd->vd_displaying_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_displaying_queue, vdf, vdf_link);
    purge(vd, vdf);
  }

  while((vdf = TAILQ_FIRST(&vd->vd_display_queue)) != NULL) {
    TAILQ_REMOVE(&vd->vd_display_queue, vdf, vdf_link);
    purge(vd, vdf);
  }
}


/**
 * 
 */
void
gvo_deinit(glw_video_overlay_t *gvo)
{
  int i;
  for(i = 0; i < gvo->gvo_entries; i++) {
    glw_tex_destroy(&gvo->gvo_textures[i]);
    glw_render_free(&gvo->gvo_renderers[i]);
  }
  free(gvo->gvo_textures);
  free(gvo->gvo_renderers);
  gvo->gvo_textures = NULL;
  gvo->gvo_renderers = NULL;
  gvo->gvo_entries = 0;

  if(gvo->gvo_child != NULL) {
    glw_destroy(gvo->gvo_child);
    gvo->gvo_child = NULL;
  }
}

/**
 * 
 */
static int
gvo_setup_bitmap(glw_video_overlay_t *gvo, int entries)
{
  if(gvo->gvo_entries == entries)
    return 0;
  gvo_deinit(gvo);

  gvo->gvo_entries = entries;
  gvo->gvo_textures  = calloc(entries, sizeof(glw_backend_texture_t));
  gvo->gvo_renderers = calloc(entries, sizeof(glw_renderer_t));
  return 1;
}


/**
 * 
 */
void
gvo_render(glw_video_overlay_t *gvo, glw_root_t *gr, glw_rctx_t *rc)
{
  int i;

  for(i = 0; i < gvo->gvo_entries; i++) {
    glw_render(&gvo->gvo_renderers[i], gr, rc,
	       GLW_RENDER_MODE_QUADS, GLW_RENDER_ATTRIBS_TEX, 
	       &gvo->gvo_textures[i], 1, 1, 1, rc->rc_alpha);
  }
}


#if ENABLE_DVD
/**
 *
 */
static void
spu_repaint(glw_video_overlay_t *gvo, video_decoder_t *vd, dvdspu_t *d,
	    const glw_root_t *gr)
{
  int width  = d->d_x2 - d->d_x1;
  int height = d->d_y2 - d->d_y1;
  int outsize = width * height * 4;
  uint32_t *tmp, *t0; 
  int x, y, i;
  uint8_t *buf = d->d_bitmap;
  pci_t *pci = &vd->vd_pci;
  dvdnav_highlight_area_t ha;
  int hi_palette[4];
  int hi_alpha[4];

  if(vd->vd_spu_clut == NULL)
    return;
  
  vd->vd_spu_in_menu = pci->hli.hl_gi.hli_ss;

  if(pci->hli.hl_gi.hli_ss &&
     dvdnav_get_highlight_area(pci, vd->vd_spu_curbut, 0, &ha) 
     == DVDNAV_STATUS_OK) {

    hi_alpha[0] = (ha.palette >>  0) & 0xf;
    hi_alpha[1] = (ha.palette >>  4) & 0xf;
    hi_alpha[2] = (ha.palette >>  8) & 0xf;
    hi_alpha[3] = (ha.palette >> 12) & 0xf;
     
    hi_palette[0] = (ha.palette >> 16) & 0xf;
    hi_palette[1] = (ha.palette >> 20) & 0xf;
    hi_palette[2] = (ha.palette >> 24) & 0xf;
    hi_palette[3] = (ha.palette >> 28) & 0xf;
  }

  t0 = tmp = malloc(outsize);


  ha.sx -= d->d_x1;
  ha.ex -= d->d_x1;

  ha.sy -= d->d_y1;
  ha.ey -= d->d_y1;

  /* XXX: this can be optimized in many ways */

  for(y = 0; y < height; y++) {
    for(x = 0; x < width; x++) {
      i = buf[0];

      if(pci->hli.hl_gi.hli_ss &&
	 x >= ha.sx && y >= ha.sy && x <= ha.ex && y <= ha.ey) {

	if(hi_alpha[i] == 0) {
	  *tmp = 0;
	} else {
	  *tmp = vd->vd_spu_clut[hi_palette[i] & 0xf] | 
	    ((hi_alpha[i] * 0x11) << 24);
	}

      } else {

	if(d->d_alpha[i] == 0) {
	  
	  /* If it's 100% transparent, write RGB as zero too, or weird
	     aliasing effect will occure when GL scales texture */
	  
	  *tmp = 0;
	} else {
	  *tmp = vd->vd_spu_clut[d->d_palette[i] & 0xf] | 
	    ((d->d_alpha[i] * 0x11) << 24);
	}
      }

      buf++;
      tmp++;
    }
  }

  if(gvo_setup_bitmap(gvo, 1))
    glw_render_init(&gvo->gvo_renderers[0], 4, GLW_RENDER_ATTRIBS_TEX);

  float w = gr->gr_normalized_texture_coords ? 1.0 : width;
  float h = gr->gr_normalized_texture_coords ? 1.0 : height;
  glw_renderer_t *r = &gvo->gvo_renderers[0];
  
  glw_render_vtx_pos(r, 0, d->d_x1, d->d_y2, 0.0f);
  glw_render_vtx_st (r, 0, 0, h);
  
  glw_render_vtx_pos(r, 1, d->d_x2, d->d_y2, 0.0f);
  glw_render_vtx_st (r, 1, w, h);
  
  glw_render_vtx_pos(r, 2, d->d_x2, d->d_y1, 0.0f);
  glw_render_vtx_st (r, 2, w, 0);
  
  glw_render_vtx_pos(r, 3, d->d_x1, d->d_y1, 0.0f);
  glw_render_vtx_st (r, 3, 0, 0);

  glw_tex_upload(gr, &gvo->gvo_textures[0], t0, GLW_TEXTURE_FORMAT_RGBA,
		 width, height, 0);
  free(t0);
}



/**
 *
 */
void
glw_video_spu_layout(video_decoder_t *vd, glw_video_overlay_t *gvo, 
		     const glw_root_t *gr, int64_t pts)
{
  dvdspu_t *d;
  int x;

  hts_mutex_lock(&vd->vd_spu_mutex);

 again:
  d = TAILQ_FIRST(&vd->vd_spu_queue);

  if(d == NULL) {
    hts_mutex_unlock(&vd->vd_spu_mutex);
    return;
  }

  if(d->d_destroyme == 1)
    goto destroy;

  x = dvdspu_decode(d, pts);

  switch(x) {
  case -1:
  destroy:
    dvdspu_destroy(vd, d);
    vd->vd_spu_in_menu = 0;
    gvo_deinit(gvo);
    goto again;

  case 0:
    if(vd->vd_spu_repaint == 0)
      break;

    vd->vd_spu_repaint = 0;
    /* FALLTHRU */

  case 1:
    spu_repaint(gvo, vd, d, gr);
    break;
  }
  hts_mutex_unlock(&vd->vd_spu_mutex);
}

#endif


/**
 *
 */
static void
glw_video_sub_layout_bitmaps(video_decoder_t *vd, glw_video_overlay_t *gvo, 
			     const glw_root_t *gr, subtitle_t *s)
{
  int i;
  if(gvo_setup_bitmap(gvo, s->s_num_rects))
    for(i = 0; i < s->s_num_rects; i++)
      glw_render_init(&gvo->gvo_renderers[i], 4, GLW_RENDER_ATTRIBS_TEX);
  
  for(i = 0; i < s->s_num_rects; i++) {
    subtitle_rect_t *sr = &s->s_rects[i];
    
    float w = gr->gr_normalized_texture_coords ? 1.0 : sr->w;
    float h = gr->gr_normalized_texture_coords ? 1.0 : sr->h;
    
    glw_renderer_t *r = &gvo->gvo_renderers[i];
    
    glw_render_vtx_pos(r, 0, sr->x,         sr->y + sr->h, 0.0f);
    glw_render_vtx_st (r, 0, 0, h);
    
    glw_render_vtx_pos(r, 1, sr->x + sr->w, sr->y + sr->h, 0.0f);
    glw_render_vtx_st (r, 1, w, h);
    
    glw_render_vtx_pos(r, 2, sr->x + sr->w, sr->y,         0.0f);
    glw_render_vtx_st (r, 2, w, 0);
    
    glw_render_vtx_pos(r, 3, sr->x,         sr->y,         0.0f);
    glw_render_vtx_st (r, 3, 0, 0);

    glw_tex_upload(gr, &gvo->gvo_textures[i], sr->bitmap,
		   GLW_TEXTURE_FORMAT_RGBA, sr->w, sr->h, 0);
  }
}


/**
 *
 */
static void
glw_video_sub_layout_text(video_decoder_t *vd, glw_video_overlay_t *gvo, 
			  glw_root_t *gr, subtitle_t *s, glw_t *parent)
{
  if(gvo->gvo_child != NULL)
    glw_destroy(gvo->gvo_child);

  gvo->gvo_child = glw_create_i(gr, 
				glw_class_find_by_name("label"),
				GLW_ATTRIB_PARENT, parent,
				GLW_ATTRIB_CAPTION, s->s_text,
				GLW_ATTRIB_ALIGNMENT, GLW_ALIGN_BOTTOM,
				GLW_ATTRIB_SIZE_SCALE, 2.0,
				GLW_ATTRIB_PADDING, 0.0, 0.0, 0.0, 20.0,
				NULL);
}


/**
 *
 */
void
glw_video_sub_layout(video_decoder_t *vd, glw_video_overlay_t *gvo, 
		     glw_root_t *gr, int64_t pts, glw_t *parent)
{
  subtitle_t *s;

  hts_mutex_lock(&vd->vd_sub_mutex);
  if((s = TAILQ_FIRST(&vd->vd_sub_queue)) != NULL && s->s_start <= pts) {
    
    if(!s->s_active) {
      s->s_active = 1;

      if(s->s_text != NULL)
	glw_video_sub_layout_text(vd, gvo, gr, s, parent);
      else
	glw_video_sub_layout_bitmaps(vd, gvo, gr, s);

    } else {
      
      subtitle_t *n = TAILQ_NEXT(s, s_link);
      if(s->s_stop <= pts || (n != NULL && n->s_start <= pts)) 
	video_subtitle_destroy(vd, s);
    }

  } else {
    gvo_deinit(gvo);
  }
  hts_mutex_unlock(&vd->vd_sub_mutex);
}



/**
 * We are going away, flush out all frames (PBOs and textures)
 * and destroy zombie video decoder 
 */
static void
glw_video_dtor(glw_t *w)
{
  glw_video_t *gv = (glw_video_t *)w;
  video_decoder_t *vd = gv->gv_vd;

  gvo_deinit(&gv->gv_spu);
  gvo_deinit(&gv->gv_sub);
  
  LIST_REMOVE(gv, gv_global_link);
  glw_video_purge_queues(vd, glw_video_framepurge);
  video_decoder_destroy(vd);
}


/**
 *
 */
static void
glw_video_newframe(glw_t *w)
{
  glw_video_t *gv = (glw_video_t *)w;
  video_decoder_t *vd = gv->gv_vd;

  glw_video_buffer_allocator(vd);
  glw_video_new_frame(vd, gv, w->glw_root);
  glw_video_update_focusable(vd, w);
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
  case GLW_SIGNAL_LAYOUT:
    if(gv->gv_sub.gvo_child != NULL)
      glw_layout0(gv->gv_sub.gvo_child, extra);
    return 0;

  case GLW_SIGNAL_EVENT:
    return glw_video_widget_event(extra, gv->gv_mp, vd->vd_spu_in_menu);

  case GLW_SIGNAL_DESTROY:
    video_playback_destroy(gv->gv_vp);
    video_decoder_stop(vd);
    mp_ref_dec(gv->gv_mp);
    gv->gv_mp = NULL;
    return 0;

#if ENABLE_DVD
  case GLW_SIGNAL_POINTER_EVENT:
    return glw_video_pointer_event(vd, gv->gv_width, gv->gv_height,
				   extra, gv->gv_mp);
#endif

  default:
    return 0;
  }
}


/**
 *
 */
static void
glw_video_set(glw_t *w, int init, va_list ap)
{
  glw_video_t *gv = (glw_video_t *)w;
  glw_root_t *gr = w->glw_root;
  glw_attribute_t attrib;
  const char *filename = NULL;
  prop_t *p, *p2;
  event_t *e;

  if(init) {

    gv->gv_mp = mp_create("Video decoder", "video", MP_VIDEO);

    LIST_INSERT_HEAD(&gr->gr_video_decoders, gv, gv_global_link);

    gv->gv_vd = video_decoder_create(gv->gv_mp, glw_video_frame_deliver, NULL);
    gv->gv_vp = video_playback_create(gv->gv_mp);

    // We like fullwindow mode if possible (should be confiurable perhaps)
    glw_set_constraints(w, 0, 0, 0, 0, GLW_CONSTRAINT_F, 0);
  }

  do {
    attrib = va_arg(ap, int);
    switch(attrib) {

    case GLW_ATTRIB_SOURCE:
      filename = va_arg(ap, char *);
      break;

    case GLW_ATTRIB_PROPROOTS:
      p = va_arg(ap, void *);

      p2 = prop_create(p, "media");
      
      prop_link(gv->gv_mp->mp_prop_root, p2);
      
      p = va_arg(ap, void *); // Parent, just throw it away
      break;

    default:
      GLW_ATTRIB_CHEW(attrib, ap);
      break;
    }
  } while(attrib);

  if(filename != NULL && filename[0] != 0) {
    e = event_create_url(EVENT_PLAY_URL, filename);
    mp_enqueue_event(gv->gv_mp, e);
    event_unref(e);
  }
}


/**
 *
 */
static glw_class_t glw_video = {
  .gc_name = "video",
  .gc_instance_size = sizeof(glw_video_t),
  .gc_set = glw_video_set,
  .gc_dtor = glw_video_dtor,
  .gc_render = glw_video_render,
  .gc_newframe = glw_video_newframe,
  .gc_signal_handler = glw_video_widget_callback,
};

GLW_REGISTER_CLASS(glw_video);
