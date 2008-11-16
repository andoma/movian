/*
 *  GL Widgets, Forms
 *  Copyright (C) 2008 Andreas Öman
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
 */

#ifndef GLW_FORM_H
#define GLW_FORM_H


TAILQ_HEAD(glw_form_focus_queue, glw_form_focus);

/**
 *
 */
typedef struct glw_form_focus {

  float gff_m[16];
  float gff_m_prim[16];

  float gff_alpha;
  float gff_alpha_prim;

  float gff_aspect;
  float gff_aspect_prim;

  TAILQ_ENTRY(glw_form_focus) gff_link;

} glw_form_focus_t;

/**
 *
 */
typedef struct glw_form {
  struct glw w;

  struct glw_form_focus_queue gf_focuses;
  glw_form_focus_t *gf_current_focus;

  


} glw_form_t;


void glw_cursor_layout_frame(void);

void glw_form_ctor(glw_t *w, int init, va_list ap);

void glw_form_cursor_set(glw_rctx_t *rc);

float glw_form_alpha_get(glw_t *w);

void glw_form_alpha_update(glw_t *w, glw_rctx_t *rc);


#endif /* GLW_FORM_H */
