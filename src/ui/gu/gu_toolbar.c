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

static char *up_url; // XXX VERY UGLY

/**
 *
 */
static void
back_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
  event_dispatch(event_create_action(ACTION_NAV_BACK));
}


/**
 *
 */
static void
fwd_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
  event_dispatch(event_create_action(ACTION_NAV_FWD));
}


/**
 *
 */
static void
home_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
  event_dispatch(event_create_action(ACTION_HOME));
}


/**
 *
 */
static void
gu_nav_url_updated(void *opaque, const char *str)
{
  gtk_ui_t *gu = opaque;
  gtk_entry_set_text(GTK_ENTRY(gu->gu_url), str ?: "");
}


/**
 *
 */
static void
gu_nav_url_set(GtkEntry *e, gpointer user_data)
{
  event_dispatch(event_create_openurl(gtk_entry_get_text(e), NULL, NULL));
}


/**
 *
 */
static void
set_go_up(void *opaque, const char *str)
{
  GtkWidget *w = opaque;

  free(up_url);

  if(str == NULL) {
    gtk_widget_set_sensitive(w, FALSE);
    up_url = NULL;
    return;
  }

  up_url = strdup(str);
  gtk_widget_set_sensitive(w, TRUE);
}

/**
 *
 */
static void
up_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
  if(up_url != NULL)
    event_dispatch(event_create_openurl(up_url, NULL, NULL));
}


/**
 *
 */
GtkWidget *
gu_toolbar_add(gtk_ui_t *gu, GtkWidget *parent)
{
  GtkToolItem *ti;
  GtkWidget *toolbar;
  GtkWidget *w;

  /* Top Toolbar */
  toolbar = gtk_toolbar_new();
  gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
  gtk_box_pack_start(GTK_BOX(parent), toolbar, FALSE, TRUE, 0);

  /* Back button */
  ti = gtk_tool_button_new_from_stock(GTK_STOCK_GO_BACK);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ti, -1);
  g_signal_connect(G_OBJECT(ti), "clicked", G_CALLBACK(back_clicked), gu);
  prop_subscribe(0,
		 PROP_TAG_NAME("global", "nav", "canGoBack"),
		 PROP_TAG_CALLBACK_INT, gu_subscription_set_sensitivity, ti,
		 PROP_TAG_COURIER, gu->gu_pc,
		 NULL);

  /* Forward button */
  ti = gtk_tool_button_new_from_stock(GTK_STOCK_GO_FORWARD);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ti, -1);
  g_signal_connect(G_OBJECT(ti), "clicked", G_CALLBACK(fwd_clicked), gu);
  prop_subscribe(0,
		 PROP_TAG_NAME("global", "nav", "canGoForward"),
		 PROP_TAG_CALLBACK_INT, gu_subscription_set_sensitivity, ti,
		 PROP_TAG_COURIER, gu->gu_pc,
		 NULL);

  /* Up button */
  ti = gtk_tool_button_new_from_stock(GTK_STOCK_GO_UP);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ti, -1);
  g_signal_connect(G_OBJECT(ti), "clicked", G_CALLBACK(up_clicked), gu);
  prop_subscribe(0,
		 PROP_TAG_NAME("global", "nav", "currentpage", "parent"),
		 PROP_TAG_CALLBACK_STRING, set_go_up, ti,
		 PROP_TAG_COURIER, gu->gu_pc,
		 NULL);

  /* Home button */
  ti = gtk_tool_button_new_from_stock(GTK_STOCK_HOME);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ti, -1);
  g_signal_connect(G_OBJECT(ti), "clicked", G_CALLBACK(home_clicked), gu);

  /* URL entry */
  ti = gtk_tool_item_new();
  gu->gu_url = w = gtk_entry_new();

  g_signal_connect(G_OBJECT(w), "activate", 
		   G_CALLBACK(gu_nav_url_set), gu);

  gtk_container_add(GTK_CONTAINER(ti), w);
  gtk_tool_item_set_expand(ti, TRUE);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ti, -1);

  prop_subscribe(0,
		 PROP_TAG_NAME("global", "nav", "currentpage", "url"),
		 PROP_TAG_CALLBACK_STRING, gu_nav_url_updated, gu,
		 PROP_TAG_COURIER, gu->gu_pc,
		 NULL);

  return toolbar;
}

