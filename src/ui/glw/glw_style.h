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

struct token;
struct glw_view_eval_context;

/*********************************************************
 * Styles
 */
static __inline glw_styleset_t * attribute_unused_result
glw_styleset_retain(glw_styleset_t *gss)
{
  if(gss != NULL)
    gss->gss_refcount++;
  return gss;
}

void glw_styleset_release(glw_styleset_t *gss);

glw_styleset_t * attribute_unused_result
glw_styleset_add(glw_styleset_t *gss, glw_style_t *gs);

glw_style_t *glw_style_create(glw_t *parent, rstr_t *name,
                              rstr_t *file, int line, int inherit);

void glw_style_update_em(glw_root_t *gr);

void glw_style_attach_rpns(glw_style_t *gs, struct token *t);

int glw_styleset_for_widget(glw_t *w, const char *name,
                            struct glw_view_eval_context *ec);

int glw_styleset_for_widget_multiple(glw_t *w, struct token *chain,
                                     struct glw_view_eval_context *ec);

void glw_style_bind_ancestor(glw_style_t *gs, glw_style_t *ancestor);

void glw_style_unbind_all(glw_t *w);

void glw_style_cleanup(glw_root_t *gr);

