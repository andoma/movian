/*
 *  Video decoding using CELL vdec
 *  Copyright (C) 2011 Andreas Öman
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

#include <codec/vdec.h>
#include <assert.h>

#include <sysmodule/sysmodule.h>
#include <psl1ght/lv2.h>

#include "arch/threads.h"
#include "showtime.h"
#include "ps3_vdec.h"
#include "video_decoder.h"
#include "video_settings.h"
#include "arch/halloc.h"
#include "notifications.h"

static int vdec_mpeg2_loaded;
static int vdec_h264_loaded;

#define VDEC_DETAILED_DEBUG 0

#define VDEC_SPU_PRIO 100
#define VDEC_PPU_PRIO 1000

LIST_HEAD(vdec_pic_list, vdec_pic);


typedef struct pktmeta {
  int64_t delta;
  int epoch;
  char skip : 1;
  char flush : 1; 
  char nopts : 1;
  char nodts : 1;
  char drive_clock : 1;
} pktmeta_t;


/**
 * Pictures are delivered out of order from the decoder so we need
 * to deal with that
 */
typedef struct vdec_pic {
  LIST_ENTRY(vdec_pic) link;

  frame_info_t fi;
  rsx_video_frame_t rvf;
  int64_t order;
  int send_pts;
} vdec_pic_t;



/**
 *
 */
static int
vp_cmp(const vdec_pic_t *a, const vdec_pic_t *b)
{
  if(a->order < b->order)
    return -1;
  if(a->order > b->order)
    return 1;
  return 0;
}

/**
 * Pointed to by media_codec->opaque
 */
typedef struct vdec_decoder {
  uint32_t handle;
  struct vdec_config config;
  void *mem;

  hts_mutex_t mtx;
  hts_cond_t audone;
  hts_cond_t seqdone;

  int sequence_done;

  size_t extradata_size;
  uint8_t *extradata;
  int extradata_injected;

  int convert_to_annexb;

  struct vdec_pic_list pictures;

  prop_t *metainfo;
  uint8_t level_major;
  uint8_t level_minor;

  int pending_blackout;
  int submitted_au;
  int pending_flush;

  int64_t max_order;
  int64_t order_base;
  int64_t flush_to;
  
  pktmeta_t pktmeta[64];
  int pktmeta_cur;

} vdec_decoder_t;

/**
 *
 */
void
video_ps3_vdec_init(void)
{
  vdec_mpeg2_loaded = !SysLoadModule(SYSMODULE_VDEC_MPEG2);
  if(!vdec_mpeg2_loaded)
    TRACE(TRACE_ERROR, "VDEC", "Unable to load MPEG2 decoder");

  vdec_h264_loaded = !SysLoadModule(SYSMODULE_VDEC_H264);
  if(!vdec_h264_loaded)
    TRACE(TRACE_ERROR, "VDEC", "Unable to load H264 decoder");
}


/**
 *
 */
static void
end_sequence_and_wait(vdec_decoder_t *vdd)
{
  TRACE(TRACE_DEBUG, "VDEC", "Waiting for end sequence");
  vdd->sequence_done = 0;
  vdec_end_sequence(vdd->handle);
  hts_mutex_lock(&vdd->mtx);
  while(vdd->sequence_done == 0)
    hts_cond_wait(&vdd->seqdone, &vdd->mtx);
  hts_mutex_unlock(&vdd->mtx);
  TRACE(TRACE_DEBUG, "VDEC", "Waiting for end sequence -> done");
}

int rsx_alloc(int size, int alignment);

void rsx_free(int pos, int size);

extern char *rsx_address;

#define rsx_to_ppu(pos) ((void *)(rsx_address + (pos)))

/**
 * yuv420 only
 */
static void
release_picture(vdec_pic_t *vp)
{
  rsx_free(vp->rvf.rvf_offset, vp->rvf.rvf_size);
  LIST_REMOVE(vp, link);
  free(vp);
}



/**
 * yuv420 only
 */
static vdec_pic_t *
alloc_picture(vdec_decoder_t *vdd, int lumasize)
{
  vdec_pic_t *vp = malloc(sizeof(vdec_pic_t));
  vp->rvf.rvf_size = lumasize + lumasize / 2;
  vp->rvf.rvf_offset = rsx_alloc(vp->rvf.rvf_size, 16);
  if(vp->rvf.rvf_offset == -1)
    panic("Cell decoder out of RSX memory");
  return vp;
}


/**
 *
 */
static void
free_picture_list(struct vdec_pic_list *l)
{
  vdec_pic_t *vp;
  while((vp = LIST_FIRST(l)) != NULL) {
    TRACE(TRACE_DEBUG, "VDEC", "Free picture %p", vp);
    TRACE(TRACE_DEBUG, "VDEC", "Free RSX mem %x +%d", 
	  vp->rvf.rvf_offset, vp->rvf.rvf_size);
    release_picture(vp);
  }
}


/**
 *
 */
static void
reset_active_pictures(vdec_decoder_t *vdd, const char *reason, int marked)
{
  vdec_pic_t *vp;

#if VDEC_DETAILED_DEBUG
  TRACE(TRACE_DEBUG, "VDEC DEC", "RESET: %s", reason);
#endif
  hts_mutex_lock(&vdd->mtx);
  while((vp = LIST_FIRST(&vdd->pictures)) != NULL)
    release_picture(vp);
  hts_mutex_unlock(&vdd->mtx);
}



const static int mpeg_durations[] = {
  40000,
  41700,
  41666,
  40000,
  33366,
  33333,
  20000,
  16677,
  16666
};


const static uint8_t h264_sar[][2] = {
 {1,  1},
 {1,  1},
 {12, 11},
 {10, 11},
 {16, 11},
 {40, 33},
 {24, 11},
 {20, 11},
 {32, 11},
 {80, 33},
 {18, 11},
 {15, 11},
 {64, 33},
 {160,99},
 {4,  3},
 {3,  2},
 {2,  1}
};
  

/**
 *
 */
static void
emit_frame(video_decoder_t *vd, vdec_pic_t *vp)
{
  vd->vd_estimated_duration = vp->fi.fi_duration; // For bitrate calculations

  if(vp->fi.fi_pts == AV_NOPTS_VALUE && vd->vd_nextpts != AV_NOPTS_VALUE)
    vp->fi.fi_pts = vd->vd_nextpts;

  if(vp->fi.fi_pts != AV_NOPTS_VALUE)
    vd->vd_nextpts = vp->fi.fi_pts + vp->fi.fi_duration;

#if VDEC_DETAILED_DEBUG
  static int64_t lastpts;
  TRACE(TRACE_DEBUG, "VDEC DPY", "Displaying 0x%llx (%lld) d:%lld dur=%d", vp->order,
	vp->fi.fi_pts, vp->fi.fi_pts - lastpts, vp->fi.fi_duration);
  lastpts = vp->fi.fi_pts;
#endif

  video_deliver_frame(vd, FRAME_BUFFER_TYPE_RSX_MEMORY, &vp->rvf,
		      &vp->fi);

#if VDEC_DETAILED_DEBUG
  TRACE(TRACE_DEBUG, "VDEC DPY", "Frame delivered");
#endif
}


/**
 *
 */
static void
picture_out(vdec_decoder_t *vdd)
{
  int r, lumasize;
  uint32_t addr;
  vdec_picture_format picfmt;
  vdec_pic_t *vp;
  char metainfo[64];
  pktmeta_t *pm;

  picfmt.alpha = 0;
  picfmt.format_type = VDEC_PICFMT_YUV420P;
  picfmt.color_matrix = VDEC_COLOR_MATRIX_BT709;

  r = vdec_get_pic_item(vdd->handle, &addr);
  if(r != 0)
    return;


  vdec_picture *pi = (void *)(intptr_t)addr;

  pm = &vdd->pktmeta[pi->userdata[0]];

  vdd->pending_flush |= pm->flush;

  if(/* pi->status != 0 ||*/ pi->attr != 0 || pm->skip) {
    vdec_get_picture(vdd->handle, &picfmt, NULL);
    reset_active_pictures(vdd, "pic err", 0);
    return;
  }

  if(vdd->pending_flush) {
    reset_active_pictures(vdd, "stream flush", 0);
    vdd->pending_flush = 0;
    vdd->max_order = -1;
  }

  int64_t pts = pm->nopts ? AV_NOPTS_VALUE : 
    pi->pts[0].low + ((uint64_t)pi->pts[0].hi << 32);

  int64_t dts = pm->nodts ? AV_NOPTS_VALUE : 
    pi->dts[0].low + ((uint64_t)pi->dts[0].hi << 32);
  int64_t order;

  if(pi->codec_type == VDEC_CODEC_TYPE_MPEG2) {
    vdec_mpeg2_info *mpeg2 = (void *)(intptr_t)pi->codec_specific_addr;

    lumasize = mpeg2->width * mpeg2->height;
    vp = alloc_picture(vdd, lumasize);

    vp->fi.fi_width = mpeg2->width;
    vp->fi.fi_height = mpeg2->height;
    vp->fi.fi_duration = mpeg2->frame_rate <= 8 ? 
      mpeg_durations[mpeg2->frame_rate] : 40000;
    vp->fi.fi_interlaced = !mpeg2->progressive_frame;
    vp->fi.fi_tff = mpeg2->top_field_first;
    
    if(mpeg2->color_description)
      vp->fi.fi_color_space = mpeg2->matrix_coefficients;

    switch(mpeg2->aspect_ratio) {
    case VDEC_MPEG2_ARI_SAR_1_1:
      vp->fi.fi_dar.num = mpeg2->width;
      vp->fi.fi_dar.den = mpeg2->height;
      break;
    case VDEC_MPEG2_ARI_DAR_4_3:
      vp->fi.fi_dar = (AVRational){4,3};
      break;
    case VDEC_MPEG2_ARI_DAR_16_9:
      vp->fi.fi_dar = (AVRational){16,9};
      break;
    case VDEC_MPEG2_ARI_DAR_2P21_1:
      vp->fi.fi_dar = (AVRational){221,100};
      break;
    }

    snprintf(metainfo, sizeof(metainfo),
	     "MPEG2 %dx%d%c (Cell)",
	     mpeg2->width, mpeg2->height, vp->fi.fi_interlaced ? 'i' : 'p');

    if(pts == AV_NOPTS_VALUE && dts != AV_NOPTS_VALUE &&
       mpeg2->picture_coding_type[0] == 3)
      pts = dts;

#if VDEC_DETAILED_DEBUG
    TRACE(TRACE_DEBUG, "VDEC DEC", "%ld %d",
	  pts,
	  mpeg2->picture_coding_type[0]);
#endif

    order = vdd->order_base;
    vdd->order_base++;

  } else {
    vdec_h264_info *h264 = (void *)(intptr_t)pi->codec_specific_addr;
    AVRational sar;

    lumasize = h264->width * h264->height;
    vp = alloc_picture(vdd, lumasize);

    vp->fi.fi_width = h264->width;
    vp->fi.fi_height = h264->height;
    vp->fi.fi_duration = h264->frame_rate <= 7 ? 
      mpeg_durations[h264->frame_rate + 1] : 40000;
    vp->fi.fi_interlaced = 0;
    vp->fi.fi_tff = 0;
    if(h264->color_description_present_flag)
      vp->fi.fi_color_space = h264->matrix_coefficients;

    vp->fi.fi_dar.num = h264->width;
    vp->fi.fi_dar.den = h264->height;

    if(h264->aspect_ratio_idc == 0xff) {
      sar.num = h264->sar_width;
      sar.den = h264->sar_height;
    } else {
      const uint8_t *p;
      p = h264_sar[h264->aspect_ratio_idc <= 16 ? h264->aspect_ratio_idc : 0];
      sar.num = p[0];
      sar.den = p[1];
    }
    vp->fi.fi_dar = av_mul_q(vp->fi.fi_dar, sar);

    if(h264->idr_picture_flag)
      vdd->order_base += 0x100000000LL;

    order = vdd->order_base + (uint16_t)h264->pic_order_count[0];

    if(pts == AV_NOPTS_VALUE && dts != AV_NOPTS_VALUE &&
       h264->picture_type[0] == 2)
      pts = dts;

#if VDEC_DETAILED_DEBUG
    TRACE(TRACE_DEBUG, "VDEC DEC", "POC=%3d:%-3d IDR=%d PS=%d LD=%d %x 0x%llx %ld %d",
	  (uint16_t)h264->pic_order_count[0],
	  (uint16_t)h264->pic_order_count[1],
	  h264->idr_picture_flag,
	  h264->pic_struct,
	  h264->low_delay_hrd_flag,
	  h264->nalUnitPresentFlags,
	  order,
	  pts,
	  h264->picture_type[0]);
#endif
    snprintf(metainfo, sizeof(metainfo),
	     "h264 (Level %d.%d) %dx%d%c (Cell)",
	     vdd->level_major, vdd->level_minor,
	     h264->width, h264->height, vp->fi.fi_interlaced ? 'i' : 'p');
  }

  prop_set_string(vdd->metainfo, metainfo);

  vp->fi.fi_pix_fmt = PIX_FMT_YUV420P;
  vp->fi.fi_pts = pts;
  
  vp->fi.fi_epoch = pm->epoch;
  vp->fi.fi_prescaled = 0;
  vp->fi.fi_color_space = -1;
  vp->fi.fi_color_range = 0;
  vp->fi.fi_delta = pm->delta;
  vp->fi.fi_drive_clock = pm->drive_clock;

  vdec_get_picture(vdd->handle, &picfmt, rsx_to_ppu(vp->rvf.rvf_offset));

  vp->order = order;

  hts_mutex_lock(&vdd->mtx);

  LIST_INSERT_SORTED(&vdd->pictures, vp, link, vp_cmp);

  if(vdd->max_order != -1) {
    if(vp->order > vdd->max_order) {
      vdd->flush_to = vdd->max_order;
      vdd->max_order = vp->order;
    }
  } else {
    vdd->max_order = vp->order;
  }
  hts_mutex_unlock(&vdd->mtx);

}




/**
 * Keep in mind, this callback fires on different threads
 */
static uint32_t
decoder_callback(uint32_t handle, uint32_t msg_type, int32_t err_code,
		 uint32_t arg)
{
  vdec_decoder_t *vdd = (vdec_decoder_t *)(intptr_t)arg;

  switch(msg_type) {
  case VDEC_CALLBACK_AUDONE:
    hts_mutex_lock(&vdd->mtx);
    if(!vdd->submitted_au)
      TRACE(TRACE_ERROR, "VDEC", "AUDONE but no buffers pending");
    vdd->submitted_au = 0;
    hts_cond_signal(&vdd->audone);
    hts_mutex_unlock(&vdd->mtx);
    break;

  case VDEC_CALLBACK_PICOUT:
    picture_out(vdd);
    break;

  case VDEC_CALLBACK_SEQDONE:
    hts_mutex_lock(&vdd->mtx);
    vdd->sequence_done = 1;
    hts_cond_signal(&vdd->seqdone);
    hts_mutex_unlock(&vdd->mtx);
    break;

  case VDEC_CALLBACK_ERROR:
    TRACE(TRACE_ERROR, "VDEC", "ERROR %x", err_code);
    break;
  }
  return 0;
}


/**
 *
 */
static void
h264_to_annexb(uint8_t *b, size_t fsize)
{
  uint8_t *p = b;
  while(p < b + fsize) {
    int len = (p[0] << 24) + (p[1] << 16) + (p[2] << 8) + p[3];
    p[0] = 0;
    p[1] = 0;
    p[2] = 0;
    p[3] = 1;
    p += len + 4;
  }
}


/**
 * Return 0 if ownership of 'data' has been transfered from caller
 */
static void
submit_au(vdec_decoder_t *vdd, struct vdec_au *au, void *data, size_t len,
	  int drop_non_ref, video_decoder_t *vd)
{
  vdec_pic_t *vp;

  au->packet_addr = (intptr_t)data;
  au->packet_size = len;
  
  hts_mutex_lock(&vdd->mtx);
  vdd->submitted_au = 1;
  int r = vdec_decode_au(vdd->handle, 
			 drop_non_ref ? VDEC_DECODER_MODE_SKIP_NON_REF : 
			 VDEC_DECODER_MODE_NORMAL, au);
    
  if(r == 0) {
    while(vdd->submitted_au) {
      if(hts_cond_wait_timeout(&vdd->audone, &vdd->mtx, 5000)) {
	panic("Cell video decoder lockup");
      }
    }
  }

  while((vp = LIST_FIRST(&vdd->pictures)) != NULL) {
    if(vdd->flush_to < vp->order)
      break;
    LIST_REMOVE(vp, link);

    hts_mutex_unlock(&vdd->mtx);
    emit_frame(vd, vp);
    hts_mutex_lock(&vdd->mtx);
    free(vp);
  }
  hts_mutex_unlock(&vdd->mtx);
}

/**
 *
 */
static void
decoder_decode(struct media_codec *mc, struct video_decoder *vd,
	       struct media_queue *mq, struct media_buf *mb, int reqsize)
{
  vdec_decoder_t *vdd = mc->opaque;
  struct vdec_au au = {0};

  if(vdd->metainfo == NULL)
    vdd->metainfo = prop_ref_inc(mq->mq_prop_codec);

  if(vd->vd_do_flush) {
    end_sequence_and_wait(vdd);
    vdec_start_sequence(vdd->handle);
    vdd->extradata_injected = 0;
    vd->vd_nextpts = AV_NOPTS_VALUE;
    vdd->flush_to = -1;
  }

  pktmeta_t *pm = &vdd->pktmeta[vdd->pktmeta_cur];
  au.userdata = vdd->pktmeta_cur;

  vdd->pktmeta_cur = (vdd->pktmeta_cur + 1) & 63;
  
  pm->epoch = mb->mb_epoch;
  pm->skip = mb->mb_skip == 1;
  pm->flush = vd->vd_do_flush;
  pm->delta = mb->mb_delta;
  pm->drive_clock = mb->mb_drive_clock;

  int64_t pts = mb->mb_pts, dts = mb->mb_dts;

  pm->nopts = pts == AV_NOPTS_VALUE;
  pm->nodts = dts == AV_NOPTS_VALUE;

  if(dts < 0)
    dts = 0;
  if(pts < 0)
    pts = 0;

  /**
     I've no idea why routing timestamps to ms precision is required.
     But without this it seems that some movies (in particular MP4) just 
     freezes.

     see ticket #640 #662 #890
  */

  pts = (pts / 1000) * 1000;
  dts = (dts / 1000) * 1000;

  au.pts.low = pts;
  au.pts.hi  = pts >> 32;
  au.dts.low = dts;
  au.dts.hi  = dts >> 32;

  if(vdd->extradata != NULL && vdd->extradata_injected == 0) {
    submit_au(vdd, &au, vdd->extradata, vdd->extradata_size, 0, vd);
    vdd->extradata_injected = 1;
  }

  if(vdd->convert_to_annexb)
    h264_to_annexb(mb->mb_data, mb->mb_size);
  
  submit_au(vdd, &au, mb->mb_data, mb->mb_size, mb->mb_skip == 1, vd);
  vd->vd_do_flush = 0;
}


/**
 *
 */
static void
decoder_close(struct media_codec *mc)
{
  vdec_decoder_t *vdd = mc->opaque;

  TRACE(TRACE_DEBUG, "VDEC", "Freeing picture list");
  free_picture_list(&vdd->pictures);

  vdec_close(vdd->handle);
  Lv2Syscall1(349, (uint64_t)vdd->mem);

  hts_cond_destroy(&vdd->audone);
  hts_cond_destroy(&vdd->seqdone);
  hts_mutex_destroy(&vdd->mtx);

  prop_ref_dec(vdd->metainfo);
  free(vdd->extradata);
  free(vdd);
  TRACE(TRACE_DEBUG, "VDEC", "Cell decoder closed");
}


/**
 *
 */
static void
vdd_append_extradata(vdec_decoder_t *vdd, const uint8_t *data, int len)
{
  vdd->extradata = realloc(vdd->extradata, vdd->extradata_size + len);
  memcpy(vdd->extradata + vdd->extradata_size, data, len);
  vdd->extradata_size += len;
}

/**
 *
 */
static void
h264_load_extradata(vdec_decoder_t *vdd, const uint8_t *data, int len)
{
  int i, n, s;

  uint8_t buf[4] = {0,0,0,1};

  if(len < 7)
    return;
  if(data[0] != 1)
    return;

  n = data[5] & 0x1f;
  data += 6;
  len -= 6;

  for(i = 0; i < n && len >= 2; i++) {
    s = ((data[0] << 8) | data[1]) + 2;
    if(len < s)
      break;

    vdd_append_extradata(vdd, buf, 4);
    vdd_append_extradata(vdd, data + 2, s - 2);
    data += s;
    len -= s;
  }
  
  if(len < 1)
    return;
  n = *data++;
  len--;

  for(i = 0; i < n && len >= 2; i++) {
    s = ((data[0] << 8) | data[1]) + 2;
    if(len < s)
      break;

    vdd_append_extradata(vdd, buf, 4);
    vdd_append_extradata(vdd, data + 2, s - 2);
    data += s;
    len -= s;
  }
  vdd->convert_to_annexb = 1;
}


/**
 *
 */
static int
no_lib(media_pipe_t *mp, const char *codec)
{
  notify_add(mp->mp_prop_notifications, NOTIFY_WARNING, NULL, 10,
	     _("Unable to accelerate %s, library not loaded."), codec);
  return 1;
}

/**
 *
 */
int
video_ps3_vdec_codec_create(media_codec_t *mc, enum CodecID id,
			    AVCodecContext *ctx, media_codec_params_t *mcp,
			    media_pipe_t *mp)
{
  vdec_decoder_t *vdd;
  struct vdec_type dec_type = {0};
  struct vdec_attr dec_attr = {0};
  int spu_threads;
  int r;

  if(mcp->width == 0 || mcp->height == 0)
    return 1;

  switch(id) {
  case CODEC_ID_MPEG2VIDEO:
    if(!vdec_mpeg2_loaded)
      return no_lib(mp, "MPEG-2");

    dec_type.codec_type = VDEC_CODEC_TYPE_MPEG2;
    dec_type.profile_level = VDEC_MPEG2_MP_HL;
    spu_threads = 1;
    break;

  case CODEC_ID_H264:
    if(mcp->profile == FF_PROFILE_H264_CONSTRAINED_BASELINE)
      return 1; // can't play this

    if(!vdec_h264_loaded) 
      return no_lib(mp, "h264");

    dec_type.codec_type = VDEC_CODEC_TYPE_H264;
    if(mcp->level != 0 && mcp->level <= 42) {
      dec_type.profile_level = mcp->level;
    } else {
      dec_type.profile_level = 42;
      notify_add(mp->mp_prop_notifications, NOTIFY_WARNING, NULL, 10,
		 _("Cell-h264: Forcing level 4.2 for content in level %d.%d. This may break video playback."), mcp->level / 10, mcp->level % 10);
    }
    spu_threads = 4;
    break;

  default:
    return 1;
  }

  r = vdec_query_attr(&dec_type, &dec_attr);
  if(r) {
    notify_add(mp->mp_prop_notifications, NOTIFY_WARNING, NULL, 10,
	       _("Unable to query Cell codec. Error 0x%x"), r);
    return 1;
  }

  vdd = calloc(1, sizeof(vdec_decoder_t));


#define ROUND_UP(p, round) ((p + round - 1) & ~(round - 1))

  size_t allocsize = ROUND_UP(dec_attr.mem_size, 1024*1024);
  u32 taddr;

  if(Lv2Syscall3(348, allocsize, 0x400, (u64)&taddr)) {
    notify_add(mp->mp_prop_notifications, NOTIFY_WARNING, NULL, 10,
	       _("Unable to open Cell codec. Unable to allocate %d bytes of RAM"), dec_attr.mem_size);
    return 1;
  }
  vdd->mem = (void *)(uint64_t)taddr;

  TRACE(TRACE_DEBUG, "VDEC", "Opening codec %s level %d using %d bytes of RAM",
	id == CODEC_ID_H264 ? "h264" : "MPEG2", dec_type.profile_level,
	dec_attr.mem_size);

  vdd->config.mem_addr = (intptr_t)vdd->mem;
  vdd->config.mem_size = dec_attr.mem_size;
  vdd->config.num_spus = spu_threads;
  vdd->config.ppu_thread_prio = VDEC_PPU_PRIO;
  vdd->config.spu_thread_prio = VDEC_SPU_PRIO;
  vdd->config.ppu_thread_stack_size = 1 << 14;

  vdec_closure c;
  c.fn = (intptr_t)OPD32(decoder_callback);
  c.arg = (intptr_t)vdd;

  r = vdec_open(&dec_type, &vdd->config, &c, &vdd->handle);
  if(r) {
    notify_add(mp->mp_prop_notifications, NOTIFY_WARNING, NULL, 10,
	       _("Unable to open Cell codec. Error 0x%x"), r);
    Lv2Syscall1(349, (uint64_t)vdd->mem);
    free(vdd);
    return 1;
  }

  vdd->level_major = mcp->level / 10;
  vdd->level_minor = mcp->level % 10;

  if(id == CODEC_ID_H264 && ctx && ctx->extradata_size)
    h264_load_extradata(vdd, ctx->extradata, ctx->extradata_size);

  vdd->max_order = -1;

  hts_mutex_init(&vdd->mtx);
  hts_cond_init(&vdd->audone, &vdd->mtx);
  hts_cond_init(&vdd->seqdone, &vdd->mtx);

  TRACE(TRACE_DEBUG, "VDEC", 
	"Cell accelerated codec created using %d bytes of RAM",
	dec_attr.mem_size);

  mc->codec_ctx = ctx ?: avcodec_alloc_context();
  mc->codec_ctx->codec_id   = id;
  mc->codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;

  mc->opaque = vdd;
  mc->decode = decoder_decode;
  mc->close = decoder_close;

  vdec_start_sequence(vdd->handle);

  return 0;
}
