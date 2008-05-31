/*
 *  glue for system dependent gl stuff
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

#ifndef SYSGLUE_H
#define SYSGLUE_H

#include <libglw/glw.h>
#include <hid/input.h>

void gl_sysglue_init(int argc, char **argv);

void gl_common_init(void);

void gl_sysglue_mainloop(void);

void gl_update_timings(void);

void display_settings_init(glw_t *m, glw_focus_stack_t *gfs, ic_t *ic);

#endif /* SYSGLUE_H */


