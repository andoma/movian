/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <libve.h>


#include "showtime.h"
#include "arch/sunxi/sunxi.h"
#include "video_decoder.h"
#include "cedar.h"
#include "h264_annexb.h"

#include <libavutil/common.h>


static hts_mutex_t cedar_mutex;

#ifdef __ANDROID__
#define OLD_VE
#endif

#define CEDAR_USE_HATA

// #define CEDAR_DECODE_LOCK

 #define CEDAR_SESSION_LOCK

// #define CEDAR_TRAP

#ifdef CEDAR_TRAP
#include <sys/mman.h>
#include <signal.h>
#include <ucontext.h>
#include <execinfo.h>

extern int (*extra_traphandler)(int sig, siginfo_t *si, void *UC);

static void *trap_page;
#endif



TAILQ_HEAD(cedar_packet_queue, cedar_packet);
TAILQ_HEAD(picture_queue, picture);

typedef struct picture {
  vpicture_t pic;  // Must be first
  TAILQ_ENTRY(picture) link;
  int frameid;
  struct fbm *fbm;
} picture_t;


typedef struct fbm {
  int refcount;
  int numpics;
  pixel_format_e fmt;

  hts_mutex_t fbm_mutex; // Protect queues
  struct picture_queue fbm_avail;
  struct picture_queue fbm_queued;
  struct picture_queue fbm_display;
  char fbm_name[64];  // Copy of mp_name

  picture_t pics[0];
} fbm_t;


typedef struct cedar_packet {
  vstream_data_t cp_vsd; // Must be first
  TAILQ_ENTRY(cedar_packet) cp_link;
} cedar_packet_t;

#define NUM_FRAME_META 64

typedef struct cedar_decoder {
  Handle *cd_ve;
  struct cedar_packet_queue cd_queue;
  
  int cd_layer;
  int cd_framecnt;

  int cd_metaptr;

  char cd_name[64];  // Copy of mp_name

  media_buf_t cd_meta[NUM_FRAME_META];
  int64_t cd_last_pts;
  int cd_estimated_duration;
} cedar_decoder_t;


static int countq(struct picture_queue *pq)
{
  int cnt = 0;
  picture_t *p;
  TAILQ_FOREACH(p, pq, link)
    cnt++;
  return cnt;
}
/**
 *
 */
static void
fbm_release(fbm_t *fbm)
{
  int i;

  if(atomic_add(&fbm->refcount, -1) > 1)
    return;
  hts_mutex_lock(&sunxi.gfxmem_mutex);

  int queued  = countq(&fbm->fbm_queued);
  int display = countq(&fbm->fbm_display);

  for(i = 0; i < fbm->numpics; i++) {
    tlsf_free(sunxi.gfxmem, fbm->pics[i].pic.y);
    tlsf_free(sunxi.gfxmem, fbm->pics[i].pic.u);
    tlsf_free(sunxi.gfxmem, fbm->pics[i].pic.v);
  }
  hts_mutex_unlock(&sunxi.gfxmem_mutex);

  hts_mutex_destroy(&fbm->fbm_mutex);

  if(queued)
    panic("FBM %p (%s) still have queued frames", fbm, fbm->fbm_name);

  if(display)
    panic("FBM %p (%s) still have displaying frames", fbm, fbm->fbm_name);

  free(fbm);
}


/**
 *
 */
void
cedar_frame_done(void *P)
{
  picture_t *pic = P;
  fbm_t *fbm = pic->fbm;
  hts_mutex_lock(&fbm->fbm_mutex);
  TAILQ_REMOVE(&fbm->fbm_display, pic, link);
  TAILQ_INSERT_HEAD(&fbm->fbm_avail, pic, link);
  hts_mutex_unlock(&fbm->fbm_mutex);
  fbm_release(fbm);
}

/**
 *
 */
static void
cedar_flush(struct media_codec *mc, struct video_decoder *vd)
{
  cedar_decoder_t *cd = mc->opaque;
  libve_reset(1, cd->cd_ve);
  cd->cd_last_pts = AV_NOPTS_VALUE;

}

/**
 *
 */
static void
cedar_decode(struct media_codec *mc, struct video_decoder *vd,
	     struct media_queue *mq, struct media_buf *mb, int reqsize)
{
  cedar_decoder_t *cd = mc->opaque;
  cedar_packet_t *cp = calloc(1, sizeof(cedar_packet_t));

  //  assert(mb->mb_pts != AV_NOPTS_VALUE);
  //  printf("=== PACKET DECODE START ====================================\n");
  //  hexdump("PACKET", mb->mb_data, MIN(16, mb->mb_size));
  // Copy packet to cedar mem
  hts_mutex_lock(&sunxi.gfxmem_mutex);
  cp->cp_vsd.data = tlsf_malloc(sunxi.gfxmem, mb->mb_size);
  hts_mutex_unlock(&sunxi.gfxmem_mutex);
  memcpy(cp->cp_vsd.data, mb->mb_data, mb->mb_size);
  cp->cp_vsd.length = mb->mb_size;

  cd->cd_meta[cd->cd_metaptr] = *mb;
  cp->cp_vsd.pcr = cd->cd_metaptr;
  cd->cd_metaptr = (cd->cd_metaptr + 1) & (NUM_FRAME_META-1);

  cp->cp_vsd.valid = 1;
  TAILQ_INSERT_TAIL(&cd->cd_queue, cp, cp_link);

#ifdef CEDAR_DECODE_LOCK
  hts_mutex_lock(&cedar_mutex);
#endif
  //  printf("%s: decode start\n", cd->cd_name);

  int64_t ts = showtime_get_ts();

  avgtime_start(&vd->vd_decode_time);
  vresult_e res = libve_decode(0, 0, 0, cd->cd_ve);
  avgtime_stop(&vd->vd_decode_time, mq->mq_prop_decode_avg,
	       mq->mq_prop_decode_peak);


  if(res < 0) {
    TRACE(TRACE_ERROR, "CEDAR", "libve_decode() failed, possible mem leak");
#ifdef CEDAR_DECODE_LOCK
    hts_mutex_unlock(&cedar_mutex);
#endif
    return;
  }

  int dectime = showtime_get_ts() - ts;
  if(dectime > 1000000) {
    printf("%s: Decode time %d very high\n", cd->cd_name, dectime);
    exit(0);
  }
  fbm_t *fbm = libve_get_fbm(cd->cd_ve);
  if(fbm == NULL) {
    printf("%s: FBM IS NULL\n", cd->cd_name);
#ifdef CEDAR_DECODE_LOCK
    hts_mutex_unlock(&cedar_mutex);
#endif
    return;
  }
  snprintf(fbm->fbm_name, sizeof(fbm->fbm_name), "%s", cd->cd_name);

  //  printf("%s: decode end\n", cd->cd_name);

#ifdef CEDAR_DECODE_LOCK
  hts_mutex_unlock(&cedar_mutex);
#endif

  picture_t *pic;
  hts_mutex_lock(&fbm->fbm_mutex);
  while((pic = TAILQ_FIRST(&fbm->fbm_queued)) != NULL) {
    TAILQ_REMOVE(&fbm->fbm_queued, pic, link);

    assert(pic->pic.pcr < NUM_FRAME_META && pic->pic.pcr >= 0);

    mb = &cd->cd_meta[pic->pic.pcr];

    if(cd->cd_last_pts != AV_NOPTS_VALUE && mb->mb_pts != AV_NOPTS_VALUE) {
      int64_t d = mb->mb_pts - cd->cd_last_pts;

      if(d > 1000 && d < 100000)
	cd->cd_estimated_duration = d;
    }

    frame_info_t fi;
    memset(&fi, 0, sizeof(fi));
    fi.fi_pts         = mb->mb_pts;
    fi.fi_epoch       = mb->mb_epoch;
    fi.fi_delta       = mb->mb_delta;
    fi.fi_duration    = mb->mb_duration > 10000 ?
      mb->mb_duration : cd->cd_estimated_duration;

    fi.fi_drive_clock = mb->mb_drive_clock;
    fi.fi_interlaced  =!pic->pic.is_progressive;
    fi.fi_tff         = pic->pic.top_field_first;
    fi.fi_width       = pic->pic.display_width;
    fi.fi_height      = pic->pic.display_height;
    fi.fi_pix_fmt     = pic->pic.pixel_format;
    fi.fi_dar_num     = pic->pic.display_width * pic->pic.aspect_ratio / 1000.0;
    fi.fi_dar_den     = pic->pic.display_height;
    fi.fi_type        = 'CEDR';

    vd->vd_estimated_duration = fi.fi_duration;
    cd->cd_last_pts   = fi.fi_pts;

    assert(pic->pic.y != NULL);
    if(fi.fi_duration > 0 && !mb->mb_skip) {
      fi.fi_data[0] = pic->pic.y;
      fi.fi_data[1] = pic->pic.u;
      fi.fi_data[2] = pic->pic.v;
      fi.fi_data[3] = (void *)pic;
      
      atomic_add(&fbm->refcount, 1);
      TAILQ_INSERT_HEAD(&fbm->fbm_display, pic, link);

      hts_mutex_unlock(&fbm->fbm_mutex);
      video_deliver_frame(vd, &fi);
      hts_mutex_lock(&fbm->fbm_mutex);

    } else {
      TAILQ_INSERT_HEAD(&fbm->fbm_avail, pic, link);

    }
  }
  hts_mutex_unlock(&fbm->fbm_mutex);
}


static void
cedar_close(struct media_codec *mc)
{
  cedar_decoder_t *cd = mc->opaque;
  libve_close(1, cd->cd_ve);
  free(cd);
  printf("%s: Close\n", cd->cd_name);
#ifdef CEDAR_SESSION_LOCK
  hts_mutex_unlock(&cedar_mutex);
#endif
}






/**
 *
 */
static int
cedar_codec_open(media_codec_t *mc, const const media_codec_params_t *mcp,
		 media_pipe_t *mp)
{
  vconfig_t cfg = {0};
  vstream_info_t si = {0};

  cfg.max_video_width  = 3840;
  cfg.max_video_height = 2160;

  switch(mc->codec_id) {
  default:
    return 1;

  case CODEC_ID_H264:
    si.format = STREAM_FORMAT_H264;
#ifdef CEDAR_USE_HATA
    if(mcp == NULL || mcp->extradata == NULL || mcp->extradata_size == 0 ||
       ((const uint8_t *)mcp->extradata)[0] != 1)
      return h264_annexb_to_avc(mc, mp, &cedar_codec_open);
#endif

    if(mcp != NULL) {
      si.init_data     = (void *)mcp->extradata;
      si.init_data_len = mcp->extradata_size;
    }
    break;

  case CODEC_ID_MPEG2VIDEO:
    return 1;
    si.format = STREAM_FORMAT_MPEG2;
    si.sub_format = MPEG2_SUB_FORMAT_MPEG2;
    break;

#if 0

  case CODEC_ID_MPEG4:
    return 1;
    si.format = STREAM_FORMAT_MPEG4;

    if(mc->codec_ctx == NULL)
      return 1;

    switch(mc->codec_ctx->codec_tag) {
    case MKTAG('X','V','I','D'):
    case MKTAG('m','p','4','v'):
    case MKTAG('p','m','p','4'):
    case MKTAG('f','m','p','4'):
      si.sub_format = MPEG4_SUB_FORMAT_XVID;
      break;

    case MKTAG('D','X','4','0'):
    case MKTAG('D','I','V','X'):
      si.sub_format = MPEG4_SUB_FORMAT_DIVX4;
      break;

    case MKTAG('D','X','5','0'):
    case MKTAG('d','i','v','5'):
      si.sub_format = MPEG4_SUB_FORMAT_DIVX5;
      break;

    default:
      return 1;
    }
    break;
#endif

  case CODEC_ID_H263:
    return 1;
    si.format = STREAM_FORMAT_MPEG4;
    si.sub_format = MPEG4_SUB_FORMAT_H263;
    break;
  }

#ifdef CEDAR_SESSION_LOCK
  hts_mutex_lock(&cedar_mutex);
#endif

#ifdef OLD_VE
  Handle *ve = libve_open(&cfg, &si);
#else
  Handle *ve = libve_open(&cfg, &si, NULL);
#endif
  if(ve == NULL) {
    TRACE(TRACE_ERROR, "libve", "Unable to open libve");
#ifdef CEDAR_SESSION_LOCK
    hts_mutex_unlock(&cedar_mutex);
#endif
    return 1;
  }


  cedar_decoder_t *cd = calloc(1, sizeof(cedar_decoder_t));
  snprintf(cd->cd_name, sizeof(cd->cd_name), "%s", mp->mp_name);
  cd->cd_ve = ve;
  TAILQ_INIT(&cd->cd_queue);
  libve_set_vbv(cd, ve);

  cd->cd_last_pts = AV_NOPTS_VALUE;

  mc->opaque = cd;
  mc->decode = cedar_decode;
  mc->close = cedar_close;
  mc->flush = cedar_flush;
  return 0;
}



/****************************************************************************
 * VE interface
 */
static void
ve_reset_hardware(void)
{
  //  ioctl(sunxi.cedarfd, IOCTL_RESET_VE, 0);
}


static void
ve_enable_clock(u8 enable, u32 speed)
{
  if(enable) {
    ioctl(sunxi.cedarfd, IOCTL_ENABLE_VE, 0);
    printf("Settings %d instead of %d\n", 240, speed);
    //    ioctl(sunxi.cedarfd, IOCTL_SET_VE_FREQ, 240);
  } else {
    ioctl(sunxi.cedarfd, IOCTL_DISABLE_VE, 0);
  }
}

static void
ve_enable_intr(u8 enable)
{
  // NOP on Linux
}

static s32
ve_wait_intr(void)
{
  return ioctl(sunxi.cedarfd, IOCTL_WAIT_VE, 2) - 1;
}

static u32
ve_get_reg_base_addr(void)
{
#ifdef CEDAR_TRAP
  return (intptr_t)trap_page;
#else
  return (intptr_t)sunxi.macc;
#endif
}

static memtype_e
ve_get_memtype(void)
{
  return MEMTYPE_DDR3_32BITS;
}


IVEControl_t IVE = {
    ve_reset_hardware,
    ve_enable_clock,
    ve_enable_intr,
    ve_wait_intr,
    ve_get_reg_base_addr,
    ve_get_memtype
};

/****************************************************************************
 * OS interface
 */
static void *mem_alloc(u32 size)
{
  return malloc(size);
}

static void
mem_free(void *p)
{
  free(p);
}

static void *
mem_palloc(u32 size, u32 align)
{
  if(size == 0)
    size = align;
  hts_mutex_lock(&sunxi.gfxmem_mutex);
  void *r = tlsf_memalign(sunxi.gfxmem, align, size);
  hts_mutex_unlock(&sunxi.gfxmem_mutex);
  return r;
}


static void
mem_pfree(void *p)
{
  hts_mutex_lock(&sunxi.gfxmem_mutex);
  tlsf_free(sunxi.gfxmem, p);
  hts_mutex_unlock(&sunxi.gfxmem_mutex);
}


static void
mem_set(void *mem, u32 value, u32 size)
{
  memset(mem, value, size);
}


static void
mem_cpy(void *dst, void *src, u32 size)
{
  memcpy(dst, src, size);
}

static void
mem_flush_cache(u8 *mem, u32 size)
{
  // NOP on linux
}

static u32
mem_get_phy_addr(u32 virtual_addr)
{
  return virtual_addr - sunxi.gfxmembase + sunxi.env_info.phymem_start;
}


static s32
sys_print(u8 *func, u32 line, ...)
{
  printf("%s\n", __FUNCTION__);

  va_list ap;
  va_start(ap, line);
  //  int r = vsnprintf(buf, sizeof(buf), ap);
  va_end(ap);
  return 1;
}



static void
sys_sleep(u32 ms)
{
  usleep(ms * 1000);
}



IOS_t IOS = {
  mem_alloc,
  mem_free,
  mem_palloc,
  mem_pfree,
  mem_set,
  mem_cpy,
  mem_flush_cache,
  mem_get_phy_addr,
  sys_print,
  sys_sleep
};

/****************************************************************************
 * FBM interface
 */

static Handle
fbm_init(u32 max_frame_num, u32 min_frame_num,
	 u32 size_y, u32 size_u, u32 size_v,
	 u32 size_a, pixel_format_e format)
{
  int i;
  fbm_t *fbm = calloc(1, sizeof(fbm_t) + sizeof(picture_t) * max_frame_num);

  TAILQ_INIT(&fbm->fbm_avail);
  TAILQ_INIT(&fbm->fbm_queued);
  TAILQ_INIT(&fbm->fbm_display);
  hts_mutex_init(&fbm->fbm_mutex);
  fbm->refcount = 1;

  fbm->numpics = max_frame_num;
  fbm->fmt = format;
  hts_mutex_lock(&sunxi.gfxmem_mutex);
  for(i = 0; i < max_frame_num; i++) {
    fbm->pics[i].pic.id = i;
    fbm->pics[i].fbm = fbm;
    fbm->pics[i].pic.size_y     = size_y;
    fbm->pics[i].pic.size_u     = size_u;
    fbm->pics[i].pic.size_v     = size_v;



    if(size_y) {
      fbm->pics[i].pic.y = tlsf_memalign(sunxi.gfxmem, 1024, size_y);
      if(fbm->pics[i].pic.y == NULL)
        panic("Out of CEDAR memory");
    } else {
      fbm->pics[i].pic.y = NULL;
    }

    if(size_u) {
      fbm->pics[i].pic.u = tlsf_memalign(sunxi.gfxmem, 1024, size_u);
      if(fbm->pics[i].pic.u == NULL)
        panic("Out of CEDAR memory");
    } else {
      fbm->pics[i].pic.u = NULL;
    }

    if(size_v) {
      fbm->pics[i].pic.v = tlsf_memalign(sunxi.gfxmem, 1024, size_v);
      if(fbm->pics[i].pic.v == NULL)
        panic("Out of CEDAR memory");
    } else {
      fbm->pics[i].pic.v = NULL;
    }
    TAILQ_INSERT_HEAD(&fbm->fbm_avail, &fbm->pics[i], link);
  }
  hts_mutex_unlock(&sunxi.gfxmem_mutex);

  return fbm;
}

#ifdef OLD_VE
static Handle
fbm_init_ex(u32 max_frame_num, u32 min_frame_num,
	    u32 size_y[], u32 size_u[], u32 size_v[],
	    u32 size_alpha[], _3d_mode_e _3d_mode,
	    pixel_format_e format)
{
  return fbm_init(max_frame_num, min_frame_num,
		  size_y[0], size_u[0], size_v[0], size_alpha[0],
		  format);
}


#else

static Handle
fbm_init_ex(u32 max_frame_num, u32 min_frame_num,
	    u32 size_y[], u32 size_u[], u32 size_v[],
	    u32 size_alpha[], _3d_mode_e _3d_mode,
	    pixel_format_e format, uint8_t unknown, void *parent)
{
  fbm_t *fbm;
  fbm = fbm_init(max_frame_num, min_frame_num,
		 size_y[0], size_u[0], size_v[0], size_alpha[0],
		 format);
  return fbm;
}
#endif


#ifdef OLD_VE
static void
fbm_deinit(Handle h)
#else
static void
fbm_deinit(Handle h, void *parent)
#endif
{
  fbm_t *fbm = h;
  fbm_release(fbm);
}


static vpicture_t *
fbm_decoder_request_frame(Handle h)
{
  fbm_t *fbm = h;

  int64_t ts = showtime_get_ts();
  hts_mutex_lock(&fbm->fbm_mutex);
  ts = showtime_get_ts() - ts;
  if(ts > 100000) {
    panic("Request frame long timeout %lld", ts);
  }
  picture_t *pic = TAILQ_FIRST(&fbm->fbm_avail);
  if(pic != NULL)
    TAILQ_REMOVE(&fbm->fbm_avail, pic, link);
  hts_mutex_unlock(&fbm->fbm_mutex);
  return &pic->pic;
}



static void
fbm_decoder_return_frame(vpicture_t *f, u8 valid, Handle h)
{
  fbm_t *fbm = h;
  picture_t *pic = (picture_t *)f;
  if(f == NULL)
    return;

  hts_mutex_lock(&fbm->fbm_mutex);
  if(valid) {
    TAILQ_INSERT_TAIL(&fbm->fbm_queued, pic, link);
  } else {
    printf("%s: Return invalid frame: %d %p\n", fbm->fbm_name, valid, fbm);
    TAILQ_INSERT_HEAD(&fbm->fbm_avail, pic, link);
  }
  hts_mutex_unlock(&fbm->fbm_mutex);
}


static void
fbm_decoder_share_frame(vpicture_t *f, Handle h)
{
  printf("%s\n", __FUNCTION__);

  printf("f=%p\n", f);
  if(f == NULL)
    return;
  printf("     dim: %d x %d\n", f->width, f->height);
  printf("  stored: %d x %d\n", f->store_width, f->store_height);
  printf("  offset: %d x %d\n", f->top_offset, f->left_offset);
  printf(" display: %d x %d\n", f->display_width, f->display_height);
  printf("      FR: %d\n", f->frame_rate);
  printf("  aspect: %f\n", (float)f->aspect_ratio / 1000);
  printf("  progressive: %d  TFF: %d, RTF: %d, RBF: %d\n",
	 f->is_progressive, f->top_field_first, 
	 f->repeat_top_field, f->repeat_bottom_field);
  printf("  pixel_format: %x\n", f->pixel_format);
  printf("     PTS: %lld\n", f->pts);

  abort();
}

#ifndef OLD_VE


static Handle
fbm_init_ex_yv12(u32 max_frame_num, u32 min_frame_num,
		 u32 size_y[], u32 size_u[], u32 size_v[],
		 u32 size_alpha[], _3d_mode_e _3d_mode,
		 pixel_format_e format, uint8_t unknown, void *parent)
{
  printf("%s\n", __FUNCTION__);
  abort();
}

static Handle
fbm_init_ex_yv32(u32 max_frame_num, u32 min_frame_num,
		 u32 size_y[], u32 size_u[], u32 size_v[],
		 u32 size_alpha[], _3d_mode_e _3d_mode,
		 pixel_format_e format, uint8_t unknown, void *parent)
{
  printf("%s\n", __FUNCTION__);
  abort();
}


static void
fbm_flush_frame(Handle h, s64 pts)
{
  printf("%s\n", __FUNCTION__);
}

static void
fbm_print_status(Handle h)
{
  printf("%s\n", __FUNCTION__);
}

static void
fbm_alloc_YV12_frame_buffer(Handle h)
{
  printf("%s\n", __FUNCTION__);
}

#endif

#ifdef OLD_VE


IFBM_t IFBM = {
  fbm_init,
  fbm_deinit,
  fbm_decoder_request_frame,
  fbm_decoder_return_frame,
  fbm_decoder_share_frame,
  fbm_init_ex,
};

#else


IFBM_t IFBM = {
  fbm_deinit,
  fbm_decoder_request_frame,
  fbm_decoder_return_frame,
  fbm_decoder_share_frame,
  fbm_init_ex,
  fbm_init_ex_yv12,
  fbm_init_ex_yv32,
  fbm_flush_frame,
  fbm_print_status,
  fbm_alloc_YV12_frame_buffer,
};

#endif

/****************************************************************************
 * VBV interface
 */
static vstream_data_t *
vbv_request_stream_frame(Handle vbv)
{
  cedar_decoder_t *cd = vbv;
  cedar_packet_t *cp = TAILQ_FIRST(&cd->cd_queue);
  if(cp == NULL)
    return NULL;

  TAILQ_REMOVE(&cd->cd_queue, cp, cp_link);
  return &cp->cp_vsd;
}

    
static void
vbv_return_stream_frame(vstream_data_t *stream, Handle vbv)
{
  cedar_decoder_t *cd = vbv;
  cedar_packet_t *cp = (cedar_packet_t *)stream;
  TAILQ_INSERT_HEAD(&cd->cd_queue, cp, cp_link);
}


static void
vbv_flush_stream_frame(vstream_data_t *stream, Handle vbv)
{
  //  cedar_decoder_t *cd = vbv;
  cedar_packet_t *cp = (cedar_packet_t *)stream;
  hts_mutex_lock(&sunxi.gfxmem_mutex);
  tlsf_free(sunxi.gfxmem, cp->cp_vsd.data);
  hts_mutex_unlock(&sunxi.gfxmem_mutex);
  free(cp);
}

static u8 *
vbv_get_base_addr(Handle vbv)
{
  return sunxi.gfxmem;
}
    
static u32
vbv_get_buffer_size(Handle vbv)
{
  return sunxi.env_info.phymem_total_size;
}




IVBV_t IVBV = {
  vbv_request_stream_frame,
  vbv_return_stream_frame,
  vbv_flush_stream_frame,
  vbv_get_base_addr,
  vbv_get_buffer_size
};



int cedarv_f23_ic_version(void);

int
cedarv_f23_ic_version(void)
{
  return 1;
}



#ifdef CEDAR_TRAP

static uint32_t
getreg(const ucontext_t *uc, int r)
{
  switch(r) {
  case 0:   return uc->uc_mcontext.arm_r0;
  case 1:   return uc->uc_mcontext.arm_r1;
  case 2:   return uc->uc_mcontext.arm_r2;
  case 3:   return uc->uc_mcontext.arm_r3;
  case 4:   return uc->uc_mcontext.arm_r4;
  case 5:   return uc->uc_mcontext.arm_r5;
  case 6:   return uc->uc_mcontext.arm_r6;
  case 7:   return uc->uc_mcontext.arm_r7;
  case 8:   return uc->uc_mcontext.arm_r8;
  case 9:   return uc->uc_mcontext.arm_r9;
  case 10:  return uc->uc_mcontext.arm_r10;
  case 11:  return uc->uc_mcontext.arm_fp;
  case 12:  return uc->uc_mcontext.arm_ip;
  case 13:  return uc->uc_mcontext.arm_sp;
  case 14:  return uc->uc_mcontext.arm_lr;
  case 15:  return uc->uc_mcontext.arm_pc;
  }
  printf("Invalid register: %d\n", r);
  return 0;
}




static void
setreg(ucontext_t *uc, int r, uint32_t v)
{
  switch(r) {
  case 0:   uc->uc_mcontext.arm_r0 = v; return;
  case 1:   uc->uc_mcontext.arm_r1 = v; return;
  case 2:   uc->uc_mcontext.arm_r2 = v; return;
  case 3:   uc->uc_mcontext.arm_r3 = v; return;
  case 4:   uc->uc_mcontext.arm_r4 = v; return;
  case 5:   uc->uc_mcontext.arm_r5 = v; return;
  case 6:   uc->uc_mcontext.arm_r6 = v; return;
  case 7:   uc->uc_mcontext.arm_r7 = v; return;
  case 8:   uc->uc_mcontext.arm_r8 = v; return;
  case 9:   uc->uc_mcontext.arm_r9 = v; return;
  case 10:  uc->uc_mcontext.arm_r10 = v; return;
  case 11:  uc->uc_mcontext.arm_fp = v; return;
  case 12:  uc->uc_mcontext.arm_ip = v; return;
  case 13:  uc->uc_mcontext.arm_sp = v; return;
  case 14:  uc->uc_mcontext.arm_lr = v; return;
  case 15:  uc->uc_mcontext.arm_pc = v; return;
  }
  printf("Invalid register: %d\n", r);
}



static void
bump_pc(ucontext_t *uc, int delta)
{
  uc->uc_mcontext.arm_pc += delta;
}

static int
cedar_trap(int sig, siginfo_t *si, void *UC)
{
  if(sig != SIGSEGV && sig != SIGBUS)
    return 1;

  ptrdiff_t offset = si->si_addr - trap_page;

  if(offset < 0 || offset >= 4096)
    return 1;

  ucontext_t *uc = UC;

  uint32_t pc =  uc->uc_mcontext.arm_pc;

  if(uc->uc_mcontext.arm_cpsr & 0x20) {
    // Thumb mode
    uint16_t opcode1 = *(uint16_t *)pc;

    const int thumb2 = 1;

    if(thumb2) {
      if((opcode1 & 0xe000) == 0xe000) {

	uint16_t opcode2 = *(uint16_t *)(pc + 2);

	if((opcode1 & 0xfff0) == 0xf8b0) {
	  // Load Register Halfword
	  const int Rn = opcode1 & 0xf;
	  const int Rt = opcode2 >> 12;

	  int ea = getreg(uc, Rn) + (opcode2 & 0xfff);
	  unsigned int off = ea - (intptr_t)trap_page;
	  assert(off < 4096);

	  uint32_t v = *(uint16_t *)(sunxi.macc + off);
	  printf(" Read16(0x%04x) = 0x%04x\n", off, v);
	  setreg(uc, Rt, v);
	  bump_pc(uc, 4);
	  return 0;
	}

	if((opcode1 & 0xfff0) == 0xf8d0) {
	  // A6.7.42 LDR Immediate - Encoding T3
	  const int Rn = opcode1 & 0xf;
	  const int Rt = opcode2 >> 12;

	  int ea = getreg(uc, Rn) + (opcode2 & 0xfff);
	  unsigned int off = ea - (intptr_t)trap_page;
	  assert(off < 4096);

	  uint32_t v = *(uint32_t *)(sunxi.macc + off);

	  cedar_decode_read32(off, v);
	  setreg(uc, Rt, v);
	  bump_pc(uc, 4);
	  return 0;
	}


	if((opcode1 & 0xfff0) == 0xf8c0) {
	  // A6.7.119 STR Immediate - Encoding T3
	  const int Rn = opcode1 & 0xf;
	  const int Rt = opcode2 >> 12;

	  int ea = getreg(uc, Rn) + (opcode2 & 0xfff);
	  unsigned int off = ea - (intptr_t)trap_page;
	  assert(off < 4096);

	  uint32_t v = getreg(uc, Rt);
	  *(uint32_t *)(sunxi.macc + off) = v;
	  cedar_decode_write32(off, v);
	  bump_pc(uc, 4);
	  return 0;
	}

      } else {
	
	if((opcode1 & 0xf800) == 0x6800) {
	  // A6.7.42 LDR - Encoding T1
	  const unsigned int imm5 = ((opcode1 >> 6) & 0x1f) << 2;
	  const int Rn = (opcode1 >> 3) & 0x7;
	  const int Rt =  opcode1 & 0x7;

	  int ea = getreg(uc, Rn) + imm5;
	  unsigned int off = ea - (intptr_t)trap_page;
	  assert(off < 4096);

	  uint32_t v = *(uint32_t *)(sunxi.macc + off);
	  cedar_decode_read32(off, v);
	  setreg(uc, Rt, v);
	  bump_pc(uc, 2);
	  return 0;
	}

	if((opcode1 & 0xf800) == 0x6000) {
	  // STR - Encoding T1
	  const unsigned int imm5 = ((opcode1 >> 6) & 0x1f) << 2;
	  const int Rn = (opcode1 >> 3) & 0x7;
	  const int Rt =  opcode1 & 0x7;

	  int ea = getreg(uc, Rn) + imm5;
	  unsigned int off = ea - (intptr_t)trap_page;
	  assert(off < 4096);

	  uint32_t v = getreg(uc, Rt);
	  *(uint32_t *)(sunxi.macc + off) = v;
	  cedar_decode_write32(off, v);
	  bump_pc(uc, 2);
	  return 0;
	}
      }
    }
  }

  return 1;
}
#endif


static void
cedar_codec_init(void)
{
  hts_mutex_init(&cedar_mutex);
  ioctl(sunxi.cedarfd, IOCTL_RESET_VE, 0);

#ifdef CEDAR_TRAP
  extra_traphandler = cedar_trap;

  trap_page = mmap(NULL, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  assert(trap_page != MAP_FAILED);
#endif
}

REGISTER_CODEC(cedar_codec_init, cedar_codec_open, 10);
