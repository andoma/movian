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
#include <stdio.h>

#include "misc/bitstream.h"
#include "main.h"
#include "media/media.h"

#include "h264_parser.h"

/**
 *
 */
static void
decode_scaling_list(bitstream_t *bs, int size)
{
  int i, last = 8, next = 8;
  if(!bs->read_bits1(bs))
    return;

  for(i=0;i<size;i++) {
    if(next)
      next = (last + bs->read_golomb_se(bs)) & 0xff;
    if(!i && !next)
      break;
    last = next ? next : last;
  }
}


/**
 *
 */
int
h264_parser_decode_sps(h264_parser_t *hp, bitstream_t *bs, h264_sps_t *sps)
{
  int profile = bs->read_bits(bs, 8);
  bs->skip_bits(bs, 8);
  int level = bs->read_bits(bs, 8);
  unsigned int sps_id = bs->read_golomb_ue(bs);

  if(hp == NULL) {
    assert(sps != NULL);
  } else {
    if(sps_id >= H264_PARSER_NUM_SPS)
      return -1;
    sps = &hp->sps_array[sps_id];
  }

  sps->present = 1;
  sps->profile = profile;
  sps->level   = level;

  if(sps->profile == 100 || sps->profile == 110 ||
     sps->profile == 122 || sps->profile == 244 ||
     sps->profile ==  44 || sps->profile ==  83 ||
     sps->profile ==  86 || sps->profile == 118 ||
     sps->profile == 128 || sps->profile == 144) {

    sps->chroma_format = bs->read_golomb_ue(bs);
    if(sps->chroma_format == 3)
      sps->residual_color_transform_flag = bs->read_bits(bs, 1);

    sps->bit_depth_luma   = bs->read_golomb_ue(bs) + 8;
    sps->bit_depth_chroma = bs->read_golomb_ue(bs) + 8;
    sps->transform_bypass = bs->read_bits(bs, 1);

    if(bs->read_bits1(bs)) {
      decode_scaling_list(bs, 16);
      decode_scaling_list(bs, 16);
      decode_scaling_list(bs, 16);
      decode_scaling_list(bs, 16);
      decode_scaling_list(bs, 16);
      decode_scaling_list(bs, 16);
      decode_scaling_list(bs, 64);
      decode_scaling_list(bs, 64);
    }
  } else {
    sps->chroma_format    = 1;
    sps->bit_depth_luma   = 8;
    sps->bit_depth_chroma = 8;
  }


  sps->max_frame_num_bits                 = bs->read_golomb_ue(bs) + 4;
  sps->poc_type                           = bs->read_golomb_ue(bs);
  if(sps->poc_type == 0) {
    sps->log2_max_poc_lsb                 = bs->read_golomb_ue(bs) + 4;

  } else if(sps->poc_type == 1) {
    sps->delta_pic_order_always_zero_flag = bs->read_bits1(bs);
    sps->offset_for_non_ref_pic           = bs->read_golomb_se(bs);
    sps->offset_for_top_to_bottom_field   = bs->read_golomb_se(bs);
    sps->poc_cycle_length                 = bs->read_golomb_ue(bs);
    for(int i = 0; i < sps->poc_cycle_length; i++)
      bs->read_golomb_se(bs);

  } else if(sps->poc_type != 2) {
    return -1;
  }

  sps->num_ref_frames = bs->read_golomb_ue(bs);

  sps->gaps_in_frame_num_value_allowed_flag = bs->read_bits1(bs);

  sps->mb_width  = bs->read_golomb_ue(bs) + 1;
  sps->mb_height = bs->read_golomb_ue(bs) + 1;
  sps->mbs_only_flag = bs->read_bits1(bs);
  if(!sps->mbs_only_flag)
    sps->aff = bs->read_bits1(bs);

  sps->direct_8x8_inference_flag = bs->read_bits1(bs);

  if(bs->read_bits1(bs)) {
    const int hshift = sps->chroma_format == 1 || sps->chroma_format == 2;
    const int vshift = sps->chroma_format == 1;

    int hscale = 1 << hshift;
    int vscale = (2 - sps->mbs_only_flag) << vshift;

    sps->crop_left   = bs->read_golomb_ue(bs) * hscale;
    sps->crop_right  = bs->read_golomb_ue(bs) * hscale;
    sps->crop_top    = bs->read_golomb_ue(bs) * vscale;
    sps->crop_bottom = bs->read_golomb_ue(bs) * vscale;
  }
  return sps_id;
}


/**
 *
 */
void
h264_parser_decode_pps(h264_parser_t *hp, bitstream_t *bs)
{
  unsigned int pps_id = bs->read_golomb_ue(bs);
  if(pps_id >= H264_PARSER_NUM_PPS)
    return;

  unsigned int sps_id = bs->read_golomb_ue(bs);
  if(sps_id >= H264_PARSER_NUM_SPS)
    return;

  h264_pps_t *pps = &hp->pps_array[pps_id];

  pps->present = 1;

  pps->sps_id = sps_id;
  pps->cabac                                = bs->read_bits1(bs);
  pps->pic_order_present                    = bs->read_bits1(bs);
  pps->slice_group_count                    = bs->read_golomb_ue(bs) + 1;
  pps->ref_count[0]                         = bs->read_golomb_ue(bs) + 1;
  pps->ref_count[1]                         = bs->read_golomb_ue(bs) + 1;
  pps->weighted_pred_flag                   = bs->read_bits1(bs);
  pps->weighted_bipred_idc                  = bs->read_bits(bs, 2);
  pps->init_qp                              = bs->read_golomb_se(bs) + 26;
  pps->init_qs                              = bs->read_golomb_se(bs) + 26;
  pps->chroma_qp_index_offset[0]            = bs->read_golomb_se(bs);
  pps->deblocking_filter_parameters_present = bs->read_bits1(bs);
  pps->constrained_intra_pred               = bs->read_bits1(bs);
  pps->redundant_pic_cnt_present            = bs->read_bits1(bs);
  pps->transform_8x8_mode                   = 0;

  pps->chroma_qp_index_offset[1] = pps->chroma_qp_index_offset[0];

  int bits_left = bs->bits_left(bs);
  if(bits_left <= 7)
    return;
  pps->transform_8x8_mode                   = bs->read_bits1(bs);
}


/**
 *
 */
int
h264_parser_init(h264_parser_t *hp, const uint8_t *data, int len)
{
  bitstream_t bs;
  int n, s, i;
  memset(hp, 0, sizeof(h264_parser_t));

  if(data == NULL)
    return 0;

  if(len < 7)
    return -1;

  if(data[0] != 1) {
    h264_parser_decode_data(hp, data, len);
    return 0;
  }

  hp->lensize = (data[4] & 0x3) + 1;

  n = data[5] & 0x1f;
  data += 6;
  len -= 6;

  // Parse SPS

  for(i = 0; i < n && len >= 2; i++) {
    s = ((data[0] << 8) | data[1]) + 2;
    if(len < s)
      break;

    init_rbits(&bs, data+3, s-3, 0);
    h264_parser_decode_sps(hp, &bs, NULL);
    data += s;
    len -= s;
  }

  // Parse PPS

  if(len < 1)
    return -1;
  n = *data++;
  len--;

  for(i = 0; i < n && len >= 2; i++) {
    s = ((data[0] << 8) | data[1]) + 2;
    if(len < s)
      break;

    init_rbits(&bs, data+3, s-3, 0);
    h264_parser_decode_pps(hp, &bs);
    data += s;
    len -= s;
  }
  return 0;
}



/**
 *
 */
static int
calc_poc(h264_parser_t *hp, const h264_sps_t *sps)
{
  const int max_frame_num = 1 << sps->max_frame_num_bits;
  int poc = 0;

  if(hp->frame_num < hp->prev_frame_num)
    hp->frame_num_offset += max_frame_num;

  if(sps->poc_type == 0) {
    uint32_t max_poc_cnt_lsb = 1 << sps->log2_max_poc_lsb;

    if((hp->pic_order_cnt_lsb < hp->prev_poc_lsb) &&
       ((hp->prev_poc_lsb - hp->pic_order_cnt_lsb) >=
            (max_poc_cnt_lsb / 2)))

      hp->poc_msb = hp->poc_msb + max_poc_cnt_lsb;

    else if ((hp->pic_order_cnt_lsb > hp->prev_poc_lsb) &&
        ((hp->pic_order_cnt_lsb - hp->prev_poc_lsb) >
            (max_poc_cnt_lsb / 2)))
      hp->poc_msb = hp->poc_msb - max_poc_cnt_lsb;

    poc = hp->poc_msb + hp->pic_order_cnt_lsb;

    hp->prev_poc_lsb = hp->pic_order_cnt_lsb;
  } else if(sps->poc_type == 2) {
    
    poc = 2 * (hp->frame_num_offset + hp->frame_num);
    if(!hp->nal_ref_idc)
      poc--;

  } else {

  }
  return poc;
}


/**
 *
 */
void
h264_parser_decode_slice_header(h264_parser_t *hp, bitstream_t *bs)
{
  hp->first_mb_in_slice = bs->read_golomb_ue(bs);
  hp->slice_type        = bs->read_golomb_ue(bs);
  if(hp->slice_type >= 5)
    hp->slice_type -= 5;


  hp->slice_type_nos = hp->slice_type & 3;

  int pps_id = bs->read_golomb_ue(bs);

  if(pps_id >= H264_PARSER_NUM_PPS)
    return;

  const h264_pps_t *pps = &hp->pps_array[pps_id];
  const h264_sps_t *sps = &hp->sps_array[pps->sps_id];

  hp->sps = sps;
  hp->pps = pps;

  hp->frame_num = bs->read_bits(bs, sps->max_frame_num_bits);
  hp->max_frame_num = 1 << sps->max_frame_num_bits;

  if(!sps->mbs_only_flag) {
    hp->field_pic_flag = bs->read_bits(bs, 1);
    if(hp->field_pic_flag)
      hp->bottom_field_flag = bs->read_bits(bs, 1);
  }

  if(hp->nal_unit_type == 5)
    hp->idr_pic_id = bs->read_golomb_ue(bs);

  if(sps->poc_type == 0) {
    hp->pic_order_cnt_lsb = bs->read_bits(bs, sps->log2_max_poc_lsb);
    if(pps->pic_order_present && !hp->field_pic_flag)
      hp->delta_pic_order_cnt_bottom = bs->read_golomb_se(bs);
  }

  if(sps->poc_type == 1 && !sps->delta_pic_order_always_zero_flag) {
    hp->delta_pic_order_cnt[0] = bs->read_golomb_se(bs);
    if(pps->pic_order_present && !hp->field_pic_flag)
      hp->delta_pic_order_cnt[1] = bs->read_golomb_se(bs);
  }

  if(pps->redundant_pic_cnt_present)
    hp->redundant_pic_cnt = bs->read_golomb_ue(bs);

  hp->num_ref_idx_l0_active_minus1 = pps->ref_count[0] - 1;
  hp->num_ref_idx_l1_active_minus1 = pps->ref_count[1] - 1;

  if(hp->slice_type_nos == SLICE_TYPE_B)
    hp->direct_spatial_mv_pred_flag = bs->read_bits(bs, 1);
  else
    hp->direct_spatial_mv_pred_flag = 0;

  if(hp->slice_type_nos == SLICE_TYPE_P ||
     hp->slice_type_nos == SLICE_TYPE_B) {

    hp->num_ref_idx_active_override_flag = bs->read_bits(bs, 1);
    if(hp->num_ref_idx_active_override_flag) {
      hp->num_ref_idx_l0_active_minus1 = bs->read_golomb_ue(bs);
      if(hp->slice_type == SLICE_TYPE_B)
        hp->num_ref_idx_l1_active_minus1 = bs->read_golomb_ue(bs);
    }
  }

  hp->current_frame.poc = calc_poc(hp, sps);
#if 0
  printf("Slicetype:%d PPS:%d frame:%d first_mb_in_slice:%d POC:%d\n",
         hp->slice_type, pps_id,
         hp->frame_num, hp->first_mb_in_slice,
         hp->current_frame.poc);
#endif
}


/**
 *
 */
static void
idr(h264_parser_t *hp)
{
  hp->prev_poc_lsb = 0;
  hp->frame_num_offset = 0;
  hp->prev_frame_num = 0;
}

/**
 *
 */
void
h264_parser_decode_nal_from_bs(h264_parser_t *hp, bitstream_t *bs)
{
  switch(hp->nal_unit_type) {
  case 5: // IDR
    idr(hp);
  case 1: // Slice
    h264_parser_decode_slice_header(hp, bs);
    break;
  case 7: // SPS
    h264_parser_decode_sps(hp, bs, NULL);
    break;
  case 8: // PPS
    h264_parser_decode_pps(hp, bs);
    break;
  }
}



/**
 *
 */
void
h264_parser_decode_nal(h264_parser_t *hp, const uint8_t *data, int len)
{
  if(len == 0)
    return;

  hp->nal_ref_idc   = data[0] >> 5;
  hp->nal_unit_type = data[0] & 0x1f;
  data++;
  len--;


  bitstream_t bs;
  init_rbits(&bs, data, len, 1);
  h264_parser_decode_nal_from_bs(hp, &bs);
}


/**
 *
 */
void
h264_parser_fini(h264_parser_t *hp)
{
}


/**
 *
 */
void
h264_parser_decode_data(h264_parser_t *hp, const uint8_t *d, int len)
{
  if(hp->lensize == 0) {

    const uint8_t *p = NULL;

    while(len > 3) {
      if(!(d[0] == 0 && d[1] == 0 && d[2] == 1)) {
        d++;
        len--;
        continue;
      }

      if(p != NULL)
        h264_parser_decode_nal(hp, p, d - p);

      d += 3;
      len -= 3;
      p = d;
    }
    d += len;

    if(p != NULL)
      h264_parser_decode_nal(hp, p, d - p);

  } else {

    while(len >= hp->lensize) {
      int nal_len = 0;
      for(int i = 0; i < hp->lensize; i++)
        nal_len = (nal_len << 8) | d[i];
      d += hp->lensize;
      len -= hp->lensize;

      h264_parser_decode_nal(hp, d, len);

      d += nal_len;
      len -= nal_len;
    }
  }
}


/**
 *
 */
void
h264_dump_extradata(const void *data, size_t size)
{
  h264_parser_t hp;

  hexdump("h264", data, size);

  if(h264_parser_init(&hp, data, size)) {
    TRACE(TRACE_DEBUG, "h264", "Corrupt extradata");
    return;
  }

  for(int i = 0; i < H264_PARSER_NUM_SPS; i++) {
    const h264_sps_t *s = &hp.sps_array[i];
    if(!s->present)
      continue;
    TRACE(TRACE_DEBUG, "h264",
          "SPS[%d]: %d x %d profile:%d level:%d.%d ref-frames:%d",
          i, s->mb_width * 16, s->mb_height * 16,
          s->profile,
          s->level / 10,
          s->level % 10,
          s->num_ref_frames);
    TRACE(TRACE_DEBUG, "h264",
          "        chromaformat:%d lumabits:%d chromabits:%d",
          s->chroma_format,
          s->bit_depth_luma,
          s->bit_depth_chroma);
  }

  for(int i = 0; i < H264_PARSER_NUM_PPS; i++) {
    const h264_pps_t *p = &hp.pps_array[i];
    if(!p->present)
      continue;
    TRACE(TRACE_DEBUG, "h264",
          "PPS[%d]: %s pop:%d wpred:%d wbipred:%d deblock:%d",
          i, p->cabac ? "CABAC" : "CAVLC",
          p->pic_order_present,
          p->weighted_pred_flag,
          p->weighted_bipred_idc,
          p->deblocking_filter_parameters_present);
  }


  h264_parser_fini(&hp);
}
