/*
 *  Audio iec958
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

#ifndef AUDIO_IEC958_H
#define AUDIO_IEC958_H


#define IEC958_AC3_FRAME_SIZE 6144

#define IEC968_MAX_FRAME_SIZE 65536

int iec958_build_ac3frame(uint8_t *src, uint8_t *dst);
int iec958_build_dtsframe(uint8_t *src, size_t srcsize, uint8_t *dst);

#endif /* AUDIO_IEC958_H */
