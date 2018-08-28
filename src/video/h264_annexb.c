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
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include "h264_annexb.h"
#include "misc/bitstream.h"
#include "h264_parser.h"

/**
 *
 */
static void
h264_to_annexb_inplace(uint8_t *b, size_t fsize)
{
  uint8_t *p = b;
  while(p < b + fsize) {
    if(p[0])
      break; // Avoid overflows with this simple check
    int len = (p[1] << 16) + (p[2] << 8) + p[3];
    p[0] = 0;
    p[1] = 0;
    p[2] = 0;
    p[3] = 1;
    p += len + 4;
  }
}


/**
 *
 */
static int
h264_to_annexb_buffered(uint8_t *dst, const uint8_t *b, size_t fsize, int lsize)
{
  const uint8_t *p = b;
  int i;
  int ol = 0;
  while(p < b + fsize) {

    int len = 0;
    for(i = 0; i < lsize; i++)
      len = len << 8 | *p++;

    if(dst) {
      dst[0] = 0;
      dst[1] = 0;
      dst[2] = 0;
      dst[3] = 1;
      memcpy(dst + 4, p, len);
      dst += 4 + len;
    }
    ol += 4 + len;
    p += len;
  }
  return ol;
}


/**
 *
 */
int
h264_to_annexb(h264_annexb_ctx_t *ctx, uint8_t **datap, size_t *sizep)
{
  int l;

  switch(ctx->lsize) {
  case 4:
    h264_to_annexb_inplace(*datap, *sizep);
  case 0:
    

    //    submit_au(vdd, &au, mb->mb_data, mb->mb_size, mb->mb_skip == 1, vd);
    break;
  case 3:
  case 2:
  case 1:
    l = h264_to_annexb_buffered(NULL, *datap, *sizep, ctx->lsize);
    if(l > ctx->tmpbufsize) {
      ctx->tmpbuf = realloc(ctx->tmpbuf, l);
      ctx->tmpbufsize = l;
    }
    if(ctx->tmpbuf == NULL)
      return -1;
    h264_to_annexb_buffered(ctx->tmpbuf, *datap, *sizep, ctx->lsize);
    *datap = ctx->tmpbuf;
    *sizep = l;
    break;
  }
  
  //    submit_au(vdd, &au, vdd->tmpbuf, l, mb->mb_skip == 1, vd);
 
 return 0;
}


/**
 *
 */
static void
append_extradata(h264_annexb_ctx_t *ctx, const uint8_t *data, int len)
{
  ctx->extradata = realloc(ctx->extradata, ctx->extradata_size + len);
  memcpy(ctx->extradata + ctx->extradata_size, data, len);
  ctx->extradata_size += len;
}

/**
 *
 */
void
h264_to_annexb_init(h264_annexb_ctx_t *ctx, const uint8_t *data, int len)
{
  int i, n, s;

  uint8_t buf[4] = {0,0,0,1};

  if(len < 7 || data[0] != 1)
    return;

  int lsize = (data[4] & 0x3) + 1;

  n = data[5] & 0x1f;
  data += 6;
  len -= 6;

  for(i = 0; i < n && len >= 2; i++) {
    s = ((data[0] << 8) | data[1]) + 2;
    if(len < s)
      break;

    append_extradata(ctx, buf, 4);
    append_extradata(ctx, data + 2, s - 2);
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

    append_extradata(ctx, buf, 4);
    append_extradata(ctx, data + 2, s - 2);
    data += s;
    len -= s;
  }

  ctx->lsize = lsize;
}



/**
 *
 */
typedef struct h264_annexb_to_avc {

  media_codec_t *hata_decoder;
  media_pipe_t *hata_mp;

  int (*hata_create_decoder)(media_codec_t *mc,
                             const media_codec_params_t *mcp,
                             media_pipe_t *mp);

  uint8_t *hata_buffer;
  int hata_buffer_capacity;
  int hata_buffer_len;

  uint16_t hata_width;
  uint16_t hata_height;

  uint8_t *hata_avcc;
  int hata_avcc_len;


  struct {
    uint8_t *data;
    int len;
    uint16_t width;
    uint16_t height;
  } sps[8];

  struct {
    uint8_t *data;
    int len;
  } pps[32];

  int hata_ps_updated;

} h264_annexb_to_avc_t;


/*
 01 64 00 29 ff e1 00 1a  67 64 00 29 ac c8 40 1e    .d.)....gd.)..@.
 00 89 f9 70 11 00 00 03  03 e9 00 00 bb 80 8f 18    ...p............
 31 96 01 00 06 68 e9 3b  13 21 30                   1....h.;.!0
 */

/**
 *
 */
static void
hata_sps(h264_annexb_to_avc_t *hata, const uint8_t *data, int len)
{
  if(len < 5)
    return;

  bitstream_t bs;
  h264_sps_t sps;
  init_rbits(&bs, data + 1, len - 1, 0);

  int sps_id = h264_parser_decode_sps(NULL, &bs, &sps);
  if(sps_id < 0 || sps_id >= 8)
    return;

  int x = 0;
  for(int i = 0; i < len; i++)
    x += data[i];
  if(hata->sps[sps_id].len == len && !memcmp(hata->sps[sps_id].data, data, len))
    return;

  free(hata->sps[sps_id].data);
  hata->sps[sps_id].width  = sps.mb_width  * 16;
  hata->sps[sps_id].height = sps.mb_height * 16 * (2 - sps.mbs_only_flag);
  hata->sps[sps_id].data = malloc(len);
  hata->sps[sps_id].len = len;
  memcpy(hata->sps[sps_id].data, data, len);
  hata->hata_ps_updated = 1;
}


/**
 *
 */
static void
hata_pps(h264_annexb_to_avc_t *hata, const uint8_t *data, int len)
{
  if(len < 2)
    return;

  bitstream_t bs;

  assert(len < 4000);

  init_rbits(&bs, data + 1, len - 1, 0);
  int pps_id = bs.read_golomb_ue(&bs);
  if(pps_id >= 32)
    return;

  if(hata->pps[pps_id].len == len && !memcmp(hata->pps[pps_id].data, data, len))
    return;

  free(hata->pps[pps_id].data);
  hata->pps[pps_id].data = malloc(len);
  hata->pps[pps_id].len = len;
  memcpy(hata->pps[pps_id].data, data, len);
  hata->hata_ps_updated = 1;
}

/**
 *
 */
static void
hata_slice(h264_annexb_to_avc_t *hata, const uint8_t *data, int len)
{
  int newlen = hata->hata_buffer_len + len + 4;

  if(newlen > hata->hata_buffer_capacity) {
    hata->hata_buffer_capacity = newlen;
    hata->hata_buffer = realloc(hata->hata_buffer, hata->hata_buffer_capacity);
  }

  hata->hata_buffer[hata->hata_buffer_len + 0] = len >> 24;
  hata->hata_buffer[hata->hata_buffer_len + 1] = len >> 16;
  hata->hata_buffer[hata->hata_buffer_len + 2] = len >> 8;
  hata->hata_buffer[hata->hata_buffer_len + 3] = len;

  memcpy(hata->hata_buffer + hata->hata_buffer_len + 4, data, len);
  hata->hata_buffer_len = newlen;
}


/**
 *
 */
static void
hata_handle_nal(h264_annexb_to_avc_t *hata, const uint8_t *data, int len)
{
  //  int nal_ref_idc   = data[0] >> 5;
  int nal_unit_type = data[0] & 0x1f;
  switch(nal_unit_type) {
  case 7: // SPS
    hata_sps(hata, data, len);
    break;
  case 8: // PPS
    hata_pps(hata, data, len);
    break;

  case 1:
  case 5:  // slice
    hata_slice(hata, data, len);
    break;
  }
}


/**
 *
 */
static int
build_extradata(h264_annexb_to_avc_t *hata, uint8_t *out, int outlen)
{
  uint8_t *start = out;
  if(outlen < 6)
    return 0;

  if(hata->sps[0].len < 4)
    return 0;

  int num_sps = 0;

  for(int i = 0; i < 8; i++)
    if(hata->sps[i].len)
      num_sps++;


  out[0] = 0x1;
  out[1] = hata->sps[0].data[1];
  out[2] = hata->sps[0].data[2];
  out[3] = hata->sps[0].data[3];
  out[4] = 0xff;
  out[5] = 0xe0 | num_sps;
  outlen -= 6;
  out += 6;

  for(int i = 0; i < 8; i++) {
    int len = hata->sps[i].len;
    if(len) {
      if(outlen < 2 + len)
        return 0;
      *out++ = len >> 8;
      *out++ = len;
      memcpy(out, hata->sps[i].data, len);
      out += len;
      outlen -= 2 + len;

      hata->hata_width  = hata->sps[i].width;
      hata->hata_height = hata->sps[i].height;

    }
  }

  if(outlen < 1)
    return 0;

  int num_pps = 0;

  for(int i = 0; i < 32; i++)
    if(hata->pps[i].len)
      num_pps++;

  *out++ = num_pps;

  for(int i = 0; i < 32; i++) {
    int len = hata->pps[i].len;
    if(len) {
      if(outlen < 2 + len)
        return 0;
      *out++ = len >> 8;
      *out++ = len;
      memcpy(out, hata->pps[i].data, len);
      out += len;
      outlen -= 2 + len;
    }
  }
  return out - start;
}


/**
 *
 */
static void
hata_decode(struct media_codec *mc, struct video_decoder *vd,
            struct media_queue *mq, struct media_buf *mb, int reqsize)
{
  h264_annexb_to_avc_t *hata = mc->opaque;

  const uint8_t *d = mb->mb_data;
  int len = mb->mb_size;
  const uint8_t *p = NULL;


  while(len > 3) {
    if(!(d[0] == 0 && d[1] == 0 && d[2] == 1)) {
      d++;
      len--;
      continue;
    }

    if(p != NULL)
      hata_handle_nal(hata, p, d - p);

    d += 3;
    len -= 3;
    p = d;
  }
  d += len;


  if(p != NULL)
    hata_handle_nal(hata, p, d - p);

  if(hata->hata_buffer_len == 0)
    return;


  int reconfig = 0;

  if(hata->hata_ps_updated) {
    hata->hata_ps_updated = 0;
    uint8_t buf[4096];
    int len;

    len = build_extradata(hata, buf, sizeof(buf));
    if(!len)
      return;

    if(hata->hata_avcc_len != len || memcmp(hata->hata_avcc, buf, len)) {
      hata->hata_avcc_len = len;
      free(hata->hata_avcc);
      hata->hata_avcc = malloc(len);
      memcpy(hata->hata_avcc, buf, len);
      reconfig = 1;
    }
  }

  if(reconfig && hata->hata_decoder != NULL) {
    media_codec_deref(hata->hata_decoder);
    hata->hata_decoder = NULL;
  }

  if(hata->hata_decoder == NULL) {
    media_codec_t *decoder;

    media_codec_params_t mcp = {0};
    mcp.extradata      = hata->hata_avcc;
    mcp.extradata_size = hata->hata_avcc_len;
    mcp.width          = hata->hata_width;
    mcp.height         = hata->hata_height;

    decoder = media_codec_create(mc->codec_id, 0, NULL, NULL, &mcp, mc->mp);
    hata->hata_decoder = decoder;
  }

  media_buf_t mb2 = *mb;
  mb2.mb_data = hata->hata_buffer;
  mb2.mb_size = hata->hata_buffer_len;
  mb2.mb_cw = NULL;
  mb2.mb_dtor = NULL;
  hata->hata_decoder->decode(hata->hata_decoder, vd, mq, &mb2, reqsize);
  hata->hata_buffer_len = 0;
}


/**
 *
 */
static void
hata_flush(struct media_codec *mc, struct video_decoder *vd)
{
  h264_annexb_to_avc_t *hata = mc->opaque;
  mc = hata->hata_decoder;

  if(mc == NULL || mc->flush == NULL)
    return;
  mc->flush(mc, vd);
}


/**
 *
 */
static void
hata_close(struct media_codec *mc)
{
  h264_annexb_to_avc_t *hata = mc->opaque;

  mc = hata->hata_decoder;

  if(mc != NULL)
    media_codec_deref(mc);
  free(hata);
}


/**
 * Converts a h264 annexb into an AVC stream
 *
 * Creating decoders as needed
 */
int
h264_annexb_to_avc(media_codec_t *mc, media_pipe_t *mp,
                   int (*create)(media_codec_t *mc,
                                 const media_codec_params_t *mcp,
                                 media_pipe_t *mp))
{
  h264_annexb_to_avc_t *hata = calloc(1, sizeof(h264_annexb_to_avc_t));

  assert(mc->codec_id == AV_CODEC_ID_H264);

  hata->hata_create_decoder = create;
  hata->hata_mp = mp;

  mc->decode = hata_decode;
  mc->close = hata_close;
  mc->flush = hata_flush;
  mc->opaque = hata;
  return 0;
}
