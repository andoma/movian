/*
 *  Browser, view functions
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

#ifndef BROWSER_VIEW_H
#define BROWSER_VIEW_H

void browser_view_expand_node(browser_node_t *bn, glw_t *parent, 
			    glw_focus_stack_t *gfs);

void browser_view_collapse_node(browser_node_t *bn, glw_focus_stack_t *gfs);

void browser_view_add_node(browser_node_t *bn);

browser_node_t *browser_view_get_current_selected_node(glw_t *stack);

browser_node_t *browser_view_get_current_node(glw_t *stack);

void browser_view_set(browser_node_t *bn, const char *viewname);

void browser_view_node_model_update(browser_node_t *bn);

#endif /* BROWSER_VIEW_H */
