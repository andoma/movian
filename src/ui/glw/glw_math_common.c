/*
 *  GLW Math
 *  Copyright (C) 2008 Andreas Öman
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

#include "glw.h"
#include "glw_math.h"

static void
vec_addmul(float *dst, const float *a, const float *b, float s)
{
  dst[0] = a[0] + b[0] * s;
  dst[1] = a[1] + b[1] * s;
  dst[2] = a[2] + b[2] * s;
}

static void
vec_sub(float *dst, const float *a, const float *b)
{
  dst[0] = a[0] - b[0];
  dst[1] = a[1] - b[1];
  dst[2] = a[2] - b[2];
}


static void
vec_cross(float *dst, const float *a, const float *b)
{
  dst[0] = (a[1] * b[2]) - (a[2] * b[1]);
  dst[1] = (a[2] * b[0]) - (a[0] * b[2]);
  dst[2] = (a[0] * b[1]) - (a[1] * b[0]);
}

static float
vec_dot(const float *a, const float *b)
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}


void
glw_mtx_mul_vec(float *dst, const float *mt, float x, float y, float z)
{
  dst[0] = mt[0] * x + mt[4] * y + mt[ 8] * z + mt[12];
  dst[1] = mt[1] * x + mt[5] * y + mt[ 9] * z + mt[13];
  dst[2] = mt[2] * x + mt[6] * y + mt[10] * z + mt[14];
}

void
glw_mtx_trans_mul_vec4(float *dst, const float *mt,
		       float x, float y, float z, float w)
{
  dst[0] = mt[ 0] * x + mt[ 1] * y + mt[ 2] * z + mt[ 3] * w;
  dst[1] = mt[ 4] * x + mt[ 5] * y + mt[ 6] * z + mt[ 7] * w;
  dst[2] = mt[ 8] * x + mt[ 9] * y + mt[10] * z + mt[11] * w;
  dst[3] = mt[12] * x + mt[13] * y + mt[14] * z + mt[15] * w;
}


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

typedef float Vector[3];
/**
 * m   Model matrix
 * x   Return x in model space
 * y   Return y in model space
 * p   Mouse pointer at camera z plane
 * dir Mouse pointer direction vector
 */
int
glw_widget_unproject(Mtx m, float *xp, float *yp, 
		     const float *p, const float *dir)
{
  Vector u, v, n, w0, T0, T1, T2, out, I;
  float b, inv[16];

  glw_mtx_mul_vec(T0, m, -1, -1, 0);
  glw_mtx_mul_vec(T1, m,  1, -1, 0);
  glw_mtx_mul_vec(T2, m,  1,  1, 0);

  vec_sub(u, T1, T0);
  vec_sub(v, T2, T0);
  vec_cross(n, u, v);
  
  vec_sub(w0, p, T0);
  b = vec_dot(n, dir);
  if(fabs(b) < 0.000001)
    return 0;

  vec_addmul(I, p, dir, -vec_dot(n, w0) / b);

  if(!glw_mtx_invert(inv, m))
    return 0;
  glw_mtx_mul_vec(out, inv, I[0], I[1], I[2]);

  *xp = out[0];
  *yp = out[1];
  return 1;
}
