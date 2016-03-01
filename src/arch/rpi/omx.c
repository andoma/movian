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
#include <string.h>
#include <unistd.h>

#include "media/media.h"
#include "omx.h"
#include "rpi_pixmap.h"

/**
 *
 */
static OMX_ERRORTYPE
oc_event_handler(OMX_HANDLETYPE component, OMX_PTR opaque, OMX_EVENTTYPE event,
                 OMX_U32 data1, OMX_U32 data2, OMX_PTR eventdata)
{
  omx_component_t *oc = opaque;

#if 1
  omxdbg("%s: event  0x%x 0x%x 0x%x %p\n",
         oc->oc_name, event, (int)data1, (int)data2, eventdata);
#endif

  switch(event) {
  case OMX_EventCmdComplete:
  complete:

    hts_mutex_lock(oc->oc_mtx);
    oc->oc_cmd_done = 1;
    hts_cond_broadcast(&oc->oc_event_cond);
    hts_mutex_unlock(oc->oc_mtx);
    break;

  case OMX_EventError:

    switch(data1) {
    case OMX_ErrorPortUnpopulated:
      break;

    case OMX_ErrorSameState:
      goto complete;

    case OMX_ErrorStreamCorrupt:
      TRACE(TRACE_INFO, "OMX", "%s: Corrupt stream", oc->oc_name);
      hts_mutex_lock(oc->oc_mtx);
      oc->oc_stream_corrupt = 1;
      hts_cond_signal(oc->oc_avail_cond);
      hts_mutex_unlock(oc->oc_mtx);
      break;

      
    default:
      TRACE(TRACE_ERROR, "OMX", "%s: ERROR 0x%x\n", oc->oc_name, (int)data1);
      break;
    }
    break;

  case OMX_EventPortSettingsChanged:
    if(oc->oc_port_settings_changed_cb != NULL)
      oc->oc_port_settings_changed_cb(oc);
    break;

  case OMX_EventMark:
    if(oc->oc_event_mark_cb != NULL)
      oc->oc_event_mark_cb(oc, eventdata);
    break;
    
  default:
    break;
  }
  return 0;
}


/**
 *
 */
static OMX_ERRORTYPE
oc_empty_buffer_done(OMX_HANDLETYPE hComponent,
                     OMX_PTR opaque,
                     OMX_BUFFERHEADERTYPE* buf)
{
  omx_component_t *oc = opaque;
  hts_mutex_lock(oc->oc_mtx);
  oc->oc_inflight_buffers--;
  buf->pAppPrivate = oc->oc_avail;
  oc->oc_avail = buf;
  oc->oc_avail_bytes += buf->nAllocLen;
  if(oc->oc_avail_bytes >= oc->oc_need_bytes)
    hts_cond_signal(oc->oc_avail_cond);
  hts_mutex_unlock(oc->oc_mtx);
  return 0;
}



/**
 *
 */
static OMX_ERRORTYPE
oc_fill_buffer_done(OMX_HANDLETYPE hComponent,
                     OMX_PTR opaque,
                     OMX_BUFFERHEADERTYPE* buf)
{
  omx_component_t *oc = opaque;

  hts_mutex_lock(oc->oc_mtx);
  oc->oc_filled = buf;
  hts_cond_signal(oc->oc_avail_cond);
  hts_mutex_unlock(oc->oc_mtx);
  return 0;
}



/**
 *
 */
void
omx_wait_command(omx_component_t *oc)
{
  hts_mutex_lock(oc->oc_mtx);
  while(!oc->oc_cmd_done)
    if(hts_cond_wait_timeout(&oc->oc_event_cond, oc->oc_mtx, 250))
      break;
  hts_mutex_unlock(oc->oc_mtx);
}


/**
 *
 */
void
omx_send_command(omx_component_t *oc, OMX_COMMANDTYPE cmd, int v, void *p,
		 int wait)
{
  oc->oc_cmd_done = 0;

  //  omxdbg("%s: CMD(0x%x, 0x%x, %p)\n", oc->oc_name, cmd, v, p);

  omxchk(OMX_SendCommand(oc->oc_handle, cmd, v, p));

  if(wait)
    omx_wait_command(oc);
}


/**
 *
 */
omx_component_t *
omx_component_create(const char *name, hts_mutex_t *mtx,
                     hts_cond_t *avail_cond)
{
  omx_component_t *oc = calloc(1, sizeof(omx_component_t));
  OMX_CALLBACKTYPE cb;
  const OMX_INDEXTYPE types[] = {OMX_IndexParamAudioInit,
                                 OMX_IndexParamVideoInit,
                                 OMX_IndexParamImageInit,
                                 OMX_IndexParamOtherInit};

  assert(mtx != NULL);
  oc->oc_mtx = mtx;

  oc->oc_avail_cond = avail_cond;

  hts_cond_init(&oc->oc_event_cond, oc->oc_mtx);

  oc->oc_name = strdup(name);

  cb.EventHandler    = oc_event_handler;
  cb.EmptyBufferDone = oc_empty_buffer_done;
  cb.FillBufferDone  = oc_fill_buffer_done;

  //  omxdbg("Creating %s\n", oc->oc_name);
  omxchk(OMX_GetHandle(&oc->oc_handle, oc->oc_name, oc, &cb));

  // Initially disable ports
  int i;
  for(i = 0; i < 4; i++) {
    OMX_PORT_PARAM_TYPE ports;
    ports.nSize = sizeof(OMX_PORT_PARAM_TYPE);
    ports.nVersion.nVersion = OMX_VERSION;

    omxchk(OMX_GetParameter(oc->oc_handle, types[i], &ports));
    omxdbg("%s: type:%d: ports: %d +%d\n", name, i, ports.nStartPortNumber, ports.nPorts);

    if(ports.nPorts > 0) {
      oc->oc_inport = ports.nStartPortNumber;
      oc->oc_outport = ports.nStartPortNumber + 1;
    }

    for(int j = 0; j < ports.nPorts; j++)
      omx_send_command(oc, OMX_CommandPortDisable, ports.nStartPortNumber + j, NULL, 1);

  }


  return oc;
}



/**
 *
 */
void
omx_component_destroy(omx_component_t *oc)
{
  omxchk(OMX_FreeHandle(oc->oc_handle));

  free(oc->oc_name);
  hts_cond_destroy(&oc->oc_event_cond);
  free(oc);
}


/**
 *
 */
void
omx_set_state(omx_component_t *oc, OMX_STATETYPE reqstate)
{
  OMX_STATETYPE state;
  int attempts = 20;
  omxchk(OMX_GetState(oc->oc_handle, &state));
  omxdbg("Telling component '%s' to go from state %d -> to state %d\n", oc->oc_name, state, reqstate);


  while(1) {
    oc->oc_cmd_done = 0;

    int r = OMX_SendCommand(oc->oc_handle, OMX_CommandStateSet,
                          reqstate, NULL);

    if(r == OMX_ErrorInsufficientResources && attempts) {
      usleep(10000);
      attempts--;
      continue;
    }

    if(r != 0) {
      panic("OMX Setstate %s from %d to %d error 0x%x",
            oc->oc_name, state, reqstate, r);
    }

    // When transitioning to loaded the component will no longer respond
    if(reqstate != OMX_StateLoaded)
      omx_wait_command(oc);

    return;
  }
}


/**
 *
 */
void
omx_alloc_buffers(omx_component_t *oc, int port)
{
  OMX_PARAM_PORTDEFINITIONTYPE portdef;

  memset(&portdef, 0, sizeof(portdef));
  portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
  portdef.nVersion.nVersion = OMX_VERSION;
  portdef.nPortIndex = port;

  omxchk(OMX_GetParameter(oc->oc_handle, OMX_IndexParamPortDefinition, &portdef));
  if(portdef.bEnabled != OMX_FALSE || portdef.nBufferCountActual == 0 || portdef.nBufferSize == 0)
    exit(3);

  omxdbg("Allocating buffers for %s:%d\n", oc->oc_name, port);
  omxdbg("  buffer count = %d\n", (int)portdef.nBufferCountActual);
  omxdbg("  buffer size  = %d\n", (int)portdef.nBufferSize);

  omx_send_command(oc, OMX_CommandPortEnable, port, NULL, 0);
  int i;
  for(i = 0; i < portdef.nBufferCountActual; i++) {
    OMX_BUFFERHEADERTYPE *buf;
    omxchk(OMX_AllocateBuffer(oc->oc_handle, &buf, port, NULL, portdef.nBufferSize));
    omxdbg("buf=%p\n", buf);
    buf->pAppPrivate = oc->oc_avail;
    oc->oc_avail = buf;
    oc->oc_avail_bytes += buf->nAllocLen;
  }
  omx_wait_command(oc); // Waits for the OMX_CommandPortEnable command

}

/**
 *
 */
OMX_BUFFERHEADERTYPE *
omx_get_buffer_locked(omx_component_t *oc)
{
  OMX_BUFFERHEADERTYPE *buf;

  while((buf = oc->oc_avail) == NULL) {
    if(hts_cond_wait_timeout(oc->oc_avail_cond, oc->oc_mtx, 3000)) {
      TRACE(TRACE_ERROR, "OMX", "Timeout while waiting for buffer");
      return NULL;
    }
  }
  oc->oc_avail = buf->pAppPrivate;
  oc->oc_avail_bytes -= buf->nAllocLen;
  oc->oc_inflight_buffers++;
  return buf;
}

/**
 *
 */
OMX_BUFFERHEADERTYPE *
omx_get_buffer(omx_component_t *oc)
{
  hts_mutex_lock(oc->oc_mtx);
  OMX_BUFFERHEADERTYPE *buf  = omx_get_buffer_locked(oc);
  hts_mutex_unlock(oc->oc_mtx);
  return buf;
}



/**
 *
 */
void
omx_wait_buffers(omx_component_t *oc)
{
  hts_mutex_lock(oc->oc_mtx);
  while(oc->oc_inflight_buffers)
    hts_cond_wait(oc->oc_avail_cond, oc->oc_mtx);
  hts_mutex_unlock(oc->oc_mtx);
}


/**
 *
 */
void
omx_release_buffers(omx_component_t *oc, int port)
{
  assert(oc->oc_inflight_buffers == 0);

  OMX_BUFFERHEADERTYPE *buf, *n;
  for(buf = oc->oc_avail; buf; buf = n) {
    n = buf->pAppPrivate;
    int r = OMX_FreeBuffer(oc->oc_handle, port, buf);
    if(r) {
      TRACE(TRACE_ERROR, "OMX", "Unable to free buffer");
      exit(1);
    }
  }
  oc->oc_avail_bytes = 0;
  oc->oc_need_bytes = 0;
}


/**
 *
 */
omx_tunnel_t *
omx_tunnel_create(omx_component_t *src, int srcport, omx_component_t *dst,
		  int dstport, const char *name)
{
  OMX_STATETYPE state;
  omxchk(OMX_GetState(src->oc_handle, &state));

  if(state == OMX_StateLoaded)
    omx_set_state(src, OMX_StateIdle);

  omxdbg("Creating tunnel %s from %s:%d to %s:%d\n",
	 name, src->oc_name, srcport, dst->oc_name, dstport);

  omx_send_command(src, OMX_CommandPortDisable, srcport, NULL, 1);
  omx_send_command(dst, OMX_CommandPortDisable, dstport, NULL, 1);
  omxchk(OMX_SetupTunnel(src->oc_handle, srcport, dst->oc_handle, dstport));
  omx_send_command(src, OMX_CommandPortEnable, srcport, NULL, 0);
  omx_send_command(dst, OMX_CommandPortEnable, dstport, NULL, 0);

  omxchk(OMX_GetState(dst->oc_handle, &state));
  if(state == OMX_StateLoaded)
    omx_set_state(dst, OMX_StateIdle);

  omx_tunnel_t *ot = malloc(sizeof(omx_tunnel_t));
  ot->ot_src = src;
  ot->ot_srcport = srcport;
  ot->ot_dst = dst;
  ot->ot_dstport = dstport;
  ot->ot_name = name;
  return ot;
}


/**
 *
 */
void
omx_port_enable(omx_component_t *c, int port)
{
  omx_send_command(c, OMX_CommandPortEnable, port, NULL, 0);
}

/**
 *
 */
void
omx_tunnel_destroy(omx_tunnel_t *ot)
{
  omxdbg("Destroying tunnel %s\n", ot->ot_name);
  omx_send_command(ot->ot_src, OMX_CommandPortDisable, ot->ot_srcport, NULL, 1);
  omx_send_command(ot->ot_dst, OMX_CommandPortDisable, ot->ot_dstport, NULL, 1);

  omxchk(OMX_SetupTunnel(ot->ot_src->oc_handle, ot->ot_srcport, NULL, 0));
  free(ot);
}


/**
 *
 */
int
omx_wait_fill_buffer(omx_component_t *oc, OMX_BUFFERHEADERTYPE *buf)
{
  hts_mutex_lock(oc->oc_mtx);
  while(oc->oc_filled == NULL)
    hts_cond_wait(oc->oc_avail_cond, oc->oc_mtx);
  hts_mutex_unlock(oc->oc_mtx);
  return 0;
}



/**
 *
 */
int64_t
omx_get_media_time(omx_component_t *oc)
{
  OMX_TIME_CONFIG_TIMESTAMPTYPE ts;
  OMX_INIT_STRUCTURE(ts);

  omxchk(OMX_GetConfig(oc->oc_handle,
		       OMX_IndexConfigTimeCurrentMediaTime,
		       &ts));
  return omx_ticks_to_s64(ts.nTimestamp);
 
}


/**
 *
 */
void
omx_enable_buffer_marks(omx_component_t *oc)
{
  OMX_CONFIG_BOOLEANTYPE t;
  OMX_INIT_STRUCTURE(t);
  t.bEnabled = 1;
  omxchk(OMX_SetParameter(oc->oc_handle, OMX_IndexParamPassBufferMarks, &t));
}



/**
 *
 */
void
omx_flush_port(omx_component_t *oc, int port)
{
  omx_send_command(oc, OMX_CommandFlush, port, NULL, 1);
}


/**
 *
 */
TAILQ_HEAD(omx_clk_cmd_queue, omx_clk_cmd);

typedef struct omx_clk_cmd {
  TAILQ_ENTRY(omx_clk_cmd) link;
  enum {
    OMX_CLK_QUIT,
    OMX_CLK_INIT,
    OMX_CLK_PAUSE,
    OMX_CLK_PLAY,
    OMX_CLK_BEGIN_SEEK,
    OMX_CLK_SEEK_AUDIO_DONE,
    OMX_CLK_SEEK_VIDEO_DONE,
  } cmd;
  int arg;
} omx_clk_cmd_t;


typedef struct omx_clk {
  hts_thread_t tid;
  struct omx_clk_cmd_queue q;
  omx_component_t *c;
  hts_cond_t cond;
  media_pipe_t *mp;
  int seek_in_progress;
  int has_audio;
} omx_clk_t;


/**
 *
 */
static void
omx_clk_set_speed(omx_clk_t *clk, int v)
{
  omx_component_t *c = clk->c;

  OMX_TIME_CONFIG_SCALETYPE scale;
  OMX_INIT_STRUCTURE(scale);

  scale.xScale = v;
  omxchk(OMX_SetConfig(c->oc_handle, OMX_IndexConfigTimeScale, &scale));
}


/**
 *
 */
static void
omx_clk_begin_seek(omx_clk_t *clk)
{
  omx_component_t *c = clk->c;

  OMX_TIME_CONFIG_CLOCKSTATETYPE cs;
  OMX_INIT_STRUCTURE(cs);
  cs.eState = OMX_TIME_ClockStateStopped;
  omxchk(OMX_SetParameter(c->oc_handle, OMX_IndexConfigTimeClockState, &cs));
  clk->seek_in_progress = 2;
}


/**
 *
 */
static void
omx_clk_seek_done(omx_clk_t *clk)
{
  if(clk->seek_in_progress == 0)
    return;

  clk->seek_in_progress--;
  if(clk->seek_in_progress)
    return;

  omx_component_t *c = clk->c;

  OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
  OMX_INIT_STRUCTURE(cstate);
  cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
  cstate.nWaitMask = clk->has_audio ? 1 : 2;
  omxchk(OMX_SetParameter(c->oc_handle, OMX_IndexConfigTimeClockState, &cstate));
}


/**
 *
 */
static void
omx_clk_init(omx_clk_t *clk, int has_audio)
{
  OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
  clk->has_audio = has_audio;
  clk->seek_in_progress = 0;
  OMX_INIT_STRUCTURE(cstate);
  cstate.eState = OMX_TIME_ClockStateStopped;
  omxchk(OMX_SetParameter(clk->c->oc_handle,
			  OMX_IndexConfigTimeClockState, &cstate));

  omx_set_state(clk->c, OMX_StateIdle);



  OMX_INIT_STRUCTURE(cstate);

  cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
  cstate.nWaitMask = has_audio ? 1 : 2;

  omxchk(OMX_SetParameter(clk->c->oc_handle,
			  OMX_IndexConfigTimeClockState, &cstate));

  OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE refClock;
  OMX_INIT_STRUCTURE(refClock);
  refClock.eClock = has_audio ? OMX_TIME_RefClockAudio : OMX_TIME_RefClockVideo;

  omxchk(OMX_SetConfig(clk->c->oc_handle,
		       OMX_IndexConfigTimeActiveRefClock, &refClock));

  omx_set_state(clk->c, OMX_StateExecuting);

}

/**
 *
 */
static void *
omx_clk_thread(void *aux)
{
  omx_clk_cmd_t *cmd;
  omx_clk_t *clk = aux;
  int run = 1;

  hts_mutex_lock(&clk->mp->mp_mutex);

  while(run) {
    while((cmd = TAILQ_FIRST(&clk->q)) == NULL) {
#if 0
      if(hts_cond_wait_timeout(&clk->cond, &clk->mp->mp_mutex, 1000)) {
	hts_mutex_unlock(&clk->mp->mp_mutex);
	int64_t ts = omx_get_media_time(clk->c);
	hts_mutex_lock(&clk->mp->mp_mutex);
      }
#else
      hts_cond_wait(&clk->cond, &clk->mp->mp_mutex);
#endif

      continue;
    }

    TAILQ_REMOVE(&clk->q, cmd, link);
    hts_mutex_unlock(&clk->mp->mp_mutex);

    switch(cmd->cmd) {
    case OMX_CLK_QUIT:
      run = 0;
      break;

    case OMX_CLK_INIT:
      omx_clk_init(clk, cmd->arg);
      break;

    case OMX_CLK_PAUSE:
      omx_clk_set_speed(clk, 0);
      break;

    case OMX_CLK_PLAY:
      omx_clk_set_speed(clk, 1 << 16);
      break;

    case OMX_CLK_BEGIN_SEEK:
      omx_clk_begin_seek(clk);
      break;

    case OMX_CLK_SEEK_AUDIO_DONE:
      omx_clk_seek_done(clk);
      break;

    case OMX_CLK_SEEK_VIDEO_DONE:
      omx_clk_seek_done(clk);
      break;
    }

    free(cmd);
    hts_mutex_lock(&clk->mp->mp_mutex);
  }

  hts_mutex_unlock(&clk->mp->mp_mutex);
  return NULL;
}




/**
 *
 */
static void
omx_clk_do(omx_clk_t *clk, int op, int arg)
{
  omx_clk_cmd_t *cmd = malloc(sizeof(omx_clk_cmd_t));
  cmd->cmd = op;
  cmd->arg = arg;
  TAILQ_INSERT_TAIL(&clk->q, cmd, link);
  hts_cond_signal(&clk->cond);
}



/**
 *
 */
static void
omx_mp_begin_seek(media_pipe_t *mp)
{
  omx_clk_do(mp->mp_extra, OMX_CLK_BEGIN_SEEK, 0);
}


/**
 *
 */
static void
omx_mp_seek_audio_done(media_pipe_t *mp)
{
  hts_mutex_lock(&mp->mp_mutex);
  omx_clk_do(mp->mp_extra, OMX_CLK_SEEK_AUDIO_DONE, 0);
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
static void
omx_mp_seek_video_done(media_pipe_t *mp)
{
  hts_mutex_lock(&mp->mp_mutex);
  omx_clk_do(mp->mp_extra, OMX_CLK_SEEK_VIDEO_DONE, 0);
  hts_mutex_unlock(&mp->mp_mutex);
}


/**
 *
 */
static void
omx_mp_hold_changed(media_pipe_t *mp)
{
  omx_clk_do(mp->mp_extra, mp->mp_hold_gate ? OMX_CLK_PAUSE : OMX_CLK_PLAY, 0);
}


/**
 *
 */
static void
omx_mp_clock_setup(media_pipe_t *mp, int has_audio)
{
  omx_clk_do(mp->mp_extra, OMX_CLK_INIT, has_audio);
}


/**
 *
 */
static void
omx_mp_init(media_pipe_t *mp)
{
  if(!(mp->mp_flags & MP_VIDEO))
    return;

  mp->mp_seek_initiate   = omx_mp_begin_seek;
  mp->mp_seek_audio_done = omx_mp_seek_audio_done;
  mp->mp_seek_video_done = omx_mp_seek_video_done;
  mp->mp_hold_changed    = omx_mp_hold_changed;
  mp->mp_clock_setup     = omx_mp_clock_setup;


  omx_clk_t *clk = calloc(1, sizeof(omx_clk_t));
  TAILQ_INIT(&clk->q);
  clk->mp = mp;
  clk->c = omx_component_create("OMX.broadcom.clock", &mp->mp_mutex, NULL);
  hts_cond_init(&clk->cond, &mp->mp_mutex);
  mp->mp_extra = clk;

  omx_set_state(clk->c, OMX_StateIdle);

  omx_clk_do(clk, OMX_CLK_INIT, 1);

  hts_thread_create_joinable("omxclkctrl", &clk->tid, omx_clk_thread, clk,
			     THREAD_PRIO_DEMUXER);
}


/**
 *
 */
static void
omx_mp_fini(media_pipe_t *mp)
{
  if(mp->mp_extra == NULL)
    return;

  omx_clk_t *clk = mp->mp_extra;

  hts_mutex_lock(&mp->mp_mutex);
  omx_clk_do(clk, OMX_CLK_QUIT, 0);
  hts_mutex_unlock(&mp->mp_mutex);
  hts_thread_join(&clk->tid);
  omx_component_destroy(clk->c);
  free(clk);
}


/**
 *
 */
omx_component_t *
omx_get_clock(media_pipe_t *mp)
{
  omx_clk_t *clk = mp->mp_extra;
  return clk ? clk->c : NULL;
}

/**
 *
 */
void
omx_init(void)
{
  OMX_Init();

  media_pipe_init_extra = omx_mp_init;
  media_pipe_fini_extra = omx_mp_fini;

  rpi_pixmap_init();
}

