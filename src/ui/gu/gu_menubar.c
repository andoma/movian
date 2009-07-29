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

#include "navigator.h"
#include "gu.h"
#include "showtime.h"

#include <gdk/gdkkeysyms.h>

#if 0

static void
fullscreen_toggle(gpointer callback_data, guint callback_action,
		  GtkWidget *menu_item)
{
  gtk_ui_t *gu = callback_data;
  int v = GTK_CHECK_MENU_ITEM(menu_item)->active;
  
  if(v) 
    gtk_window_fullscreen(GTK_WINDOW(gu->gu_window));
  else
    gtk_window_unfullscreen(GTK_WINDOW(gu->gu_window));
}
#endif


/**
 *
 */
static void
m_quit(GtkWidget *menu_item, gpointer callback_data)
{
  showtime_shutdown(0);
}


/**
 *
 */
static void
m_openplayqueue(GtkWidget *menu_item, gpointer callback_data)
{
  nav_open("playqueue:", NULL, NULL, NAV_OPEN_ASYNC);
}


/**
 *
 */
static void
m_about(GtkWidget *menu_item, gpointer callback_data)
{
  gtk_ui_t *gu = callback_data;
  extern const char *htsversion;

  gtk_show_about_dialog(GTK_WINDOW(gu->gu_window),
			"program-name", "HTS Showtime",
			"version", htsversion,
			"website", "http://www.lonelycoder.com/hts",
			"copyright", "2006 - 2009 Andreas Öman, et al.",
			NULL);
}


/**
 *
 */
static void
m_open_response(GtkDialog *dialog, gint response_id, gpointer user_data)
{
  GSList *l, *l0;
  if(response_id == GTK_RESPONSE_ACCEPT) {
    l0 = l = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));

    for(; l != NULL; l = l->next) {
      nav_open((const char *)l->data, NULL, NULL, NAV_OPEN_ASYNC);
    }
    g_slist_free(l0);

  }
  gtk_widget_destroy(GTK_WIDGET(dialog));
}

/**
 *
 */
static void
m_openfile(GtkWidget *menu_item, gpointer callback_data)
{
  gtk_ui_t *gu = callback_data;
  GtkWidget *dialog;

  dialog = gtk_file_chooser_dialog_new("Open File",
				       GTK_WINDOW(gu->gu_window),
				       GTK_FILE_CHOOSER_ACTION_OPEN,
				       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				       GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				       NULL);

  gtk_widget_show(dialog);

  g_signal_connect(G_OBJECT(dialog), "response",
		   G_CALLBACK(m_open_response), gu);
}

/**
 *
 */
static void
m_opendir(GtkWidget *menu_item, gpointer callback_data)
{
  gtk_ui_t *gu = callback_data;
  GtkWidget *dialog;

  dialog = gtk_file_chooser_dialog_new("Open Directory",
				       GTK_WINDOW(gu->gu_window),
				       GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
				       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				       GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				       NULL);

  gtk_widget_show(dialog);

  g_signal_connect(G_OBJECT(dialog), "response",
		   G_CALLBACK(m_open_response), gu);
}


/**
 *
 */
static const char *
accel_path(const char *path, guint key,  GdkModifierType accel_mods)
{
  gtk_accel_map_add_entry(path, key, accel_mods);
  return path;
}



/**
 *
 */
static GtkWidget *
add_item(GtkWidget *parent, const char *title,
	 void (*cb)(GtkWidget *menuitem, gpointer aux),
	 gpointer aux, const char *image, const char *accel)
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

  gtk_menu_shell_append(GTK_MENU_SHELL(parent), w);
  g_signal_connect(G_OBJECT(w), "activate", (void *)cb, aux);
  return w;
}


/**
 *
 */
static void
add_sep(GtkWidget *parent)
{
  GtkWidget *w = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(parent), w);
}



/**
 * File menu
 */
static GtkWidget *
gu_menu_File(gtk_ui_t *gu, GtkAccelGroup *accel_group)
{
  GtkWidget *M = gtk_menu_item_new_with_mnemonic("_File");
  GtkWidget *m = gtk_menu_new();

  gtk_menu_set_accel_group(GTK_MENU(m), accel_group);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(M), m);


  add_item(m, "_Open File...", 
	   m_openfile, gu, GTK_STOCK_OPEN,
	   accel_path("<Showtime-Main>/_File/OpenFile", 
		      GDK_O, GDK_CONTROL_MASK));

  add_item(m, "_Open Directory...", 
	   m_opendir, gu, GTK_STOCK_OPEN,
	   accel_path("<Showtime-Main>/_File/OpenDir", 
		      GDK_D, GDK_CONTROL_MASK));

  add_sep(m);

  add_item(m, "_Quit", 
	   m_quit, gu, GTK_STOCK_QUIT,
	   accel_path("<Showtime-Main>/_File/Quit", 
		      GDK_Q, GDK_CONTROL_MASK));
  return M;
}


/**
 * Go menu
 */
static GtkWidget *
gu_menu_Go(gtk_ui_t *gu, GtkAccelGroup *accel_group)
{
  GtkWidget *M = gtk_menu_item_new_with_mnemonic("_Go");
  GtkWidget *m = gtk_menu_new();

  gtk_menu_set_accel_group(GTK_MENU(m), accel_group);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(M), m);


  add_item(m, "_Playqueue", 
	   m_openplayqueue, gu, GTK_STOCK_EXECUTE,
	   accel_path("<Showtime-Main>/_Go/Playqueue", 
		      GDK_P, GDK_CONTROL_MASK));
  return M;
}


/**
 * Help menu
 */
static GtkWidget *
gu_menu_Help(gtk_ui_t *gu, GtkAccelGroup *accel_group)
{
  GtkWidget *M = gtk_menu_item_new_with_mnemonic("_Help");
  GtkWidget *m = gtk_menu_new();

  gtk_menu_set_accel_group(GTK_MENU(m), accel_group);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(M), m);

  add_item(m, "_About Showtime", 
	   m_about, gu, GTK_STOCK_ABOUT,
	   NULL);
  return M;
}


/**
 *
 */
void
gu_menubar_add(gtk_ui_t *gu, GtkWidget *parent)
{
  GtkAccelGroup *accel_group;
  GtkWidget *menubar;

  accel_group = gtk_accel_group_new();
  menubar = gtk_menu_bar_new();
  gtk_box_pack_start(GTK_BOX(parent), menubar, FALSE, TRUE, 0);


  gtk_menu_shell_append(GTK_MENU_SHELL(menubar),
			gu_menu_File(gu, accel_group));

  gtk_menu_shell_append(GTK_MENU_SHELL(menubar),
			gu_menu_Go(gu, accel_group));

  gtk_menu_shell_append(GTK_MENU_SHELL(menubar),
			gu_menu_Help(gu, accel_group));


  gtk_window_add_accel_group(GTK_WINDOW(gu->gu_window), accel_group);
}




