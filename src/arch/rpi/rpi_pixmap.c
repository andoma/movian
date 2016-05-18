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
#include "main.h"
#include "rpi_pixmap.h"
#include "omx.h"
#include "image/pixmap.h"
#include "misc/buf.h"
#include "misc/minmax.h"

// #define NOCOPY



typedef struct rpi_pixmap_decoder {
  hts_mutex_t rpd_mtx;
  hts_cond_t rpd_cond;
  int rpd_change;
  OMX_BUFFERHEADERTYPE *rpd_outbuf;
  omx_component_t *rpd_decoder;
  omx_component_t *rpd_resizer;
  omx_tunnel_t *rpd_tunnel;
  const image_meta_t *rpd_im;
  pixmap_t *rpd_pm;
  OMX_BUFFERHEADERTYPE *rpd_buf;
} rpi_pixmap_decoder_t;


/**
 *
 */
static void 
decoder_port_settings_changed(omx_component_t *oc)
{
  rpi_pixmap_decoder_t *rpd = oc->oc_opaque;

  hts_mutex_lock(&rpd->rpd_mtx);
  rpd->rpd_change = 1;
  hts_cond_signal(&rpd->rpd_cond);
  hts_mutex_unlock(&rpd->rpd_mtx);
}


/**
 *
 */
static rpi_pixmap_decoder_t *
pixmap_decoder_create(int cfmt)
{
  rpi_pixmap_decoder_t *rpd = calloc(1, sizeof(rpi_pixmap_decoder_t));
  hts_mutex_init(&rpd->rpd_mtx);
  hts_cond_init(&rpd->rpd_cond, &rpd->rpd_mtx);

  rpd->rpd_decoder = omx_component_create("OMX.broadcom.image_decode",
					  &rpd->rpd_mtx, &rpd->rpd_cond);

  rpd->rpd_decoder->oc_port_settings_changed_cb = decoder_port_settings_changed;
  rpd->rpd_decoder->oc_opaque = rpd;

  rpd->rpd_resizer = omx_component_create("OMX.broadcom.resize",
					  &rpd->rpd_mtx, &rpd->rpd_cond);

  omx_set_state(rpd->rpd_decoder, OMX_StateIdle);

  OMX_IMAGE_PARAM_PORTFORMATTYPE fmt;
  OMX_INIT_STRUCTURE(fmt);
  fmt.nPortIndex = rpd->rpd_decoder->oc_inport;
  fmt.eCompressionFormat = cfmt;
  omxchk(OMX_SetParameter(rpd->rpd_decoder->oc_handle,
			  OMX_IndexParamImagePortFormat, &fmt));

#ifndef NOCOPY
  omx_alloc_buffers(rpd->rpd_decoder, rpd->rpd_decoder->oc_inport);
  omx_set_state(rpd->rpd_decoder, OMX_StateExecuting);
#endif
  return rpd;
}


/**
 *
 */
static void
setup_tunnel(rpi_pixmap_decoder_t *rpd)
{
  int dst_width, dst_height;
  OMX_PARAM_PORTDEFINITIONTYPE portdef;

  if(rpd->rpd_tunnel != NULL)
    return;

  OMX_INIT_STRUCTURE(portdef);
  portdef.nPortIndex = rpd->rpd_decoder->oc_outport;
  omxchk(OMX_GetParameter(rpd->rpd_decoder->oc_handle,
			  OMX_IndexParamPortDefinition, &portdef));
  portdef.format.image.nSliceHeight = 16;
  omxchk(OMX_SetParameter(rpd->rpd_decoder->oc_handle,
			  OMX_IndexParamPortDefinition, &portdef));

  pixmap_compute_rescale_dim(rpd->rpd_im,
			     portdef.format.image.nFrameWidth,
			     portdef.format.image.nFrameHeight,
			     &dst_width, &dst_height);

  portdef.nPortIndex = rpd->rpd_resizer->oc_inport;
  omxchk(OMX_SetParameter(rpd->rpd_resizer->oc_handle,
			  OMX_IndexParamPortDefinition, &portdef));

  rpd->rpd_tunnel = omx_tunnel_create(rpd->rpd_decoder,
				      rpd->rpd_decoder->oc_outport,
				      rpd->rpd_resizer,
				      rpd->rpd_resizer->oc_inport,
				      "decoder -> resizer");
  OMX_INIT_STRUCTURE(portdef);

  portdef.nPortIndex = rpd->rpd_resizer->oc_outport;
  omxchk(OMX_GetParameter(rpd->rpd_resizer->oc_handle,
			  OMX_IndexParamPortDefinition, &portdef));

  int stride = (dst_width * 4 + PIXMAP_ROW_ALIGN - 1) & ~(PIXMAP_ROW_ALIGN - 1);

  portdef.format.image.eCompressionFormat    = OMX_IMAGE_CodingUnused;
  portdef.format.image.eColorFormat          = OMX_COLOR_Format32bitABGR8888;
  portdef.format.image.nFrameWidth           = dst_width;
  portdef.format.image.nFrameHeight          = dst_height;
  portdef.format.image.nStride               = stride;
  portdef.format.image.nSliceHeight          = 0;
  portdef.format.image.bFlagErrorConcealment = OMX_FALSE;

  omxchk(OMX_SetParameter(rpd->rpd_resizer->oc_handle,
			  OMX_IndexParamPortDefinition, &portdef));

  omxchk(OMX_GetParameter(rpd->rpd_resizer->oc_handle,
			  OMX_IndexParamPortDefinition, &portdef));

  omx_set_state(rpd->rpd_resizer, OMX_StateExecuting);
  omx_port_enable(rpd->rpd_resizer, rpd->rpd_resizer->oc_outport);

  pixmap_t *pm = rpd->rpd_pm = calloc(1, sizeof(pixmap_t));
  atomic_set(&pm->pm_refcount, 1);
  pm->pm_width    = portdef.format.image.nFrameWidth;
  pm->pm_height   = portdef.format.image.nFrameHeight;
  pm->pm_linesize = portdef.format.image.nStride;
  pm->pm_type     = PIXMAP_BGR32;
  pm->pm_data     = mymemalign(portdef.nBufferAlignment, portdef.nBufferSize);
  pm->pm_aspect   = (float)pm->pm_width / (float)pm->pm_height;
  pm->pm_flags   |= PIXMAP_OPAQUE;

  omxchk(OMX_UseBuffer(rpd->rpd_resizer->oc_handle, &rpd->rpd_buf,
		       rpd->rpd_resizer->oc_outport,
		       NULL, portdef.nBufferSize, pm->pm_data));

  omx_wait_command(rpd->rpd_resizer);
  omxchk(OMX_FillThisBuffer(rpd->rpd_resizer->oc_handle, rpd->rpd_buf));
}


#ifdef TIMING
#define CHECKPOINT(x)				\
  ts2 = arch_get_ts();			\
  printf("%s in %lld\n", x, ts2 - ts);		\
  ts = ts2;
#else
#define CHECKPOINT(x)
#endif


/**
 *
 */
static pixmap_t *
rpi_pixmap_decode(image_coded_type_t type,
		  buf_t *buf, const image_meta_t *im,
		  char *errbuf, size_t errlen,
                  const image_t *img)
{
  if(type != IMAGE_JPEG)
    return NULL;

  if(img->im_flags & IMAGE_PROGRESSIVE)
    return NULL;

  if(img->im_color_planes != 3)
    return NULL;

#ifdef TIMING
  int64_t ts = arch_get_ts(), ts2;
#endif
  rpi_pixmap_decoder_t *rpd = pixmap_decoder_create(OMX_IMAGE_CodingJPEG);

  if(rpd == NULL)
    return NULL;
  rpd->rpd_im = im;

#ifdef NOCOPY


#error check rpd->rpd_decoder->oc_stream_corrupt

  OMX_PARAM_PORTDEFINITIONTYPE portdef;

  memset(&portdef, 0, sizeof(portdef));
  portdef.nSize = sizeof(OMX_PARAM_PORTDEFINITIONTYPE);
  portdef.nVersion.nVersion = OMX_VERSION;
  portdef.nPortIndex = rpd->rpd_decoder->oc_inport;

  omxchk(OMX_GetParameter(rpd->rpd_decoder->oc_handle,
			  OMX_IndexParamPortDefinition, &portdef));

  omx_send_command(rpd->rpd_decoder, OMX_CommandPortEnable,
		   rpd->rpd_decoder->oc_inport, NULL, 0);

  OMX_BUFFERHEADERTYPE *buf;
  
  for(int i = 0; i < portdef.nBufferCountActual; i++) {
    omxchk(OMX_UseBuffer(rpd->rpd_decoder->oc_handle, &buf,
			 rpd->rpd_decoder->oc_inport, 
			 NULL, pm->pm_size, pm->pm_data));
  }

  // Waits for the OMX_CommandPortEnable command
  omx_wait_command(rpd->rpd_decoder);
  
  omx_set_state(rpd->rpd_decoder, OMX_StateExecuting);

  CHECKPOINT("Initialized");

  buf->nOffset = 0;
  buf->nFilledLen = pm->pm_size;

  buf->nFlags |= OMX_BUFFERFLAG_EOS;

  rpd->rpd_decoder->oc_inflight_buffers++;

  omxchk(OMX_EmptyThisBuffer(rpd->rpd_decoder->oc_handle, buf));
  
  hts_mutex_lock(&rpd->rpd_mtx);
  while(rpd->rpd_change == 0)
    hts_cond_wait(&rpd->rpd_cond, &rpd->rpd_mtx);
  hts_mutex_unlock(&rpd->rpd_mtx);
  CHECKPOINT("Setup tunnel");
  setup_tunnel(rpd);



#else

  const void *data = buf_data(buf);
  size_t len       = buf_size(buf);

  hts_mutex_lock(&rpd->rpd_mtx);

  while(len > 0) {
    OMX_BUFFERHEADERTYPE *buf;

    if(rpd->rpd_decoder->oc_stream_corrupt)
      break;

    if(rpd->rpd_change == 1) {
      rpd->rpd_change = 2;
      hts_mutex_unlock(&rpd->rpd_mtx);
      setup_tunnel(rpd);
      hts_mutex_lock(&rpd->rpd_mtx);
      continue;
    }

    buf = rpd->rpd_decoder->oc_avail;

    if(buf == NULL) {
      hts_cond_wait(&rpd->rpd_cond, &rpd->rpd_mtx);
      continue;
    }

    rpd->rpd_decoder->oc_avail = buf->pAppPrivate;
    rpd->rpd_decoder->oc_inflight_buffers++;
    rpd->rpd_decoder->oc_avail_bytes -= buf->nAllocLen;

    hts_mutex_unlock(&rpd->rpd_mtx);

    buf->nOffset = 0;
    buf->nFilledLen = MIN(len, buf->nAllocLen);
    memcpy(buf->pBuffer, data, buf->nFilledLen);
    buf->nFlags = 0;

    if(len <= buf->nAllocLen)
      buf->nFlags |= OMX_BUFFERFLAG_EOS;

    data += buf->nFilledLen;
    len  -= buf->nFilledLen;
    omxchk(OMX_EmptyThisBuffer(rpd->rpd_decoder->oc_handle, buf));

    hts_mutex_lock(&rpd->rpd_mtx);
  }

  if(rpd->rpd_decoder->oc_stream_corrupt) {
    hts_mutex_unlock(&rpd->rpd_mtx);
    goto err;
  }

  if(rpd->rpd_change != 2) {
    while(rpd->rpd_change == 0 && !rpd->rpd_decoder->oc_stream_corrupt)
      hts_cond_wait(&rpd->rpd_cond, &rpd->rpd_mtx);

    

    hts_mutex_unlock(&rpd->rpd_mtx);
    if(rpd->rpd_decoder->oc_stream_corrupt)
      goto err;

    setup_tunnel(rpd);
  } else {
    hts_mutex_unlock(&rpd->rpd_mtx);
  }
#endif

  
  omx_wait_fill_buffer(rpd->rpd_resizer, rpd->rpd_buf);
  CHECKPOINT("Got buffer");

 err:
  omx_flush_port(rpd->rpd_decoder, rpd->rpd_decoder->oc_inport);
  omx_flush_port(rpd->rpd_decoder, rpd->rpd_decoder->oc_outport);

  omx_flush_port(rpd->rpd_resizer, rpd->rpd_resizer->oc_inport);
  omx_flush_port(rpd->rpd_resizer, rpd->rpd_resizer->oc_outport);



  if(rpd->rpd_tunnel != NULL) {
    omx_tunnel_destroy(rpd->rpd_tunnel);
    rpd->rpd_tunnel = NULL;
  }

  omx_set_state(rpd->rpd_decoder, OMX_StateIdle);
  omx_set_state(rpd->rpd_resizer, OMX_StateIdle);

  if(rpd->rpd_buf != NULL) {
    omxchk(OMX_FreeBuffer(rpd->rpd_resizer->oc_handle,
			  rpd->rpd_resizer->oc_outport, rpd->rpd_buf));
  }

  omx_release_buffers(rpd->rpd_decoder, rpd->rpd_decoder->oc_inport);

  omx_set_state(rpd->rpd_resizer, OMX_StateLoaded);
  omx_set_state(rpd->rpd_decoder, OMX_StateLoaded);

  omx_component_destroy(rpd->rpd_resizer);
  omx_component_destroy(rpd->rpd_decoder);
  hts_cond_destroy(&rpd->rpd_cond);
  hts_mutex_destroy(&rpd->rpd_mtx);

  pixmap_t *out = rpd->rpd_pm;
  if(out == NULL)
    snprintf(errbuf, errlen, "Load error");

  free(rpd);
  CHECKPOINT("All done");
  return out;
}


/**
 *
 */
void
rpi_pixmap_init(void)
{
  accel_image_decode = rpi_pixmap_decode;
}
