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
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include "main.h"
#include "sunxi.h"
#include "backend/backend.h"
#include "misc/pixmap.h"
#include "misc/rstr.h"

static void sunxi_set_bg(const char *path);

sunxi_t sunxi;

static void
disp_cleanup(void)
{

  unsigned long args[4] = {0};

  args[1] = 101;
  ioctl(sunxi.dispfd, DISP_CMD_VIDEO_STOP, args);
  ioctl(sunxi.dispfd,DISP_CMD_LAYER_CLOSE, args);
  ioctl(sunxi.dispfd, DISP_CMD_LAYER_RELEASE, args);
  args[1] = 102;
  ioctl(sunxi.dispfd, DISP_CMD_VIDEO_STOP, args);
  ioctl(sunxi.dispfd,DISP_CMD_LAYER_CLOSE, args);
  ioctl(sunxi.dispfd, DISP_CMD_LAYER_RELEASE, args);
  args[1] = 103;
  ioctl(sunxi.dispfd, DISP_CMD_VIDEO_STOP, args);
  ioctl(sunxi.dispfd,DISP_CMD_LAYER_CLOSE, args);
  ioctl(sunxi.dispfd, DISP_CMD_LAYER_RELEASE, args);
}


void
sunxi_init(void)
{
  
  sunxi.dispfd = open("/dev/disp", O_RDWR);
  if(sunxi.dispfd == -1) {
    perror("open(/dev/disp)");
    exit(1);
  }

  
  sunxi.fb0fd = open("/dev/fb0", O_RDWR);
  if(sunxi.fb0fd == -1) {
    perror("open(/dev/fb0)");
    exit(1);
  }
#if 0
  int tmp = SUNXI_DISP_VERSION;
  if(ioctl(sunxi.dispfd, DISP_CMD_VERSION, &tmp) < 0) {
    perror("ioctl(DISP_CMD_VERSION))");
    exit(1);
  }
#endif

  disp_cleanup();

  sunxi.cedarfd = open("/dev/cedar_dev", O_RDWR);
  if(sunxi.cedarfd == -1) {
    perror("open(/dev/cedar_dev)");
    exit(1);
  }

  if(ioctl(sunxi.cedarfd, IOCTL_GET_ENV_INFO, &sunxi.env_info)) {
    perror("ioctl(IOCTL_GET_ENV_INFO)");
    exit(1);
  }

  sunxi.macc = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
		    MAP_SHARED, sunxi.cedarfd, sunxi.env_info.address_macc);

  if(sunxi.macc == MAP_FAILED) {
    perror("mmap of macc failed");
    exit(1);
  }



  void *ptr = mmap(NULL, sunxi.env_info.phymem_total_size,
		   PROT_READ | PROT_WRITE, MAP_SHARED,
		   sunxi.cedarfd, sunxi.env_info.phymem_start);

  if(ptr == MAP_FAILED) {
    perror("mmap failed");
    exit(1);
  }

  sunxi.gfxmembase = (intptr_t)ptr;
  sunxi.gfxmem = tlsf_create(ptr, sunxi.env_info.phymem_total_size);

  if(ioctl(sunxi.cedarfd, IOCTL_ENGINE_REQ, 0)) {
    perror("IOCTL_ENGINE_REQ");
    exit(1);
  }

  ioctl(sunxi.cedarfd, IOCTL_ENABLE_VE, 0);
  ioctl(sunxi.cedarfd, IOCTL_SET_VE_FREQ, 320);
  ioctl(sunxi.cedarfd, IOCTL_RESET_VE, 0);


  ve_write(VE_CTRL, 0x00130007);

  sunxi.ve_version = ve_read(VE_VERSION) >> 16;

  TRACE(TRACE_INFO, "CEDAR", "Version 0x%04x opened. PA: %x VA: %p",
	sunxi.ve_version, sunxi.env_info.phymem_start, sunxi.gfxmembase);

  hts_mutex_init(&sunxi.gfxmem_mutex);

  unsigned long args[4] = {0};

  args[1] = 0x64;
  if(ioctl(sunxi.dispfd, DISP_CMD_LAYER_CK_OFF, args)) {
    perror("DISP_CMD_LAYER_CK_OFF");
    exit(1);
  }

  args[1] = 0x64;
  if(ioctl(sunxi.dispfd, DISP_CMD_LAYER_ALPHA_OFF, args)) {
    perror("DISP_CMD_LAYER_ALPHA_OFF");
    exit(1);
  }

#if 0
  __disp_colorkey_t   colorkey;

  colorkey.ck_min.alpha = 0;
  colorkey.ck_min.red   = 0;
  colorkey.ck_min.green = 0;
  colorkey.ck_min.blue  = 0;
  colorkey.ck_max.alpha = 255;
  colorkey.ck_max.red   = 255;
  colorkey.ck_max.green = 255;
  colorkey.ck_max.blue  = 255;
  colorkey.red_match_rule   = 3;
  colorkey.green_match_rule = 3;
  colorkey.blue_match_rule  = 3;

  args[1] = (intptr_t)&colorkey;
  if(ioctl(sunxi.dispfd, DISP_CMD_SET_COLORKEY, args)) {
    perror("DISP_CMD_SET_COLORKEY failed.\n");
  }
#endif
  sunxi_set_bg("/root/background.jpg");
}


void
sunxi_fini(void)
{
  if(ioctl(sunxi.cedarfd, IOCTL_ENGINE_REL, 0)) {
    perror("IOCTL_ENGINE_REL");
    exit(1);
  }

  disp_cleanup();
}

static int bg_layer = 0;
static float bg_wanted_alpha;
static int bg_open;


static void
sunxi_set_bg(const char *path)
{
  char errbuf[128];
  image_meta_t im = {0};
  unsigned long args[4] = {0};
  pixmap_t *pm;
  int width = 1280, height = 720;
  int r;

  return;

  // hum
  im.im_req_width  = width;
  im.im_req_height = height;

  rstr_t *rpath = rstr_alloc(path);

  pm = backend_imageloader(rpath, &im, NULL, errbuf, sizeof(errbuf),
			   NULL, NULL, NULL);
  rstr_release(rpath);

  if(pm == NULL) {
    TRACE(TRACE_ERROR, "BG", "Unable to load %s -- %s", path, errbuf);
    return;
  }

  int bpp;

  switch(pm->pm_type) {
  case PIXMAP_RGB24:
    bpp = 3;
    break;
  case PIXMAP_BGR32:
  case PIXMAP_RGBA:
  case PIXMAP_BGRA:
    bpp = 4;
    break;
  default:
    abort();
  }


  size_t tsize = pm->pm_height * pm->pm_linesize;

  hts_mutex_lock(&sunxi.gfxmem_mutex);
  uint8_t *dst = tlsf_memalign(sunxi.gfxmem, 1024, tsize);
  hts_mutex_unlock(&sunxi.gfxmem_mutex);
  memcpy(dst, pm->pm_pixels, tsize);

  pixmap_release(pm);

  __disp_video_fb_t   frmbuf;
  memset(&frmbuf, 0, sizeof(__disp_video_fb_t));
  frmbuf.addr[0] = va_to_pa(dst);
  frmbuf.addr[1] = va_to_pa(dst);
  frmbuf.addr[2] = va_to_pa(dst);

  args[1] = DISP_LAYER_WORK_MODE_NORMAL;
  int hlay = ioctl(sunxi.dispfd, DISP_CMD_LAYER_REQUEST, args);
  if(hlay == -1)
    exit(3);

  __disp_layer_info_t l;
  memset(&l, 0, sizeof(l));
    
  l.mode = DISP_LAYER_WORK_MODE_NORMAL;
  l.pipe = 1;

  l.fb.size.width  = pm->pm_linesize / bpp;
  l.fb.size.height = pm->pm_height;
  l.fb.addr[0] = frmbuf.addr[0];
  l.fb.addr[1] = frmbuf.addr[1];
  l.fb.addr[2] = frmbuf.addr[2];

  switch(pm->pm_type) {
  case PIXMAP_RGB24:
    l.fb.format = DISP_FORMAT_RGB888;
    l.fb.br_swap       = 1;
    l.fb.mode  = DISP_MOD_INTERLEAVED;
    break;
  case PIXMAP_BGR32:
    l.fb.format = DISP_FORMAT_ARGB8888;
    l.fb.br_swap       = 1;
    l.fb.mode  = DISP_MOD_INTERLEAVED;
    break;
  default:
    abort();
  }

  ///  l.fb.seq   = 0;
  //  l.fb.mode   = DISP_MOD_NON_MB_PLANAR;
  //  l.fb.format = DISP_FORMAT_YUV420;

  l.ck_enable        = 0;
  l.alpha_en         = 1;
  l.alpha_val        = 0;
  l.src_win.x        = 0;
  l.src_win.y        = 0;
  l.src_win.width    = width;
  l.src_win.height   = height;
  l.scn_win.x        = 0;
  l.scn_win.y        = 0;
  l.scn_win.width    = width;
  l.scn_win.height   = height;
    
  args[1] = hlay;
  args[2] = (__u32)&l;
  args[3] = 0;
  r = ioctl(sunxi.dispfd,DISP_CMD_LAYER_SET_PARA,(void*)args);
  if(r)
    perror("ioctl(disphd,DISP_CMD_LAYER_SET_PARA)");
 
  args[1] = hlay;
  args[2] = 0;
  r = ioctl(sunxi.dispfd,DISP_CMD_LAYER_OPEN,(void*)args);
  if(r)
    perror("ioctl(disphd,DISP_CMD_LAYER_OPEN)");

  bg_open = 1;

  args[1] = hlay;
  if(ioctl(sunxi.dispfd, DISP_CMD_LAYER_BOTTOM, args))
    perror("ioctl(disphd,DISP_CMD_LAYER_BOTTOM)");

  bg_layer = hlay;
}


#define LP(a, y0, y1) (((y0) * ((a) - 1.0) + (y1)) / (a))

void
sunxi_bg_every_frame(int hide)
{
  unsigned long args[4] = {0};
  int r;
  if(!bg_layer)
    return;

  args[1] = bg_layer;

  float alpha = hide ? 0 : 1;

  bg_wanted_alpha = LP(16, bg_wanted_alpha, alpha);

  int a8 = bg_wanted_alpha * 255.0;

  if(a8 == 0) {
    if(bg_open) {
      r = ioctl(sunxi.dispfd, DISP_CMD_LAYER_CLOSE, args);
      if(r)
	perror("ioctl(disphd,DISP_CMD_LAYER_CLOSE)");
      else
	bg_open = 0;
    }
  } else {
    if(!bg_open) {
      r = ioctl(sunxi.dispfd, DISP_CMD_LAYER_OPEN, args);
      if(r) {
	perror("ioctl(disphd,DISP_CMD_LAYER_OPEN)");
	return;
      }
      else
	bg_open = 1;
    }
    
    if(ioctl(sunxi.dispfd, DISP_CMD_LAYER_BOTTOM, args))
      perror("ioctl(disphd,DISP_CMD_LAYER_BOTTOM)");

    args[2] = a8;
    if(ioctl(sunxi.dispfd, DISP_CMD_LAYER_SET_ALPHA_VALUE, args))
      perror("ioctl(disphd, DISP_CMD_LAYER_SET_ALPHA_VALUE)");
  }
}


void
sunxi_flush_cache(void *start, int len)
{
  struct cedarv_cache_range range = {
    .start = (int)start,
    .end = (int)(start + len)
  };
  ioctl(sunxi.cedarfd, IOCTL_FLUSH_CACHE, (void*)(&range));
  //  printf("Flush %p + %x = %d\n", start, len, r);
}


int
sunxi_ve_wait(int timeout)
{
  if(sunxi.cedarfd == -1)
    return 0;

  return ioctl(sunxi.cedarfd, IOCTL_WAIT_VE, timeout);
}





static int sram_write_ptr;
static uint32_t cedar_mirror[4096];

static uint32_t cedar_sram_mirror[4096];

static uint32_t h264_trig;
static uint32_t output_frame_idx;

static void
dump_dpb(void)
{
  int i;
  for(i = 0; i < 18; i++) {
    
    printf("DPB[%02d] %3d:%-3d flags:0x%02x bufs:%08x %08x %08x %08x %x %s\n",
	   i, 
	   cedar_sram_mirror[0x100 + 0 + i * 8],
	   cedar_sram_mirror[0x100 + 1 + i * 8],
	   cedar_sram_mirror[0x100 + 2 + i * 8],
	   cedar_sram_mirror[0x100 + 3 + i * 8],
	   cedar_sram_mirror[0x100 + 4 + i * 8],
	   cedar_sram_mirror[0x100 + 5 + i * 8],
	   cedar_sram_mirror[0x100 + 6 + i * 8],
	   cedar_sram_mirror[0x100 + 7 + i * 8],
	   i == output_frame_idx ? "[OUTPUT]" : "");
  }
}

static void
dump_refs(void)
{
  printf("REFLIST0: 0x%08x 0x%08x 0x%08x 0x%08x\n",
	 cedar_sram_mirror[VE_SRAM_H264_REF_LIST0 / 4 + 0],
	 cedar_sram_mirror[VE_SRAM_H264_REF_LIST0 / 4 + 1],
	 cedar_sram_mirror[VE_SRAM_H264_REF_LIST0 / 4 + 2],
	 cedar_sram_mirror[VE_SRAM_H264_REF_LIST0 / 4 + 3]);

  printf("REFLIST1: 0x%08x 0x%08x 0x%08x 0x%08x\n",
	 cedar_sram_mirror[VE_SRAM_H264_REF_LIST1 / 4 + 0],
	 cedar_sram_mirror[VE_SRAM_H264_REF_LIST1 / 4 + 1],
	 cedar_sram_mirror[VE_SRAM_H264_REF_LIST1 / 4 + 2],
	 cedar_sram_mirror[VE_SRAM_H264_REF_LIST1 / 4 + 3]);
}

static void
dump_regs(void)
{
  printf("REGS: %08x %08x %08x %08x %08x %08x %08x %08x\n",
	 cedar_mirror[VE_H264_FRAME_SIZE],
	 cedar_mirror[VE_H264_PIC_HDR],
	 cedar_mirror[VE_H264_SLICE_HDR],
	 cedar_mirror[VE_H264_SLICE_HDR2],
	 cedar_mirror[VE_H264_PRED_WEIGHT],
	 cedar_mirror[VE_H264_QP_PARAM],
	 cedar_mirror[VE_H264_CUR_MB_NUM],
	 cedar_mirror[VE_H264_SDROT_CTRL]);
}

#if 0
static void
dump_weights(void)
{
  

}
#endif

void
cedar_decode_write32(int off, int value)
{
  if(off < 4096)
    cedar_mirror[off] = value;

  switch(off) {
  case VE_H264_RAM_WRITE_PTR:
    assert((value & 3) == 0);
    sram_write_ptr = value;
    printf("   SRAM Write starts at 0x%x\n", value);
    return;

  case VE_H264_RAM_WRITE_DATA:
    assert(sram_write_ptr < 4096);
    //    printf("   SRAM(0x%04x) = 0x%08x\n", sram_write_ptr, value);
    cedar_sram_mirror[sram_write_ptr >> 2] = value;
    sram_write_ptr += 4;
    return;

  case VE_H264_OUTPUT_FRAME_IDX:
    output_frame_idx = value;
    return;

  case VE_H264_TRIGGER:


    h264_trig = value;

    switch(value & 0xf) {
    case 8:
      printf("Decode h264 start\n");
      dump_regs();
      if(0) dump_dpb();
      if(0) dump_refs();
      return;
    case 2:
    case 4:
    case 5:
      return;
    }
    break;
  }
  //  printf("Write32(0x%04x) = 0x%08x\n", off, value);
}


void
cedar_decode_read32(int off, int value)
{
  switch(off) {
  case VE_H264_STATUS:
    return;

  case VE_H264_BASIC_BITS:
    //    printf("Read basic bits: %08x\n", value);
    return;
  }

  //  printf(" Read32(0x%04x) = 0x%08x\n", off, value);
}
