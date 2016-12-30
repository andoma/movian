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
#include "navigator.h"
#include "gu.h"
#include "main.h"
#include "gu_menu.h"

#include <gdk/gdkkeysyms.h>


/**
 *
 */
static void
m_quit(GtkWidget *menu_item, gpointer data)
{
  app_shutdown(0);
}


/**
 *
 */
static void
m_openplayqueue(GtkWidget *menu_item, gpointer data)
{
  gu_window_t *gw = data;
  gu_tab_open(gw->gw_current_tab, "playqueue:");
}


/**
 *
 */
static void
m_about(GtkWidget *menu_item, gpointer callback_data)
{
  gu_window_t *gw = callback_data;

  gtk_show_about_dialog(GTK_WINDOW(gw->gw_window),
			"program-name", APPNAMEUSER,
			"version", appversion,
			"website", "https://movian.tv/",
			"copyright", "Lonelycoder AB",
			NULL);
}


/**
 *
 */
static void
m_open_response(GtkDialog *dialog, gint response_id, gpointer data)
{
  gu_window_t *gw = data;
  GSList *l, *l0;
  if(response_id == GTK_RESPONSE_ACCEPT) {
    l0 = l = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));

    for(; l != NULL; l = l->next) {
      gu_tab_open(gw->gw_current_tab, (const char *)l->data);
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
  gu_window_t *gw = callback_data;
  GtkWidget *dialog;

  dialog = gtk_file_chooser_dialog_new("Open File",
				       GTK_WINDOW(gw->gw_window),
				       GTK_FILE_CHOOSER_ACTION_OPEN,
				       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				       GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				       NULL);

  gtk_widget_show(dialog);

  g_signal_connect(G_OBJECT(dialog), "response",
		   G_CALLBACK(m_open_response), gw);
}


/**
 *
 */
static void
m_openwindow(GtkWidget *menu_item, gpointer callback_data)
{
  gu_window_t *gw = callback_data;
  gu_win_create(gw->gw_gu, 0, NULL);
}


/**
 *
 */
static void
m_opentab(GtkWidget *menu_item, gpointer callback_data)
{
  gu_window_t *gw = callback_data;
  gu_tab_create(gw, 1, NULL);
}


/**
 *
 */
static void
m_closewindow(GtkWidget *menu_item, gpointer callback_data)
{
  gu_window_t *gw = callback_data;
  gu_win_destroy(gw);
}


/**
 *
 */
static void
m_closetab(GtkWidget *menu_item, gpointer callback_data)
{
  gu_window_t *gw = callback_data;
  gu_tab_close(gw->gw_current_tab);
}


/**
 *
 */
static void
m_opendir(GtkWidget *menu_item, gpointer callback_data)
{
  gu_window_t *gw = callback_data;
  GtkWidget *dialog;

  dialog = gtk_file_chooser_dialog_new("Open Directory",
				       GTK_WINDOW(gw->gw_window),
				       GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
				       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				       GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				       NULL);

  gtk_widget_show(dialog);

  g_signal_connect(G_OBJECT(dialog), "response",
		   G_CALLBACK(m_open_response), gw);
}




/**
 * File menu
 */
static GtkWidget *
gu_menubar_File(gu_window_t *gw, GtkAccelGroup *accel_group)
{
  GtkWidget *M = gtk_menu_item_new_with_mnemonic("_File");
  GtkWidget *m = gtk_menu_new();

  gtk_menu_set_accel_group(GTK_MENU(m), accel_group);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(M), m);


  gu_menu_add_item(m, "_New Window", 
		   m_openwindow, gw, GTK_STOCK_NEW,
		   gu_menu_accel_path("<Showtime-Main>/_File/NewWindow", 
				      GDK_N, GDK_CONTROL_MASK), TRUE);

  gu_menu_add_item(m, "New _Tab", 
		   m_opentab, gw, GTK_STOCK_NEW,
		   gu_menu_accel_path("<Showtime-Main>/_File/NewTab", 
				      GDK_T, GDK_CONTROL_MASK), TRUE);

  gu_menu_add_item(m, "_Open File...", 
		   m_openfile, gw, GTK_STOCK_OPEN,
		   gu_menu_accel_path("<Showtime-Main>/_File/OpenFile", 
				      GDK_O, GDK_CONTROL_MASK), TRUE);
  
  gu_menu_add_item(m, "_Open Directory...", 
		   m_opendir, gw, GTK_STOCK_OPEN,
		   gu_menu_accel_path("<Showtime-Main>/_File/OpenDir", 
				      GDK_D, GDK_CONTROL_MASK), TRUE);

  gu_menu_add_item(m, "Close Win_dow", 
		   m_closewindow, gw, GTK_STOCK_CLOSE,
		   gu_menu_accel_path("<Showtime-Main>/_File/CloseWindow", 
				      GDK_W, GDK_SHIFT_MASK | 
				      GDK_CONTROL_MASK), TRUE);

  gu_menu_add_item(m, "_Close Tab", 
		   m_closetab, gw, GTK_STOCK_CLOSE,
		   gu_menu_accel_path("<Showtime-Main>/_File/CloseTab", 
				      GDK_W, GDK_CONTROL_MASK), TRUE);
  
  gu_menu_add_sep(m);

  gu_menu_add_item(m, "_Quit", 
		   m_quit, gw, GTK_STOCK_QUIT,
		   gu_menu_accel_path("<Showtime-Main>/_File/Quit", 
				      GDK_Q, GDK_CONTROL_MASK), TRUE);
  return M;
}


/**
 *
 */
static void
m_toggle_playdeck(GtkCheckMenuItem *w, gpointer data)
{
  gu_window_t *gw = data;
  gw->gw_view_playdeck = gtk_check_menu_item_get_active(w);
  g_object_set(G_OBJECT(gw->gw_playdeck), "visible", 
	       gw->gw_view_playdeck, NULL);
}



/**
 *
 */
static void
m_toggle_statusbar(GtkCheckMenuItem *w, gpointer data)
{
  gu_window_t *gw = data;
  gw->gw_view_statusbar = gtk_check_menu_item_get_active(w);
  g_object_set(G_OBJECT(gw->gw_statusbar), "visible", 
	       gw->gw_view_statusbar, NULL);
}


/**
 *
 */
static gboolean
state_event(GtkWidget *w, GdkEventWindowState *event, gpointer user_data)
{
  gboolean v = !!(event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(user_data), v);
  return FALSE;
}


/**
 *
 */
static void
m_toggle_fullscreen(GtkCheckMenuItem *w, gpointer data)
{
  gu_window_t *gw = data;
  if(gtk_check_menu_item_get_active(w)) {
    gtk_window_fullscreen(GTK_WINDOW(gw->gw_window));
  } else {
    gtk_window_unfullscreen(GTK_WINDOW(gw->gw_window));
  }
}


/**
 * Go menu
 */
static GtkWidget *
gu_menubar_View(gu_window_t *gw, GtkAccelGroup *accel_group)
{
  GtkWidget *M = gtk_menu_item_new_with_mnemonic("_View");
  GtkWidget *m = gtk_menu_new();
  GtkWidget *w;

  gtk_menu_set_accel_group(GTK_MENU(m), accel_group);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(M), m);

  gu_menu_add_toggle(m, "_Playdeck", 
		     m_toggle_playdeck, gw, gw->gw_view_playdeck,
		     NULL, TRUE);

  gu_menu_add_toggle(m, "_Statusbar", 
		     m_toggle_statusbar, gw, gw->gw_view_statusbar,
		     NULL, TRUE);

  gu_menu_add_sep(m);

  w = 
    gu_menu_add_toggle(m, "_Fullscreen", 
		       m_toggle_fullscreen, gw, gw->gw_view_statusbar,
		       gu_menu_accel_path("<Showtime-Main>/_View/_Fullscreen", 
					  GDK_F11, 0),
		       TRUE);
  
  g_signal_connect(G_OBJECT(gw->gw_window), "window-state-event",
		   G_CALLBACK(state_event), w);

  return M;
}


/**
 * Go menu
 */
static GtkWidget *
gu_menubar_Go(gu_window_t *gw, GtkAccelGroup *accel_group)
{
  GtkWidget *M = gtk_menu_item_new_with_mnemonic("_Go");
  GtkWidget *m = gtk_menu_new();

  gtk_menu_set_accel_group(GTK_MENU(m), accel_group);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(M), m);


  gu_menu_add_item(m, "_Playqueue", 
		   m_openplayqueue, gw, GTK_STOCK_EXECUTE,
		   gu_menu_accel_path("<Showtime-Main>/_Go/Playqueue", 
				      GDK_P, GDK_CONTROL_MASK), TRUE);
  return M;
}


/**
 * Help menu
 */
static GtkWidget *
gu_menubar_Help(gu_window_t *gw, GtkAccelGroup *accel_group)
{
  GtkWidget *M = gtk_menu_item_new_with_mnemonic("_Help");
  GtkWidget *m = gtk_menu_new();

  gtk_menu_set_accel_group(GTK_MENU(m), accel_group);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(M), m);

  gu_menu_add_item(m, "_About Showtime", 
		   m_about, gw, GTK_STOCK_ABOUT,
		   NULL, TRUE);
  return M;
}


/**
 *
 */
GtkWidget *
gu_menubar_add(gu_window_t *gw, GtkWidget *parent)
{
  GtkAccelGroup *accel_group;
  GtkWidget *menubar;

  accel_group = gtk_accel_group_new();
  menubar = gtk_menu_bar_new();
  gtk_box_pack_start(GTK_BOX(parent), menubar, FALSE, TRUE, 0);


  gtk_menu_shell_append(GTK_MENU_SHELL(menubar),
			gu_menubar_File(gw, accel_group));

  gtk_menu_shell_append(GTK_MENU_SHELL(menubar),
			gu_menubar_View(gw, accel_group));

  gtk_menu_shell_append(GTK_MENU_SHELL(menubar),
			gu_menubar_Go(gw, accel_group));

  gtk_menu_shell_append(GTK_MENU_SHELL(menubar),
			gu_menubar_Help(gw, accel_group));

  gtk_window_add_accel_group(GTK_WINDOW(gw->gw_window), accel_group);
  return menubar;
}




