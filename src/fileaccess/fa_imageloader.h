/*
 *  Imageloader
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

#ifndef FA_IMAGELOADER_H
#define FA_IMAGELOADER_H

typedef struct fa_image_load_ctrl {
  const char *url;
  int got_thumb;
  int want_thumb;
  void *data;
  size_t datasize;
  int codecid;              /* LAVC codec id */
} fa_image_load_ctrl_t;

int fa_imageloader(fa_image_load_ctrl_t *ctrl, const char *theme);

#endif /* FA_IMAGELOADER_H */
