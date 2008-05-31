/*
 *  Display settings
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

#ifndef DISPLAY_SETTINGS_H_
#define DISPLAY_SETTINGS_H_

typedef struct display_settings {
  enum {
    DISPLAYMODE_WINDOWED = 0,
    DISPLAYMODE_FULLSCREEN,
  } displaymode;

} display_settings_t;

extern display_settings_t display_settings;

void display_settings_init(glw_t *m, glw_focus_stack_t *gfs, ic_t *ic);

void display_settings_save(void);
void display_settings_load(void);

#endif /* DISPLAY_SETTINGS_H_ */
