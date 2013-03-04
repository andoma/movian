#include <assert.h>
#include <string.h>

#include "media.h"
#include "omx.h"

/**
 *
 */
static OMX_ERRORTYPE
oc_event_handler(OMX_HANDLETYPE component, OMX_PTR opaque, OMX_EVENTTYPE event,
                 OMX_U32 data1, OMX_U32 data2, OMX_PTR eventdata)
{
  omx_component_t *oc = opaque;

  omxdbg("%s: event  0x%x 0x%x 0x%x %p\n",
         oc->oc_name, event, (int)data1, (int)data2, eventdata);

  hts_mutex_lock(&oc->oc_event_mtx);

  switch(event) {
  case OMX_EventCmdComplete:
    oc->oc_cmd_done = 1;
    hts_cond_broadcast(&oc->oc_event_cond);
    break;
  case OMX_EventError:
    if(data1 == OMX_ErrorPortUnpopulated) {
      printf("Port unpopulated\n");
      break;
    }

    if(data1 == OMX_ErrorSameState) {
      oc->oc_cmd_done = 1;
      hts_cond_broadcast(&oc->oc_event_cond);
      break;
    }

    printf("%s: ERROR 0x%x\n", oc->oc_name, (int)data1);
    exit(1);

  case OMX_EventPortSettingsChanged: 
    oc->oc_port_settings_changed = 1;
    break;
    
  default:
    break;
  }

  hts_mutex_unlock(&oc->oc_event_mtx);
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
  hts_mutex_lock(oc->oc_avail_mtx);
  oc->oc_inflight_buffers--;
  buf->pAppPrivate = oc->oc_avail;
  oc->oc_avail = buf;
  hts_cond_signal(oc->oc_avail_cond);
  hts_mutex_unlock(oc->oc_avail_mtx);
  return 0;
}



/**
 *
 */
static void
omx_wait_command(omx_component_t *oc)
{
  hts_mutex_lock(&oc->oc_event_mtx);
  while(!oc->oc_cmd_done)
    hts_cond_wait(&oc->oc_event_cond, &oc->oc_event_mtx);
  hts_mutex_unlock(&oc->oc_event_mtx);
}


/**
 *
 */
static void
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
omx_component_create(const char *name, hts_mutex_t *mtx, hts_cond_t *avail)
{
  omx_component_t *oc = calloc(1, sizeof(omx_component_t));
  OMX_CALLBACKTYPE cb;
  const OMX_INDEXTYPE types[] = {OMX_IndexParamAudioInit,
                                 OMX_IndexParamVideoInit,
                                 OMX_IndexParamImageInit,
                                 OMX_IndexParamOtherInit};

  oc->oc_avail_mtx = mtx;
  oc->oc_avail_cond = avail;
  hts_mutex_init(&oc->oc_event_mtx);
  hts_cond_init(&oc->oc_event_cond, &oc->oc_event_mtx);

  oc->oc_name = strdup(name);

  cb.EventHandler    = oc_event_handler;
  cb.EmptyBufferDone = oc_empty_buffer_done;
  cb.FillBufferDone  = NULL;

  //  omxdbg("Creating %s\n", oc->oc_name);
  omxchk(OMX_GetHandle(&oc->oc_handle, oc->oc_name, oc, &cb));

  // Initially disable ports
  int i;
  for(i = 0; i < 4; i++) {
    OMX_PORT_PARAM_TYPE ports;
    ports.nSize = sizeof(OMX_PORT_PARAM_TYPE);
    ports.nVersion.nVersion = OMX_VERSION;

    omxchk(OMX_GetParameter(oc->oc_handle, types[i], &ports));
    //    omxdbg("%s: type:%d: ports: %ld +%ld\n", name, i, ports.nStartPortNumber, ports.nPorts);

    int i;
    for(i = 0; i < ports.nPorts; i++)
      omx_send_command(oc, OMX_CommandPortDisable, ports.nStartPortNumber + i, NULL, 0);
  }

  //  oc->oc_inport = ports.nStartPortNumber;
  //  oc->oc_outport = ports.nStartPortNumber + 1;

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

  omxchk(OMX_GetState(oc->oc_handle, &state));
  omxdbg("Telling component '%s' to go from state %d -> to state %d\n", oc->oc_name, state, reqstate);
  omx_send_command(oc, OMX_CommandStateSet, reqstate, NULL, reqstate != OMX_StateLoaded);
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
    exit(2);

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

  while((buf = oc->oc_avail) == NULL)
    hts_cond_wait(oc->oc_avail_cond, oc->oc_avail_mtx);
  oc->oc_avail = buf->pAppPrivate;
  oc->oc_inflight_buffers++;
  return buf;
}

/**
 *
 */
OMX_BUFFERHEADERTYPE *
omx_get_buffer(omx_component_t *oc)
{
  hts_mutex_lock(oc->oc_avail_mtx);
  OMX_BUFFERHEADERTYPE *buf  = omx_get_buffer_locked(oc);
  hts_mutex_unlock(oc->oc_avail_mtx);
  return buf;
}



/**
 *
 */
void
omx_wait_buffers(omx_component_t *oc)
{
  hts_mutex_lock(oc->oc_avail_mtx);
  while(oc->oc_inflight_buffers)
    hts_cond_wait(oc->oc_avail_cond, oc->oc_avail_mtx);
  hts_mutex_unlock(oc->oc_avail_mtx);
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
    printf("%s freeing buf %p : %x\n", oc->oc_name, buf, r);
  }
}


/**
 *
 */
omx_tunnel_t *
omx_tunnel_create(omx_component_t *src, int srcport, omx_component_t *dst,
		  int dstport)
{
  OMX_STATETYPE state;
  omxchk(OMX_GetState(src->oc_handle, &state));

  if(state == OMX_StateLoaded)
    omx_set_state(src, OMX_StateIdle);

  omxdbg("Creating tunnel from %s:%d to %s:%d\n",
	 src->oc_name, srcport, dst->oc_name, dstport);

  omx_send_command(src, OMX_CommandPortDisable, srcport, NULL, 1);
  omx_send_command(dst, OMX_CommandPortDisable, dstport, NULL, 1);
  omxchk(OMX_SetupTunnel(src->oc_handle, srcport, dst->oc_handle, dstport));
  omx_send_command(src, OMX_CommandPortEnable, srcport, NULL, 0);
  omx_send_command(dst, OMX_CommandPortEnable, dstport, NULL, 0);
  omx_set_state(dst, OMX_StateIdle);

  omx_tunnel_t *ot = malloc(sizeof(omx_tunnel_t));
  ot->ot_src = src;
  ot->ot_srcport = srcport;
  ot->ot_dst = dst;
  ot->ot_dstport = dstport;
  return ot;
}


/**
 *
 */
void
omx_tunnel_destroy(omx_tunnel_t *ot)
{
  omxdbg("Destroying tunnel\n");
  omx_send_command(ot->ot_src, OMX_CommandPortDisable, ot->ot_srcport, NULL, 0);
  omx_send_command(ot->ot_dst, OMX_CommandPortDisable, ot->ot_dstport, NULL, 0);
  omxchk(OMX_SetupTunnel(ot->ot_src->oc_handle, ot->ot_srcport, NULL, 0));
  free(ot);
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
omx_flush_port(omx_component_t *oc, int port)
{
  printf("Flushing %s %d\n", oc->oc_name, port);
  omx_send_command(oc, OMX_CommandFlush, port, NULL, 1);
}


/**
 *
 */
static void
omx_mp_init(media_pipe_t *mp)
{
  if(!(mp->mp_flags & MP_VIDEO))
    return;

  omx_component_t *c;

  c = omx_component_create("OMX.broadcom.clock", &mp->mp_mutex, NULL);
  mp->mp_extra = c;

  omx_set_state(c, OMX_StateIdle);

  OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
  OMX_INIT_STRUCTURE(cstate);
  cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
  cstate.nWaitMask = 1;
  omxchk(OMX_SetParameter(c->oc_handle,
			  OMX_IndexConfigTimeClockState, &cstate));

  OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE refClock;
  OMX_INIT_STRUCTURE(refClock);
  refClock.eClock = OMX_TIME_RefClockAudio;
  //  refClock.eClock = OMX_TIME_RefClockVideo;
  // refClock.eClock = OMX_TIME_RefClockNone;

  omxchk(OMX_SetConfig(c->oc_handle,
		       OMX_IndexConfigTimeActiveRefClock, &refClock));

  omx_set_state(c, OMX_StateExecuting);
}


/**
 *
 */
static void
omx_mp_fini(media_pipe_t *mp)
{
  if(mp->mp_extra != NULL)
    omx_component_destroy(mp->mp_extra);
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
}

