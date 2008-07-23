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

#define IEC958_MAX_FRAME_SIZE 65536 /* Figure a 'real' value */

#define IEC958_PAYLOAD_NULL                         0
#define IEC958_PAYLOAD_AC3                          1
#define IEC958_PAYLOAD_SMPTE_TIME_STAMP             2
#define IEC958_PAYLOAD_MPEG1AUDIO_LAYER1            3
#define IEC958_PAYLOAD_MPEG1AUDIO_LAYER2_3          4
#define IEC958_PAYLOAD_MPEG2AUDIO_EX         T      5
#define IEC958_PAYLOAD_PAUSE                        6
#define IEC958_PAYLOAD_ACX                          7
#define IEC958_PAYLOAD_MPEG2AUDIO_LAYER1_LOW_RATE   8
#define IEC958_PAYLOAD_MPEG2AUDIO_LAYER2_3_LOW_RATE 9
#define IEC958_PAYLOAD_DTS_1                        11
#define IEC958_PAYLOAD_DTS_2                        12
#define IEC958_PAYLOAD_DTS_3                        13

int iec958_build_ac3frame(const uint8_t *src, size_t srcsize, uint8_t *dst);
int iec958_build_dtsframe(const uint8_t *src, size_t srcsize, uint8_t *dst);

#endif /* AUDIO_IEC958_H */
