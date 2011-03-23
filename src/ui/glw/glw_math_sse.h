static inline void
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



static inline void
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

static inline void 
glw_LoadMatrixf(glw_rctx_t *rc, float *src)
{
  memcpy(rc->rc_mtx, src, sizeof(float) * 16);
}


static inline void
glw_LerpMatrix(Mtx out, float v, const Mtx a, const Mtx b)
{
  __m128 vv = _mm_set1_ps(v);
  int i;

  for(i = 0; i < 4; i++)
    out[i] = _mm_mul_ps(_mm_add_ps(a[i], vv), _mm_sub_ps(b[i], a[i]));
}


static inline void
glw_vec2_store(float *p, const Vec2 v)
{
  _mm_storeu_ps(p, v);
}

static inline void
glw_vec3_store(float *p, const Vec3 v)
{
  _mm_storeu_ps(p, v);
}

static inline void
glw_vec4_store(float *p, const Vec4 v)
{
  _mm_storeu_ps(p, v);
}



// BAD
#define glw_mtx_mul_vec3(dst, mt, a) dst=a

#define glw_vec3_make(x,y,z) _mm_set_ps(x, y, z, 1)

#define glw_vec3_copy(dst, src) (dst) = (src)

#define glw_vec3_addmul(dst, a, b, s) do { \
    dst = _mm_add_ps((a), _mm_mul_ps((b), _mm_set1_ps(s))); } while(0)

#define glw_vec3_sub(dst, a, b) do { \
    dst = _mm_sub_ps((a), (b)); } while(0)

// BAD
#define glw_vec3_cross(dst, a, b) dst = a

// BAD
extern float glw_vec3_dot(const Vec3 a, const Vec3 b);

// BAD
extern float glw_vec34_dot(const Vec3 a, const Vec4 b);

// BAD
extern int glw_mtx_invert(Mtx dst, const Mtx src);


static inline float glw_vec3_extract(const Vec3 a, int pos)
{
  return __builtin_ia32_vec_ext_v4sf(a, pos);
}

static inline float glw_vec4_extract(const Vec4 a, int pos)
{
  return __builtin_ia32_vec_ext_v4sf(a, pos);
}

const static inline float *
glw_mtx_get(const Mtx src)
{
  return (const float *)&src[0];
}

static inline Vec2 glw_vec2_get(const float *p)
{
  return _mm_loadu_ps(p);
}

static inline Vec2 glw_vec3_get(const float *p)
{
  return _mm_loadu_ps(p);
}

static inline Vec2 glw_vec4_get(const float *p)
{
  return _mm_loadu_ps(p);
}

#define glw_vec2_lerp(dst, s, a, b) do { \
    dst = _mm_add_ps((a), _mm_mul_ps(_mm_set1_ps(s), _mm_sub_ps(b, a))); } \
  while(0)

#define glw_vec3_lerp(dst, s, a, b) do { \
    dst = _mm_add_ps((a), _mm_mul_ps(_mm_set1_ps(s), _mm_sub_ps(b, a))); } \
  while(0)

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
