static inline void
glw_Translatef(glw_rctx_t *rc, float x, float y, float z)
{
  float *m = rc->rc_be.gbr_mtx;

  m[12] += m[0]*x + m[4]*y +  m[8]*z;
  m[13] += m[1]*x + m[5]*y +  m[9]*z;
  m[14] += m[2]*x + m[6]*y + m[10]*z;
}


static inline void
glw_Scalef(glw_rctx_t *rc, float x, float y, float z)
{
  float *m = rc->rc_be.gbr_mtx;

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

static inline void 
glw_LoadMatrixf(glw_rctx_t *rc, float *src)
{
  memcpy(rc->rc_be.gbr_mtx, src, sizeof(float) * 16);
}
