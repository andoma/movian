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
#include "fileaccess/fileaccess.h"

/**
 *
 */
glw_scope_t *
glw_scope_dup(const glw_scope_t *src, int retain_mask)
{
  glw_scope_t *o = malloc(sizeof(glw_scope_t));

  memcpy(o, src, sizeof(glw_scope_t));
  for(int i = 0; i < src->gs_num_roots; i++) {
    if(!((1 << i) & retain_mask))
      o->gs_roots[i].p = prop_ref_inc(o->gs_roots[i].p);
  }
  o->gs_refcount = 1;
  return o;
}


/**
 *
 */
glw_scope_t *
glw_scope_create(void)
{
  glw_scope_t *o = calloc(1, sizeof(glw_scope_t));

  o->gs_roots[GLW_ROOT_SELF].name   = "self";
  o->gs_roots[GLW_ROOT_PARENT].name = "parent";
  o->gs_roots[GLW_ROOT_VIEW].name   = "view";
  o->gs_roots[GLW_ROOT_ARGS].name   = "args";
  o->gs_roots[GLW_ROOT_CLONE].name  = "clone";
  o->gs_roots[GLW_ROOT_EVENT].name  = "event";
  o->gs_roots[GLW_ROOT_CORE].name   = "core";
  o->gs_num_roots = GLW_ROOT_static;
  o->gs_refcount = 1;
  return o;
}


void
glw_scope_release(glw_scope_t *gs)
{
  gs->gs_refcount--;
  if(gs->gs_refcount)
    return;

  for(int i = 0; i < gs->gs_num_roots; i++)
    prop_ref_dec(gs->gs_roots[i].p);
  free(gs);
}


glw_scope_t *
glw_scope_retain(glw_scope_t *gs)
{
  gs->gs_refcount++;
  return gs;
}
