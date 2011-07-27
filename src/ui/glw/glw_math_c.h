static inline void
glw_Translatef(glw_rctx_t *rc, float x, float y, float z)
{
  float *m = rc->rc_mtx;

  m[12] += m[0]*x + m[4]*y +  m[8]*z;
  m[13] += m[1]*x + m[5]*y +  m[9]*z;
  m[14] += m[2]*x + m[6]*y + m[10]*z;
}


static inline void
glw_Scalef(glw_rctx_t *rc, float x, float y, float z)
{
  float *m = rc->rc_mtx;

  m[0] *= x;
  m[4] *= y;
  m[8] *= z;

  m[1] *= x;
  m[5] *= y;
  m[9] *= z;

  m[2] *= x;
  m[6] *= y;
  m[10]*= z;
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
  int i;
  for(i = 0; i < 16; i++)
    out[i] = GLW_LERP(v, a[i], b[i]);
}


typedef float *PMtx;

#define glw_pmtx_mul_prepare(dst, src) dst = &src[0]

static inline void
glw_pmtx_mul_vec3(Vec3 dst, const float *mt, const Vec3 a)
{
  dst[0] = mt[0] * a[0] + mt[4] * a[1] + mt[ 8] * a[2] + mt[12];
  dst[1] = mt[1] * a[0] + mt[5] * a[1] + mt[ 9] * a[2] + mt[13];
  dst[2] = mt[2] * a[0] + mt[6] * a[1] + mt[10] * a[2] + mt[14];
}


static inline float
glw_vec34_dot(const Vec3 A, const Vec4 B)
{
  return A[0] * B[0] + A[1] * B[1] + A[2] * B[2] + B[3];
}

static inline void
glw_vec2_lerp(Vec2 dst, float s, const Vec2 a, const Vec2 b)
{
  dst[0] = a[0] + s * (b[0] - a[0]);
  dst[1] = a[1] + s * (b[1] - a[1]);
}

static inline void
glw_vec3_lerp(Vec3 dst, float s, const Vec3 a, const Vec3 b)
{
  dst[0] = a[0] + s * (b[0] - a[0]);
  dst[1] = a[1] + s * (b[1] - a[1]);
  dst[2] = a[2] + s * (b[2] - a[2]);
}

static inline void
glw_vec4_lerp(Vec4 dst, float s, const Vec4 a, const Vec4 b)
{
  dst[0] = a[0] + s * (b[0] - a[0]);
  dst[1] = a[1] + s * (b[1] - a[1]);
  dst[2] = a[2] + s * (b[2] - a[2]);
  dst[3] = a[3] + s * (b[3] - a[3]);
}

static inline void
glw_vec2_store(float *p, const Vec2 v)
{
  p[0] = v[0];
  p[1] = v[1];
}

static inline void
glw_vec3_store(float *p, const Vec3 v)
{
  p[0] = v[0];
  p[1] = v[1];
  p[2] = v[2];
}

static inline void
glw_vec4_store(float *p, const Vec4 v)
{
  p[0] = v[0];
  p[1] = v[1];
  p[2] = v[2];
  p[3] = v[3];
}

#define glw_vec2_get(p) (p)
#define glw_vec3_get(p) (p)
#define glw_vec4_get(p) (p)


#define glw_vec3_make(x,y,z) ((const float[3]){x,y,z})
#define glw_vec4_make(x,y,z,w) ((const float[4]){x,y,z,w})

    
static inline void
glw_vec3_copy(Vec3 dst, const Vec3 src)
{
  dst[0] = src[0];
  dst[1] = src[1];
  dst[2] = src[2];
}

static inline void
glw_vec4_copy(Vec4 dst, const Vec4 src)
{
  dst[0] = src[0];
  dst[1] = src[1];
  dst[2] = src[2];
  dst[3] = src[3];
}

static inline void
glw_vec3_addmul(Vec3 dst, const Vec3 a, const Vec3 b, float s)
{
  dst[0] = a[0] + b[0] * s;
  dst[1] = a[1] + b[1] * s;
  dst[2] = a[2] + b[2] * s;
}

static inline void
glw_vec3_sub(Vec3 dst, const Vec3 a, const Vec3 b)
{
  dst[0] = a[0] - b[0];
  dst[1] = a[1] - b[1];
  dst[2] = a[2] - b[2];
}

static inline void
glw_vec3_cross(Vec3 dst, const Vec3 a, const Vec3 b)
{
  dst[0] = (a[1] * b[2]) - (a[2] * b[1]);
  dst[1] = (a[2] * b[0]) - (a[0] * b[2]);
  dst[2] = (a[0] * b[1]) - (a[1] * b[0]);
}

static inline float
glw_vec3_dot(const Vec3 a, const Vec3 b)
{
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline void
glw_mtx_trans_mul_vec4(Vec4 dst, const Mtx mt, const Vec4 v)
{
  dst[0] = mt[ 0] * v[0] + mt[ 1] * v[1] + mt[ 2] * v[2] + mt[ 3] * v[3];
  dst[1] = mt[ 4] * v[0] + mt[ 5] * v[1] + mt[ 6] * v[2] + mt[ 7] * v[3];
  dst[2] = mt[ 8] * v[0] + mt[ 9] * v[1] + mt[10] * v[2] + mt[11] * v[3];
  dst[3] = mt[12] * v[0] + mt[13] * v[1] + mt[14] * v[2] + mt[15] * v[3];
}

extern int glw_mtx_invert(Mtx dst, const Mtx src);

#define glw_vec2_extract(v, i) v[i]
#define glw_vec3_extract(v, i) v[i]
#define glw_vec4_extract(v, i) v[i]
#define glw_mtx_get(m) (m)
