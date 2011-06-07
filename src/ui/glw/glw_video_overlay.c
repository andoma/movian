/*
 *  Video output
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
#include "glw_video_common.h"
#include "glw_video_overlay.h"
#include "glw_texture.h"
#include "video/video_playback.h"
#include "video/video_overlay.h"



/**
 *
 */
static glw_video_overlay_t *
gvo_create(glw_video_t *gv, int64_t pts)
{
  glw_video_overlay_t *gvo = calloc(1, sizeof(glw_video_overlay_t));
  LIST_INSERT_HEAD(&gv->gv_overlays, gvo, gvo_link);
  gvo->gvo_start = pts;
  gvo->gvo_stop = AV_NOPTS_VALUE;
  gvo->gvo_alpha = 1;
  return gvo;
}


/**
 *
 */
static void
gvo_destroy(glw_video_t *gv, glw_video_overlay_t *gvo)
{
  LIST_REMOVE(gvo, gvo_link);
  glw_tex_destroy(gv->w.glw_root, &gvo->gvo_texture);
  glw_renderer_free(&gvo->gvo_renderer);
  free(gvo);
}


/**
 *
 */
static void
gvo_flush_all(glw_video_t *gv)
{
  glw_video_overlay_t *gvo;

  while((gvo = LIST_FIRST(&gv->gv_overlays)) != NULL)
    gvo_destroy(gv, gvo);
}


/**
 * Destroy all overlays without an end time
 */
static void
gvo_flush_infinite(glw_video_t *gv)
{
  glw_video_overlay_t *gvo, *next;

  for(gvo = LIST_FIRST(&gv->gv_overlays); gvo != NULL; gvo = next) {
    next = LIST_NEXT(gvo, gvo_link);

    if(gvo->gvo_stop == AV_NOPTS_VALUE)
      gvo_destroy(gv, gvo);
  }
}


/**
 *
 */
static void
gvo_update(glw_video_t *gv, int64_t pts)
{
  glw_video_overlay_t *gvo, *next;
  float a;

  for(gvo = LIST_FIRST(&gv->gv_overlays); gvo != NULL; gvo = next) {
    next = LIST_NEXT(gvo, gvo_link);

    if(gvo->gvo_stop != AV_NOPTS_VALUE && gvo->gvo_stop <= pts) {
      gvo_destroy(gv, gvo);
      continue;
    }
    
    if(gvo->gvo_fadein) {
      a = GLW_RESCALE((double)pts, gvo->gvo_start,
		      gvo->gvo_start + gvo->gvo_fadein);
      if(a > 0.99) {
	a = 1;
	gvo->gvo_fadein = 0;
      }
    } else if(gvo->gvo_fadeout) {
      a = GLW_RESCALE((double)pts, gvo->gvo_stop,
		      gvo->gvo_stop - gvo->gvo_fadeout);
    } else {
      a = 1;
    }
    gvo->gvo_alpha = GLW_CLAMP(a, 0.0, 1.0);
  }
}


/**
 * 
 */
void
glw_video_overlay_render(glw_video_t *gv, glw_rctx_t *rc, glw_rctx_t *vrc)
{
  glw_video_overlay_t *gvo;
  glw_root_t *gr = gv->w.glw_root;
  int show_dvd_overlays = 0;
  glw_rctx_t rc0;

#if ENABLE_DVD
  video_decoder_t *vd = gv->gv_vd;
  if(gv->gv_cfg_cur.gvc_width[0] > 0 &&
     (glw_is_focused(&gv->w) || !vd->vd_pci.hli.hl_gi.hli_ss))
    show_dvd_overlays = 1;
#endif
  
  LIST_FOREACH(gvo, &gv->gv_overlays, gvo_link) {
    if(gvo->gvo_is_dvd && !show_dvd_overlays)
      continue;

    rc0 = *rc;
    
    if(gvo->gvo_alignment != 0) {
      

      int left   =                 gvo->gvo_padding_left;
      int top    = rc->rc_height - gvo->gvo_padding_top;
      int right  = rc->rc_width  - gvo->gvo_padding_right;
      int bottom =                 gvo->gvo_padding_bottom;
    
      int width  = gvo->gvo_width;
      int height = gvo->gvo_height;
    
      float x1, y1, x2, y2;

      // Horizontal 
      if(width > right - left) {
	// Oversized, must cut
	width = right - left;
      } else { 
	switch(gvo->gvo_alignment) {
	case 2:
	case 5:
	case 8:
	  left = (left + right - width) / 2;
	  right = left + width;
	  break;

	case 1:
	case 4:
	case 7:
	  right = left + gvo->gvo_width;
	  break;

	case 3:
	case 6:
	case 9:
	  left = right - gvo->gvo_width;
	  break;
	}
      }
      
      // Vertical 
      if(height > top - bottom) {
	// Oversized, must cut
	height = top - bottom;
      } else { 
	switch(gvo->gvo_alignment) {
	case 4 ... 6:
	  bottom = (bottom + top - height) / 2;
	  top = bottom + height;
	  break;

	case 7 ... 9:
	  bottom = top - gvo->gvo_height;
	  break;

	case 1 ... 3:
	  top = bottom + gvo->gvo_height;
	  break;
	}
      }
      
      x1 = -1.0f + 2.0f * left   / (float)rc->rc_width;
      y1 = -1.0f + 2.0f * bottom / (float)rc->rc_height;
      x2 = -1.0f + 2.0f * right  / (float)rc->rc_width;
      y2 = -1.0f + 2.0f * top    / (float)rc->rc_height;

      glw_renderer_vtx_pos(&gvo->gvo_renderer, 0, x1, y1, 0.0);
      glw_renderer_vtx_pos(&gvo->gvo_renderer, 1, x2, y1, 0.0);
      glw_renderer_vtx_pos(&gvo->gvo_renderer, 2, x2, y2, 0.0);
      glw_renderer_vtx_pos(&gvo->gvo_renderer, 3, x1, y2, 0.0);

      rc0 = *rc;
      
    } else {
      float ys = gv->gv_cfg_cur.gvc_flags & GVC_YHALF ? 2 : 1;

      rc0 = *vrc;
      glw_Scalef(&rc0, 
		 2.0f / gv->gv_cfg_cur.gvc_width[0], 
		 -2.0f / (ys * gv->gv_cfg_cur.gvc_height[0]), 
		 0.0f);
      
      glw_Translatef(&rc0, 
		     -gv->gv_cfg_cur.gvc_width[0]  / 2,
		     (ys * -gv->gv_cfg_cur.gvc_height[0]) / 2, 
		     0.0f);
    }

    glw_renderer_draw(&gvo->gvo_renderer, gr, &rc0,
		      &gvo->gvo_texture, NULL, NULL, 
		      gvo->gvo_alpha * rc->rc_alpha);
  }
}


/**
 *
 */
int
glw_video_overlay_pointer_event(video_decoder_t *vd, int width, int height,
				glw_pointer_event_t *gpe, media_pipe_t *mp)
{
#if ENABLE_DVD
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
  event_release(e);
#endif
  return 1;
}


#if ENABLE_DVD
/**
 *
 */
static void
spu_repaint(glw_video_t *gv, dvdspu_t *d)
{
  video_decoder_t *vd = gv->gv_vd;
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

  gvo_flush_all(gv);

  
  glw_video_overlay_t *gvo = gvo_create(gv, AV_NOPTS_VALUE);
  gvo->gvo_is_dvd = 1;
  glw_root_t *gr = gv->w.glw_root;

  glw_renderer_init_quad(&gvo->gvo_renderer);

  float w = gr->gr_normalized_texture_coords ? 1.0 : width;
  float h = gr->gr_normalized_texture_coords ? 1.0 : height;
  glw_renderer_t *r = &gvo->gvo_renderer;
  
  glw_renderer_vtx_pos(r, 0, d->d_x1, d->d_y2, 0.0f);
  glw_renderer_vtx_st (r, 0, 0, h);
  
  glw_renderer_vtx_pos(r, 1, d->d_x2, d->d_y2, 0.0f);
  glw_renderer_vtx_st (r, 1, w, h);
  
  glw_renderer_vtx_pos(r, 2, d->d_x2, d->d_y1, 0.0f);
  glw_renderer_vtx_st (r, 2, w, 0);
  
  glw_renderer_vtx_pos(r, 3, d->d_x1, d->d_y1, 0.0f);
  glw_renderer_vtx_st (r, 3, 0, 0);

  glw_tex_upload(gr, &gvo->gvo_texture, t0, GLW_TEXTURE_FORMAT_BGR32,
		 width, height, 0);
  free(t0);
}



/**
 *
 */
static void
glw_video_overlay_spu_layout(glw_video_t *gv, int64_t pts)
{
  video_decoder_t *vd = gv->gv_vd;
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
    gvo_flush_all(gv);
    goto again;

  case 0:
    if(vd->vd_spu_repaint == 0)
      break;

    vd->vd_spu_repaint = 0;
    /* FALLTHRU */

  case 1:
    spu_repaint(gv, d);
    break;
  }
  hts_mutex_unlock(&vd->vd_spu_mutex);
}
#endif


/**
 *
 */
static void
gvo_create_from_vo(glw_video_t *gv, video_overlay_t *vo)
{
  glw_video_overlay_t *gvo = gvo_create(gv, vo->vo_start);
  glw_root_t *gr = gv->w.glw_root;
  int fmt;

  pixmap_t *pm = vo->vo_pixmap;
  int W = pm->pm_width;
  int H = pm->pm_height;

  gvo->gvo_stop = vo->vo_stop;
  gvo->gvo_fadein = vo->vo_fadein;
  gvo->gvo_fadeout = vo->vo_fadeout;

  glw_renderer_init_quad(&gvo->gvo_renderer);

  float w = gr->gr_normalized_texture_coords ? 1.0 : W;
  float h = gr->gr_normalized_texture_coords ? 1.0 : H;
    
  glw_renderer_t *r = &gvo->gvo_renderer;
    
  glw_renderer_vtx_st (r, 0, 0, h);
  glw_renderer_vtx_st (r, 1, w, h);
  glw_renderer_vtx_st (r, 2, w, 0);
  glw_renderer_vtx_st (r, 3, 0, 0);

  gvo->gvo_alignment = vo->vo_alignment;

  if(vo->vo_alignment == 0) {
    glw_renderer_vtx_pos(r, 0, vo->vo_x,     vo->vo_y + H, 0.0f);
    glw_renderer_vtx_pos(r, 1, vo->vo_x + W, vo->vo_y + H, 0.0f);
    glw_renderer_vtx_pos(r, 2, vo->vo_x + W, vo->vo_y,     0.0f);
    glw_renderer_vtx_pos(r, 3, vo->vo_x,     vo->vo_y,     0.0f);
  } else {
    gvo->gvo_padding_left   = vo->vo_padding_left;
    gvo->gvo_padding_top    = vo->vo_padding_top;
    gvo->gvo_padding_right  = vo->vo_padding_right;
    gvo->gvo_padding_bottom = vo->vo_padding_bottom;
    gvo->gvo_width  = pm->pm_width;
    gvo->gvo_height = pm->pm_height;
  }

  switch(pm->pm_pixfmt) {
  case PIX_FMT_Y400A:
    fmt = GLW_TEXTURE_FORMAT_I8A8;
    break;

  case PIX_FMT_BGR32:
    fmt = GLW_TEXTURE_FORMAT_BGR32;
    break;

  default:
    return;
  }

  glw_tex_upload(gr, &gvo->gvo_texture, pm->pm_pixels, fmt, W, H, 0);
}


/**
 *
 */
static void
glw_video_overlay_sub_layout(glw_video_t *gv, int64_t pts)
{
  video_decoder_t *vd = gv->gv_vd;
  video_overlay_t *vo;

  hts_mutex_lock(&vd->vd_overlay_mutex);

  while((vo = TAILQ_FIRST(&vd->vd_overlay_queue)) != NULL) {
    switch(vo->vo_type) {
    case VO_TIMED_FLUSH:
      if(vo->vo_start > pts)
	break;
      // FALLTHRU
    case VO_FLUSH:
      gvo_flush_all(gv);
      video_overlay_destroy(vd, vo);
      continue;

    case VO_BITMAP:
      if(vo->vo_start > pts)
	break;
      gvo_flush_infinite(gv);
      if(vo->vo_pixmap != NULL)
	gvo_create_from_vo(gv, vo);
      video_overlay_destroy(vd, vo);
      continue;
    }
    break;
  }
  hts_mutex_unlock(&vd->vd_overlay_mutex);
  gvo_update(gv, pts);
}


/**
 *
 */
void
glw_video_overlay_layout(glw_video_t *gv, int64_t pts)
{
  const video_decoder_t *vd = gv->gv_vd;
  int want_focus = 0;

#if ENABLE_DVD
  glw_video_overlay_spu_layout(gv, pts);
#endif
  pts -= vd->vd_mp->mp_svdelta;

  glw_video_overlay_sub_layout(gv, pts);


#if ENABLE_DVD
  if(vd->vd_pci.hli.hl_gi.hli_ss)
    want_focus = 1;
#endif
  glw_set_focus_weight(&gv->w, want_focus ? 1.0 : 0.0);
}


/**
 *
 */
void
glw_video_overlay_deinit(glw_video_t *gv)
{
  gvo_flush_all(gv);
}
