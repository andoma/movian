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

#include "glw.h"

LIST_HEAD(glw_style_attribute_list, glw_style_attribute);

typedef struct glw_style_attribute {
  LIST_ENTRY(glw_style_attribute) gsa_link;
  glw_attribute_t gsa_attribute;

  union {
    int i32;
    float float;
    float em;
    rstr_t *rstr;
    float fvec[4];
    int16_t i16vec[4];

    struct {
      int set;
      int clr;
    } flags;
  };

} glw_style_attribute_t;



/**
 *
 */
typedef struct glw_style {

  glw_t w;
  struct glw_style_attribute_list attributes;

} glw_style_t;




/**
 *
 */
static glw_class_t glw_text = {
  .gc_name            = "style",
  .gc_instance_size   = sizeof(glw_style_t),
  .gc_dtor            = glw_style_dtor,

  .gc_set_float3      = glw_style_set_float3,
  .gc_set_int16_4     = gly_style_set_int16_4,
  .gc_mod_text_flags  = glw_style_text_flags,
  .gc_mod_text_flags  = glw_style_text_flags,
  .gc_set_rstr        = glw_style_set_rstr,
  .gc_set_float       = glw_set_float,
  .gc_set_em          = glw_set_em,
  .gc_set_int         = glw_set_int,

  .gc_mod_flags2      = glw_style_flags2,
  .gc_mod_text_flags  = glw_style_text_flags,
  .gc_mod_image_flags = glw_style_image_flags,
};
