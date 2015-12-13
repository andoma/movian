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
#pragma once
#define DISPLAY_STATUS_OFF             0
#define DISPLAY_STATUS_ON              1

#define RUNMODE_EXIT                   0
#define RUNMODE_RUNNING                1
#define RUNMODE_STANDBY                2

extern int display_status;
extern int cec_we_are_not_active;

void rpi_cec_init(void);

int rpi_is_codec_enabled(const char *id);

int rpi_set_display_framerate(float fps, int width, int height);
