#include <unistd.h>
#include <assert.h>
#include <string.h>

#include "misc/bitstream.h"
#include "showtime.h"
#include "media.h"
#include "arch/sunxi/sunxi.h"
#include "video/video_decoder.h"

#include "h264_parser.h"

#define MAX_FRAME_SIZE (512 * 1024)

static int global_pic_allocs;

typedef struct cedar_picture {
  void *planes[2];
  void *side_buf;

  int width;
  int height;

  int refcount;
  int side_buf_size;

} cedar_picture_t;


/**
 *
 */
typedef struct cedar_decoder {
  h264_parser_t p;
  void *packet_buf;
  void *aux_buf;

  void *mb_buf;
  int mb_buf_size;

  struct video_decoder *vd;

} cedar_decoder_t;




static uint32_t
ve_read_bits(bitstream_t *bs, int num)
{
  ve_write(VE_H264_TRIGGER, 0x2 | (num << 8));
  while(ve_read(VE_H264_STATUS) & (1 << 8)) {}
  return ve_read(VE_H264_BASIC_BITS);
}

static uint32_t
ve_read_bits1(bitstream_t *bs)
{
  return ve_read_bits(bs, 1);
}

static void
ve_skip_bits(bitstream_t *bs, int num)
{
  ve_write(VE_H264_TRIGGER, 0x2 | (num << 8));
  while(ve_read(VE_H264_STATUS) & (1 << 8)) {}
}

static uint32_t
ve_read_golomb_ue(bitstream_t *bs)
{
  ve_write(VE_H264_TRIGGER, 0x5);
  while(ve_read(VE_H264_STATUS) & (1 << 8)) {}
  return ve_read(VE_H264_BASIC_BITS);
}

static int32_t
ve_read_golomb_se(bitstream_t *bs)
{
  ve_write(VE_H264_TRIGGER, 0x4);
  while(ve_read(VE_H264_STATUS) & (1 << 8)) {}
  return ve_read(VE_H264_BASIC_BITS);
}

#define ALIGN(x, y) (((x) + y - 1) & ~(y-1))

/**
 *
 */
static void *
picture_alloc(const h264_sps_t *sps)
{
  cedar_picture_t *cp = malloc(sizeof(cedar_picture_t));
  
  assert(sps != NULL);
  assert(sps->mb_width > 0);
  assert(sps->mb_height > 0);

  cp->side_buf_size = ALIGN(sps->mb_width * sps->mb_height * 64, 2048);

  cp->width  = 16 * sps->mb_width;
  cp->height = 16 * sps->mb_height;

  int plane_size = ALIGN(cp->width, 64) * ALIGN(cp->height, 64);

  // We only do 420

  hts_mutex_lock(&sunxi.gfxmem_mutex);

  cp->side_buf  = tlsf_memalign(sunxi.gfxmem, 4096, cp->side_buf_size);
  memset(cp->side_buf, 0, cp->side_buf_size);
  assert(cp->side_buf != NULL);
  cp->planes[0] = tlsf_memalign(sunxi.gfxmem, 4096, plane_size);
  assert(cp->planes[0] != NULL);
  cp->planes[1] = tlsf_memalign(sunxi.gfxmem, 4096, plane_size / 2);
  assert(cp->planes[1] != NULL);

  hts_mutex_unlock(&sunxi.gfxmem_mutex);
  cp->refcount = 1;

  atomic_add(&global_pic_allocs, 1);

  //  printf("PICTURE ALLOC %p (global: %d)\n", cp, global_pic_allocs);

  return cp;
}


/**
 *
 */
static void
picture_release(void *p)
{
  cedar_picture_t *cp = p;

  if(atomic_add(&cp->refcount, -1) > 1)
    return;

  atomic_add(&global_pic_allocs, -1);

  //  printf("PICTURE FREE %p (global:%d)\n", p, global_pic_allocs);

  hts_mutex_lock(&sunxi.gfxmem_mutex);

  tlsf_free(sunxi.gfxmem, cp->side_buf);
  tlsf_free(sunxi.gfxmem, cp->planes[0]);
  tlsf_free(sunxi.gfxmem, cp->planes[1]);

  hts_mutex_unlock(&sunxi.gfxmem_mutex);
  free(cp);
}


/**
 *
 */
static void
write_frame(const h264_frame_t *hf, int isref)
{
  cedar_picture_t *cp = hf->picture;
  assert(cp != NULL);
  assert(cp->planes[0] != NULL);

  ve_write(VE_H264_RAM_WRITE_DATA, hf->poc); // top
  ve_write(VE_H264_RAM_WRITE_DATA, hf->poc); // bottom
  ve_write(VE_H264_RAM_WRITE_DATA, isref ? 0 : 0x22);
  ve_write(VE_H264_RAM_WRITE_DATA, va_to_pa(cp->planes[0])); // Y plane
  ve_write(VE_H264_RAM_WRITE_DATA, va_to_pa(cp->planes[1])); // UV plane
  ve_write(VE_H264_RAM_WRITE_DATA, va_to_pa(cp->side_buf));
  ve_write(VE_H264_RAM_WRITE_DATA, va_to_pa(cp->side_buf) + cp->side_buf_size / 2);
  ve_write(VE_H264_RAM_WRITE_DATA, 0);
}


/**
 *
 */
static void
write_null_frame()
{
  for(int i = 0; i < 8; i++)
    ve_write(VE_H264_RAM_WRITE_DATA, 0);
}


/**
 *
 */
static void
fill_frame_lists(cedar_decoder_t *cd)
{
  int i;

  assert(cd->p.current_frame.picture == NULL);
  
  cd->p.current_frame.picture = picture_alloc(cd->p.sps);

  ve_write(VE_H264_RAM_WRITE_PTR, VE_SRAM_H264_FRAMEBUFFER_LIST);

  write_frame(&cd->p.current_frame, cd->p.nal_ref_idc);
  
  h264_parser_t *hp = &cd->p;

  for(i = 0; i < hp->dpb_num_frames; i++) {
    write_frame(&hp->frames[i], 1);
    hp->frames[i].pos = i + 1;
  }

  for(; i < 18; i++) 
    write_null_frame();
}


/**
 *
 */
static void
write_pred_weights(h264_parser_t *hp)
{
  ve_write(VE_H264_PRED_WEIGHT,
	   ((hp->chroma_log2_weight_denom & 0xf) << 4) |
	   ((hp->luma_log2_weight_denom   & 0xf) << 0));

  ve_write(VE_H264_RAM_WRITE_PTR, VE_SRAM_H264_PRED_WEIGHT_TABLE);


  for(int i = 0; i < 32; i++) {
    ve_write(VE_H264_RAM_WRITE_DATA,
	     ((hp->luma_offset_l0[i] & 0x1ff) << 16) | 
	     (hp->luma_weight_l0[i] & 0xff));
  }

  for(int i = 0; i < 32; i++) {
    for(int j = 0; j < 2; j++) {
      ve_write(VE_H264_RAM_WRITE_DATA,
	       ((hp->chroma_offset_l0[i][j] & 0x1ff) << 16) |
	       (hp->chroma_weight_l0[i][j] & 0xff));
    }
  }


  for(int i = 0; i < 32; i++) {
    ve_write(VE_H264_RAM_WRITE_DATA,
	     ((hp->luma_offset_l1[i] & 0x1ff) << 16) |
	     (hp->luma_weight_l1[i] & 0xff));
  }

  for(int i = 0; i < 32; i++) {
    for(int j = 0; j < 2; j++) {
      ve_write(VE_H264_RAM_WRITE_DATA,
	       ((hp->chroma_offset_l1[i][j] & 0x1ff) << 16) |
	       (hp->chroma_weight_l1[i][j] & 0xff));
    }
  }
}


/**
 *
 */
static void
decode_nal(cedar_decoder_t *cd, const uint8_t *data, int len)
{
  h264_parser_t *hp = &cd->p;

  if(len == 0)
    return;

  hp->nal_ref_idc   = data[0] >> 5;
  hp->nal_unit_type = data[0] & 0x1f;

  int i;
  for(i = 0; i < len; i++) {
    if(i + 2 < len && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 3) {
      printf("Escape needed !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
      printf("Escape needed !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
      printf("Escape needed !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
      printf("Escape needed !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
      printf("Escape needed !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
      printf("Escape needed !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
      printf("Escape needed !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
      printf("Escape needed !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
      i += 2;
    } else {

    }
  }


#if 0
  printf("NAL %d bytes  type:%d, ref_idc:%d len:%d\n",
         len, hp->nal_unit_type,
         hp->nal_ref_idc, len);
#endif


  if(hp->nal_unit_type != 1 && hp->nal_unit_type != 5) {
    h264_parser_decode_nal(hp, data, len);
    return;
  }

  data++;
  len--;

  int offset = data - (const uint8_t *)cd->packet_buf;
  ve_write(VE_H264_VLD_LEN,    len * 8);
  ve_write(VE_H264_VLD_OFFSET, offset * 8);
    
  uint32_t input_addr = va_to_pa(cd->packet_buf);
  ve_write(VE_H264_VLD_END, input_addr + MAX_FRAME_SIZE - 1);
  ve_write(VE_H264_VLD_ADDR,
	   (input_addr & 0x0ffffff0) | (input_addr >> 28) | (0x7 << 28));

  ve_write(VE_H264_TRIGGER, 0x7);

  bitstream_t bs;
  bs.skip_bits      = ve_skip_bits;
  bs.read_bits      = ve_read_bits;
  bs.read_bits1     = ve_read_bits1;
  bs.read_golomb_ue = ve_read_golomb_ue;
  bs.read_golomb_se = ve_read_golomb_se;


  h264_parser_decode_nal_from_bs(hp, &bs);

  int first_slice = !hp->first_mb_in_slice;

  if(first_slice) {

    hp->current_frame.picture = NULL;

    ve_write(VE_H264_CUR_MB_NUM, 0);
    ve_write(VE_H264_SDROT_CTRL, 0);

    fill_frame_lists(cd);

    ve_write(VE_H264_OUTPUT_FRAME_IDX, 0);
  }

#ifdef REFLIST_DEBUG
  printf("num_active_ref : %d %d\n",
	 hp->num_ref_idx_l0_active_minus1 + 1,
	 hp->num_ref_idx_l1_active_minus1 + 1);
#endif

  if(hp->slice_type_nos != SLICE_TYPE_I) {
    ve_write(VE_H264_RAM_WRITE_PTR, VE_SRAM_H264_REF_LIST0);
    
    for(int i = 0; i < hp->num_ref_idx_l0_active_minus1 + 1; i += 4) {
      uint32_t list = 0;
      for(int j = 0; j < 4; j++) {
	if(hp->RefPicList0[i + j] != NULL) {
#ifdef REFLIST_DEBUG
	  printf("hp->RefPicList0[%d] at pos %d POC:%d\n", i + j,
		 hp->RefPicList0[i + j]->pos,
		 hp->RefPicList0[i + j]->poc
		 );
#endif
	  list |= (hp->RefPicList0[i + j]->pos * 2) << (j * 8);
	}
      }
#ifdef REFLIST_DEBUG
      printf("REF_LIST0: 0x%08x\n", list);
#endif
      ve_write(VE_H264_RAM_WRITE_DATA, list);
    }

    if(hp->slice_type_nos == SLICE_TYPE_B) {
      ve_write(VE_H264_RAM_WRITE_PTR, VE_SRAM_H264_REF_LIST1);

      for(int i = 0; i < hp->num_ref_idx_l1_active_minus1 + 1; i += 4) {
	uint32_t list = 0;
	for(int j = 0; j < 4; j++)  {
	  if(hp->RefPicList1[i + j] != NULL) {
#ifdef REFLIST_DEBUG
	    printf("hp->RefPicList1[%d] at pos %d POC:%d\n", i + j,
		   hp->RefPicList1[i + j]->pos,
		   hp->RefPicList1[i + j]->poc
		   );
#endif
	    list |= (hp->RefPicList1[i + j]->pos * 2) << (j * 8);
	  }
	}
#ifdef REFLIST_DEBUG
	printf("REF_LIST1: 0x%08x\n", list);
#endif
	ve_write(VE_H264_RAM_WRITE_DATA, list);
      }
    }
  }

  const h264_pps_t *pps = hp->pps;
  const h264_sps_t *sps = hp->sps;

  write_pred_weights(hp);


  ve_write(VE_H264_PIC_HDR,
	   (pps->cabac                       << 15) |
	   (hp->num_ref_idx_l0_active_minus1 << 10) |
	   (pps->weighted_pred_flag          <<  4) |
	   (pps->weighted_bipred_idc         <<  2) |
	   (pps->transform_8x8_mode          <<  0));


  ve_write(VE_H264_FRAME_SIZE,
	   (0xd                   << 16) |
	   ((sps->mb_width - 1)   <<  8) |
	   ((sps->mb_height - 1)  <<  0));

  int is_ref = !!hp->nal_ref_idc;

  ve_write(VE_H264_SLICE_HDR,
	   (hp->first_mb_in_slice % sps->mb_width << 24) |
	   (hp->first_mb_in_slice / sps->mb_width << 16) |
	   (is_ref                                << 12) |
	   (hp->slice_type                        <<  8) |
	   (first_slice                           <<  5) |
	   (hp->direct_spatial_mv_pred_flag       <<  2));

  int x = hp->slice_type != 2;

  ve_write(VE_H264_SLICE_HDR2,
	   (hp->num_ref_idx_l0_active_minus1       << 24) |
	   (hp->num_ref_idx_l1_active_minus1       << 16) |
	   //	   (hp->num_ref_idx_active_override_flag   << 12) |
	   (x                                      << 12) |
	   (hp->disable_deblocking_filter_idc      <<  8) |
	   ((hp->slice_alpha_c0_offset_div2 & 0xf) <<  4) |
	   ((hp->slice_beta_offset_div2     & 0xf) <<  0));

  ve_write(VE_H264_QP_PARAM,
	   (1                                       << 24) |
	   ((pps->chroma_qp_index_offset[1] & 0x3f) << 16) |
	   ((pps->chroma_qp_index_offset[0] & 0x3f) <<  8) |
	   (pps->init_qp + hp->slice_qp_delta));

  ve_write(VE_H264_STATUS, ve_read(VE_H264_STATUS));

  ve_write(VE_H264_CTRL, ve_read(VE_H264_CTRL) | 0x7);

  int status = ve_read(VE_H264_STATUS);

  ve_write(VE_H264_TRIGGER, 0x8);

  //  int64_t ts = showtime_get_ts();

  sunxi_ve_wait(1);

  //  ts = showtime_get_ts() - ts;

  status = ve_read(VE_H264_STATUS);
  ve_write(VE_H264_STATUS, status);
}


static void
cedar_flush(struct media_codec *mc, struct video_decoder *vd)
{

}


static void
cedar_decode(struct media_codec *mc, struct video_decoder *vd,
             struct media_queue *mq, struct media_buf *mb, int reqsize)
{
  cedar_decoder_t *cd = mc->opaque;
  h264_parser_t *hp = &cd->p;

  printf("######## NEW PACKET ##########################################\n");

  cd->vd = vd;

  memcpy(cd->packet_buf, mb->mb_data, mb->mb_size);
  sunxi_flush_cache(cd->packet_buf, mb->mb_size);

  ve_write(VE_CTRL, (ve_read(VE_CTRL) & ~0xf) | 0x1);
   
  uint32_t aux_buf = va_to_pa(cd->aux_buf);
  ve_write(VE_H264_EXTRA_BUFFER1, aux_buf);
  ve_write(VE_H264_EXTRA_BUFFER2, aux_buf + 0x48000);
  
  ve_write(VE_H264_CTRL, (0x1 << 25) | (0x1 << 10));

  int len = mb->mb_size;
  const uint8_t *d = cd->packet_buf;

  hp->mmco_size = 0;

  if(hp->lensize == 0) {

    const uint8_t *p = NULL;

    while(len > 4) {
      if(!(d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 1)) {
        d++;
        len--;
        continue;
      }

      if(p != NULL)
        decode_nal(cd, p, d - p);

      d += 4;
      len -= 4;
      p = d;
    }
    d += len;

    if(p != NULL)
      decode_nal(cd, p, d - p);

  } else {

    while(len >= hp->lensize) {
      int nal_len = 0;
      for(int i = 0; i < hp->lensize; i++)
        nal_len = (nal_len << 8) | d[i];
      d += hp->lensize;
      len -= hp->lensize;

      decode_nal(cd, d, len);

      d += nal_len;
      len -= nal_len;
    }
  }

  ve_write(VE_CTRL, (ve_read(VE_CTRL) & ~0xf) | 0x7);

  // Metadata

  hp->current_frame.meta_index = vd->vd_reorder_ptr;

  vd->vd_reorder[vd->vd_reorder_ptr] = mb->mb_meta;
  vd->vd_reorder_ptr = (vd->vd_reorder_ptr + 1) & VIDEO_DECODER_REORDER_MASK;

  h264_parser_update_dpb(hp);
}


/**
 *
 */
static void
cedar_picture_refop(void *data2, int delta)
{
  cedar_picture_t *cp = data2;
  //  printf("REFOP for picture %p: %d\n", cp, delta);
  if(delta == 1) {
    atomic_add(&cp->refcount, 1);
  } else {
    picture_release(cp);
  }
}


/**
 *
 */
static void
cedar_output(void *opaque, h264_frame_t *hf)
{
  cedar_decoder_t *cd = opaque;
  cedar_picture_t *cp = hf->picture;
  video_decoder_t *vd = cd->vd;

  const media_buf_meta_t *mbm = &vd->vd_reorder[hf->meta_index];

  printf(">>\t\t\t\t OUTPUT FRAME WITH POC %d %d x %d\n",
	 hf->poc, cp->width, cp->height);

  frame_info_t fi;
  memset(&fi, 0, sizeof(fi));

  fi.fi_type = 'CED2';

  fi.fi_refop = cedar_picture_refop;

  fi.fi_dar_num = cp->width;
  fi.fi_dar_den = cp->height;
  fi.fi_width   = cp->width;
  fi.fi_height  = cp->height;

  fi.fi_pts      = mbm->mbm_pts;
  fi.fi_duration = mbm->mbm_duration;
  fi.fi_epoch    = mbm->mbm_epoch;
  fi.fi_delta    = mbm->mbm_delta;

  fi.fi_data[0] = cp->planes[0];
  fi.fi_data[1] = cp->planes[1];
  fi.fi_data[2] = (void *)cp;

  video_deliver_frame(cd->vd, &fi);
}


/**
 *
 */
static void
cedar_close(struct media_codec *mc)
{
  cedar_decoder_t *cd = mc->opaque;
  hts_mutex_lock(&sunxi.gfxmem_mutex);
  tlsf_free(sunxi.gfxmem, cd->packet_buf);
  tlsf_free(sunxi.gfxmem, cd->aux_buf);
  hts_mutex_unlock(&sunxi.gfxmem_mutex);

  h264_parser_fini(&cd->p);
  free(cd);
}

/**
 *
 */
static int
cedar_h264_codec_create(media_codec_t *mc, const media_codec_params_t *mcp,
                        media_pipe_t *mp)
{
  return 1;

  if(mc->codec_id != CODEC_ID_H264)
    return 1;

  cedar_decoder_t *cd = calloc(1, sizeof(cedar_decoder_t));

  h264_parser_init(&cd->p, cd, cedar_output, picture_release,
		   mcp ? mcp->extradata : NULL,
		   mcp ? mcp->extradata_size : 0);

  mc->opaque = cd;
  mc->decode = cedar_decode;
  mc->flush  = cedar_flush;
  mc->close  = cedar_close;

  int aux_buf_size = 320 * 1024;

  hts_mutex_lock(&sunxi.gfxmem_mutex);
  cd->packet_buf = tlsf_memalign(sunxi.gfxmem, 4096, MAX_FRAME_SIZE);
  cd->aux_buf    = tlsf_memalign(sunxi.gfxmem, 4096, aux_buf_size);
  hts_mutex_unlock(&sunxi.gfxmem_mutex);

  memset(cd->aux_buf, 0, aux_buf_size);
  sunxi_flush_cache(cd->aux_buf, aux_buf_size);

  return 0;
}


REGISTER_CODEC(NULL, cedar_h264_codec_create, 10);

