/*
 *  Copyright 2013 (C) Spotify AB
 */

#include <libavutil/mathematics.h>
#include <stdio.h>
#include <assert.h>

#include "arch/threads.h"
#include "showtime.h"
#include "media.h"
#include "amp.h"

AMP_FACTORY amp_factory;
AMP_COMPONENT amp_disp;


/**
 *
 */
static void
amp_mp_init(media_pipe_t *mp)
{
  if(!(mp->mp_flags & MP_VIDEO))
    return;

  HRESULT ret;
  AMP_COMPONENT_CONFIG amp_config;

  TRACE(TRACE_DEBUG, "AMP", "Initialize AMP for MP");

  amp_extra_t *ae = calloc(1, sizeof(amp_extra_t));
  mp->mp_extra = ae;

  AMP_RPC(ret,
          AMP_FACTORY_CreateComponent,
          amp_factory,
          AMP_COMPONENT_CLK,
          0,
          &ae->amp_clk);
  assert(ret == SUCCESS);

  AmpMemClear(&amp_config, sizeof(AMP_COMPONENT_CONFIG));
  amp_config._d = AMP_COMPONENT_CLK;
  amp_config._u.pCLK.mode = AMP_TUNNEL;
  amp_config._u.pCLK.uiInputPortNum = 0;
  amp_config._u.pCLK.uiOutputPortNum = 2;
  amp_config._u.pCLK.uiNotifierNum = 0;
  amp_config._u.pCLK.eClockSource = AMP_CLK_SRC_VPP;
  amp_config._u.pCLK.eAVSyncPolicy = AMP_CLK_POLICY_LOCAL_FILE;
  AMP_RPC(ret, AMP_CLK_Open, ae->amp_clk, &amp_config);
  assert(ret == SUCCESS);

  AMP_RPC(ret, AMP_CLK_SetClockRate, ae->amp_clk, 1000, 1000);
  assert(ret == SUCCESS);

#if 0
  AMP_RPC(ret, AMP_CLK_SetState,  ae->amp_clk, AMP_EXECUTING);
  assert(ret == SUCCESS);
#endif
}


/**
 *
 */
static void
amp_mp_fini(media_pipe_t *mp)
{
  HRESULT ret;

  if(mp->mp_extra == NULL)
    return;

  TRACE(TRACE_DEBUG, "AMP", "Finalized AMP for MP");

  amp_extra_t *ae = mp->mp_extra;

  AMP_RPC(ret, AMP_CLK_SetState,  ae->amp_clk, AMP_IDLE);
  assert(ret == SUCCESS);

  AMP_RPC(ret, AMP_CLK_Close,  ae->amp_clk);
  assert(ret == SUCCESS);

  AMP_RPC(ret, AMP_CLK_Destroy,  ae->amp_clk);
  assert(ret == SUCCESS);

  free(ae);
}


/**
 *
 */
static void
amp_init(void)
{
  HRESULT ret;

  MV_OSAL_Init();

  int result = AMP_Initialize(0, NULL, &amp_factory);
  if(result != SUCCESS) {
    fprintf(stderr,
            "Can't initialize AMP successful! Please check AMP server.");
    exit(0);
  }

  AMP_RPC(ret, AMP_FACTORY_CreateDisplayService, amp_factory, &amp_disp);
  assert(ret == SUCCESS);


  AMP_DISP_ZORDER amp_zorder;

  amp_zorder.iMain = 1;
  amp_zorder.iPip  = 2;
  amp_zorder.iGfx0 = 3;
  amp_zorder.iGfx1 = 4;
  amp_zorder.iGfx2 = 5;
  amp_zorder.iPg   = 6;
  amp_zorder.iBg   = 0;
  amp_zorder.iAux  = 7;

  AMP_RPC(ret, AMP_DISP_SetPlaneZOrder, amp_disp, 0, &amp_zorder);
  assert(ret == SUCCESS);


  TRACE(TRACE_DEBUG, "AMP", "AMP INIT done");

  media_pipe_init_extra = amp_mp_init;
  media_pipe_fini_extra = amp_mp_fini;
}


INITME(INIT_GROUP_IPC, amp_init);
