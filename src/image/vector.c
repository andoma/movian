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


#include "vector.h"

#include <stdlib.h>

/**
 *
 */
static const char vec_cmd_len[] = {
  [VC_SET_FILL_ENABLE] = 1,
  [VC_SET_FILL_COLOR] = 1,
  [VC_SET_STROKE_WIDTH] = 1,
  [VC_SET_STROKE_COLOR] = 1,
  [VC_MOVE_TO] = 2,
  [VC_LINE_TO] = 2,
  [VC_CUBIC_TO] = 6,
};


/**
 *
 */
static void
vec_resize(image_component_vector_t *icv, vec_cmd_t cmd)
{
  const int len = vec_cmd_len[cmd] + 1;
  if(icv->icv_used + len > icv->icv_capacity) {
    icv->icv_capacity = 2 * icv->icv_capacity + len + 16;
    icv->icv_data = realloc(icv->icv_data, icv->icv_capacity * sizeof(float));
  }
}


/**
 *
 */
void
vec_emit_0(image_component_vector_t *icv, vec_cmd_t cmd)
{
  vec_resize(icv, cmd);
  icv->icv_int[icv->icv_used++] = cmd;
}


/**
 *
 */
void
vec_emit_i1(image_component_vector_t *icv, vec_cmd_t cmd, int32_t i)
{
  vec_resize(icv, cmd);

  switch(cmd) {
  case VC_SET_FILL_COLOR:
  case VC_SET_STROKE_COLOR:
    icv->icv_colorized = 1;
    break;
  default:
    break;
  }

  icv->icv_int[icv->icv_used++] = cmd;
  icv->icv_int[icv->icv_used++] = i;
}


/**
 *
 */
void
vec_emit_f1(image_component_vector_t *icv, vec_cmd_t cmd, const float *a)
{
  vec_resize(icv, cmd);
  icv->icv_int[icv->icv_used++] = cmd;
  icv->icv_flt[icv->icv_used++] = a[0];
  icv->icv_flt[icv->icv_used++] = a[1];
}


/**
 *
 */
void
vec_emit_f3(image_component_vector_t *icv, vec_cmd_t cmd,
            const float *a, const float *b, const float *c)
{
  vec_resize(icv, cmd);
  int ptr = icv->icv_used;
  icv->icv_int[ptr+0] = cmd;
  icv->icv_flt[ptr+1] = a[0];
  icv->icv_flt[ptr+2] = a[1];
  icv->icv_flt[ptr+3] = b[0];
  icv->icv_flt[ptr+4] = b[1];
  icv->icv_flt[ptr+5] = c[0];
  icv->icv_flt[ptr+6] = c[1];
  icv->icv_used = ptr + 7;
}

