#pragma once
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
#pragma once

#include <stddef.h>

#include "misc/bitstream.h"

#define REFLIST_DEBUG

#define SLICE_TYPE_P	0
#define SLICE_TYPE_B	1
#define SLICE_TYPE_I	2
#define SLICE_TYPE_SP	3
#define SLICE_TYPE_SI	4




#define H264_PARSER_NUM_SPS 4
#define H264_PARSER_NUM_PPS 16

#define H264_MMCO_SIZE      66

#define H264_MMCO_END          0
#define H264_MMCO_SHORT2UNUSED 1
#define H264_MMCO_LONG2UNUSED  2
#define H264_MMCO_SHORT2LONG   3
#define H264_MMCO_SET_MAX_LONG 4
#define H264_MMCO_RESET        5
#define H264_MMCO_LONG         6


/**
 *
 */
typedef struct h264_sps {
  uint16_t mb_width;
  uint16_t mb_height;
  uint16_t aspect_num;
  uint16_t aspect_den;

  uint8_t num_ref_frames;

  uint8_t mbs_only_flag;
  uint8_t aff;
  uint8_t fixed_rate;
  uint8_t  gaps_in_frame_num_value_allowed_flag;

  uint8_t profile;
  uint8_t level;

  uint8_t chroma_format;
  uint8_t bit_depth_luma;
  uint8_t bit_depth_chroma;
  uint8_t residual_color_transform_flag;
  uint8_t transform_bypass;

  uint8_t max_frame_num_bits;
  unsigned int poc_type;

  int log2_max_poc_lsb;

  int delta_pic_order_always_zero_flag;
  int offset_for_non_ref_pic;
  int offset_for_top_to_bottom_field;
  int poc_cycle_length;
  int direct_8x8_inference_flag;

  char present;

} h264_sps_t;


/**
 *
 */
typedef struct h264_pps {
  int sps_id;
  unsigned int ref_count[2];
  char cabac;
  char pic_order_present;
  char weighted_pred_flag;
  char weighted_bipred_idc;
  char deblocking_filter_parameters_present;
  char constrained_intra_pred;
  char redundant_pic_cnt_present;
  char transform_8x8_mode;


  int init_qp;
  int init_qs;
  int chroma_qp_index_offset[2];
  int slice_group_count;
} h264_pps_t;


/**
 *
 */
typedef struct h264_frame {

  void *picture;
  int poc;
  uint16_t frame_idx;

  uint8_t is_reference;
  uint8_t output_needed;
  uint8_t is_long_term;

  uint8_t meta_index;

  int pos;

} h264_frame_t;

typedef void (h264_parser_output_frame_t)(void *oapque, h264_frame_t *frame);
typedef void (h264_parser_picture_release_t)(void *picture);

/**
 *
 */
typedef struct h264_parser {
  h264_sps_t sps_array[H264_PARSER_NUM_SPS];
  h264_pps_t pps_array[H264_PARSER_NUM_PPS];

  const h264_sps_t *sps;
  const h264_pps_t *pps;

  int lensize;  // Size of NAL length

  int ref_count;

  int nal_ref_idc;
  int nal_unit_type;

  //

  h264_frame_t current_frame;

  int first_mb_in_slice;
  int slice_type;
  int slice_type_nos;
  int frame_num;
  int frame_num_offset;
  int prev_frame_num;

  //

  int field_pic_flag;
  int bottom_field_flag;

  int idr_pic_id;

  int pic_order_cnt_lsb;
  int delta_pic_order_cnt_bottom;

  int delta_pic_order_cnt[2];

  int redundant_pic_cnt;

  int direct_spatial_mv_pred_flag;

  int num_ref_idx_active_override_flag;

  int num_ref_idx_l0_active_minus1;
  int num_ref_idx_l1_active_minus1;

  int prev_poc_lsb;
  int poc_msb;

  int max_frame_num;

  
} h264_parser_t;


int h264_parser_init(h264_parser_t *hp,
		     const uint8_t *extradata,
                     int extradata_size);

void h264_parser_fini(h264_parser_t *hp);

void h264_parser_decode_slice_header(h264_parser_t *hp, bitstream_t *bs);

int h264_parser_decode_sps(h264_parser_t *hp, bitstream_t *bs, h264_sps_t *sps);

void h264_parser_decode_pps(h264_parser_t *hp, bitstream_t *bs);

void h264_parser_decode_nal(h264_parser_t *hp, const uint8_t *data, int len);

void h264_parser_decode_nal_from_bs(h264_parser_t *hp, bitstream_t *bs);

void h264_parser_decode_data(h264_parser_t *hp, const uint8_t *d, int len);
