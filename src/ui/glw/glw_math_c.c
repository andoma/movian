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
#include "glw.h"


int
glw_mtx_invert(float *dst, const float *src)
{
  float det = 
    src[0]*src[5]*src[10] + 
    src[4]*src[9]*src[2] + 
    src[8]*src[1]*src[6] -
    src[2]*src[5]*src[8] - 
    src[1]*src[4]*src[10] - 
    src[0]*src[6]*src[9];

  if(det == 0)
    return 0;

  det = 1.0f / det;

  dst[0]  =  (src[5]*src[10] - src[6]*src[9]) * det;
  dst[4]  = -(src[4]*src[10] - src[6]*src[8]) * det;
  dst[8]  =  (src[4]*src[9]  - src[5]*src[8]) * det;

  dst[1]  = -(src[1]*src[10] - src[2]*src[9]) * det;
  dst[5]  =  (src[0]*src[10] - src[2]*src[8]) * det;
  dst[9]  = -(src[0]*src[9]  - src[1]*src[8]) * det;

  dst[2]  =  (src[1]*src[6]  - src[2]*src[5]) * det;
  dst[6]  = -(src[0]*src[6]  - src[2]*src[4]) * det;
  dst[10] =  (src[0]*src[5]  - src[1]*src[4]) * det;

  dst[12] = -dst[0]*src[12] - dst[4]*src[13] - dst[8]*src[14];
  dst[13] = -dst[1]*src[12] - dst[5]*src[13] - dst[9]*src[14];
  dst[14] = -dst[2]*src[12] - dst[6]*src[13] - dst[10]*src[14];

  dst[3]  = 0;
  dst[7]  = 0;
  dst[11] = 0;
  dst[15] = 1;
  return 1;
}


/**
 *
 */
void
glw_Rotatef(glw_rctx_t *rc, float a, float x, float y, float z)
{
  float s = sinf(GLW_DEG2RAD(a));
  float c = cosf(GLW_DEG2RAD(a));
  float t = 1.0 - c;
  float n = 1 / sqrtf(x*x + y*y + z*z);
  float m[16];
  float *o = rc->rc_mtx;
  float p[16];

  x *= n;
  y *= n;
  z *= n;
  
  m[ 0] = t * x * x + c;
  m[ 4] = t * x * y - s * z;
  m[ 8] = t * x * z + s * y;
  m[12] = 0;

  m[ 1] = t * y * x + s * z;
  m[ 5] = t * y * y + c;
  m[ 9] = t * y * z - s * x;
  m[13] = 0;

  m[ 2] = t * z * x - s * y;
  m[ 6] = t * z * y + s * x;
  m[10] = t * z * z + c;
  m[14] = 0;

  p[0]  = o[0]*m[0]  + o[4]*m[1]  + o[8]*m[2];
  p[4]  = o[0]*m[4]  + o[4]*m[5]  + o[8]*m[6];
  p[8]  = o[0]*m[8]  + o[4]*m[9]  + o[8]*m[10];
  p[12] = o[0]*m[12] + o[4]*m[13] + o[8]*m[14] + o[12];
 
  p[1]  = o[1]*m[0]  + o[5]*m[1]  + o[9]*m[2];
  p[5]  = o[1]*m[4]  + o[5]*m[5]  + o[9]*m[6];
  p[9]  = o[1]*m[8]  + o[5]*m[9]  + o[9]*m[10];
  p[13] = o[1]*m[12] + o[5]*m[13] + o[9]*m[14] + o[13];
  
  p[2]  = o[2]*m[0]  + o[6]*m[1]  + o[10]*m[2];
  p[6]  = o[2]*m[4]  + o[6]*m[5]  + o[10]*m[6];
  p[10] = o[2]*m[8]  + o[6]*m[9]  + o[10]*m[10];
  p[14] = o[2]*m[12] + o[6]*m[13] + o[10]*m[14] + o[14];

  p[ 3] = 0;
  p[ 7] = 0;
  p[11] = 0;
  p[15] = 1;

  memcpy(o, p, sizeof(float) * 16);
}


/**
 *
 */
void
glw_LoadIdentity(glw_rctx_t *rc)
{
  memset(&rc->rc_mtx, 0, sizeof(Mtx));
  
  rc->rc_mtx[0]  = 1;
  rc->rc_mtx[5]  = 1;
  rc->rc_mtx[10] = 1;
  rc->rc_mtx[15] = 1;
}


void
glw_mtx_mul(Mtx dst, Mtx a, Mtx b)
{
  dst[0] = a[0] * b[0] + a[4] * b[1] + a[8]  * b[2] + a[12] * b[3];
  dst[1] = a[1] * b[0] + a[5] * b[1] + a[9]  * b[2] + a[13] * b[3];
  dst[2] = a[2] * b[0] + a[6] * b[1] + a[10] * b[2] + a[14] * b[3];
  dst[3] = a[3] * b[0] + a[7] * b[1] + a[11] * b[2] + a[15] * b[3];

  dst[4] = a[0] * b[4] + a[4] * b[5] + a[8]  * b[6] + a[12] * b[7];
  dst[5] = a[1] * b[4] + a[5] * b[5] + a[9]  * b[6] + a[13] * b[7];
  dst[6] = a[2] * b[4] + a[6] * b[5] + a[10] * b[6] + a[14] * b[7];
  dst[7] = a[3] * b[4] + a[7] * b[5] + a[11] * b[6] + a[15] * b[7];

  dst[8]  = a[0] * b[8] + a[4] * b[9] + a[8]  * b[10] + a[12] * b[11];
  dst[9]  = a[1] * b[8] + a[5] * b[9] + a[9]  * b[10] + a[13] * b[11];
  dst[10] = a[2] * b[8] + a[6] * b[9] + a[10] * b[10] + a[14] * b[11];
  dst[11] = a[3] * b[8] + a[7] * b[9] + a[11] * b[10] + a[15] * b[11];

  dst[12] = a[0] * b[12] + a[4] * b[13] + a[8]  * b[14] + a[12] * b[15];
  dst[13] = a[1] * b[12] + a[5] * b[13] + a[9]  * b[14] + a[13] * b[15];
  dst[14] = a[2] * b[12] + a[6] * b[13] + a[10] * b[14] + a[14] * b[15];
  dst[15] = a[3] * b[12] + a[7] * b[13] + a[11] * b[14] + a[15] * b[15];
}

