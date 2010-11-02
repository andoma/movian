/*
 *  GLW Recoder - Record video of display output
 *  Copyright (C) 2010 Andreas Öman
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

#ifndef GLW_REC_H__
#define GLW_REC_H__

typedef struct glw_rec glw_rec_t;

glw_rec_t *glw_rec_init(const char *filename, int width, int height, int fps);

void glw_rec_deliver_vframe(glw_rec_t *gr, void *data);

void glw_rec_stop(glw_rec_t *);

#endif // GLW_REC_H__
