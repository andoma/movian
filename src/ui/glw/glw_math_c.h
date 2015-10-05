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


typedef struct {
  Vec4 c[4];
} PMtx;

static __inline void
glw_pmtx_mul_prepare(PMtx *dst, const Mtx *src)
{
  // Transpose a matrix so it's faster to vectorize
  for(int i = 0; i < 4 ; i++) {
    dst->c[i][0] = src->r[0][i];
    dst->c[i][1] = src->r[1][i];
    dst->c[i][2] = src->r[2][i];
    dst->c[i][3] = src->r[3][i];
  }
}


static __inline void
glw_pmtx_mul_vec3(Vec3 dst, const PMtx *m, const Vec3 a)
{
  dst[0] =
    m->c[0][0] * a[0] + m->c[0][1] * a[1] + m->c[0][2] * a[2] + m->c[0][3];
  dst[1] =
    m->c[1][0] * a[0] + m->c[1][1] * a[1] + m->c[1][2] * a[2] + m->c[1][3];
  dst[2] =
    m->c[2][0] * a[0] + m->c[2][1] * a[1] + m->c[2][2] * a[2] + m->c[2][3];
}


static __inline void
glw_pmtx_mul_vec4_i(Vec4 dst, const PMtx *m, const Vec4 a)
{
  dst[0] =
    m->c[0][0] * a[0] + m->c[0][1] * a[1] + m->c[0][2] * a[2] + m->c[0][3];
  dst[1] =
    m->c[1][0] * a[0] + m->c[1][1] * a[1] + m->c[1][2] * a[2] + m->c[1][3];
  dst[2] =
    m->c[2][0] * a[0] + m->c[2][1] * a[1] + m->c[2][2] * a[2] + m->c[2][3];
  dst[3] = a[3];
}


static __inline void
glw_pmtx_mul_vec4(Vec4 dst, const PMtx *m, const Vec4 a)
{
  dst[0] =
    m->c[0][0] * a[0] + m->c[0][1] * a[1] + m->c[0][2] * a[2] + m->c[0][3] * a[3];
  dst[1] =
    m->c[1][0] * a[0] + m->c[1][1] * a[1] + m->c[1][2] * a[2] + m->c[1][3] * a[3];
  dst[2] =
    m->c[2][0] * a[0] + m->c[2][1] * a[1] + m->c[2][2] * a[2] + m->c[2][3] * a[3];
  dst[3] =
    m->c[3][0] * a[0] + m->c[3][1] * a[1] + m->c[3][2] * a[2] + m->c[3][3] * a[3];
}


static __inline float
glw_vec34_dot(const Vec3 A, const Vec4 B)
{
  return A[0] * B[0] + A[1] * B[1] + A[2] * B[2] + B[3];
}


static __inline void
glw_vec4_lerp(Vec4 dst, float s, const Vec4 a, const Vec4 b)
{
  dst[0] = a[0] + s * (b[0] - a[0]);
  dst[1] = a[1] + s * (b[1] - a[1]);
  dst[2] = a[2] + s * (b[2] - a[2]);
  dst[3] = a[3] + s * (b[3] - a[3]);
}

static __inline void
glw_vec4_store(float *p, const Vec4 v)
{
  p[0] = v[0];
  p[1] = v[1];
  p[2] = v[2];
  p[3] = v[3];
}

#define glw_vec4_get(p) (p)


#define glw_vec3_make(x,y,z) ((const float[3]){x,y,z})
#define glw_vec4_make(x,y,z,w) ((const float[4]){x,y,z,w})

    
static __inline void
glw_vec3_copy(Vec3 dst, const Vec3 src)
{
  dst[0] = src[0];
  dst[1] = src[1];
  dst[2] = src[2];
}

static __inline void
glw_vec4_copy(Vec4 dst, const Vec4 src)
{
  dst[0] = src[0];
  dst[1] = src[1];
  dst[2] = src[2];
  dst[3] = src[3];
}

static __inline void
glw_vec3_addmul(Vec3 dst, const Vec3 a, const Vec3 b, float s)
{
  dst[0] = a[0] + b[0] * s;
  dst[1] = a[1] + b[1] * s;
  dst[2] = a[2] + b[2] * s;
}

static __inline void
glw_vec3_sub(Vec3 dst, const Vec3 a, const Vec3 b)
{
  dst[0] = a[0] - b[0];
  dst[1] = a[1] - b[1];
  dst[2] = a[2] - b[2];
}

static __inline void
glw_vec3_cross(Vec3 dst, const Vec3 a, const Vec3 b)
{
  dst[0] = (a[1] * b[2]) - (a[2] * b[1]);
  dst[1] = (a[2] * b[0]) - (a[0] * b[2]);
  dst[2] = (a[0] * b[1]) - (a[1] * b[0]);
}

static __inline float
glw_vec3_dot(const Vec3 a, const Vec3 b)
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static __inline void
glw_mtx_trans_mul_vec4(Vec4 dst, const Mtx *m, const Vec4 v)
{
  dst[0] = m->r[0][0] * v[0] + m->r[0][1] * v[1] + m->r[0][2] * v[2] + m->r[0][3] * v[3];
  dst[1] = m->r[1][0] * v[0] + m->r[1][1] * v[1] + m->r[1][2] * v[2] + m->r[1][3] * v[3];
  dst[2] = m->r[2][0] * v[0] + m->r[2][1] * v[1] + m->r[2][2] * v[2] + m->r[2][3] * v[3];
  dst[3] = m->r[3][0] * v[0] + m->r[3][1] * v[1] + m->r[3][2] * v[2] + m->r[3][3] * v[3];
}

extern int glw_mtx_invert(Mtx *dst, const Mtx *src);

#define glw_vec2_extract(v, i) v[i]
#define glw_vec3_extract(v, i) v[i]
#define glw_vec4_extract(v, i) v[i]

#define glw_vec4_mul_c0(v, s) v[0] *= (s)
#define glw_vec4_mul_c1(v, s) v[1] *= (s)
#define glw_vec4_mul_c2(v, s) v[2] *= (s)
#define glw_vec4_mul_c3(v, s) v[3] *= (s)

#define glw_vec4_set(v, i, s) v[i] = (s)

#define glw_mtx_get(m) (const float *)(&(m))

static __inline void
glw_mtx_copy(Mtx *dst, const Mtx *src)
{
  memcpy(dst, src, sizeof(Mtx));
}

void glw_mtx_mul(Mtx *dst, const Mtx *a, const Mtx *b);





static __inline void
glw_Translatef(glw_rctx_t *rc, float x, float y, float z)
{
  Mtx *m = &rc->rc_mtx;

  m->r[3][0] += m->r[0][0]*x + m->r[1][0]*y + m->r[2][0]*z;
  m->r[3][1] += m->r[0][1]*x + m->r[1][1]*y + m->r[2][1]*z;
  m->r[3][2] += m->r[0][2]*x + m->r[1][2]*y + m->r[2][2]*z;
}


static __inline void
glw_Scalef(glw_rctx_t *rc, float x, float y, float z)
{
  Mtx *m = &rc->rc_mtx;

  m->r[0][0] *= x;
  m->r[1][0] *= y;
  m->r[2][0] *= z;
  m->r[0][1] *= x;
  m->r[1][1] *= y;
  m->r[2][1] *= z;
  m->r[0][2] *= x;
  m->r[1][2] *= y;
  m->r[2][2] *= z;
}

void glw_Rotatef(glw_rctx_t *rc, float a, float x, float y, float z);

void glw_LoadIdentity(glw_rctx_t *rc);

static __inline void
glw_LoadMatrixf(glw_rctx_t *rc, const float *src)
{
  memcpy(&rc->rc_mtx, src, sizeof(float) * 16);
}


static __inline void
glw_LerpMatrix(Mtx *out, float v, const Mtx *a, const Mtx *b)
{
  int i;
  for(i = 0; i < 4; i++)
    glw_vec4_lerp(out->r[i], v, a->r[i], b->r[i]);
}
