/*
 *  Showtime GTK frontend
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

#ifndef GU_MENU_H__
#define GU_MENU_H__

const char *gu_menu_accel_path(const char *path, 
			       guint key, GdkModifierType accel_mods);

GtkWidget *gu_menu_add_item(GtkWidget *parent, const char *title,
			    void (*cb)(GtkWidget *menuitem, gpointer aux),
			    gpointer aux, const char *image, const char *accel,
			    gboolean sensitive);


void gu_menu_add_sep(GtkWidget *parent);

#endif // GU_MENU_H__
