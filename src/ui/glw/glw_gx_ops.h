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


static inline void
glw_PushMatrix(glw_rctx_t *newrc, glw_rctx_t *oldrc)
{
  guMtxCopy(oldrc->rc_be.gbr_model_matrix, newrc->rc_be.gbr_model_matrix);
}

#define glw_PopMatrix()


static inline void
glw_Translatef(glw_rctx_t *rc, float x, float y, float z)
{
  Mtx mtx;

  guMtxTrans(mtx, x, y, z);
  guMtxConcat(rc->rc_be.gbr_model_matrix, mtx, rc->rc_be.gbr_model_matrix);
}


static inline void
glw_Scalef(glw_rctx_t *rc, float x, float y, float z)
{
  Mtx mtx;

  guMtxScale(mtx, x, y, z);
  guMtxConcat(rc->rc_be.gbr_model_matrix, mtx, rc->rc_be.gbr_model_matrix);
}


static inline void
glw_Rotatef(glw_rctx_t *rc, float a, float x, float y, float z)
{
  Mtx mtx;
  guVector axis = {x,y,z};

  guMtxRotAxisDeg(mtx, &axis, a);
  guMtxConcat(rc->rc_be.gbr_model_matrix, mtx, rc->rc_be.gbr_model_matrix);
}


static inline void 
glw_LoadMatrixf(glw_rctx_t *rc, float *src)
{
  memcpy(rc->rc_be.gbr_model_matrix, src, sizeof(float) * 12);
}
