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

#ifndef GU_DIRECTORY_H__
#define GU_DIRECTORY_H__

#define GU_DIR_HEADERS        0x1
#define GU_DIR_COL_TYPE       0x2
#define GU_DIR_COL_ARTIST     0x4
#define GU_DIR_COL_DURATION   0x8
#define GU_DIR_COL_ALBUM      0x10
#define GU_DIR_COL_NUM_TRACKS 0x20
#define GU_DIR_COL_TRACKINDEX 0x40
#define GU_DIR_SCROLLBOX      0x80
#define GU_DIR_VISIBLE_HEADERS 0x100
#define GU_DIR_COL_POPULARITY 0x200
#define GU_DIR_COL_USER 0x400

GtkWidget *gu_directory_list_create(gu_tab_t *gt, prop_t *root, int flags);

GtkWidget *gu_directory_album_create(gu_tab_t *gt, prop_t *root);

GtkWidget *gu_directory_albumcollection_create(gu_tab_t *gt, prop_t *root);

#endif /* GU_DIRECTORY_H__ */

