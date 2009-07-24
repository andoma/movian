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

/**
 *
 */
typedef struct gu_directory {
  GtkWidget *gd_curview;       /* Root widget for current view.
				  Must always be the only child of
				  gnp->gnp_pageroot (which is a vbox) */

  gtk_ui_t *gd_gu;
  gu_nav_page_t *gd_gnp;
  prop_sub_t *gd_view_sub;
} gu_directory_t;


void gu_directory_list_create(gu_directory_t *gu);

void gu_directory_album_create(gu_directory_t *gu);

#endif /* GU_DIRECTORY_H__ */

