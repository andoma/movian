#include "arch/threads.h"
#include "showtime.h"
#include <OMX_Component.h>
#include <OMX_Core.h>
#include <OMX_Broadcom.h>


#define OMX_INIT_STRUCTURE(a) \
  memset(&(a), 0, sizeof(a)); \
  (a).nSize = sizeof(a); \
  (a).nVersion.s.nVersionMajor = OMX_VERSION_MAJOR; \
  (a).nVersion.s.nVersionMinor = OMX_VERSION_MINOR; \
  (a).nVersion.s.nRevision = OMX_VERSION_REVISION; \
  (a).nVersion.s.nStep = OMX_VERSION_STEP

typedef struct omx_component {
  OMX_HANDLETYPE oc_handle;
  char *oc_name;
  hts_mutex_t *oc_avail_mtx;
  hts_cond_t *oc_avail_cond;

  hts_mutex_t oc_event_mtx;
  hts_cond_t oc_event_cond;

  OMX_BUFFERHEADERTYPE *oc_avail;
  OMX_BUFFERHEADERTYPE *oc_filled;
  int oc_inflight_buffers;
  int oc_cmd_done;

  void *oc_opaque;
  void (*oc_port_settings_changed_cb)(struct omx_component *oc);
  void (*oc_event_mark_cb)(struct omx_component *oc, void *ptr);

  int oc_inport;
  int oc_outport;

  int oc_stream_corrupt;

} omx_component_t;

typedef struct omx_tunnel {
  omx_component_t *ot_src;
  int ot_srcport;
  omx_component_t *ot_dst;
  int ot_dstport;
  const char *ot_name;
} omx_tunnel_t;




static inline void
omxchk0(OMX_ERRORTYPE er, const char *fn, int line)
{
  if(!er)
    return;
  panic("%s: OMX Error (line:%d) 0x%x\n", fn, line, er);
}


#define omxchk(fn) omxchk0(fn, # fn, __LINE__);

#if 0
#define omxdbg(fmt...) TRACE(TRACE_DEBUG, "OMX", fmt);
#else
#define omxdbg(fmt...)
#endif


void omx_init(void);
omx_component_t *omx_component_create(const char *name, hts_mutex_t *mtx,
				      hts_cond_t *avail);
void omx_component_destroy(omx_component_t *oc);
void omx_send_command(omx_component_t *oc, OMX_COMMANDTYPE cmd, int v, void *p,
		      int wait);
void omx_set_state(omx_component_t *oc, OMX_STATETYPE reqstate);
void omx_alloc_buffers(omx_component_t *oc, int port);
OMX_BUFFERHEADERTYPE *omx_get_buffer_locked(omx_component_t *oc);
OMX_BUFFERHEADERTYPE *omx_get_buffer(omx_component_t *oc);
int omx_wait_fill_buffer(omx_component_t *oc, OMX_BUFFERHEADERTYPE *buf);
void omx_wait_buffers(omx_component_t *oc);
void omx_release_buffers(omx_component_t *oc, int port);
omx_tunnel_t *omx_tunnel_create(omx_component_t *src, int srcport,
				omx_component_t *dst, int dstport,
				const char *name);
void omx_tunnel_destroy(omx_tunnel_t *ot);
int64_t omx_get_media_time(omx_component_t *oc);
void omx_flush_port(omx_component_t *oc, int port);
struct media_pipe;
omx_component_t *omx_get_clock(struct media_pipe *mp);
void omx_enable_buffer_marks(omx_component_t *oc);
void omx_port_enable(omx_component_t *c, int port);
void omx_wait_command(omx_component_t *oc);

