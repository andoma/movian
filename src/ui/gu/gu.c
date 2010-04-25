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

#include <X11/Xlib.h>

hts_mutex_t gu_mutex;


/**
 *
 */
static void
gu_enter(void)
{
  hts_mutex_lock(&gu_mutex);
}


/**
 *
 */
static void
gu_leave(void)
{
  hts_mutex_unlock(&gu_mutex);
}


/**
 *
 */
static gboolean 
gu_close(GtkWidget *widget, GdkEvent  *event, gpointer data)
{
  showtime_shutdown(0);
  return TRUE;
}


/**
 *
 */
static int
gu_start(ui_t *ui, int argc, char **argv, int primary)
{
  GtkWidget *win;
  gtk_ui_t *gu = calloc(1, sizeof(gtk_ui_t));

  XInitThreads();

  hts_mutex_init(&gu_mutex);

  g_thread_init(NULL);

  gdk_threads_set_lock_functions(gu_enter, gu_leave);

  gdk_threads_init();
  gdk_threads_enter();

  gtk_init(&argc, &argv);

  gu_pixbuf_init();

  gu->gu_pc = prop_courier_create_thread(&gu_mutex, "GU");

  gu->gu_window = win = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_window_set_title(GTK_WINDOW(win), "Showtime");
  gtk_window_set_default_size(GTK_WINDOW(win), 640, 400);

  g_signal_connect(G_OBJECT(win), "delete_event",
		   G_CALLBACK(gu_close), NULL);


  gu->gu_vbox = gtk_vbox_new(FALSE, 1);
  gtk_container_add(GTK_CONTAINER(win), gu->gu_vbox);

  /* Menubar */
  gu->gu_menubar = gu_menubar_add(gu, gu->gu_vbox);
 
  /* Top Toolbar */
  gu->gu_toolbar = gu_toolbar_add(gu, gu->gu_vbox);

  /* Page container */
  gu->gu_page_container = gtk_vbox_new(FALSE, 0);
  gtk_container_set_border_width(GTK_CONTAINER(gu->gu_page_container), 0);
  gtk_box_pack_start(GTK_BOX(gu->gu_vbox), gu->gu_page_container, TRUE, TRUE, 0);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "nav", "pages"),
		 PROP_TAG_CALLBACK, gu_nav_pages, gu, 
		 PROP_TAG_COURIER, gu->gu_pc,
		 NULL);

  /* Playback controls */
  gu->gu_playdeck = gu_playdeck_add(gu, gu->gu_vbox);

  /* Statusbar */
  gu->gu_statusbar = gu_statusbar_add(gu, gu->gu_vbox);

  /* Init popup controller */
  gu_popup_init(gu);

  gtk_widget_show_all(win);
  gtk_main();
  return 0;
}


/**
 *
 */
void
gu_fullwindow_update(gtk_ui_t *gu)
{
  int req = gu->gu_page_current ? gu->gu_page_current->gnp_fullwindow : 0;
  
  if(req == gu->gu_fullwindow)
    return;

  gu->gu_fullwindow = req;
  if(gu->gu_menubar != NULL)
    g_object_set(G_OBJECT(gu->gu_menubar), "visible", !req, NULL);

  if(gu->gu_toolbar != NULL)
    g_object_set(G_OBJECT(gu->gu_toolbar), "visible", !req, NULL);

  if(gu->gu_playdeck != NULL)
    g_object_set(G_OBJECT(gu->gu_playdeck), "visible", !req, NULL);

  if(gu->gu_statusbar != NULL)
    g_object_set(G_OBJECT(gu->gu_statusbar), "visible", !req, NULL);
}


/**
 *
 */
static void
gu_dispatch_event(uii_t *uii, event_t *e)
{
  return;
}



/**
 *
 */
ui_t gu_ui = {
  .ui_title = "gu",
  .ui_start = gu_start,
  .ui_dispatch_event = gu_dispatch_event,
  .ui_flags = UI_SINGLETON,
};


