/*
 *  Video menu
 *
 *  Copyright (C) 2007 Andreas Öman
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

#ifndef VIDEO_MENU_H
#define VIDOE_MENU_H

void video_menu_add_tab(glw_t *m, glw_focus_stack_t *gfs, ic_t *ic,
			vd_conf_t *vdc, const char *src);

void video_menu_attach(glw_t *m, glw_focus_stack_t *gfs, ic_t *ic,
		       vd_conf_t *vdc);

#endif /* VIDEO_MENU_H */
