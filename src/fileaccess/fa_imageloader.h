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
#ifndef FA_IMAGELOADER_H
#define FA_IMAGELOADER_H

struct image_meta;
struct backend;

void fa_imageloader_init(void);

struct image *fa_imageloader(const char *url, const struct image_meta *im,
                             char *errbuf, size_t errlen,
                             int *cache_control, cancellable_t *c,
                             struct backend *be);


#endif /* FA_IMAGELOADER_H */
