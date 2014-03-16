/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2014 Lonelycoder AB
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

#pragma once

#include "image.h"

/**
 * Vector graphics
 */
typedef enum {
  VC_SET_FILL_ENABLE,
  VC_SET_FILL_COLOR,
  VC_SET_STROKE_WIDTH,
  VC_SET_STROKE_COLOR,
  VC_BEGIN,
  VC_END,
  VC_MOVE_TO,
  VC_LINE_TO,
  VC_CUBIC_TO,
  VC_CLOSE,
} vec_cmd_t;

void vec_emit_0(image_component_vector_t *icv, vec_cmd_t cmd);

void vec_emit_i1(image_component_vector_t *icv, vec_cmd_t cmd, int arg);

void vec_emit_f1(image_component_vector_t *icv, vec_cmd_t cmd,
                 const float *a);

void vec_emit_f3(image_component_vector_t *icv, vec_cmd_t cmd,
                 const float *a, const float *b, const float *c);

