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
window_key_pressed(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
  gu_window_t *gw = user_data;

  if(event->keyval == GDK_F11) {

    if(gw->gw_fullwindow) {
      if(gw->gw_fullscreen) {
	gtk_window_unfullscreen(GTK_WINDOW(gw->gw_window));
      } else {
	gtk_window_fullscreen(GTK_WINDOW(gw->gw_window));
      }
    }
  }
  return FALSE;
}


/**
 *
 */
static gboolean
window_state_event(GtkWidget *w, GdkEventWindowState *event, gpointer user_data)
{
  gu_window_t *gw = user_data;
  gboolean v = !!(event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN);
  gw->gw_fullscreen = v;
  return FALSE;
}



/**
 *
 */
void
gu_win_destroy(gu_window_t *gw)
{
  gtk_ui_t *gu = gw->gw_gu;

  gtk_widget_destroy(gw->gw_window);

  prop_destroy(gw->gw_nav);

  LIST_REMOVE(gw, gw_link);
  free(gw);

  if(LIST_FIRST(&gu->gu_windows) == NULL)
    showtime_shutdown(0);
}


/**
 *
 */
static gboolean 
gw_close(GtkWidget *widget,GdkEvent *event, gpointer data)
{
  gu_win_destroy(data);
  return TRUE;
}

/**
 *
 */
gu_window_t *
gu_win_create(gtk_ui_t *gu, prop_t *nav, int all)
{
  gu_window_t *gw = calloc(1, sizeof(gu_window_t));

  if(nav == NULL) {
    // No navigator supplied, spawn one
    gw->gw_nav = nav_spawn();
  } else {
    gw->gw_nav = prop_xref_addref(nav);
  }

  LIST_INSERT_HEAD(&gu->gu_windows, gw, gw_link);

  gw->gw_gu = gu;

  gw->gw_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_window_set_title(GTK_WINDOW(gw->gw_window), "Showtime");
  gtk_window_set_default_size(GTK_WINDOW(gw->gw_window), 640, 400);

  g_signal_connect(G_OBJECT(gw->gw_window), "delete_event",
		   G_CALLBACK(gw_close), gw);

  gw->gw_vbox = gtk_vbox_new(FALSE, 1);
  gtk_container_add(GTK_CONTAINER(gw->gw_window), gw->gw_vbox);
  gtk_widget_show(gw->gw_vbox);

  gw->gw_view_toolbar = 1;
  if(all) {
    gw->gw_view_playdeck = 1;
    gw->gw_view_statusbar = 1;
  }

  /* Menubar */
  gw->gw_menubar = gu_menubar_add(gw, gw->gw_vbox);
  gtk_widget_show_all(gw->gw_menubar);
 
  /* Top Toolbar */
  gw->gw_toolbar = gu_toolbar_add(gw, gw->gw_vbox);
  if(gw->gw_view_toolbar)
    gtk_widget_show(gw->gw_toolbar);

  /* Page container */
  gw->gw_page_container = gtk_vbox_new(FALSE, 0);
  gtk_widget_show_all(gw->gw_page_container);
  gtk_container_set_border_width(GTK_CONTAINER(gw->gw_page_container), 0);
  gtk_box_pack_start(GTK_BOX(gw->gw_vbox),
		     gw->gw_page_container, TRUE, TRUE, 0);

  prop_sub_t *s = 
    prop_subscribe(0,
		   PROP_TAG_NAME("nav", "pages"),
		   PROP_TAG_CALLBACK, gu_nav_pages, gw, 
		   PROP_TAG_COURIER, gu->gu_pc,
		   PROP_TAG_NAMED_ROOT, gw->gw_nav, "nav",
		   NULL);

  gu_unsubscribe_on_destroy(GTK_OBJECT(gw->gw_vbox), s);

  /* Playback controls */
  gw->gw_playdeck = gu_playdeck_add(gw, gw->gw_vbox);
  if(gw->gw_view_playdeck)
    gtk_widget_show(gw->gw_playdeck);

  /* Statusbar */
  gw->gw_statusbar = gu_statusbar_add(gw, gw->gw_vbox);
  if(gw->gw_view_statusbar)
    gtk_widget_show(gw->gw_statusbar);

  g_signal_connect(G_OBJECT(gw->gw_window), "key-press-event",
		   G_CALLBACK(window_key_pressed), gw);

  g_signal_connect(G_OBJECT(gw->gw_window), "window-state-event",
		   G_CALLBACK(window_state_event), gw);

  gtk_widget_show(gw->gw_window);

  return gw;
}


/**
 *
 */
static int
gu_start(ui_t *ui, int argc, char **argv, int primary)
{
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

  gu_win_create(gu, prop_create(prop_get_global(), "nav"), 1);

  /* Init popup controller */
  gu_popup_init(gu);

  gtk_main();
  return 0;
}


/**
 *
 */
void
gu_fullwindow_update(gu_window_t *gw)
{
  int req = gw->gw_page_current ? gw->gw_page_current->gnp_fullwindow : 0;
  
  if(req == gw->gw_fullwindow)
    return;

  gw->gw_fullwindow = req;
  if(gw->gw_menubar != NULL)
    g_object_set(G_OBJECT(gw->gw_menubar), "visible",
		 req ? 0 : 1, NULL);

  if(gw->gw_toolbar != NULL)
    g_object_set(G_OBJECT(gw->gw_toolbar), "visible", 
		 req ? 0 : gw->gw_view_toolbar, NULL);

  if(gw->gw_playdeck != NULL)
    g_object_set(G_OBJECT(gw->gw_playdeck), "visible", 
		 req ? 0 : gw->gw_view_playdeck, NULL);

  if(gw->gw_statusbar != NULL)
    g_object_set(G_OBJECT(gw->gw_statusbar), "visible", 
		 req ? 0 : gw->gw_view_statusbar, NULL);
}


/**
 *
 */
void
gu_nav_send_event(gu_window_t *gw, event_t *e)
{
  prop_t *p = prop_get_by_name(PNVEC("nav", "eventsink"), 1,
			       PROP_TAG_NAMED_ROOT, gw->gw_nav, "nav",
			       NULL);

  prop_send_ext_event(p, e);
  prop_ref_dec(p);
  event_unref(e);
}


/**
 *
 */
void
gu_nav_open(gu_window_t *gw, const char *url, const char *type, prop_t *psource)
{
  gu_nav_send_event(gw, event_create_openurl(url, type, psource));
}


/**
 *
 */
void
gu_nav_open_newwin(gtk_ui_t *gu, const char *url, const char *type,
		   prop_t *psource)
{
  gu_window_t *gw = gu_win_create(gu, NULL, 0);
  gu_nav_send_event(gw, event_create_openurl(url, type, psource));
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


