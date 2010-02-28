/*
 * High quality image resampling with polyphase filters
 * Copyright (c) 2001 Fabrice Bellard.
 * Copyright (c) 2008 Andreas Öman.
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <libavcodec/avcodec.h>

#include "glw_scaler.h"

#define PHASE_BITS 4
#define NB_PHASES  (1 << PHASE_BITS)
#define NB_TAPS    4
#define FCENTER    1  /* index of the center of the filter */

#define POS_FRAC_BITS 16
#define POS_FRAC      (1 << POS_FRAC_BITS)
/* 6 bits precision is needed for MMX */
#define FILTER_BITS   8

#define LINE_BUF_HEIGHT (NB_TAPS * 4)

#define FELEM int16_t


/**
 * 0th order modified bessel function of the first kind.
 */
static double bessel(double x){
    double v=1;
    double lastv=0;
    double t=1;
    int i;

    x= x*x/4;
    for(i=1; v != lastv; i++){
        lastv=v;
        t *= x/(i*i);
        v += t;
    }
    return v;
}

#define FELEM_MAX INT32_MAX
#define FELEM_MIN INT32_MIN

/**
 * builds a polyphase filterbank.
 * @param factor resampling factor
 * @param scale wanted sum of coefficients for each filter
 * @param type 0->cubic, 1->blackman nuttall windowed sinc, 2..16->kaiser windowed sinc beta=2..16
 */
static void build_filter(FELEM *filter, double factor, int tap_count, int phase_count, int scale, int type){
    int ph, i;
    double x, y, w, tab[tap_count];
    const int center= (tap_count-1)/2;

    /* if upsampling, only need to interpolate, no filter */
    if (factor > 1.0)
        factor = 1.0;

    for(ph=0;ph<phase_count;ph++) {
        double norm = 0;
        for(i=0;i<tap_count;i++) {
            x = M_PI * ((double)(i - center) - (double)ph / phase_count) * factor;
            if (x == 0) y = 1.0;
            else        y = sin(x) / x;
            switch(type){
            case 0:{
                const float d= -0.5; //first order derivative = -0.5
                x = fabs(((double)(i - center) - (double)ph / phase_count) * factor);
                if(x<1.0) y= 1 - 3*x*x + 2*x*x*x + d*(            -x*x + x*x*x);
                else      y=                       d*(-4 + 8*x - 5*x*x + x*x*x);
                break;}
            case 1:
                w = 2.0*x / (factor*tap_count) + M_PI;
                y *= 0.3635819 - 0.4891775 * cos(w) + 0.1365995 * cos(2*w) - 0.0106411 * cos(3*w);
                break;
            default:
                w = 2.0*x / (factor*tap_count*M_PI);
                y *= bessel(type*sqrt(FFMAX(1-w*w, 0)));
                break;
            }

            tab[i] = y;
            norm += y;
        }

        /* normalize so that an uniform color remains the same */
        for(i=0;i<tap_count;i++) {
#ifdef CONFIG_RESAMPLE_AUDIOPHILE_KIDDY_MODE
            filter[ph * tap_count + i] = tab[i] / norm;
#else
            filter[ph * tap_count + i] = av_clip(lrintf(tab[i] * scale / norm), FELEM_MIN, FELEM_MAX);
#endif
        }
    }
#if 0
    {
#define LEN 1024
        int j,k;
        double sine[LEN + tap_count];
        double filtered[LEN];
        double maxff=-2, minff=2, maxsf=-2, minsf=2;
        for(i=0; i<LEN; i++){
            double ss=0, sf=0, ff=0;
            for(j=0; j<LEN+tap_count; j++)
                sine[j]= cos(i*j*M_PI/LEN);
            for(j=0; j<LEN; j++){
                double sum=0;
                ph=0;
                for(k=0; k<tap_count; k++)
                    sum += filter[ph * tap_count + k] * sine[k+j];
                filtered[j]= sum / (1<<FILTER_SHIFT);
                ss+= sine[j + center] * sine[j + center];
                ff+= filtered[j] * filtered[j];
                sf+= sine[j + center] * filtered[j];
            }
            ss= sqrt(2*ss/LEN);
            ff= sqrt(2*ff/LEN);
            sf= 2*sf/LEN;
            maxff= FFMAX(maxff, ff);
            minff= FFMIN(minff, ff);
            maxsf= FFMAX(maxsf, sf);
            minsf= FFMIN(minsf, sf);
            if(i%11==0){
                av_log(NULL, AV_LOG_ERROR, "i:%4d ss:%f ff:%13.6e-%13.6e sf:%13.6e-%13.6e\n", i, ss, maxff, minff, maxsf, minsf);
                minff=minsf= 2;
                maxff=maxsf= -2;
            }
        }
    }
#endif
}

static inline int get_phase(int pos)
{
    return ((pos) >> (POS_FRAC_BITS - PHASE_BITS)) & ((1 << PHASE_BITS) - 1);
}


typedef struct {
  int h_incr;
  int v_incr;

  int16_t h_filters[NB_PHASES][NB_TAPS];
  int16_t v_filters[NB_PHASES][NB_TAPS];
  uint8_t *line_buf;

} rescale_context_t;


/* This function must be optimized */
static void __attribute__((always_inline)) inline 
h_resample_fast2(uint8_t *dst, int dst_width, const uint8_t *src,
		 int src_width, int src_start, int src_incr,
		 int16_t *filters, const int bpp)
{
  int src_pos, phase, sum, i, c;
  const uint8_t *s;
  int16_t *filter;

  src_pos = src_start;
  for(i=0;i<dst_width;i++) {
    s = src + (src_pos >> POS_FRAC_BITS) * bpp;
    phase = get_phase(src_pos);
    filter = filters + phase * NB_TAPS;

    for(c = 0; c < bpp; c++) {

      sum = 
	s[0 * bpp] * filter[0] +
	s[1 * bpp] * filter[1] +
	s[2 * bpp] * filter[2] +
	s[3 * bpp] * filter[3];
      s++;

      sum = sum >> FILTER_BITS;
      if (sum < 0)
	sum = 0;
      else if (sum > 255)
	sum = 255;

      dst[0] = sum;
      dst++;
    }
    src_pos += src_incr;
  }
}

static void
h_resample_fast(uint8_t *dst, int dst_width, const uint8_t *src,
		 int src_width, int src_start, int src_incr,
		int16_t *filters, int bpp)
{
  if(bpp == 4) {
    h_resample_fast2(dst, dst_width, src, src_width, src_start, src_incr,
		     filters, 4);
  } else if(bpp == 3) {
    h_resample_fast2(dst, dst_width, src, src_width, src_start, src_incr,
		     filters, 3);
  } else {
    h_resample_fast2(dst, dst_width, src, src_width, src_start, src_incr,
		     filters, bpp);
  }
}



/* This function must be optimized */
static void
v_resample(uint8_t *dst, int dst_width, const uint8_t *src,
	   int wrap, int16_t *filter)
{
    int sum, i;
    const uint8_t *s;

    s = src;
    for(i=0;i<dst_width;i++) {
      sum =
	s[0 * wrap] * filter[0] +
	s[1 * wrap] * filter[1] +
	s[2 * wrap] * filter[2] +
	s[3 * wrap] * filter[3];
      
        sum = sum >> FILTER_BITS;
        if (sum < 0)
            sum = 0;
        else if (sum > 255)
            sum = 255;
        dst[0] = sum;
        dst++;
        s++;
    }
}


/* slow version to handle limit cases. Does not need optimization */
static void
h_resample_slow(uint8_t *dst, int dst_width,
		const uint8_t *src, int src_width,
		int src_start, int src_incr, int16_t *filters,
		int bpp)
{
  int src_pos, phase, sum, j, v, i, c;
  const uint8_t *s, *src_end;
  int16_t *filter;

  src_end = src + src_width * bpp;
  src_pos = src_start;
  for(i=0;i<dst_width;i++) {
    phase = get_phase(src_pos);
    filter = filters + phase * NB_TAPS;
    for(c = 0; c < bpp; c++) {
      sum = 0;
      s = src + (src_pos >> POS_FRAC_BITS) * bpp + c;

      for(j=0;j<NB_TAPS;j++) {
	if (s <= src + c)
	  v = src[c];
	else if (s >= src_end)
	  v = src_end[-c-bpp];
	else
	  v = s[0];
	sum += v * filter[j];
	s += bpp;
      }
      sum = sum >> FILTER_BITS;
      if (sum < 0)
	sum = 0;
      else if (sum > 255)
	sum = 255;
      dst[0] = sum;
      dst++;
    }
    src_pos += src_incr;
  }
}




/**
 *
 */
static void
h_resample(uint8_t *dst, int dst_width, const uint8_t *src,
	   int src_width, int src_start, int src_incr,
	   int16_t *filters, int bpp)
{
  int n, src_end;

  if (src_start < 0) {
    n = (0 - src_start + src_incr - 1) / src_incr;
    h_resample_slow(dst, n, src, src_width, src_start, src_incr, filters,
		    bpp);
    dst += n * bpp;
    dst_width -= n;
    src_start += n * src_incr;
  }
  src_end = src_start + dst_width * src_incr;
  if (src_end > ((src_width - NB_TAPS) << POS_FRAC_BITS)) {
    n = (((src_width - NB_TAPS + 1) << POS_FRAC_BITS) - 1 - src_start) /
      src_incr;
  } else {
    n = dst_width;
  }
  h_resample_fast(dst, n, src, src_width, src_start, src_incr, filters, bpp);

  if (n < dst_width) {
    dst += n * bpp;
    dst_width -= n;
    src_start += n * src_incr;
    h_resample_slow(dst, dst_width,
		    src, src_width, src_start, src_incr, filters, bpp);
  }
}




/**
 *
 */
static void
component_resample(rescale_context_t *s, int bpp,
		   uint8_t *output, int owrap, int owidth, int oheight,
		   uint8_t *input,  int iwrap, int iwidth, int iheight)
{
  int src_y, src_y1, last_src_y, ring_y, phase_y, y1, y;
  uint8_t *new_line, *src_line;

  last_src_y = - FCENTER - 1;
  /* position of the bottom of the filter in the source image */
  src_y = (last_src_y + NB_TAPS) * POS_FRAC;
  ring_y = NB_TAPS; /* position in ring buffer */
  for(y=0;y<oheight;y++) {
    /* apply horizontal filter on new lines from input if needed */
    src_y1 = src_y >> POS_FRAC_BITS;

    while (last_src_y < src_y1) {
      if (++ring_y >= LINE_BUF_HEIGHT + NB_TAPS)
	ring_y = NB_TAPS;
      last_src_y++;
      /* handle limit conditions : replicate line (slightly
	 inefficient because we filter multiple times) */
      y1 = last_src_y;
      if (y1 < 0) {
	y1 = 0;
      } else if (y1 >= iheight) {
	y1 = iheight - 1;
      }
      src_line = input + y1 * iwrap;
      new_line = s->line_buf + ring_y * owidth * bpp;
      /* apply filter and handle limit cases correctly */
      h_resample(new_line, owidth,
		 src_line, iwidth, - FCENTER * POS_FRAC, s->h_incr,
		 &s->h_filters[0][0], bpp);
      /* handle ring buffer wrapping */
      if (ring_y >= LINE_BUF_HEIGHT) {
	memcpy(s->line_buf + (ring_y - LINE_BUF_HEIGHT) * owidth * bpp,
	       new_line, owidth * bpp);
      }
    }
    /* apply vertical filter */
    phase_y = get_phase(src_y);
    v_resample(output, owidth * bpp,
	       s->line_buf + (ring_y - NB_TAPS + 1) * owidth * bpp, 
	       owidth * bpp, &s->v_filters[phase_y][0]);

    src_y += s->v_incr;

    output += owrap;
  }
}


/**
 *
 */
void
glw_bitmap_rescale(uint8_t *src, int src_width, int src_height, int src_stride,
		   uint8_t *dst, int dst_width, int dst_height, int dst_stride,
		   int bpp)
{
  rescale_context_t *rc;

  rc = calloc(1, sizeof(rescale_context_t));

  rc->line_buf = calloc(1, dst_width * bpp * (LINE_BUF_HEIGHT + NB_TAPS));
  
  build_filter(&rc->h_filters[0][0], 
	       (double)dst_width / (double)src_width,
	       NB_TAPS, NB_PHASES, 1 << FILTER_BITS, 0);
  
  build_filter(&rc->v_filters[0][0], 
	       (double)dst_height / (double)src_height,
	       NB_TAPS, NB_PHASES, 1 << FILTER_BITS, 0);

  rc->h_incr = (src_width  * POS_FRAC) / dst_width;
  rc->v_incr = (src_height * POS_FRAC) / dst_height;

  component_resample(rc, bpp,
		     dst, dst_stride, dst_width, dst_height,
		     src, src_stride, src_width, src_height);

  free(rc->line_buf);
  free(rc);
}
