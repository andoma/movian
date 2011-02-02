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
