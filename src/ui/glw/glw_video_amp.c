/*
 *  Copyright 2013 (C) Spotify AB
 */
#include "config.h"

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
#include "pipelines/amp/amp.h"


typedef struct amp_render {
  AMP_COMPONENT ar_vout;
  media_codec_t *ar_mc;
  glw_rect_t ar_current_pos;
  int ar_muted;
  int ar_vout_running;
  glw_video_t *ar_widget;
} amp_render_t;


static hts_mutex_t plane_mutex;

static amp_render_t *main_plane;


static const char *ar_name(amp_render_t *ar)
{
  return ar->ar_widget->w.glw_id ?: "???";
}


/**
 *
 */
static int
amp_create_vout(amp_render_t *ar, amp_extra_t *ae)
{
  HRESULT ret;
  AMP_COMPONENT_CONFIG amp_config;

  hts_mutex_lock(&plane_mutex);

  if(main_plane != NULL) {
    TRACE(TRACE_DEBUG, "AMP", "%s: Fail create vout owned by %s",
          ar_name(ar), ar_name(main_plane));
    hts_mutex_unlock(&plane_mutex);
    return 1;
  }

  main_plane = ar;


  TRACE(TRACE_DEBUG, "AMP", "%s: will create vout", ar_name(ar));

  AMP_RPC(ret,
          AMP_FACTORY_CreateComponent,
          amp_factory,
          AMP_COMPONENT_VOUT,
          0,
          &ar->ar_vout);
  assert(ret == SUCCESS);

  AmpMemClear(&amp_config, sizeof(AMP_COMPONENT_CONFIG));
  amp_config._d = AMP_COMPONENT_VOUT;
  amp_config._u.pVOUT.uiInputPortNum = 2;
  amp_config._u.pVOUT.uiOutputPortNum = 0;
  AMP_RPC(ret, AMP_VOUT_Open, ar->ar_vout, &amp_config);
  assert(ret == SUCCESS);

  ret = AMP_ConnectComp(ae->amp_clk, 0, ar->ar_vout, 1);
  assert(ret == SUCCESS);

  AMP_RPC(ret, AMP_VOUT_SetState, ar->ar_vout, AMP_EXECUTING);
  assert(ret == SUCCESS);

#if 1
  AMP_RPC(ret, AMP_CLK_SetState,  ae->amp_clk, AMP_EXECUTING);
  assert(ret == SUCCESS);
#endif
  TRACE(TRACE_DEBUG, "AMP", "%s: Vout created", ar_name(ar));

  AMP_RPC(ret, AMP_DISP_SetPlaneMute, amp_disp, PLANE_MAIN, ar->ar_muted);

  hts_mutex_unlock(&plane_mutex);

  return 0;
}


/**
 *
 */
static void
amp_destroy_vout(amp_render_t *ar, amp_extra_t *ae)
{
  HRESULT ret;

  if(!ar->ar_vout_running)
    return;

  TRACE(TRACE_DEBUG, "AMP", "%s: will destroy vout", ar_name(ar));

  ar->ar_vout_running = 0;

  hts_mutex_lock(&plane_mutex);
#if 1
  AMP_RPC(ret, AMP_CLK_SetState,  ae->amp_clk, AMP_IDLE);
  assert(ret == SUCCESS);
#endif
  AMP_RPC(ret, AMP_VOUT_SetState, ar->ar_vout, AMP_IDLE);
  assert(ret == SUCCESS);

  media_codec_t *mc = ar->ar_mc;
  if(mc != NULL) {
    amp_video_t *av = mc->opaque;
    ret = AMP_DisconnectComp(av->amp_vdec, 0, ar->ar_vout, 0);
    assert(ret == SUCCESS);

    media_codec_deref(mc);
    mc = NULL;
  }

  ret = AMP_DisconnectComp(ae->amp_clk, 0, ar->ar_vout, 1);
  assert(ret == SUCCESS);

  AMP_RPC(ret, AMP_VOUT_Close, ar->ar_vout);
  assert(ret == SUCCESS);

  AMP_RPC(ret, AMP_VOUT_Destroy, ar->ar_vout);
  assert(ret == SUCCESS);

  assert(ar == main_plane);
  main_plane = NULL;
  hts_mutex_unlock(&plane_mutex);

  TRACE(TRACE_DEBUG, "AMP", "%s: vout destroyed", ar_name(ar));

}


/**
 *
 */
static int
amp_render_init(glw_video_t *gv)
{
  gv->gv_aux = calloc(1, sizeof(amp_render_t));
  amp_render_t *ar = gv->gv_aux;

  ar->ar_widget = gv;

  return 0;
}


/**
 *
 */
static int64_t
amp_render_newframe(glw_video_t *gv, video_decoder_t *vd, int flags)
{
  return PTS_UNSET;
}


/**
 *
 */
static void
amp_render_reset(glw_video_t *gv)
{
  amp_render_t *ar = gv->gv_aux;
  TRACE(TRACE_DEBUG, "AMP", "%s: render_reset", ar_name(ar));
  amp_destroy_vout(ar, gv->gv_mp->mp_extra);
  free(ar);
}


/**
 *
 */
static void
amp_render_set_mute(glw_video_t *gv, int muted)
{
  HRESULT ret;
  amp_render_t *ar = gv->gv_aux;

  TRACE(TRACE_DEBUG, "AMP", "%s: Mute set to %d", ar_name(ar), muted);

  hts_mutex_lock(&plane_mutex);

  if(ar->ar_vout_running)
    AMP_RPC(ret, AMP_DISP_SetPlaneMute, amp_disp, PLANE_MAIN, muted);
  ar->ar_muted = muted;

  hts_mutex_unlock(&plane_mutex);
}


/**
 *
 */
static void
amp_render_render(glw_video_t *gv, glw_rctx_t *rc)
{
  HRESULT ret;
  amp_render_t *ar = gv->gv_aux;

  hts_mutex_lock(&plane_mutex);

  if(ar->ar_vout_running &&
     memcmp(&ar->ar_current_pos, &gv->gv_rect, sizeof(glw_rect_t))) {

    ar->ar_current_pos = gv->gv_rect;

    TRACE(TRACE_DEBUG, "AMP",
          "%s: Updating video rectangle (%d, %d) - (%d, %d)",
          ar_name(ar),
          gv->gv_rect.x1,
          gv->gv_rect.y1,
          gv->gv_rect.x2,
          gv->gv_rect.y2);

    AMP_DISP_WIN src = {0}, dst;
    dst.iX      = gv->gv_rect.x1;
    dst.iY      = gv->gv_rect.y1;
    dst.iWidth  = gv->gv_rect.x2;
    dst.iHeight = gv->gv_rect.y2;
    AMP_RPC(ret, AMP_DISP_SetScale, amp_disp, PLANE_MAIN, &src, &dst);
  }
  hts_mutex_unlock(&plane_mutex);
}


/**
 *
 */
static void
amp_render_blackout(glw_video_t *gv)
{
  TRACE(TRACE_DEBUG, "AMP", "%s: blackout", ar_name(gv->gv_aux));
  amp_destroy_vout(gv->gv_aux, gv->gv_mp->mp_extra);
}



static int amp_render_set_codec(media_codec_t *mc, glw_video_t *gv);

/**
 *
 */
static glw_video_engine_t glw_video_amp = {
  .gve_type =    'amp',
  .gve_newframe = amp_render_newframe,
  .gve_render   = amp_render_render,
  .gve_reset    = amp_render_reset,
  .gve_init     = amp_render_init,
  .gve_set_codec= amp_render_set_codec,
  .gve_set_mute = amp_render_set_mute,
  .gve_blackout = amp_render_blackout,
};

GLW_REGISTER_GVE(glw_video_amp);

/**
 *
 */
static int
amp_render_set_codec(media_codec_t *mc, glw_video_t *gv)
{
  HRESULT ret;

  glw_video_configure(gv, &glw_video_amp);

  amp_render_t *ar = gv->gv_aux;

  TRACE(TRACE_DEBUG, "AMP", "%s: Set codec", ar_name(ar));

  if(!ar->ar_vout_running) {

    if(amp_create_vout(ar, gv->gv_mp->mp_extra))
      return 1;

    ar->ar_vout_running = 1;
  }

  if(ar->ar_mc == mc)
    return 0;

  assert(ar->ar_mc == NULL); // Tear down tunnel plz

  ar->ar_mc = media_codec_ref(mc);

  amp_video_t *av = mc->opaque;

  ret = AMP_ConnectComp(av->amp_vdec, 0, ar->ar_vout, 0);
  assert(ret == SUCCESS);

  return 0;
}
