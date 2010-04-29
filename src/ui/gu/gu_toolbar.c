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

#include <string.h>
#include "navigator.h"
#include "gu.h"
#include "showtime.h"

typedef struct toolbar {

  gu_window_t *gw;

  char *parent_url;

  prop_sub_t *sub_canGoBack;
  prop_sub_t *sub_canGoFwd;
  prop_sub_t *sub_canGoHome;
  prop_sub_t *sub_parent;
  prop_sub_t *sub_url;

  GtkToolItem *up;

} toolbar_t;


/**
 *
 */
static void
back_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
  gu_window_t *gw = user_data;
  gu_nav_send_event(gw, event_create_action(ACTION_NAV_BACK));
}


/**
 *
 */
static void
fwd_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
  gu_window_t *gw = user_data;
  gu_nav_send_event(gw, event_create_action(ACTION_NAV_FWD));
}


/**
 *
 */
static void
home_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
  gu_window_t *gw = user_data;
  gu_nav_send_event(gw, event_create_action(ACTION_HOME));
}


/**
 *
 */
static void
gu_nav_url_updated(void *opaque, const char *str)
{
  gu_window_t *gw = opaque;
  gtk_entry_set_text(GTK_ENTRY(gw->gw_url), str ?: "");
}


/**
 *
 */
static void
gu_nav_url_set(GtkEntry *e, gpointer user_data)
{
  gu_window_t *gw = user_data;
  gu_nav_open(gw, gtk_entry_get_text(e), NULL, NULL);
}


/**
 *
 */
static void
set_go_up(void *opaque, const char *str)
{
  toolbar_t *t = opaque;

  free(t->parent_url);

  gtk_widget_set_sensitive(GTK_WIDGET(t->up), !!str);
  t->parent_url = str ? strdup(str) : NULL;
}

/**
 *
 */
static void
up_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
  toolbar_t *t = user_data;
  if(t->parent_url != NULL)
    gu_nav_open(t->gw, t->parent_url, NULL, NULL);
}


/**
 *
 */
static void
toolbar_dtor(GtkObject *object, gpointer user_data)
{
  toolbar_t *t = user_data;

  prop_unsubscribe(t->sub_canGoBack);
  prop_unsubscribe(t->sub_canGoFwd);
  prop_unsubscribe(t->sub_canGoHome);
  prop_unsubscribe(t->sub_parent);
  prop_unsubscribe(t->sub_url);
  free(t->parent_url);
  free(t);
}

/**
 *
 */
GtkWidget *
gu_toolbar_add(gu_window_t *gw, GtkWidget *parent)
{
  toolbar_t *t = calloc(1, sizeof(toolbar_t));
  GtkWidget *toolbar;
  GtkWidget *w;
  prop_courier_t *pc = gw->gw_gu->gu_pc;
  GtkToolItem *ti;

  t->gw = gw;

  /* Top Toolbar */
  toolbar = gtk_toolbar_new();
  gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
  gtk_box_pack_start(GTK_BOX(parent), toolbar, FALSE, TRUE, 0);

  /* Back button */
  ti = gtk_tool_button_new_from_stock(GTK_STOCK_GO_BACK);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ti, -1);
  g_signal_connect(G_OBJECT(ti), "clicked", G_CALLBACK(back_clicked), gw);

  t->sub_canGoBack =
    prop_subscribe(0,
		   PROP_TAG_NAME("nav", "canGoBack"),
		   PROP_TAG_CALLBACK_INT, gu_subscription_set_sensitivity, ti,
		   PROP_TAG_COURIER, pc,
		   PROP_TAG_NAMED_ROOT, gw->gw_nav, "nav",
		   NULL);
  gtk_widget_show(GTK_WIDGET(ti));

  /* Forward button */
  ti = gtk_tool_button_new_from_stock(GTK_STOCK_GO_FORWARD);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ti, -1);
  g_signal_connect(G_OBJECT(ti), "clicked", G_CALLBACK(fwd_clicked), gw);

  t->sub_canGoFwd =
    prop_subscribe(0,
		   PROP_TAG_NAME("nav", "canGoForward"),
		   PROP_TAG_CALLBACK_INT, gu_subscription_set_sensitivity, ti,
		   PROP_TAG_COURIER, pc,
		   PROP_TAG_NAMED_ROOT, gw->gw_nav, "nav",
		   NULL);
  gtk_widget_show(GTK_WIDGET(ti));
  
  /* Up button */
  t->up = gtk_tool_button_new_from_stock(GTK_STOCK_GO_UP);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), t->up, -1);
  g_signal_connect(G_OBJECT(t->up), "clicked", G_CALLBACK(up_clicked), t);

  t->sub_parent =
    prop_subscribe(0,
		   PROP_TAG_NAME("nav", "currentpage", "parent"),
		   PROP_TAG_CALLBACK_STRING, set_go_up, t,
		   PROP_TAG_COURIER, pc,
		   PROP_TAG_NAMED_ROOT, gw->gw_nav, "nav",
		   NULL);
  gtk_widget_show(GTK_WIDGET(t->up));

  /* Home button */
  ti = gtk_tool_button_new_from_stock(GTK_STOCK_HOME);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ti, -1);
  g_signal_connect(G_OBJECT(ti), "clicked", G_CALLBACK(home_clicked), gw);

  t->sub_canGoHome =
    prop_subscribe(0,
		   PROP_TAG_NAME("nav", "canGoHome"),
		   PROP_TAG_CALLBACK_INT, gu_subscription_set_sensitivity, ti,
		   PROP_TAG_COURIER, pc,
		   PROP_TAG_NAMED_ROOT, gw->gw_nav, "nav",
		   NULL);
  gtk_widget_show(GTK_WIDGET(ti));

  /* URL entry */
  ti = gtk_tool_item_new();
  gw->gw_url = w = gtk_entry_new();

  g_signal_connect(G_OBJECT(w), "activate", G_CALLBACK(gu_nav_url_set), gw);

  gtk_container_add(GTK_CONTAINER(ti), w);
  gtk_tool_item_set_expand(ti, TRUE);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ti, -1);

  t->sub_url =
    prop_subscribe(0,
		   PROP_TAG_NAME("nav", "currentpage", "url"),
		   PROP_TAG_CALLBACK_STRING, gu_nav_url_updated, gw,
		   PROP_TAG_COURIER, pc,
		   PROP_TAG_NAMED_ROOT, gw->gw_nav, "nav",
		   NULL);
  gtk_widget_show_all(GTK_WIDGET(ti));

  g_signal_connect(toolbar, "destroy", G_CALLBACK(toolbar_dtor), t);

  return toolbar;
}

