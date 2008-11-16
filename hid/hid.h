/*
 *  Common HID functions
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

#ifndef HID_H
#define HID_H

#include <libglw/glw.h>

typedef enum {
  HID_IR_NONE,
  HID_IR_LIRC,
  HID_IR_IMONPAD,
} hid_ir_mode_t;

extern hid_ir_mode_t hid_ir_mode;

void hid_init(glw_t *m);

#endif /* HID_H */
