/*
 *  Simple scaler interface
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

#ifndef GLW_SCALER_H
#define GLW_SCALER_H

void
glw_bitmap_rescale(uint8_t *src, int src_width, int src_height, int src_stride,
		   uint8_t *dst, int dst_width, int dst_height, int dst_stride,
		   int bpp);

#endif /* GLW_SCALER_H */
