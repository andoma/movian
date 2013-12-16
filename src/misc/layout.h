#pragma once
/*
 *  Showtime Mediacenter
 *  Copyright (C) 2007-2013 Lonelycoder AB
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

/**
 * Based on JPEG/EXIF orientations
 *
 * http://sylvana.net/jpegcrop/exif_orientation.html
 */
#define LAYOUT_ORIENTATION_NONE       0
#define LAYOUT_ORIENTATION_NORMAL     1
#define LAYOUT_ORIENTATION_MIRROR_X   2
#define LAYOUT_ORIENTATION_ROT_180    3
#define LAYOUT_ORIENTATION_MIRROR_Y   4
#define LAYOUT_ORIENTATION_TRANSPOSE  5
#define LAYOUT_ORIENTATION_ROT_90     6
#define LAYOUT_ORIENTATION_TRANSVERSE 7
#define LAYOUT_ORIENTATION_ROT_270    8


// "numpad" style layout. Also used by SSA rendering, so don't change
#define LAYOUT_ALIGN_BOTTOM_LEFT   1
#define LAYOUT_ALIGN_BOTTOM        2
#define LAYOUT_ALIGN_BOTTOM_RIGHT  3
#define LAYOUT_ALIGN_LEFT          4
#define LAYOUT_ALIGN_CENTER        5
#define LAYOUT_ALIGN_RIGHT         6
#define LAYOUT_ALIGN_TOP_LEFT      7
#define LAYOUT_ALIGN_TOP           8
#define LAYOUT_ALIGN_TOP_RIGHT     9
#define LAYOUT_ALIGN_JUSTIFIED     10 // special

