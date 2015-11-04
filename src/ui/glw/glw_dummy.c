/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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

/**
 *
 */
static void
glw_dummy_layout(glw_t *w, const glw_rctx_t *rc)
{
}


/**
 *
 */
static void
glw_dummy_render(glw_t *w, const glw_rctx_t *rc)
{
  if(glw_is_focusable_or_clickable(w))
    glw_store_matrix(w, rc);
}


/**
 *
 */
static glw_class_t glw_dummy = {
  .gc_name = "dummy",
  .gc_instance_size = sizeof(glw_t),
  .gc_layout = glw_dummy_layout,
  .gc_render = glw_dummy_render,
};

GLW_REGISTER_CLASS(glw_dummy);
