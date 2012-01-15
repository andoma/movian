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
#include <assert.h>

#include "showtime.h"
#include "navigator.h"
#include "gu.h"
#include "event.h"

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
static gu_tab_t *
resolvetab(gu_window_t *gw, int page_num)
{
  GtkWidget *c;
  gu_tab_t *gt;
  c = gtk_notebook_get_nth_page(GTK_NOTEBOOK(gw->gw_notebook), page_num);
  LIST_FOREACH(gt, &gw->gw_tabs, gt_link)
    if(c == gt->gt_vbox)
      return gt;
  return NULL;

}


/**
 *
 */
static void
tab_changed(GtkWidget *w, GtkNotebookPage *page,
	    guint page_num, gpointer user_data)
{
  gu_window_t *gw = user_data;
  gu_tab_t *gt = resolvetab(gw, page_num);
  assert(gt != NULL);
  gw->gw_current_tab = gt;
}


/**
 *
 */
gu_window_t *
gu_win_create(gtk_ui_t *gu, prop_t *nav, int all)
{
  gu_window_t *gw = calloc(1, sizeof(gu_window_t));

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
 
  /* Notebook (that which contains tabs) */
  gw->gw_notebook = gtk_notebook_new();
  gtk_box_pack_start(GTK_BOX(gw->gw_vbox),
		     gw->gw_notebook, TRUE, TRUE, 0);
  gtk_notebook_set_show_border(GTK_NOTEBOOK(gw->gw_notebook), 0);
  gtk_notebook_set_show_tabs(GTK_NOTEBOOK(gw->gw_notebook), 0);
  gtk_notebook_set_scrollable(GTK_NOTEBOOK(gw->gw_notebook), 1);

  gtk_widget_show(gw->gw_notebook);

  g_signal_connect(G_OBJECT(gw->gw_notebook), "switch-page",
		   G_CALLBACK(tab_changed), gw);

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

  gu_tab_create(gw, nav, 1);

  gtk_widget_show(gw->gw_window);

  return gw;
}

/**
 *
 */
void
gu_tab_destroy(gu_tab_t *gt)
{
  gu_window_t *gw = gt->gt_gw;

  gtk_widget_destroy(gt->gt_notebook);

  prop_destroy(gt->gt_nav);

  LIST_REMOVE(gt, gt_link);
  free(gt);

  gw->gw_ntabs--;

  if(gw->gw_ntabs == 0) {
    gu_win_destroy(gw);
  } else if(gw->gw_ntabs < 2) {
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(gw->gw_notebook), 0);
  }
}

/**
 *
 */
static void
build_tab_header(gu_tab_t *gt)
{
  prop_courier_t *pc = gt->gt_gw->gw_gu->gu_pc;
  GtkWidget *l, *img;
  prop_sub_t *s;

  // Tab header
  gt->gt_label = gtk_hbox_new(FALSE, 0);
  gtk_widget_set_size_request(gt->gt_label, 150, -1);

  img = gtk_image_new();
  gtk_box_pack_start(GTK_BOX(gt->gt_label), img, FALSE, TRUE, 0);

  s = prop_subscribe(PROP_SUB_DIRECT_UPDATE,
		     PROP_TAG_NAME("nav", "currentpage", 
				   "model", "type"),
		     PROP_TAG_CALLBACK_STRING, gu_set_icon_by_type, img,
		     PROP_TAG_COURIER, pc, 
		     PROP_TAG_NAMED_ROOT, gt->gt_nav, "nav",
		     NULL);

  gu_unsubscribe_on_destroy(GTK_OBJECT(img), s);

  l = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(gt->gt_label), l, TRUE, TRUE, 0);

  gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);
  gtk_misc_set_alignment(GTK_MISC(l), 0, 0.5);
  gtk_misc_set_padding(GTK_MISC(l), 5, 0);

  s = prop_subscribe(PROP_SUB_DIRECT_UPDATE,
		     PROP_TAG_NAME("nav", "currentpage", 
				   "model", "metadata", "title"),
		     PROP_TAG_CALLBACK_STRING, gu_subscription_set_label, l,
		     PROP_TAG_COURIER, pc,
		     PROP_TAG_NAMED_ROOT, gt->gt_nav, "nav",
		     NULL);
  gu_unsubscribe_on_destroy(GTK_OBJECT(l), s);

  gtk_widget_show_all(gt->gt_label);
}


/**
 *
 */
gu_tab_t *
gu_tab_create(gu_window_t *gw, prop_t *nav, int select)
{
  gu_tab_t *gt = calloc(1, sizeof(gu_tab_t));
  prop_sub_t *s;
  int idx;

  gt->gt_gw = gw;

  LIST_INSERT_HEAD(&gw->gw_tabs, gt, gt_link);
  gw->gw_current_tab = gt;
  gw->gw_ntabs++;

  if(nav == NULL) {
    gt->gt_nav = nav_spawn(); // No navigator supplied, spawn one
  } else {
    gt->gt_nav = prop_xref_addref(nav);
  }

  gt->gt_vbox = gtk_vbox_new(FALSE, 1);
  gtk_widget_show(gt->gt_vbox);

  gt->gt_notebook = gtk_notebook_new();
  gtk_widget_show(gt->gt_notebook);

  gtk_notebook_set_show_border(GTK_NOTEBOOK(gt->gt_notebook), 0);
  gtk_notebook_set_show_tabs(GTK_NOTEBOOK(gt->gt_notebook), 0);
  
  gu_toolbar_add(gt, gt->gt_vbox);
  gtk_container_add(GTK_CONTAINER(gt->gt_vbox), gt->gt_notebook);

  build_tab_header(gt);

  s = prop_subscribe(0,
		     PROP_TAG_NAME("nav", "pages"),
		     PROP_TAG_CALLBACK, gu_nav_pages, gt,
		     PROP_TAG_COURIER, gw->gw_gu->gu_pc,
		     PROP_TAG_NAMED_ROOT, gt->gt_nav, "nav",
		     NULL);

  gu_unsubscribe_on_destroy(GTK_OBJECT(gt->gt_notebook), s);


  // Add to tab's notebook

  idx = gtk_notebook_append_page(GTK_NOTEBOOK(gw->gw_notebook),
				 gt->gt_vbox, gt->gt_label);

  gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(gw->gw_notebook), 
				   gt->gt_vbox, 1);


  if(select)
    gtk_notebook_set_current_page(GTK_NOTEBOOK(gw->gw_notebook), idx);

  if(gw->gw_ntabs > 1)
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(gw->gw_notebook), 1);
  

  return gt;
}



/**
 *
 */
static int
gu_start(ui_t *ui, prop_t *root, int argc, char **argv, int primary)
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
  gu_tab_t *gt = gw->gw_current_tab;

  int req = gt->gt_page_current ? gt->gt_page_current->gnp_fullwindow : 0;
  
  if(req == gw->gw_fullwindow)
    return;

  gw->gw_fullwindow = req;
  if(gw->gw_menubar != NULL)
    g_object_set(G_OBJECT(gw->gw_menubar), "visible",
		 req ? 0 : 1, NULL);
#if 0
  if(gw->gw_toolbar != NULL)
    g_object_set(G_OBJECT(gw->gw_toolbar), "visible", 
		 req ? 0 : gw->gw_view_toolbar, NULL);
#endif

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
static void
gu_playqueue_send_event(gu_tab_t *gt, struct event *e)
{
  prop_t *p = prop_get_by_name(PNVEC("global", "playqueue", "eventsink"), 1,
			       NULL);

  prop_send_ext_event(p, e);
  prop_ref_dec(p);
  event_release(e);
}


/**
 *
 */
void
gu_tab_play_track(gu_tab_t *gt, prop_t *track, prop_t *source)
{
  gu_playqueue_send_event(gt, event_create_playtrack(track, source, 0));
}


/**
 *
 */
void
gu_tab_send_event(gu_tab_t *gt, event_t *e)
{
  prop_t *p = prop_get_by_name(PNVEC("nav", "eventsink"), 1,
			       PROP_TAG_NAMED_ROOT, gt->gt_nav, "nav",
			       NULL);

  prop_send_ext_event(p, e);
  prop_ref_dec(p);
  event_release(e);
}


/**
 *
 */
void
gu_tab_open(gu_tab_t *gt, const char *url)
{
  gu_tab_send_event(gt, event_create_openurl(url, NULL, NULL));
}


/**
 *
 */
void
gu_nav_open_newwin(gtk_ui_t *gu, const char *url)
{
  gu_window_t *gw = gu_win_create(gu, NULL, 0);
  gu_tab_send_event(gw->gw_current_tab, event_create_openurl(url, NULL, NULL));
}


/**
 *
 */
void
gu_nav_open_newtab(gu_window_t *gw, const char *url)
{
  gu_tab_t *gt = gu_tab_create(gw, NULL, 0);
  gu_tab_send_event(gt, event_create_openurl(url, NULL, NULL));
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
