/*
 *  Showtime GTK frontend
 *  Copyright (C) 2009 Andreas Öman
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

#ifndef GU_DIRECTORY_H__
#define GU_DIRECTORY_H__

#define GU_DIR_HEADERS        0x1
#define GU_DIR_COL_TYPE       0x2
#define GU_DIR_COL_ARTIST     0x4
#define GU_DIR_COL_DURATION   0x8
#define GU_DIR_COL_ALBUM      0x10
#define GU_DIR_COL_NUM_TRACKS 0x20
#define GU_DIR_COL_TRACKINDEX 0x40


GtkWidget *gu_directory_list_create(gtk_ui_t *gu, prop_t *root,
				    char **parenturlptr,
				    int flags);

GtkWidget *gu_directory_album_create(gtk_ui_t *gu, prop_t *root,
				     char **parenturlptr);

#endif /* GU_DIRECTORY_H__ */

