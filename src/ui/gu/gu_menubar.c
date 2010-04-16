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
#include "gu_menu.h"

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
  nav_open("playqueue:", NULL, NULL);
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
      nav_open((const char *)l->data, NULL, NULL);
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
 * File menu
 */
static GtkWidget *
gu_menubar_File(gtk_ui_t *gu, GtkAccelGroup *accel_group)
{
  GtkWidget *M = gtk_menu_item_new_with_mnemonic("_File");
  GtkWidget *m = gtk_menu_new();

  gtk_menu_set_accel_group(GTK_MENU(m), accel_group);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(M), m);


  gu_menu_add_item(m, "_Open File...", 
		   m_openfile, gu, GTK_STOCK_OPEN,
		   gu_menu_accel_path("<Showtime-Main>/_File/OpenFile", 
				      GDK_O, GDK_CONTROL_MASK), TRUE);
  
  gu_menu_add_item(m, "_Open Directory...", 
		   m_opendir, gu, GTK_STOCK_OPEN,
		   gu_menu_accel_path("<Showtime-Main>/_File/OpenDir", 
				      GDK_D, GDK_CONTROL_MASK), TRUE);
  
  gu_menu_add_sep(m);

  gu_menu_add_item(m, "_Quit", 
		   m_quit, gu, GTK_STOCK_QUIT,
		   gu_menu_accel_path("<Showtime-Main>/_File/Quit", 
				      GDK_Q, GDK_CONTROL_MASK), TRUE);
  return M;
}


/**
 * Go menu
 */
static GtkWidget *
gu_menubar_Go(gtk_ui_t *gu, GtkAccelGroup *accel_group)
{
  GtkWidget *M = gtk_menu_item_new_with_mnemonic("_Go");
  GtkWidget *m = gtk_menu_new();

  gtk_menu_set_accel_group(GTK_MENU(m), accel_group);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(M), m);


  gu_menu_add_item(m, "_Playqueue", 
		   m_openplayqueue, gu, GTK_STOCK_EXECUTE,
		   gu_menu_accel_path("<Showtime-Main>/_Go/Playqueue", 
				      GDK_P, GDK_CONTROL_MASK), TRUE);
  return M;
}


/**
 * Help menu
 */
static GtkWidget *
gu_menubar_Help(gtk_ui_t *gu, GtkAccelGroup *accel_group)
{
  GtkWidget *M = gtk_menu_item_new_with_mnemonic("_Help");
  GtkWidget *m = gtk_menu_new();

  gtk_menu_set_accel_group(GTK_MENU(m), accel_group);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(M), m);

  gu_menu_add_item(m, "_About Showtime", 
		   m_about, gu, GTK_STOCK_ABOUT,
		   NULL, TRUE);
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
			gu_menubar_File(gu, accel_group));

  gtk_menu_shell_append(GTK_MENU_SHELL(menubar),
			gu_menubar_Go(gu, accel_group));

  gtk_menu_shell_append(GTK_MENU_SHELL(menubar),
			gu_menubar_Help(gu, accel_group));


  gtk_window_add_accel_group(GTK_WINDOW(gu->gu_window), accel_group);
}




