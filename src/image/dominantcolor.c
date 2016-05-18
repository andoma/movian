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
#include <stdio.h>
#include "image.h"
#include "pixmap.h"


#define LOW_THRESHOLD  60
#define HIGH_THRESHOLD 220

typedef struct centroid {
  int r, g, b;
  int num_pixels;
  int or, og, ob;
} centroid_t;


static void
randomize_centroids(centroid_t *c, int K)
{
  for(int i = 0; i < K; i++) {
    c->r = rand() & 0xff;
    c->g = rand() & 0xff;
    c->b = rand() & 0xff;

    c->or = 0;
    c->og = 0;
    c->ob = 0;
    c++;
  }
}


typedef struct pixel {
  uint8_t r,g,b,c;
} pixel_t;


/**
 *
 */
static int
extract_pixels_bgr32(pixel_t *out, const pixmap_t *pm)
{
  int num = 0;

  const uint8_t *s = pm->pm_data;
  for(int y = 0; y < pm->pm_height; y++) {
    const uint32_t *src = (const uint32_t *)s;
    for(int x = 0; x < pm->pm_width; x++) {

      const uint32_t u32 = *src++;

      // Pixel values
      const int pR =  u32        & 0xff;
      const int pG = (u32 >> 8)  & 0xff;
      const int pB = (u32 >> 16) & 0xff;
      const int pA = (u32 >> 24) & 0xff;

      if(pA < 128)
        continue;

      if(pR > HIGH_THRESHOLD && pG > HIGH_THRESHOLD && pB > HIGH_THRESHOLD)
        continue;

      if(pR < LOW_THRESHOLD && pG < LOW_THRESHOLD && pB < LOW_THRESHOLD)
        continue;

      num++;
      if(out) {
        out->r = pR;
        out->g = pG;
        out->b = pB;
        out->c = -1;
        out++;
      }
    }
    s += pm->pm_linesize;
  }
  return num;
}


static int
extract_pixels_rgb24(pixel_t *out, const pixmap_t *pm)
{
  int num = 0;

  const uint8_t *s = pm->pm_data;
  for(int y = 0; y < pm->pm_height; y++) {
    const uint8_t *src = (const uint8_t *)s;
    for(int x = 0; x < pm->pm_width; x++) {

      const int pR = *src++;
      const int pG = *src++;
      const int pB = *src++;

      if(pR > HIGH_THRESHOLD && pG > HIGH_THRESHOLD && pB > HIGH_THRESHOLD)
        continue;

      if(pR < LOW_THRESHOLD && pG < LOW_THRESHOLD && pB < LOW_THRESHOLD)
        continue;

      num++;
      if(out) {
        out->r = pR;
        out->g = pG;
        out->b = pB;
        out->c = -1;
        out++;
      }
    }
    s += pm->pm_linesize;
  }
  return num;
}


/**
 *
 */
void
dominant_color(pixmap_t *pm)
{
  int K = 8;
  int num_pixels;
  pixel_t *pixels;
  centroid_t centroids[K];


  switch(pm->pm_type) {
  case PIXMAP_RGB24:
    num_pixels = extract_pixels_rgb24(NULL, pm);
    pixels = malloc(num_pixels * sizeof(pixel_t));
    extract_pixels_rgb24(pixels, pm);
    break;

  case PIXMAP_BGR32:
    num_pixels = extract_pixels_bgr32(NULL, pm);
    pixels = malloc(num_pixels * sizeof(pixel_t));
    extract_pixels_bgr32(pixels, pm);
    break;
  default:
    return;
  }
  randomize_centroids(centroids, K);

  while(1) {
    pixel_t *p = pixels;
    for(int i = 0; i < num_pixels; i++) {
      int s = INT32_MAX;

      for(int j = 0; j < K; j++) {
        const centroid_t *c = centroids + j;

        int d =
          ((c->r - (int)p->r) * (c->r - (int)p->r)) +
          ((c->g - (int)p->g) * (c->g - (int)p->g)) +
          ((c->b - (int)p->b) * (c->b - (int)p->b));

        if(d < s) {
          s = d;
          p->c = j;
        }
      }
      p++;
    }

    for(int j = 0; j < K; j++) {
      centroid_t *c = centroids + j;
      c->or = c->r;
      c->og = c->g;
      c->ob = c->b;
      c->r = 0;
      c->g = 0;
      c->b = 0;
      c->num_pixels = 0;
    }

    for(int i = 0; i < num_pixels; i++) {
      centroid_t *c = centroids + pixels[i].c;
      c->r += pixels[i].r;
      c->g += pixels[i].g;
      c->b += pixels[i].b;
      c->num_pixels++;
    }

    int move = 0;

    for(int j = 0; j < K; j++) {
      centroid_t *c = centroids + j;
      if(c->num_pixels) {
        c->r /= c->num_pixels;
        c->g /= c->num_pixels;
        c->b /= c->num_pixels;
      }

      if(c->r != c->or ||
         c->g != c->og ||
         c->b != c->ob) {
        move = 1;
      }
    }
    if(!move)
      break;
  }

  const centroid_t *best = NULL;
  for(int j = 0; j < K; j++) {
    const centroid_t *c = centroids + j;
    if(best == NULL || c->num_pixels > best->num_pixels)
      best = c;
  }
  if(best) {
    pm->pm_primary_color[0] = best->r / 255.0f;
    pm->pm_primary_color[1] = best->g / 255.0f;
    pm->pm_primary_color[2] = best->b / 255.0f;
  }
  free(pixels);
}
