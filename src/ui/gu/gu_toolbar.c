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
#include <assert.h>
#include "navigator.h"
#include "gu.h"
#include "showtime.h"
#include "event.h"

typedef struct toolbar {

  gu_tab_t *gt;

  char *parent_url;

  prop_sub_t *sub_canGoBack;
  prop_sub_t *sub_canGoFwd;
  prop_sub_t *sub_canGoHome;
  prop_sub_t *sub_parent;
  prop_sub_t *sub_url;

  GtkToolItem *back;
  GtkToolItem *fwd;
  GtkToolItem *home;
  GtkToolItem *up;
  GtkWidget *url;
  GtkToolItem *opts;

} toolbar_t;


/**
 *
 */
static void
back_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
  gu_tab_t *gt = user_data;
  gu_tab_send_event(gt, event_create_action(ACTION_NAV_BACK));
}


/**
 *
 */
static void
fwd_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
  gu_tab_t *gt = user_data;
  gu_tab_send_event(gt, event_create_action(ACTION_NAV_FWD));
}


/**
 *
 */
static void
home_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
  gu_tab_t *gt = user_data;
  gu_tab_send_event(gt, event_create_action(ACTION_HOME));
}


/**
 *
 */
static void
gu_nav_url_updated(void *opaque, const char *str)
{
  toolbar_t *t = opaque;
  gtk_entry_set_text(GTK_ENTRY(t->url), str ?: "");
}


/**
 *
 */
static void
gu_nav_url_set(GtkEntry *e, gpointer user_data)
{
  gu_tab_t *gt = user_data;
  gu_tab_open(gt, gtk_entry_get_text(e));
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
    gu_tab_open(t->gt, t->parent_url);
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


typedef enum {
  DMI_NONE,
  DMI_TOGGLE,
  DMI_SEPARATOR,
} dmi_type_t;


/**
 *
 */
typedef struct dyn_menu {
  GtkWidget *dm_menu;
  gu_cloner_t dm_nodes;
  prop_sub_t *dm_node_sub;
} dyn_menu_t;




typedef struct dyn_menu_item {
  gu_cloner_node_t dmi_gcn;

  GtkWidget *dmi_widget;
  
  dmi_type_t dmi_type;
  char *dmi_title;
  int dmi_enabled;
  int dmi_value;

  prop_sub_t *dmi_type_sub;
  prop_sub_t *dmi_enabled_sub;

  prop_sub_t *dmi_title_sub;
  prop_sub_t *dmi_value_sub;

  dyn_menu_t *dmi_dm;

  prop_t *dmi_prop;

} dyn_menu_item_t;



/**
 *
 */
static void
dmi_toggle_cb(GtkCheckMenuItem *menuitem, gpointer aux)
{
  dyn_menu_item_t *dmi = aux;
  int set = gtk_check_menu_item_get_active(menuitem);
  prop_set_int_ex(prop_create(dmi->dmi_prop, "value"),
		  dmi->dmi_value_sub, set);
}


/**
 *
 */
static void
dmi_create_toggle(dyn_menu_item_t *dmi)
{
  GtkWidget *w;

  if(dmi->dmi_title == NULL)
    return;

  assert(dmi->dmi_widget == NULL);

  w = gtk_check_menu_item_new_with_mnemonic(dmi->dmi_title);

  gtk_widget_set_sensitive(w, dmi->dmi_enabled);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w), dmi->dmi_value);
  gtk_widget_show(w);

  gtk_menu_shell_append(GTK_MENU_SHELL(dmi->dmi_dm->dm_menu), w);

  g_signal_connect(G_OBJECT(w), "toggled", (void *)dmi_toggle_cb, dmi);
  dmi->dmi_widget = w;
}


/**
 *
 */
static void
dmi_create_separator(dyn_menu_item_t *dmi)
{
  GtkWidget *w = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(dmi->dmi_dm->dm_menu), w);
  gtk_widget_show(w);
  dmi->dmi_widget = w;
}


/**
 *
 */
static void
dmi_create(dyn_menu_item_t *dmi)
{
  switch(dmi->dmi_type) {
  case DMI_NONE:
    return;
  case DMI_TOGGLE:
    return dmi_create_toggle(dmi);
  case DMI_SEPARATOR:
    return dmi_create_separator(dmi);
  }
}

/**
 *
 */
static void
dmi_set_type(void *opaque, const char *str)
{
  dyn_menu_item_t *dmi = opaque;
  dmi_type_t nt;

  if(str == NULL)
    nt = DMI_NONE;
  else if(!strcmp(str, "toggle"))
    nt = DMI_TOGGLE;
  else if(!strcmp(str, "separator"))
    nt = DMI_SEPARATOR;
  else
    nt = DMI_NONE;

  if(nt == dmi->dmi_type)
    return;

  if(dmi->dmi_widget != NULL) {
    gtk_widget_destroy(dmi->dmi_widget);
    dmi->dmi_widget = NULL;
  }

  dmi->dmi_type = nt;
  dmi_create(dmi);
}


/**
 *
 */
static void
dmi_set_title(void *opaque, const char *str)
{
  dyn_menu_item_t *dmi = opaque;

  if(!strcmp(str ?: "", dmi->dmi_title ?: ""))
    return;

  free(dmi->dmi_title);
  dmi->dmi_title = str ? strdup(str) : NULL;
  dmi_create(dmi);
}


/**
 *
 */
static void
dmi_set_enabled(void *opaque, int v)
{
  dyn_menu_item_t *dmi = opaque;

  dmi->dmi_enabled = v;
  if(dmi->dmi_widget != NULL)
    gtk_widget_set_sensitive(dmi->dmi_widget, v);
}


/**
 *
 */
static void
dmi_set_value(void *opaque, int v)
{
  dyn_menu_item_t *dmi = opaque;

  dmi->dmi_value = v;
  if(dmi->dmi_widget == NULL)
    return;

  switch(dmi->dmi_type) {
  case DMI_NONE:
    return;
  case DMI_TOGGLE:
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(dmi->dmi_widget),
				   dmi->dmi_value);
    return;
  case DMI_SEPARATOR:
    return;
  }
}


/**
 *
 */
static void
dyn_menu_item_add(gtk_ui_t *gu, dyn_menu_t *dm,
		  prop_t *p, dyn_menu_item_t *dmi, dyn_menu_item_t *before,
		  int position)
{
  dmi->dmi_dm = dm;

  dmi->dmi_prop = prop_ref_inc(p);

  dmi->dmi_type_sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("node", "type"),
		   PROP_TAG_CALLBACK_STRING, dmi_set_type, dmi,
		   PROP_TAG_NAMED_ROOT, p, "node",
		   PROP_TAG_COURIER, gu->gu_pc,
		   NULL);

  dmi->dmi_title_sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("node", "title"),
		   PROP_TAG_CALLBACK_STRING, dmi_set_title, dmi,
		   PROP_TAG_NAMED_ROOT, p, "node",
		   PROP_TAG_COURIER, gu->gu_pc,
		   NULL);

  dmi->dmi_enabled_sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("node", "enabled"),
		   PROP_TAG_CALLBACK_INT, dmi_set_enabled, dmi,
		   PROP_TAG_NAMED_ROOT, p, "node",
		   PROP_TAG_COURIER, gu->gu_pc,
		   NULL);

  dmi->dmi_value_sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("node", "value"),
		   PROP_TAG_CALLBACK_INT, dmi_set_value, dmi,
		   PROP_TAG_NAMED_ROOT, p, "node",
		   PROP_TAG_COURIER, gu->gu_pc,
		   NULL);
}


/**
 *
 */
static void
dyn_menu_item_del(gtk_ui_t *gu, dyn_menu_t *dn, dyn_menu_item_t *dmi)
{
  if(dmi->dmi_widget != NULL)
    gtk_widget_destroy(dmi->dmi_widget);

  free(dmi->dmi_title);
  prop_unsubscribe(dmi->dmi_type_sub);
  prop_unsubscribe(dmi->dmi_enabled_sub);
  prop_unsubscribe(dmi->dmi_title_sub);
  prop_unsubscribe(dmi->dmi_value_sub);
  prop_ref_dec(dmi->dmi_prop);
}


/**
 *
 */
static void
dyn_menu_destroyed(GtkObject *object, gpointer user_data)
{
  dyn_menu_t *dm = user_data;
  prop_unsubscribe(dm->dm_node_sub);
  gu_cloner_destroy(&dm->dm_nodes);
  free(dm);
}


/**
 *
 */
static void
opts_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
  int event_time = gtk_get_current_event_time();
  gu_tab_t *gt = user_data;
  gtk_ui_t *gu = gt->gt_gw->gw_gu;

  dyn_menu_t *dm = calloc(1, sizeof(dyn_menu_t));

  dm->dm_menu = gtk_menu_new();
  gtk_widget_show_all(dm->dm_menu);

  gtk_menu_attach_to_widget(GTK_MENU(dm->dm_menu), GTK_WIDGET(toolbutton), NULL);

  gtk_menu_popup(GTK_MENU(dm->dm_menu), NULL, NULL, NULL, NULL, 
		 0, event_time);

  gu_cloner_init(&dm->dm_nodes, dm, dyn_menu_item_add, dyn_menu_item_del,
		 sizeof(dyn_menu_item_t), gu, GU_CLONER_TRACK_POSITION);

  dm->dm_node_sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("nav", "currentpage", "model", "options"),
		   PROP_TAG_CALLBACK, gu_cloner_subscription, &dm->dm_nodes,
		   PROP_TAG_NAMED_ROOT, gt->gt_nav, "nav",
		   PROP_TAG_COURIER, gu->gu_pc,
		   NULL);

  g_signal_connect(dm->dm_menu, "destroy", G_CALLBACK(dyn_menu_destroyed), dm);
}


/**
 *
 */
GtkWidget *
gu_toolbar_add(gu_tab_t *gt, GtkWidget *parent)
{
  toolbar_t *t = calloc(1, sizeof(toolbar_t));
  prop_courier_t *pc = gt->gt_gw->gw_gu->gu_pc;
  GtkWidget *toolbar;
  GtkWidget *w;

  t->gt = gt;

  /* Top Toolbar */
  toolbar = gtk_toolbar_new();
  gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
  gtk_box_pack_start(GTK_BOX(parent), toolbar, FALSE, TRUE, 0);

  /* Back button */
  t->back = gtk_tool_button_new_from_stock(GTK_STOCK_GO_BACK);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), t->back, -1);
  g_signal_connect(G_OBJECT(t->back), "clicked", G_CALLBACK(back_clicked), gt);
  gtk_widget_show(GTK_WIDGET(t->back));

  /* Forward button */
  t->fwd = gtk_tool_button_new_from_stock(GTK_STOCK_GO_FORWARD);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), t->fwd, -1);
  g_signal_connect(G_OBJECT(t->fwd), "clicked", G_CALLBACK(fwd_clicked), gt);
  gtk_widget_show(GTK_WIDGET(t->fwd));
  
  /* Up button */
  t->up = gtk_tool_button_new_from_stock(GTK_STOCK_GO_UP);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), t->up, -1);
  g_signal_connect(G_OBJECT(t->up), "clicked", G_CALLBACK(up_clicked), t);
  gtk_widget_show(GTK_WIDGET(t->up));

  /* Home button */
  t->home = gtk_tool_button_new_from_stock(GTK_STOCK_HOME);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), t->home, -1);
  g_signal_connect(G_OBJECT(t->home), "clicked", G_CALLBACK(home_clicked), gt);
  gtk_widget_show(GTK_WIDGET(t->home));


  /* URL entry */
  GtkToolItem *ti = gtk_tool_item_new();
  t->url = w = gtk_entry_new();

  g_signal_connect(G_OBJECT(w), "activate", G_CALLBACK(gu_nav_url_set), gt);

  gtk_container_add(GTK_CONTAINER(ti), w);
  gtk_tool_item_set_expand(ti, TRUE);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ti, -1);

  gtk_widget_show_all(GTK_WIDGET(ti));

  g_signal_connect(toolbar, "destroy", G_CALLBACK(toolbar_dtor), t);

  /* Opts button */
  t->opts = gtk_tool_button_new_from_stock(GTK_STOCK_EDIT);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), t->opts, -1);
  g_signal_connect(G_OBJECT(t->opts), "clicked", G_CALLBACK(opts_clicked), gt);
  gtk_widget_show(GTK_WIDGET(t->opts));

  t->sub_canGoBack =
    prop_subscribe(0,
		   PROP_TAG_NAME("nav", "canGoBack"),
		   PROP_TAG_CALLBACK_INT, gu_subscription_set_sensitivity,
		   t->back,
		   PROP_TAG_COURIER, pc,
		   PROP_TAG_NAMED_ROOT, gt->gt_nav, "nav",
		   NULL);

  t->sub_canGoFwd =
    prop_subscribe(0,
		   PROP_TAG_NAME("nav", "canGoForward"),
		   PROP_TAG_CALLBACK_INT, gu_subscription_set_sensitivity,
		   t->fwd,
		   PROP_TAG_COURIER, pc,
		   PROP_TAG_NAMED_ROOT, gt->gt_nav, "nav",
		   NULL);

  t->sub_parent =
    prop_subscribe(0,
		   PROP_TAG_NAME("nav", "currentpage", "parent"),
		   PROP_TAG_CALLBACK_STRING, set_go_up, t,
		   PROP_TAG_COURIER, pc,
		   PROP_TAG_NAMED_ROOT, gt->gt_nav, "nav",
		   NULL);

  t->sub_canGoHome =
    prop_subscribe(0,
		   PROP_TAG_NAME("nav", "canGoHome"),
		   PROP_TAG_CALLBACK_INT, gu_subscription_set_sensitivity,
		   t->home,
		   PROP_TAG_COURIER, pc,
		   PROP_TAG_NAMED_ROOT, gt->gt_nav, "nav",
		   NULL);

  t->sub_url =
    prop_subscribe(0,
		   PROP_TAG_NAME("nav", "currentpage", "url"),
		   PROP_TAG_CALLBACK_STRING, gu_nav_url_updated, t,
		   PROP_TAG_COURIER, pc,
		   PROP_TAG_NAMED_ROOT, gt->gt_nav, "nav",
		   NULL);

  gtk_widget_show_all(toolbar);

  return toolbar;
}

