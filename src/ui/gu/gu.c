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
#include <assert.h>

#include "showtime.h"
#include "navigator.h"
#include "gu.h"
#include "event.h"

#include "arch/linux/linux.h"

#include <gdk/gdkkeysyms.h>
#include <X11/Xlib.h>
#include <gtk/gtk.h>

static void gu_tab_destroy(gu_tab_t *gt);

/**
 *
 */
static gboolean
window_key_pressed(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
  gu_window_t *gw = user_data;

  if(event->keyval == GDK_F12)
    event_dispatch(event_create_action(ACTION_SWITCH_UI));

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
  gu_tab_t *gt;
  while((gt = LIST_FIRST(&gw->gw_tabs)) != NULL)
    gu_tab_destroy(gt);

  if(gw->gw_gu->gu_last_focused == gw)
    gw->gw_gu->gu_last_focused = NULL;

  gtk_widget_destroy(gw->gw_window);
  LIST_REMOVE(gw, gw_link);
  free(gw);
}


/**
 *
 */
static gboolean 
gw_close(GtkWidget *widget,GdkEvent *event, gpointer data)
{
  gu_window_t *gw = data;
  gtk_ui_t *gu = gw->gw_gu;
  gu_win_destroy(gw);

  if(LIST_FIRST(&gu->gu_windows) == NULL)
    showtime_shutdown(0);

  return TRUE;
}


/**
 *
 */
static gboolean 
gw_focused(GtkWidget *widget,GdkEvent *event, gpointer data)
{
  gu_window_t *gw = data;
  gtk_ui_t *gu = gw->gw_gu;
  gu->gu_last_focused = gw;
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
gu_win_create(gtk_ui_t *gu, int all, prop_t *nav)
{
  gu_window_t *gw = calloc(1, sizeof(gu_window_t));

  LIST_INSERT_HEAD(&gu->gu_windows, gw, gw_link);

  gw->gw_gu = gu;

  gw->gw_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_widget_set_events(gw->gw_window, GDK_FOCUS_CHANGE_MASK);

  gtk_window_set_title(GTK_WINDOW(gw->gw_window), "Showtime");
  gtk_window_set_default_size(GTK_WINDOW(gw->gw_window), 640, 400);

  g_signal_connect(G_OBJECT(gw->gw_window), "delete_event",
		   G_CALLBACK(gw_close), gw);

  g_signal_connect(G_OBJECT(gw->gw_window), "focus-in-event",
		   G_CALLBACK(gw_focused), gw);

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

  gu_tab_create(gw, 1, nav);

  gtk_widget_show(gw->gw_window);

  return gw;
}



/**
 *
 */
static void
gu_tab_destroy(gu_tab_t *gt)
{
  gtk_widget_destroy(gt->gt_notebook);
  prop_destroy(gt->gt_nav);
  gt->gt_gw->gw_ntabs--;
  LIST_REMOVE(gt, gt_link);
  free(gt);
}

/**
 *
 */
void
gu_tab_close(gu_tab_t *gt)
{
  gu_window_t *gw = gt->gt_gw;

  gu_tab_destroy(gt);

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
  prop_courier_t *pc = glibcourier;
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
gu_tab_create(gu_window_t *gw, int select, prop_t *nav)
{
  gu_tab_t *gt = calloc(1, sizeof(gu_tab_t));
  prop_sub_t *s;
  int idx;

  gt->gt_gw = gw;

  LIST_INSERT_HEAD(&gw->gw_tabs, gt, gt_link);
  gw->gw_current_tab = gt;
  gw->gw_ntabs++;

  gt->gt_nav = nav ?: nav_spawn();
  if(prop_set_parent(gt->gt_nav, prop_get_global()))
    abort();

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
		     PROP_TAG_COURIER, glibcourier,
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
#if 0

#include <webkit/webkit.h>

static void
webkit_test(void)
{
/* Create the widgets */
GtkWidget *main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
GtkWidget *scrolled_window = gtk_scrolled_window_new (NULL, NULL);
GtkWidget *web_view = webkit_web_view_new ();

/* Place the WebKitWebView in the GtkScrolledWindow */
gtk_container_add (GTK_CONTAINER (scrolled_window), web_view);
gtk_container_add (GTK_CONTAINER (main_window), scrolled_window);

/* Open a webpage */
webkit_web_view_load_uri (WEBKIT_WEB_VIEW (web_view), "http://www.gnome.org");

/* Show the result */
gtk_window_set_default_size (GTK_WINDOW (main_window), 800, 600);
gtk_widget_show_all (main_window);

}
#endif


/**
 *
 */
static void *
gu_start(struct prop *nav)
{
  gu_pixbuf_init();

  gtk_ui_t *gu = calloc(1, sizeof(gtk_ui_t));

  gu_win_create(gu, 1, nav);

  /* Init popup controller */
  gu_popup_init(gu);

  return gu;
}


/**
 *
 */
static struct prop *
gu_stop(void *ui)
{
  gtk_ui_t *gu = ui;

  gu_window_t *gw;
  prop_t *nav = NULL;

  gu_popup_fini(gu);

  // Try to find the last/currently focused navigator and return that

  gw = gu->gu_last_focused ?: LIST_FIRST(&gu->gu_windows);

  if(gw != NULL)  {
    nav = gw->gw_current_tab->gt_nav;
    gw->gw_current_tab->gt_nav = NULL; // Don't destroy it below
  }

  // Destroy all windows (and tabs)

  while((gw = LIST_FIRST(&gu->gu_windows)) != NULL)
    gu_win_destroy(gw);

  return nav;
}


/**
 *
 */
const linux_ui_t ui_gu = {
  .start = gu_start,
  .stop  = gu_stop,
};


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
  gu_tab_send_event(gt, event_create_openurl(url, NULL, NULL, NULL, NULL));
}


/**
 *
 */
void
gu_nav_open_newwin(gtk_ui_t *gu, const char *url)
{
  gu_window_t *gw = gu_win_create(gu, 0, NULL);
  gu_tab_send_event(gw->gw_current_tab,
		    event_create_openurl(url, NULL, NULL, NULL, NULL));
}


/**
 *
 */
void
gu_nav_open_newtab(gu_window_t *gw, const char *url)
{
  gu_tab_t *gt = gu_tab_create(gw, 0, NULL);
  gu_tab_send_event(gt, event_create_openurl(url, NULL, NULL, NULL, NULL));
}
