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

/*********************************************************
 * Styles
 */
static __inline glw_style_set_t * attribute_unused_result
glw_style_set_retain(glw_style_set_t *gss)
{
  if(gss != NULL)
    gss->gss_refcount++;
  return gss;
}

void glw_style_set_release(glw_style_set_t *gss);

glw_style_set_t * attribute_unused_result
glw_style_set_add(glw_style_set_t *gss, glw_style_t *gs);

glw_style_t *glw_style_create(glw_root_t *gr, rstr_t *name);

int glw_style_set_for_widget(glw_t *w, const char *name);

int glw_style_bind(glw_t *w, glw_style_t *gs);

void glw_style_update_em(glw_root_t *gr);
