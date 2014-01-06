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
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "misc/bitstream.h"
#include "showtime.h"
#include "media.h"

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
  int sps_id = bs->read_golomb_ue(bs);

  if(hp == NULL) {
    assert(sps != NULL);
  } else {
    if(sps_id >= H264_PARSER_NUM_SPS)
      return -1;
    sps = &hp->sps_array[sps_id];
  }

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
    printf("Illegal poc\n");
    return -1;
  }

  sps->num_ref_frames = bs->read_golomb_ue(bs);

  sps->gaps_in_frame_num_value_allowed_flag = bs->read_bits1(bs);

  sps->mb_width  = bs->read_golomb_ue(bs) + 1;
  sps->mb_height = bs->read_golomb_ue(bs) + 1;
  sps->mbs_only_flag = bs->read_bits1(bs);
  if(sps->mbs_only_flag)
    sps->aff = bs->read_bits1(bs);

  sps->direct_8x8_inference_flag = bs->read_bits1(bs);

  if(bs->read_bits1(bs)) {
    bs->read_golomb_ue(bs);
    bs->read_golomb_ue(bs);
    bs->read_golomb_ue(bs);
    bs->read_golomb_ue(bs);
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

  int bits_left = bs->len - bs->offset;
  if(bits_left <= 7)
    return;
  pps->transform_8x8_mode                   = bs->read_bits1(bs);
}


static void
dummy_output_frame(void *oapque, h264_frame_t *frame)
{
}

static void
dummy_picture_release(void *picture)
{
}


/**
 *
 */
int
h264_parser_init(h264_parser_t *hp,
		 void *opaque,
		 h264_parser_output_frame_t *output_frame,
		 h264_parser_picture_release_t *picture_release,
		 const uint8_t *data,
		 int len)
{
  bitstream_t bs;
  int n, s, i;
  memset(hp, 0, sizeof(h264_parser_t));

  hp->hp_opaque = hp;
  hp->hp_output_frame    = output_frame    ?: dummy_output_frame;
  hp->hp_picture_release = picture_release ?: dummy_picture_release;

  if(data == NULL)
    return 0;

  if(len < 7 || data[0] != 1)
    return -1;

  hp->lensize = (data[4] & 0x3) + 1;

  n = data[5] & 0x1f;
  data += 6;
  len -= 6;

  // Parse SPS

  for(i = 0; i < n && len >= 2; i++) {
    s = ((data[0] << 8) | data[1]) + 2;
    if(len < s)
      break;

    init_rbits(&bs, data+3, s-3);
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

    init_rbits(&bs, data+3, s-3);
    h264_parser_decode_pps(hp, &bs);
    data += s;
    len -= s;
  }
  return 0;
}



/**
 *
 */
static void
pred_weight_table(h264_parser_t *hp, bitstream_t *bs, const h264_sps_t *sps)
{
  int i, j;

  hp->luma_log2_weight_denom = bs->read_golomb_ue(bs);
  if(sps->chroma_format)
    hp->chroma_log2_weight_denom = bs->read_golomb_ue(bs);

  for(i = 0; i < 32; i++) {
    hp->luma_weight_l0[i]      = (1 << hp->luma_log2_weight_denom);
    hp->luma_weight_l1[i]      = (1 << hp->luma_log2_weight_denom);
    hp->chroma_weight_l0[i][0] = (1 << hp->chroma_log2_weight_denom);
    hp->chroma_weight_l1[i][0] = (1 << hp->chroma_log2_weight_denom);
    hp->chroma_weight_l0[i][1] = (1 << hp->chroma_log2_weight_denom);
    hp->chroma_weight_l1[i][1] = (1 << hp->chroma_log2_weight_denom);
  }

  for(i = 0; i <= hp->num_ref_idx_l0_active_minus1; i++) {
    if(bs->read_bits1(bs)) {
      hp->luma_weight_l0[i] = bs->read_golomb_se(bs);
      hp->luma_offset_l0[i] = bs->read_golomb_se(bs);
    }

    if(sps->chroma_format && bs->read_bits1(bs)) {
      for(j = 0; j < 2; j++) {
        hp->chroma_weight_l0[i][j] = bs->read_golomb_se(bs);
        hp->chroma_offset_l0[i][j] = bs->read_golomb_se(bs);
      }
    }
  }

  if(hp->slice_type_nos == SLICE_TYPE_B) {
    for(i = 0; i <= hp->num_ref_idx_l1_active_minus1; i++) {
      if(bs->read_bits1(bs)) {
        hp->luma_weight_l1[i] = bs->read_golomb_se(bs);
        hp->luma_offset_l1[i] = bs->read_golomb_se(bs);
      }

      if(sps->chroma_format && bs->read_bits1(bs)) {
        for (j = 0; j < 2; j++) {
          hp->chroma_weight_l1[i][j] = bs->read_golomb_se(bs);
          hp->chroma_offset_l1[i][j] = bs->read_golomb_se(bs);
        }
      }
    }
  }
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
    printf("can't deal with poc type %d\n", sps->poc_type);
    exit(0);
  }
  return poc;
}


/**
 *
 */
static void
prep_frame(h264_parser_t *hp, h264_frame_t *frame)
{
  hp->prev_frame_num = hp->frame_num;

  frame->output_needed = 1;
  frame->is_long_term = 0;
  frame->frame_idx = hp->frame_num;

  if(!hp->nal_ref_idc) {
    frame->is_reference = 0;
  } else if(hp->nal_unit_type == 5) {
    frame->is_reference = 1;
    if(hp->long_term_reference_flag) {
      frame->is_long_term = 1;
      frame->frame_idx = 0;
    }
  } else {
    frame->is_reference = 1;

    if(hp->adaptive_ref_pic_marking_mode_flag) {
      int i;
      for (i = 0; i < hp->mmco_size; i++) {
        if(hp->mmco[i].opcode == 6) {
          frame->is_long_term = 1;
          frame->frame_idx = hp->mmco[i].long_term_frame_idx;
          break;
        }
      }
    }
  }
}


/**
 *
 */
static void
dpb_remove(h264_parser_t *hp, int idx)
{
  int i;
  hp->dpb_num_frames--;

  assert(hp->frames[idx].output_needed == 0);
  assert(hp->frames[idx].is_reference  == 0);

  hp->hp_picture_release(hp->frames[idx].picture);

  for(i = idx; i < hp->dpb_num_frames; i++)
    hp->frames[i] = hp->frames[i + 1];
}


/**
 *
 */
static void
dpb_mark_sliding(h264_parser_t *hp)
{
  int mark_idx = -1;
  int i;

  if(hp->dpb_num_frames != hp->dpb_max_frames)
    return;

  for (i = 0; i < hp->dpb_num_frames; i++) {
    if(hp->frames[i].is_reference && !hp->frames[i].is_long_term) {
      mark_idx = i;
      break;
    }
  }

  if(mark_idx != -1) {
    for(i = mark_idx; i < hp->dpb_num_frames; i++) {
      if(hp->frames[i].is_reference && !hp->frames[i].is_long_term &&
         hp->frames[i].frame_idx < hp->frames[mark_idx].frame_idx)
        mark_idx = i;
    }

    hp->frames[mark_idx].is_reference = 0;
    if(!hp->frames[mark_idx].output_needed)
      dpb_remove(hp, mark_idx);
  }
}


static void
outframe(h264_parser_t *hp, h264_frame_t *frame)
{
  printf("Callint out frame %p\n", hp->hp_output_frame);
  hp->hp_output_frame(hp->hp_opaque, frame);
  printf("Callint out frame done\n");
}


/**
 *
 */
static void
dpb_output(h264_parser_t *hp, int idx)
{
  h264_frame_t *frame = &hp->frames[idx];

  assert(frame->output_needed);

  outframe(hp, frame);
  frame->output_needed = 0;
  if(!frame->is_reference)
    dpb_remove(hp, idx);
}


/**
 *
 */
static int
dpb_bump(h264_parser_t *hp, int poc)
{
  int bump_idx = -1;
  int i;
  for(i = 0; i < hp->dpb_num_frames; i++) {
    if(hp->frames[i].output_needed) {
      bump_idx = i;
      break;
    }
  }

  if(bump_idx != -1) {
    for(i = bump_idx + 1; i < hp->dpb_num_frames; i++) {
      if(hp->frames[i].output_needed &&
         hp->frames[i].poc < hp->frames[bump_idx].poc)
        bump_idx = i;
    }

    if(hp->frames[bump_idx].poc < poc) {
      dpb_output(hp, bump_idx);
      return 1;
    }
  }
  return 0;
}

/**
 *
 */
static void
dpb_add(h264_parser_t *hp, h264_frame_t *frame)
{
  if(frame->is_reference && frame->is_long_term &&
     (frame->frame_idx > hp->dpb_max_longterm_frame_idx))
    frame->is_reference = 0;

  if(frame->is_reference) {
    while(hp->dpb_num_frames == hp->dpb_max_frames)
      if(!dpb_bump(hp, INT32_MAX)) {
        printf("DPB full (%d / %d)\n", hp->dpb_num_frames, hp->dpb_max_frames);
        return;
      }
    hp->frames[hp->dpb_num_frames++] = *frame;
  } else {
    while(dpb_bump(hp, frame->poc)) {}

    outframe(hp, frame);
  }
}


/**
 *
 */
static void
dpb_flush(h264_parser_t *hp, int output)
{
  printf("IDR %d frames in DPB\n", hp->dpb_num_frames);

  if(output)
    while(dpb_bump(hp, INT32_MAX));

  printf("IDR Flushing %d frames\n",  hp->dpb_num_frames);

  int i;
  for(i = 0; i < hp->dpb_num_frames; i++)
    hp->hp_picture_release(hp->frames[i].picture);

  hp->dpb_num_frames = 0;
}


/**
 *
 */
static void
dec_ref_pic_marking(h264_parser_t *hp, bitstream_t *bs)
{
  hp->adaptive_ref_pic_marking_mode_flag = 0;
  if(hp->nal_unit_type == 5) {

    hp->no_output_of_prior_pics_flags = bs->read_bits1(bs);
    hp->long_term_reference_flag      = bs->read_bits1(bs);

  } else if(bs->read_bits(bs, 1)) {
    hp->adaptive_ref_pic_marking_mode_flag = 1;
    int i;
    for(i= 0; i < H264_MMCO_SIZE; i++) {
      int opcode = bs->read_golomb_ue(bs);
      printf("opcode:%d\n", opcode);
      hp->mmco[i].opcode = opcode;

      if(opcode == 0)
        break;

      if(opcode == H264_MMCO_SHORT2UNUSED || opcode == H264_MMCO_SHORT2LONG)
        hp->mmco[i].difference_of_pic_nums_minus1 = bs->read_golomb_ue(bs);

      if(opcode == H264_MMCO_LONG2UNUSED)
        hp->mmco[i].long_term_pic_num             = bs->read_golomb_ue(bs);

      if(opcode == H264_MMCO_SHORT2LONG   || opcode == H264_MMCO_LONG)
        hp->mmco[i].long_term_frame_idx           = bs->read_golomb_ue(bs);

      if(opcode == H264_MMCO_SET_MAX_LONG)
        hp->mmco[i].max_long_term_frame_idx_plus1 = bs->read_golomb_ue(bs);
    }
    hp->mmco_size = i;
  }
}


/**
 *
 */
static void
mark_short_term_unused(h264_parser_t *hp, int pic_num)
{
  int i;

  for(i = 0; i < hp->dpb_num_frames; i++) {
    if(hp->frames[i].is_reference && !hp->frames[i].is_long_term &&
       hp->frames[i].frame_idx == pic_num) {

      hp->frames[i].is_reference = 0;
      if(!hp->frames[i].output_needed)
        dpb_remove(hp, i);
      return;
    }
  }
  printf("Didn't manage to flush frame %d\n", pic_num);
}


/**
 *
 */
static void
mark_long_term_unused(h264_parser_t *hp, int pic_num)
{
  printf("long term unused %d\n", pic_num);
  exit(1);
}

/**
 *
 */
static void
mark_long_term_used(h264_parser_t *hp, int pic_num, int frame_index)
{
  printf("long term used %d -> %d\n", pic_num, frame_index);
  exit(1);
}


/**
 *
 */
static void
mark_all_unused(h264_parser_t *hp)
{
  printf("mark all unused\n");
  exit(1);
}


/**
 *
 */
void
h264_parser_update_dpb(h264_parser_t *hp)
{
  int i;
  int pic_num;

  h264_frame_t *frame = &hp->current_frame;

  prep_frame(hp, frame);

  if(hp->nal_ref_idc && hp->nal_unit_type != 5) {
    if(hp->adaptive_ref_pic_marking_mode_flag) {

      for(i = 0; i < hp->mmco_size; i++) {
        switch(hp->mmco[i].opcode) {
        case H264_MMCO_SHORT2UNUSED:
          pic_num = hp->frame_num -
                 (hp->mmco[i].difference_of_pic_nums_minus1 + 1);
          pic_num = pic_num & (hp->max_frame_num - 1);
#if 0
          printf("Flushing %d - %d - 1 = %d\n",
                 hp->frame_num,
                 hp->mmco[i].difference_of_pic_nums_minus1,
                 pic_num);
#endif

          mark_short_term_unused(hp, pic_num);
          break;

        case H264_MMCO_LONG2UNUSED:
          mark_long_term_unused(hp, hp->mmco[i].long_term_pic_num);
          break;

        case H264_MMCO_SHORT2LONG:
          pic_num = hp->frame_num -
            (hp->mmco[i].difference_of_pic_nums_minus1 + 1);
          mark_long_term_used(hp, pic_num, hp->mmco[i].long_term_frame_idx);
          break;

        default:
          printf("MMCO %d not implemented\n", hp->mmco[i].opcode);
          exit(1);
          break;

        case H264_MMCO_RESET:
          mark_all_unused(hp);
          break;
        }
      }
    } else {
      dpb_mark_sliding(hp);
    }
  }
  dpb_add(hp, frame);
  memset(frame, 0, sizeof(h264_frame_t));
}


/**
 *
 */
static void
ref_pic_list_modification(h264_parser_t *hp, bitstream_t *bs,
                          const h264_sps_t *sps)
{
  int max_frame_num = 1 << sps->max_frame_num_bits;
  int max_pic_num = max_frame_num + (hp->field_pic_flag ? max_frame_num : 0);

  if(hp->slice_type_nos != SLICE_TYPE_I) {
    if(bs->read_bits1(bs)) {
      printf("REF PIC LIST MODIFICATION IN PLACE!!!!!!!!!!!!!!!!\n");
      unsigned int modification_of_pic_nums_idc;
      int refIdxL0 = 0;
      unsigned int picNumL0 = hp->frame_num;
      do {
        modification_of_pic_nums_idc = bs->read_golomb_ue(bs);
        if (modification_of_pic_nums_idc == 0 ||
            modification_of_pic_nums_idc == 1) {
          unsigned int abs_diff_pic_num_minus1 = bs->read_golomb_ue(bs);

          if (modification_of_pic_nums_idc == 0)
            picNumL0 -= (abs_diff_pic_num_minus1 + 1);
          else
            picNumL0 += (abs_diff_pic_num_minus1 + 1);

          picNumL0 &= (max_pic_num - 1);

          int i, j;
          for (i = 0; i < hp->ref_count; i++) {
            if(hp->frames[i].frame_idx == picNumL0)
              break;
          }

          for(j = hp->num_ref_idx_l0_active_minus1 + 1; j > refIdxL0; j--)
            hp->RefPicList0[j] = hp->RefPicList0[j - 1];
          hp->RefPicList0[refIdxL0++] = &hp->frames[i];
          i = refIdxL0;
          for (j = refIdxL0; j <= hp->num_ref_idx_l0_active_minus1 + 1; j++)
            if (hp->RefPicList0[j] && hp->RefPicList0[j]->frame_idx != picNumL0)
              hp->RefPicList0[i++] = hp->RefPicList0[j];
        }
        else if(modification_of_pic_nums_idc == 2) {
          fprintf(stderr, "NOT IMPLEMENTED: modification_of_pic_nums_idc == 2\n");
          /* unsigned int long_term_pic_num = */ bs->read_golomb_ue(bs);
        }
      } while (modification_of_pic_nums_idc != 3);
    }
  }

  if(hp->slice_type_nos == SLICE_TYPE_B) {
    if(bs->read_bits1(bs)) {
      fprintf(stderr, "Unsupported stuff B\n");
      exit(0);
#if 0
      fprintf(stderr, "NOT IMPLEMENTED: ref_pic_list_modification_flag_l1 == 1\n");
      unsigned int modification_of_pic_nums_idc;
      do {
        modification_of_pic_nums_idc = get_ue(c->regs);
        if (modification_of_pic_nums_idc == 0 ||
            modification_of_pic_nums_idc == 1) {
          /* unsigned int abs_diff_pic_num_minus1 = */ get_ue(c->regs);
        } else if (modification_of_pic_nums_idc == 2) {
          /* unsigned int long_term_pic_num = */ get_ue(c->regs);
        }
      } while (modification_of_pic_nums_idc != 3);
#endif
    }
  }

}




static int
poccmp(const void *A, const void *B)
{
  const h264_frame_t *a = *(const h264_frame_t **)A;
  const h264_frame_t *b = *(const h264_frame_t **)B;

  return a->poc - b->poc;
}

 


/**
 *
 */
static void
build_refpiclist(h264_parser_t *hp)
{
  h264_frame_t *tmp[hp->dpb_num_frames];

  int i;
  for(i = 0; i < hp->dpb_num_frames; i++)
    tmp[i] = &hp->frames[i];
  
  qsort(tmp, hp->dpb_num_frames, sizeof(h264_frame_t *), poccmp);

#ifdef REFLIST_DEBUG
  printf("reflist [%d items]\n", hp->dpb_num_frames);
  for(i = 0; i < hp->dpb_num_frames; i++)
    printf("  [%2d] POC:%d\n", i, tmp[i]->poc);
#endif

  int ptr0 = 0;
  int ptr1 = 0;
  for(i = 0; i < hp->dpb_num_frames; i++) {
    if(tmp[hp->dpb_num_frames - 1 - i]->poc < hp->current_frame.poc)
      hp->RefPicList0[ptr0++] = tmp[hp->dpb_num_frames - 1 - i];
    
    if(tmp[i]->poc >= hp->current_frame.poc)
      hp->RefPicList1[ptr1++] = tmp[i];
  }

#ifdef REFLIST_DEBUG
  printf("refpiclist0 (%d items)\n", ptr0);
  for(i = 0; i < ptr0; i++)
    printf("  [%2d] POC:%d\n", i, hp->RefPicList0[i]->poc);

  printf("refpiclist1 (%d items)\n", ptr1);
  for(i = 0; i < ptr1; i++)
    printf("  [%2d] POC:%d\n", i, hp->RefPicList1[i]->poc);
 
  printf("\n");
#endif
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

  hp->dpb_max_frames = sps->num_ref_frames; // might need to flush

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
#if 1
  printf("Slicetype:%d PPS:%d frame:%d first_mb_in_slice:%d POC:%d\n",
         hp->slice_type, pps_id,
         hp->frame_num, hp->first_mb_in_slice,
         hp->current_frame.poc);
#endif

  build_refpiclist(hp);

  ref_pic_list_modification(hp, bs, sps);

  if((pps->weighted_pred_flag   && hp->slice_type_nos == SLICE_TYPE_P) ||
     (pps->weighted_bipred_idc == 1 && hp->slice_type_nos == SLICE_TYPE_B)) {
    pred_weight_table(hp, bs, sps);
  } else if(pps->weighted_bipred_idc == 2 &&
	    hp->slice_type_nos == SLICE_TYPE_B) {
    pred_weight_table(hp, bs, sps);
  } else {
    hp->luma_log2_weight_denom = 0;
    hp->chroma_log2_weight_denom = 0;
  }




  if(hp->nal_ref_idc)
    dec_ref_pic_marking(hp, bs);

  if(pps->cabac && hp->slice_type_nos != SLICE_TYPE_I)
    hp->cabac_init_idc = bs->read_golomb_ue(bs);

  hp->slice_qp_delta = bs->read_golomb_se(bs);

  if(hp->slice_type == SLICE_TYPE_SP || hp->slice_type == SLICE_TYPE_SI) {
    if(hp->slice_type == SLICE_TYPE_SP)
      hp->sp_for_switch_flag = bs->read_bits(bs, 1);
    hp->slice_qs_delta = bs->read_golomb_se(bs);
  }

  if(pps->deblocking_filter_parameters_present) {
    hp->disable_deblocking_filter_idc = bs->read_golomb_ue(bs);
    if(hp->disable_deblocking_filter_idc != 1) {
      hp->slice_alpha_c0_offset_div2 = bs->read_golomb_se(bs);
      hp->slice_beta_offset_div2     = bs->read_golomb_se(bs);
    }
  }
}


/**
 *
 */
static void
idr(h264_parser_t *hp)
{
  printf("========= IDR =============================================\n");
  dpb_flush(hp, 1);
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

  if(len > hp->escbuf_size) {
    hp->escbuf_size = len;
    hp->escbuf = realloc(hp->escbuf, len);
  }

  int rbsp_size = 0;
  int i;
  uint8_t *d = hp->escbuf;
  for(i = 0; i < len; i++) {
    if(i + 2 < len && data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 3) {
      d[rbsp_size++] = 0;
      d[rbsp_size++] = 0;
      i += 2;
    } else {
      d[rbsp_size++] = data[i];
    }
  }

#if 0
  printf("NAL %d bytes  type:%d, ref_idc:%d rbsp_size:%d\n",
         len, hp->nal_unit_type,
         hp->nal_ref_idc, rbsp_size);
#endif

  bitstream_t bs;
  init_rbits(&bs, hp->escbuf, rbsp_size);
  h264_parser_decode_nal_from_bs(hp, &bs);
}


/**
 *
 */
void
h264_parser_dpb_dump(h264_parser_t *hp)
{
  int i;
  printf("Decoded Picture Buffer\n");
  for(i = 0; i < hp->dpb_num_frames; i++) {
    printf("%d fn:%d poc:%d PIC:%p\n",
	   i, hp->frames[i].frame_idx, hp->frames[i].poc,
	   hp->frames[i].picture);
  }
}


/**
 *
 */
void
h264_parser_fini(h264_parser_t *hp)
{
  dpb_flush(hp, 0);
  free(hp->escbuf);
}


/**
 * Dummy decoder
 */
static void
dummy_decode(struct media_codec *mc, struct video_decoder *vd,
             struct media_queue *mq, struct media_buf *mb, int reqsize)
{
  h264_parser_t *hp = mc->opaque;
  int len = mb->mb_size;
  const uint8_t *d = mb->mb_data;

  printf("-----------------------------------------\n");

  //  if(1)dpb_dump(hp);

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
        h264_parser_decode_nal(hp, p, d - p);

      d += 4;
      len -= 4;
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
  h264_parser_update_dpb(hp);
  printf("-----------------------------------------\n");
  usleep(20000);
}


static void
dummy_flush(struct media_codec *mc, struct video_decoder *vd)
{

}


/**
 *
 */
static void
dummy_close(struct media_codec *mc)
{
  free(mc->opaque);
}

/**
 *
 */
static int
dummy_h264_codec_create(media_codec_t *mc, const media_codec_params_t *mcp,
                        media_pipe_t *mp)
{
  return 1;

  if(mc->codec_id != CODEC_ID_H264)
    return 1;

  h264_parser_t *hp = calloc(1, sizeof(h264_parser_t));
  hp->dpb_max_longterm_frame_idx = -1;

  if(mcp != NULL && mcp->extradata_size != 0)
    h264_parser_init(hp, NULL, NULL, NULL, mcp->extradata, mcp->extradata_size);

  mc->opaque = hp;
  mc->decode = dummy_decode;
  mc->flush  = dummy_flush;
  mc->close  = dummy_close;

  return 0;
}


REGISTER_CODEC(NULL, dummy_h264_codec_create, 30);
