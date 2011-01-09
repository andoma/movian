

static inline void
glw_PushMatrix(glw_rctx_t *newrc, glw_rctx_t *oldrc)
{
  guMtxCopy(oldrc->rc_mtx, newrc->rc_mtx);
}

#define glw_PopMatrix()


static inline void
glw_Translatef(glw_rctx_t *rc, float x, float y, float z)
{
  Mtx mtx;

  guMtxTrans(mtx, x, y, z);
  guMtxConcat(rc->rc_mtx, mtx, rc->rc_mtx);
}


static inline void
glw_Scalef(glw_rctx_t *rc, float x, float y, float z)
{
  Mtx mtx;

  guMtxScale(mtx, x, y, z);
  guMtxConcat(rc->rc_mtx, mtx, rc->rc_mtx);
}


static inline void
glw_Rotatef(glw_rctx_t *rc, float a, float x, float y, float z)
{
  Mtx mtx;
  guVector axis = {x,y,z};

  guMtxRotAxisDeg(mtx, &axis, a);
  guMtxConcat(rc->rc_mtx, mtx, rc->rc_mtx);
}


static inline void 
glw_LoadMatrixf(glw_rctx_t *rc, float *src)
{
  memcpy(rc->rc_mtx, src, sizeof(float) * 12);
}

static inline void
glw_LerpMatrix(Mtx out, float v, Mtx a, Mtx b)
{
  int y, x;
  for(y = 0; y < 3; y++)
    for(x = 0; x < 4; x++)
      out[y][x] = GLW_LERP(v, a[y][x], b[y][x]);
}
