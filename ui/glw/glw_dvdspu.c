/*
 *  DVD SPU decoding for GL surfaces
 *  Copyright (C) 2007 Andreas Öman
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

#include <sys/time.h>
#include <time.h>
#include <hts.h>

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <inttypes.h>

#include <GL/gl.h>

#include "showtime.h"
#include "media.h"
#include "video_decoder.h"
#include "gl_dvdspu.h"
#include "apps/dvdplayer/dvd.h"

typedef struct gl_dvdspu_pic {

  TAILQ_ENTRY(gl_dvdspu_pic) gdp_link;

  uint8_t *gdp_bitmap;

  hts_rect_t gdp_cords;

  uint8_t gdp_palette[4];
  uint8_t gdp_alpha[4];

  unsigned int gdp_tex;

  uint8_t *gdp_spu;

  int gdp_cmdpos;
  int gdp_spusize;

  int64_t gdp_loadtime;

  int gdp_destroyme;

} gl_dvdspu_pic_t;


/*****************************************************************************
 *
 *
 *
 */

static inline uint16_t
getbe16(const uint8_t *p)
{
  return (p[0] << 8) | p[1];
}

static int
get_nibble(const uint8_t *buf, int nibble_offset)
{
  return (buf[nibble_offset >> 1] >> ((1 - (nibble_offset & 1)) << 2)) & 0xf;
}

static int
decode_rle(uint8_t *bitmap, int linesize, int w, int h, 
	   const uint8_t *buf, int nibble_offset, int buf_size)
{
  unsigned int v;
  int x, y, len, color, nibble_end;
  uint8_t *d;

  nibble_end = buf_size * 2;
  x = 0;
  y = 0;
  d = bitmap;
  for(;;) {
    if (nibble_offset >= nibble_end)
      return -1;
    v = get_nibble(buf, nibble_offset++);
    if (v < 0x4) {
      v = (v << 4) | get_nibble(buf, nibble_offset++);
      if (v < 0x10) {
	v = (v << 4) | get_nibble(buf, nibble_offset++);
	if (v < 0x040) {
	  v = (v << 4) | get_nibble(buf, nibble_offset++);
	  if (v < 4) {
	    v |= (w - x) << 2;
	  }
	}
      }
    }
    len = v >> 2;
    if (len > (w - x))
      len = (w - x);
    color = v & 0x03;

    memset(d + x, color, len);
    x += len;
    if (x >= w) {
      y++;
      if (y >= h)
	break;
      d += linesize;
      x = 0;
      /* byte align */
      nibble_offset += (nibble_offset & 1);
    }
  }
  return 0;
}

/****************************************************************************
 *
 *  Public functions
 *
 */

static int
gl_dvdspu_chew(gl_dvdspu_t *gl, gl_dvdspu_pic_t *gdp)
{
  int retval = 0;
  uint8_t *buf = gdp->gdp_spu;
  int date;
  int64_t now = wallclock;
  int64_t picts;
  int stop;
  int pos, cmd, x1, y1, x2, y2, offset1, offset2, next_cmd_pos;

  if(gdp->gdp_cmdpos == -1)
    return TAILQ_NEXT(gdp, gdp_link) != NULL ? -1 : 0;
  
  while(gdp->gdp_cmdpos + 4 < gdp->gdp_spusize) {
    date = getbe16(buf + gdp->gdp_cmdpos);
    picts = gdp->gdp_loadtime + ((date << 10) / 90) * 1000;

    if(now < picts) {
      return retval;
    }

    next_cmd_pos = getbe16(buf + gdp->gdp_cmdpos + 2);


    pos = gdp->gdp_cmdpos + 4;
    offset1 = -1;
    offset2 = -1;
    x1 = y1 = x2 = y2 = 0;
    stop = 0;

    while(!stop && pos < gdp->gdp_spusize) {
      cmd = buf[pos++];

      switch(cmd) {
      case 0x00:
	break;

      case 0x01:
	/* Start of picture */
	break;
      case 0x02:
	/* End of picture */
	return -1;

      case 0x03:
	/* set palette */
	if(gdp->gdp_spusize - pos < 2)
	  return -1;

	gdp->gdp_palette[3] = buf[pos] >> 4;
	gdp->gdp_palette[2] = buf[pos] & 0x0f;
	gdp->gdp_palette[1] = buf[pos + 1] >> 4;
	gdp->gdp_palette[0] = buf[pos + 1] & 0x0f;

	retval = 1;
	pos += 2;
	break;

      case 0x04:
	/* set alpha */
	if(gdp->gdp_spusize - pos < 2)
	  return -1;

	gdp->gdp_alpha[3] = buf[pos] >> 4;
	gdp->gdp_alpha[2] = buf[pos] & 0x0f;
	gdp->gdp_alpha[1] = buf[pos + 1] >> 4;
	gdp->gdp_alpha[0] = buf[pos + 1] & 0x0f;

	retval = 1;
	pos += 2;
	break;

      case 0x05:
	if(gdp->gdp_spusize - pos < 6)
	  return -1;

	x1 = (buf[pos] << 4) | (buf[pos + 1] >> 4);
	x2 = ((buf[pos + 1] & 0x0f) << 8) | buf[pos + 2];
	y1 = (buf[pos + 3] << 4) | (buf[pos + 4] >> 4);
	y2 = ((buf[pos + 4] & 0x0f) << 8) | buf[pos + 5];

	pos += 6;
	break;

      case 0x06:
	if(gdp->gdp_spusize - pos < 4)
	  return -1;

	offset1 = getbe16(buf + pos);
	offset2 = getbe16(buf + pos + 2);

	pos += 4;
	break;

      case 0x07:

	/* This is blindly reverse-engineered */

	gdp->gdp_alpha[3] = buf[pos + 10] >> 4;
	gdp->gdp_alpha[2] = buf[pos + 10] & 0x0f;
	gdp->gdp_alpha[1] = buf[pos + 11] >> 4;
	gdp->gdp_alpha[0] = buf[pos + 11] & 0x0f;

	retval = 1;
	stop = 1;
	break;
	      
      default:
	stop = 1;
	break;
      }
    }

    if(offset1 >= 0 && (x2 - x1 + 1) > 0 && (y2 - y1) > 0) {

      gdp->gdp_cords.w = x2 - x1 + 1;
      gdp->gdp_cords.h = y2 - y1;
      gdp->gdp_cords.x = x1;
      gdp->gdp_cords.y = y1;
      
      if(gdp->gdp_bitmap != NULL)
	free(gdp->gdp_bitmap);

      gdp->gdp_bitmap = malloc(gdp->gdp_cords.w * gdp->gdp_cords.h);
      
      decode_rle(gdp->gdp_bitmap, 
		 gdp->gdp_cords.w * 2,
		 gdp->gdp_cords.w, 
		 gdp->gdp_cords.h / 2 + (gdp->gdp_cords.h & 1),

		 buf, offset1 * 2, gdp->gdp_spusize);

      decode_rle(gdp->gdp_bitmap + gdp->gdp_cords.w, 
		 gdp->gdp_cords.w * 2, 
		 gdp->gdp_cords.w, 
		 gdp->gdp_cords.h / 2,

		 buf, offset2 * 2, gdp->gdp_spusize);
    }

    if(next_cmd_pos == gdp->gdp_cmdpos) {
      gdp->gdp_cmdpos = -1;
      break;
    }
    gdp->gdp_cmdpos = next_cmd_pos;
  }

  return retval;
}

/****************************************************************************
 *
 *  Public functions
 *
 */
static void
spu_repaint(dvd_player_t *dp, gl_dvdspu_t *gd, gl_dvdspu_pic_t *gdp)
{
  int dsize = gdp->gdp_cords.w * gdp->gdp_cords.h * 4;
  uint32_t *tmp, *t0; 
  int x, y, i;
  uint8_t *buf = gdp->gdp_bitmap;
  pci_t *pci = &dp->dp_pci;
  dvdnav_highlight_area_t ha;
  int hi_palette[4];
  int hi_alpha[4];

  if(gd->gd_clut == NULL)
    return;

  if(pci->hli.hl_gi.hli_ss &&
     dvdnav_get_highlight_area(pci, gd->gd_curbut, 0, &ha) 
     == DVDNAV_STATUS_OK) {

    hi_alpha[0] = (ha.palette >>  0) & 0xf;
    hi_alpha[1] = (ha.palette >>  4) & 0xf;
    hi_alpha[2] = (ha.palette >>  8) & 0xf;
    hi_alpha[3] = (ha.palette >> 12) & 0xf;
     
    hi_palette[0] = (ha.palette >> 16) & 0xf;
    hi_palette[1] = (ha.palette >> 20) & 0xf;
    hi_palette[2] = (ha.palette >> 24) & 0xf;
    hi_palette[3] = (ha.palette >> 28) & 0xf;

  }

  t0 = tmp = malloc(dsize);


  ha.sx -= gdp->gdp_cords.x;
  ha.ex -= gdp->gdp_cords.x;

  ha.sy -= gdp->gdp_cords.y;
  ha.ey -= gdp->gdp_cords.y;

  /* XXX: this can be optimized in many ways */

  for(y = 0; y < gdp->gdp_cords.h; y++) {
    for(x = 0; x < gdp->gdp_cords.w; x++) {
      i = buf[0];

      if(pci->hli.hl_gi.hli_ss &&
	 x >= ha.sx && y >= ha.sy && x <= ha.ex && y <= ha.ey) {

	if(hi_alpha[i] == 0) {
	  *tmp = 0;
	} else {
	  *tmp = gd->gd_clut[hi_palette[i] & 0xf] | 
	    ((hi_alpha[i] * 0x11) << 24);

	}
      } else {

	if(gdp->gdp_alpha[i] == 0) {
	  
	  /* If it's 100% transparent, write RGB as zero too, or weird
	     aliasing effect will occure when GL scales texture */
	  
	  *tmp = 0;
	} else {
	  *tmp = gd->gd_clut[gdp->gdp_palette[i] & 0xf] | 
	    ((gdp->gdp_alpha[i] * 0x11) << 24);
	}
      }

      buf++;
      tmp++;
    }
  }

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 
	       gdp->gdp_cords.w, gdp->gdp_cords.h, 0,
	       GL_RGBA, GL_UNSIGNED_BYTE, t0);

  free(t0);
}



static void
gl_dvdspu_destroy_pic(gl_dvdspu_t *gd, gl_dvdspu_pic_t *gdp)
{
  hts_mutex_lock(&gd->gd_mutex);
  TAILQ_REMOVE(&gd->gd_pics, gdp, gdp_link);
  hts_mutex_unlock(&gd->gd_mutex);

  if(gdp->gdp_tex != 0)
    glDeleteTextures(1, &gdp->gdp_tex);
  gdp->gdp_tex = 0;

  free(gdp->gdp_spu);
  free(gdp->gdp_bitmap);
  free(gdp);

}


static void
gl_dvdspu_new_clut(gl_dvdspu_t *gd, media_buf_t *mb)
{
  int i;
  int C, D, E, Y, U, V, R, G, B;
  uint32_t u32;

  if(gd->gd_clut != NULL)
    free(gd->gd_clut);

  gd->gd_clut = mb->mb_data;

  for(i = 0; i < 16; i++) {

    u32 = gd->gd_clut[i];

    Y = (u32 >> 16) & 0xff;
    V = (u32 >> 8) & 0xff;
    U = (u32 >> 0) & 0xff;

    C = Y - 16;
    D = U - 128;
    E = V - 128;

    R = ( 298 * C           + 409 * E + 128) >> 8;
    G = ( 298 * C - 100 * D - 208 * E + 128) >> 8;
    B = ( 298 * C + 516 * D           + 128) >> 8;

#define clip256(x) ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))

    R = clip256(R);
    G = clip256(G);
    B = clip256(B);

    u32 = R << 0 | G << 8 | B << 16;

    gd->gd_clut[i] = u32;
  }
  mb->mb_data = NULL;
}

static void
gl_dvdspu_load(gl_dvdspu_t *gd, media_buf_t *mb)
{
  gl_dvdspu_pic_t *gdp;

  if(mb->mb_size < 4)
    return;

  gdp = calloc(1, sizeof(gl_dvdspu_pic_t));

  gdp->gdp_spu = mb->mb_data;
  gdp->gdp_cmdpos = getbe16(gdp->gdp_spu + 2);
  gdp->gdp_spusize = mb->mb_size;
  gdp->gdp_loadtime = wallclock;

  mb->mb_data = NULL;

  hts_mutex_lock(&gd->gd_mutex);
  TAILQ_INSERT_TAIL(&gd->gd_pics, gdp, gdp_link);
  hts_mutex_unlock(&gd->gd_mutex);

}



/****************************************************************************
 *
 *  Public functions
 *
 */


struct gl_dvdspu *
gl_dvdspu_init(void)
{
  gl_dvdspu_t *gd = malloc(sizeof(gl_dvdspu_t));

  TAILQ_INIT(&gd->gd_pics);
  gd->gd_clut = NULL;
  hts_mutex_init(&gd->gd_mutex, NULL);

  return gd;
}


void
gl_dvdspu_deinit(gl_dvdspu_t *gd)
{
  gl_dvdspu_pic_t *gdp;
  
  hts_mutex_lock(&gd->gd_mutex);

  while((gdp = TAILQ_FIRST(&gd->gd_pics)) != NULL) {

    TAILQ_REMOVE(&gd->gd_pics, gdp, gdp_link);

    if(gdp->gdp_tex != 0)
      glDeleteTextures(1, &gdp->gdp_tex);

    free(gdp->gdp_spu);
    free(gdp->gdp_bitmap);
    free(gdp);
  }

  free(gd);
}





void
gl_dvdspu_flush(gl_dvdspu_t *gd)
{
  gl_dvdspu_pic_t *gdp;

  TAILQ_FOREACH(gdp, &gd->gd_pics, gdp_link) 
    gdp->gdp_destroyme = 1;
}



/*
 *
 */
void
gl_dvdspu_layout(struct dvd_player *dp, struct gl_dvdspu *gd)
{
  gl_dvdspu_pic_t *gdp;
  int x;

 again:
  gdp = TAILQ_FIRST(&gd->gd_pics);

  if(gdp == NULL)
    return;

  if(gdp->gdp_destroyme == 1)
    goto destroy;

  glActiveTextureARB(GL_TEXTURE0_ARB);

  x = gl_dvdspu_chew(gd, gdp);

  switch(x) {
  case -1:
  destroy:
    gl_dvdspu_destroy_pic(gd, gdp);
    goto again;

  case 0:
    if(gd->gd_repaint == 0) {
      glBindTexture(GL_TEXTURE_2D, gdp->gdp_tex);
      break;
    }

    gd->gd_repaint = 0;
    /* FALLTHRU */

  case 1:
    if(gdp->gdp_tex == 0) {
      glGenTextures(1, &gdp->gdp_tex);

      glBindTexture(GL_TEXTURE_2D, gdp->gdp_tex);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    } else {
      glBindTexture(GL_TEXTURE_2D, gdp->gdp_tex);
    }
    spu_repaint(dp, gd, gdp);
    break;
  }
}


/*
 *
 */
void
gl_dvdspu_render(struct gl_dvdspu *gd, float xsize, float ysize, float alpha)
{
  gl_dvdspu_pic_t *gdp;

  gdp = TAILQ_FIRST(&gd->gd_pics);

  if(gdp == NULL || gdp->gdp_tex == 0)
    return;

  glActiveTextureARB(GL_TEXTURE0_ARB);

  glBindTexture(GL_TEXTURE_2D, gdp->gdp_tex);

  glEnable(GL_TEXTURE_2D);

  glPushMatrix();

  glScalef(2.0f / xsize, -2.0f / ysize, 0.0f);
  glTranslatef(-xsize / 2, -ysize / 2, 0.0f);

  glColor4f(1.0, 1.0, 1.0, alpha);

  glBegin(GL_QUADS);

  glTexCoord2f(0, 0.0);
  glVertex3f(gdp->gdp_cords.x, gdp->gdp_cords.y, 0.0f);
    
  glTexCoord2f(1.0, 0.0);
  glVertex3f(gdp->gdp_cords.x + gdp->gdp_cords.w, gdp->gdp_cords.y, 0.0f);
    
  glTexCoord2f(1.0, 1.0);
  glVertex3f(gdp->gdp_cords.x + gdp->gdp_cords.w,
	     gdp->gdp_cords.y + gdp->gdp_cords.h,
	     0.0f);
    
  glTexCoord2f(0, 1.0);
  glVertex3f(gdp->gdp_cords.x, gdp->gdp_cords.y + gdp->gdp_cords.h, 0.0f);

  glEnd();

  glPopMatrix();

  glDisable(GL_TEXTURE_2D);
}







void
gl_dvdspu_dispatch(dvd_player_t *dp, gl_dvdspu_t *gd, media_buf_t *mb)
{
  switch(mb->mb_data_type) {
  case MB_DVD_SPU:
    gl_dvdspu_load(gd, mb);
    break;
    
  case MB_CLUT:
    gl_dvdspu_new_clut(gd, mb);
    break;
    
  case MB_DVD_PCI:
    memcpy(&dp->dp_pci, mb->mb_data, sizeof(pci_t));
    gd->gd_repaint = 1;
    break;
      
  case MB_DVD_HILITE:
    gd->gd_curbut = mb->mb_data32;
    gd->gd_repaint = 1;
    break;

  default:
    break;
  }
}
