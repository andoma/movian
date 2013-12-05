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

#include "gu.h"
#include "gu_menu.h"

/**
 *
 */
const char *
gu_menu_accel_path(const char *path, guint key,  GdkModifierType accel_mods)
{
  gtk_accel_map_add_entry(path, key, accel_mods);
  return path;
}



/**
 *
 */
GtkWidget *
gu_menu_add_item(GtkWidget *parent, const char *title,
		 void (*cb)(GtkWidget *menuitem, gpointer aux),
		 gpointer aux, const char *image, const char *accel,
		 gboolean sensitive)
{
  GtkWidget *w;
  GtkWidget *img;

  if(image != NULL) {
    w = gtk_image_menu_item_new_with_mnemonic(title);

    img = gtk_image_new_from_stock(image, GTK_ICON_SIZE_MENU);
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(w), img);


  } else {
    w = gtk_menu_item_new_with_mnemonic(title);
  }

  if(accel != NULL) {
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(w), accel);
  }

  gtk_widget_set_sensitive(w, sensitive);
  gtk_menu_shell_append(GTK_MENU_SHELL(parent), w);
  g_signal_connect(G_OBJECT(w), "activate", (void *)cb, aux);
  return w;
}


/**
 *
 */
GtkWidget *
gu_menu_add_toggle(GtkWidget *parent, const char *title,
		   void (*cb)(GtkCheckMenuItem *menuitem, gpointer aux),
		   gpointer aux, gboolean active, const char *accel,
		   gboolean sensitive)
{
  GtkWidget *w;

  w = gtk_check_menu_item_new_with_mnemonic(title);

  if(accel != NULL) {
    gtk_menu_item_set_accel_path(GTK_MENU_ITEM(w), accel);
  }

  gtk_widget_set_sensitive(w, sensitive);
  gtk_menu_shell_append(GTK_MENU_SHELL(parent), w);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w), active);
  g_signal_connect(G_OBJECT(w), "toggled", (void *)cb, aux);
  return w;
}


/**
 *
 */
void
gu_menu_add_sep(GtkWidget *parent)
{
  GtkWidget *w = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(parent), w);
}

