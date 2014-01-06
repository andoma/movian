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

#include <stdint.h>
#include <assert.h>

#include "ext/tlsf/tlsf.h"
#include <cedardev_api.h>
#include <linux/fb.h>
#include "drv_display_sun4i.h"

#include "arch/threads.h"

typedef struct sunxi {
  int cedarfd;
  int dispfd;
  int fb0fd;
  cedarv_env_info_t env_info;
  tlsf_pool gfxmem;
  uint32_t gfxmembase;
  hts_mutex_t gfxmem_mutex;
  void *macc;
  int ve_version;
} sunxi_t;


extern sunxi_t sunxi;


void sunxi_init(void);

void sunxi_fini(void);

void sunxi_bg_every_frame(int hide);

void sunxi_flush_cache(void *start, int len); // VA

static inline uint32_t va_to_pa(void *va)
{
  return (intptr_t)va - sunxi.gfxmembase + sunxi.env_info.phymem_start;
}




#define VE_CTRL				0x000
#define VE_VERSION			0x0f0

#define VE_MPEG_PIC_HDR			0x100
#define VE_MPEG_SIZE			0x108
#define VE_MPEG_FRAME_SIZE		0x10c
#define VE_MPEG_CTRL			0x114
#define VE_MPEG_TRIGGER			0x118
#define VE_MPEG_STATUS			0x11c
#define VE_MPEG_VLD_ADDR		0x128
#define VE_MPEG_VLD_OFFSET		0x12c
#define VE_MPEG_VLD_LEN			0x130
#define VE_MPEG_VLD_END			0x134
#define VE_MPEG_REC_LUMA		0x148
#define VE_MPEG_REC_CHROMA		0x14c
#define VE_MPEG_FWD_LUMA		0x150
#define VE_MPEG_FWD_CHROMA		0x154
#define VE_MPEG_BACK_LUMA		0x158
#define VE_MPEG_BACK_CHROMA		0x15c
#define VE_MPEG_IQ_MIN_INPUT		0x180
#define VE_MPEG_ROT_LUMA		0x1cc
#define VE_MPEG_ROT_CHROMA		0x1d0
#define VE_MPEG_SDROT_CTRL		0x1d4

#define VE_H264_FRAME_SIZE		0x200
#define VE_H264_PIC_HDR			0x204
#define VE_H264_SLICE_HDR		0x208
#define VE_H264_SLICE_HDR2		0x20c
#define VE_H264_PRED_WEIGHT		0x210
#define VE_H264_QP_PARAM		0x21c
#define VE_H264_CTRL			0x220
#define VE_H264_TRIGGER			0x224
#define VE_H264_STATUS			0x228
#define VE_H264_CUR_MB_NUM		0x22c
#define VE_H264_VLD_ADDR		0x230
#define VE_H264_VLD_OFFSET		0x234
#define VE_H264_VLD_LEN			0x238
#define VE_H264_VLD_END			0x23c
#define VE_H264_SDROT_CTRL		0x240
#define VE_H264_OUTPUT_FRAME_IDX	0x24c
#define VE_H264_EXTRA_BUFFER1		0x250
#define VE_H264_EXTRA_BUFFER2		0x254
#define VE_H264_BASIC_BITS		0x2dc
#define VE_H264_RAM_WRITE_PTR		0x2e0
#define VE_H264_RAM_WRITE_DATA		0x2e4

#define VE_SRAM_H264_PRED_WEIGHT_TABLE	0x000
#define VE_SRAM_H264_FRAMEBUFFER_LIST	0x400
#define VE_SRAM_H264_REF_LIST0		0x640
#define VE_SRAM_H264_REF_LIST1		0x664


void cedar_decode_write32(int off, int value);
void cedar_decode_read32(int off, int value);


static inline void
ve_write(int reg, uint32_t val)
{
  assert(reg <= 0x1000);
  void *addr = sunxi.macc + reg;
  cedar_decode_write32(reg, val);
  *((volatile uint32_t *)addr) = val;
}

static inline uint32_t
ve_read(int reg)
{
  assert(reg <= 0x1000);
  void *addr = sunxi.macc + reg;
  uint32_t v = *((volatile uint32_t *)addr);
  cedar_decode_read32(reg, v);
  return v;
}

int sunxi_ve_wait(int timeout);

