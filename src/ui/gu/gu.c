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
  GtkWidget *vbox;
  gtk_ui_t *gu = calloc(1, sizeof(gtk_ui_t));

  hts_mutex_init(&gu_mutex);

  g_thread_init(NULL);

  gdk_threads_set_lock_functions(gu_enter, gu_leave);

  gdk_threads_init();
  gdk_threads_enter();

  gtk_init(&argc, &argv);

  gu_pixbuf_init();

  gu->gu_pc = prop_courier_create(&gu_mutex, PROP_COURIER_THREAD, "GU");

  gu->gu_window = win = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_window_set_title(GTK_WINDOW(win), "Showtime");
  gtk_window_set_default_size(GTK_WINDOW(win), 640, 400);

  g_signal_connect(G_OBJECT(win), "delete_event",
		   G_CALLBACK(gu_close), NULL);


  vbox = gtk_vbox_new(FALSE, 1);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 1);
  gtk_container_add(GTK_CONTAINER(win), vbox);

  /* Menubar */
  gu_menubar_add(gu, vbox);
 
  /* Top Toolbar */
  gu_toolbar_add(gu, vbox);

  /* Page container */
  gu->gu_page_container = gtk_vbox_new(FALSE, 0);
  gtk_container_set_border_width(GTK_CONTAINER(gu->gu_page_container), 0);
  gtk_box_pack_start(GTK_BOX(vbox), gu->gu_page_container, TRUE, TRUE, 0);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "nav", "pages"),
		 PROP_TAG_CALLBACK, gu_nav_pages, gu, 
		 PROP_TAG_COURIER, gu->gu_pc,
		 NULL);

  /* Playback controls */
  gu_playdeck_add(gu, vbox);

  /* Statusbar */
  gu_statusbar_add(gu, vbox);

  /* Init popup controller */
  gu_popup_init(gu);

  gtk_widget_show_all(win);
  gtk_main();
  return 0;
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


