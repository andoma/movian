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
static __inline void
glw_Translatef(glw_rctx_t *rc, float x, float y, float z)
{
  __m128 vec = (__m128){x, y, z, 0};

  __m128 X =  _mm_shuffle_ps(vec, vec, _MM_SHUFFLE(3,0,0,0));
  __m128 Y =  _mm_shuffle_ps(vec, vec, _MM_SHUFFLE(3,1,1,1));
  __m128 Z =  _mm_shuffle_ps(vec, vec, _MM_SHUFFLE(3,2,2,2));

  __m128 a = _mm_mul_ps(rc->rc_mtx[0], X);
  __m128 b = _mm_mul_ps(rc->rc_mtx[1], Y);
  __m128 c = _mm_mul_ps(rc->rc_mtx[2], Z);
  
  rc->rc_mtx[3] = _mm_add_ps(_mm_add_ps(a, b), _mm_add_ps(c, rc->rc_mtx[3]));
}



static __inline void
glw_Scalef(glw_rctx_t *rc, float x, float y, float z)
{
  __m128 vec = (__m128){x, y, z, 0};

  __m128 X =  _mm_shuffle_ps(vec, vec, _MM_SHUFFLE(3,0,0,0));
  __m128 Y =  _mm_shuffle_ps(vec, vec, _MM_SHUFFLE(3,1,1,1));
  __m128 Z =  _mm_shuffle_ps(vec, vec, _MM_SHUFFLE(3,2,2,2));
  rc->rc_mtx[0] = _mm_mul_ps(rc->rc_mtx[0], X);
  rc->rc_mtx[1] = _mm_mul_ps(rc->rc_mtx[1], Y);
  rc->rc_mtx[2] = _mm_mul_ps(rc->rc_mtx[2], Z);
}

void glw_Rotatef(glw_rctx_t *rc, float a, float x, float y, float z);
void glw_LoadIdentity(glw_rctx_t *rc);

static __inline void 
glw_LoadMatrixf(glw_rctx_t *rc, float *src)
{
  memcpy(rc->rc_mtx, src, sizeof(float) * 16);
}


static __inline void
glw_LerpMatrix(Mtx out, float v, const Mtx a, const Mtx b)
{
  __m128 vv = _mm_set1_ps(v);
  int i;

  for(i = 0; i < 4; i++)
    out[i] = _mm_add_ps(a[i], _mm_mul_ps(vv, _mm_sub_ps(b[i], a[i])));
}


static __inline void
glw_vec4_store(float *p, const Vec4 v)
{
  _mm_storeu_ps(p, v);
}


typedef Mtx PMtx;

#define glw_pmtx_mul_prepare(dst, src) do {				\
    __v4sf __r0 = (src)[0], __r1 = (src)[1], __r2 = (src)[2], __r3 = (src)[3]; \
    __v4sf __t0 = __builtin_ia32_unpcklps (__r0, __r1);			\
    __v4sf __t1 = __builtin_ia32_unpcklps (__r2, __r3);			\
    __v4sf __t2 = __builtin_ia32_unpckhps (__r0, __r1);			\
    __v4sf __t3 = __builtin_ia32_unpckhps (__r2, __r3);			\
    (dst)[0] = __builtin_ia32_movlhps (__t0, __t1);			\
    (dst)[1] = __builtin_ia32_movhlps (__t1, __t0);			\
    (dst)[2] = __builtin_ia32_movlhps (__t2, __t3);			\
    (dst)[3] = __builtin_ia32_movhlps (__t3, __t2);			\
  } while (0)


#define glw_pmtx_mul_vec4(dst, pmt, v) do {	\
    __v4sf a0 = _mm_mul_ps(pmt[0], v);		\
    __v4sf a1 = _mm_mul_ps(pmt[1], v);		\
    __v4sf a2 = _mm_mul_ps(pmt[2], v);		\
    __v4sf a3 = _mm_mul_ps(pmt[3], v);		\
    _MM_TRANSPOSE4_PS(a0, a1, a2, a3);			    \
    dst = _mm_add_ps(_mm_add_ps(a0, a1), _mm_add_ps(a2, a3));	\
  } while(0)

#define glw_pmtx_mul_vec4_i glw_pmtx_mul_vec4

#define glw_pmtx_mul_vec3(dst, pmt, V) do {			\
    __v4sf v = _mm_set_ps(1,					\
			  __builtin_ia32_vec_ext_v4sf(V, 2),	\
			  __builtin_ia32_vec_ext_v4sf(V, 1),	\
			  __builtin_ia32_vec_ext_v4sf(V, 0));	\
    __v4sf a0 = _mm_mul_ps(pmt[0], v);				\
    __v4sf a1 = _mm_mul_ps(pmt[1], v);				\
    __v4sf a2 = _mm_mul_ps(pmt[2], v);				\
    __v4sf a3 = _mm_mul_ps(pmt[3], v);				\
    _MM_TRANSPOSE4_PS(a0, a1, a2, a3);				\
    dst = _mm_add_ps(_mm_add_ps(a0, a1), _mm_add_ps(a2, a3));	\
  } while(0)


#define glw_vec3_make(x,y,z) _mm_set_ps(0, z, y, x)
#define glw_vec4_make(x,y,z,w) _mm_set_ps(w, z, y, x)
#define glw_vec4_make1(w) _mm_set_ss(w)

#define glw_vec3_copy(dst, src) (dst) = (src)
#define glw_vec4_copy(dst, src) (dst) = (src)

#define glw_vec3_addmul(dst, a, b, s) do { \
    dst = _mm_add_ps((a), _mm_mul_ps((b), _mm_set1_ps(s))); } while(0)

#define glw_vec3_sub(dst, a, b) do { \
    dst = _mm_sub_ps((a), (b)); } while(0)

#define glw_vec3_cross(dst, A, B) do {		\
    const float *a = (const float *)&A;		\
    const float *b = (const float *)&B;		\
    dst = (__m128){				\
      (a[1] * b[2]) - (a[2] * b[1]),		\
      (a[2] * b[0]) - (a[0] * b[2]),		\
      (a[0] * b[1]) - (a[1] * b[0]), 0};	\
  } while(0)


static __inline float glw_vec3_dot(const Vec3 a, const Vec3 b)
{
  __v4sf n = _mm_mul_ps(a,b);
  return 
    __builtin_ia32_vec_ext_v4sf(n, 0) + 
    __builtin_ia32_vec_ext_v4sf(n, 1) + 
    __builtin_ia32_vec_ext_v4sf(n, 2);
}

static __inline float glw_vec34_dot(const Vec3 a, const Vec4 b)
{
  __v4sf n = _mm_mul_ps(a,b);
  return 
    __builtin_ia32_vec_ext_v4sf(n, 0) + 
    __builtin_ia32_vec_ext_v4sf(n, 1) + 
    __builtin_ia32_vec_ext_v4sf(n, 2) + 
    __builtin_ia32_vec_ext_v4sf(b, 3);
}

static __inline void
glw_mtx_copy(Mtx dst, const Mtx src)
{
  dst[0] = src[0];
  dst[1] = src[1];
  dst[2] = src[2];
  dst[3] = src[3];
}

extern int glw_mtx_invert(Mtx dst, const Mtx src);

#define glw_vec3_extract(a, pos) __builtin_ia32_vec_ext_v4sf(a, pos)

#define glw_vec4_extract(a, pos) __builtin_ia32_vec_ext_v4sf(a, pos)

#define glw_vec4_mul_c3(a, v) do { \
    a = _mm_mul_ps(a, _mm_set_ps(v, 1, 1, 1));	\
  } while(0)


const static __inline float *
glw_mtx_get(const Mtx src)
{
  return (const float *)&src[0];
}

static __inline Vec2 glw_vec4_get(const float *p)
{
  return _mm_loadu_ps(p);
}

#define glw_vec4_lerp(dst, s, a, b) do { \
    dst = _mm_add_ps((a), _mm_mul_ps(_mm_set1_ps(s), _mm_sub_ps(b, a))); } \
  while(0)


#define glw_mtx_trans_mul_vec4(dst, mt, v) do { \
  __v4sf a0 = _mm_mul_ps(mt[0], v); \
  __v4sf a1 = _mm_mul_ps(mt[1], v); \
  __v4sf a2 = _mm_mul_ps(mt[2], v); \
  __v4sf a3 = _mm_mul_ps(mt[3], v); \
  _MM_TRANSPOSE4_PS(a0, a1, a2, a3); \
  dst = _mm_add_ps(_mm_add_ps(a0, a1), _mm_add_ps(a2, a3)); \
  } while(0)



#define X_DX (0)
#define X_DY (1)
#define X_DZ (2)
#define X_DW (3)
#define Y_DX (0 << 2)
#define Y_DY (1 << 2)
#define Y_DZ (2 << 2)
#define Y_DW (3 << 2)
#define Z_SX (0 << 4)
#define Z_SY (1 << 4)
#define Z_SZ (2 << 4)
#define Z_SW (3 << 4)
#define W_SX (0 << 6)
#define W_SY (1 << 6)
#define W_SZ (2 << 6)
#define W_SW (3 << 6)


#define glw_vec4_set(v, i, s) do {			\
	__v4sf tmp = _mm_set_ss(s);			\
	switch(i) {					\
	case 0:						       \
	    v = _mm_move_ss(v, tmp);			       \
	    break;					       \
        case 1:								\
	    tmp = _mm_unpacklo_ps(v, tmp);				\
	    v = _mm_shuffle_ps(tmp, v, X_DX | Y_DY | Z_SZ | W_SW);	\
	    break;							\
        case 2:								\
	    v = _mm_shuffle_ps(v, v, X_DZ | Y_DY | Z_SX | W_SW );	\
	    tmp = _mm_move_ss(v, tmp);					\
	    v = _mm_shuffle_ps(tmp, tmp, X_DZ | Y_DY | Z_SX | W_SW);	\
	    break;							\
        case 3:								\
	    v = _mm_shuffle_ps(v, v, X_DW | Y_DY | Z_SZ | W_SX);	\
	    tmp = _mm_move_ss(v, tmp);					\
	    v = _mm_shuffle_ps(tmp, tmp, X_DW | Y_DY | Z_SZ | W_SX);	\
	    break;							\
	}								\
    } while(0);
