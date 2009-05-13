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

static hts_mutex_t gu_mutex;



static void
gu_enter(void)
{
  hts_mutex_lock(&gu_mutex);
}

static void
gu_leave(void)
{
  hts_mutex_unlock(&gu_mutex);
}



/* XXX Example code */
static void print_toggle( gpointer   callback_data,
                          guint      callback_action,
                          GtkWidget *menu_item )
{
   g_message ("Check button state - %d\n",
              GTK_CHECK_MENU_ITEM (menu_item)->active);
}

/* XXX Example code */
static void print_selected( gpointer   callback_data,
                            guint      callback_action,
                            GtkWidget *menu_item )
{
   if(GTK_CHECK_MENU_ITEM(menu_item)->active)
     g_message ("Radio button %d selected\n", callback_action);
}




static GtkItemFactoryEntry menu_items[] = {
  { "/_File",         NULL,         NULL,           0, "<Branch>" },
  { "/File/_Quit",    "<CTRL>Q",    NULL, 0, "<StockItem>", GTK_STOCK_QUIT },
  { "/_Options",      NULL,         NULL,           0, "<Branch>" },
  { "/Options/Check", NULL,         print_toggle,   1, "<CheckItem>" },
  { "/Options/sep",   NULL,         NULL,           0, "<Separator>" },
  { "/Options/Rad1",  NULL,         print_selected, 1, "<RadioItem>" },
  { "/Options/Rad2",  NULL,         print_selected, 2, "/Options/Rad1" },
  { "/Options/Rad3",  NULL,         print_selected, 3, "/Options/Rad1" },
  { "/_Help",         NULL,         NULL,           0, "<LastBranch>" },
  { "/_Help/About",   NULL,         NULL,           0, "<Item>" },
};



static gint nmenu_items = sizeof (menu_items) / sizeof (menu_items[0]);

/* Returns a menubar widget made from the above menu */
static GtkWidget *get_menubar_menu( GtkWidget  *window )
{
  GtkItemFactory *item_factory;
  GtkAccelGroup *accel_group;

  /* Make an accelerator group (shortcut keys) */
  accel_group = gtk_accel_group_new();

  /* Make an ItemFactory (that makes a menubar) */
  item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<main>",
				      accel_group);

  /* This function generates the menu items. Pass the item factory,
     the number of items in the array, the array itself, and any
     callback data for the the menu items. */
  gtk_item_factory_create_items(item_factory, nmenu_items, menu_items, NULL);

  /* Attach the new accelerator group to the window. */
  gtk_window_add_accel_group(GTK_WINDOW (window), accel_group);

  /* Finally, return the actual menu bar created by the item factory. */
  return gtk_item_factory_get_widget(item_factory, "<main>");
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
static void
back_clicked(GtkToolButton *toolbutton, gpointer user_data)
{
  nav_back();
}

#if 0
/**
 * Convert a subscription callback into a gvalue string
 */
void
gu_sub_to_str(va_list ap, prop_event_t event, GValue *gv)
{
  memset(gv, 0 sizeof(GValue));
	 
  g_value_init(gv, G_TYPE_STRING);

  if(event == PROP_SET_STRING)
    g_value_set_string(&gv, va_arg(ap, const char *));
  else
    g_value_set_string(&gv, "");
}
#endif



/**
 *
 */
static void
gu_nav_url_updated(void *opaque, prop_event_t event, ...)
{
  gtk_ui_t *gu = opaque;
  va_list ap;
  va_start(ap, event);
  gtk_entry_set_text(GTK_ENTRY(gu->gu_url), event == PROP_SET_STRING ? 
		     va_arg(ap, const char *) : "");
}


/**
 *
 */
static void
gu_nav_url_set(GtkEntry *e, gpointer user_data)
{
  nav_open(gtk_entry_get_text(e), NAV_OPEN_ASYNC);
}


/**
 *
 */
static int
gu_start(ui_t *ui, int argc, char **argv, int primary)
{
  GtkWidget *win;
  GtkWidget *vbox;
  GtkWidget *menubar;
  GtkWidget *toolbar;
  GtkWidget *w;
  GtkToolItem *ti;
  gtk_ui_t *gu = calloc(1, sizeof(gtk_ui_t));

  hts_mutex_init(&gu_mutex);

  g_thread_init(NULL);

  gdk_threads_set_lock_functions(gu_enter, gu_leave);

  gdk_threads_init();
  gdk_threads_enter();

  gtk_init(&argc, &argv);

  gu->gu_pc = prop_courier_create(&gu_mutex);

  win = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_window_set_title(GTK_WINDOW(win), "Showtime");
  gtk_window_set_default_size(GTK_WINDOW(win), 640, 280);

  g_signal_connect(G_OBJECT(win), "delete_event",
		   G_CALLBACK(gu_close), NULL);


  vbox = gtk_vbox_new(FALSE, 1);
  gtk_container_set_border_width(GTK_CONTAINER(vbox), 1);
  gtk_container_add(GTK_CONTAINER(win), vbox);

 /* Menubar */

  menubar = get_menubar_menu(win);
  gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, TRUE, 0);
 
  /* Top Toolbar */
  toolbar = gtk_toolbar_new();
  gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
  gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, TRUE, 0);

  ti = gtk_tool_button_new_from_stock(GTK_STOCK_GO_BACK);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ti, -1);

  g_signal_connect(G_OBJECT(ti), "clicked", G_CALLBACK(back_clicked), gu);

  ti = gtk_tool_button_new_from_stock(GTK_STOCK_GO_FORWARD);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ti, -1);

  ti = gtk_tool_button_new_from_stock(GTK_STOCK_HOME);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ti, -1);

  /* URL entry */
  ti = gtk_tool_item_new();
  gu->gu_url = w = gtk_entry_new();

  g_signal_connect(G_OBJECT(w), "activate", 
		   G_CALLBACK(gu_nav_url_set), gu);

  gtk_container_add(GTK_CONTAINER(ti), w);
  gtk_tool_item_set_expand(ti, TRUE);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ti, -1);

  prop_subscribe(0,
		 PROP_TAG_NAME_VECTOR, 
		 (const char *[]){"global","nav","currentpage","url",NULL},
		 PROP_TAG_CALLBACK, gu_nav_url_updated, gu,
		 PROP_TAG_COURIER, gu->gu_pc,
		 NULL);

#if 0
  /* Search entry */
  ti = gtk_tool_item_new();
  w = gtk_entry_new();
  gtk_container_add(GTK_CONTAINER(ti), w);
  gtk_tool_item_set_expand(ti, TRUE);
  gtk_toolbar_insert(GTK_TOOLBAR(toolbar), ti, -1);
#endif

  /* Page container */
  gu->gu_page_container = gtk_vbox_new(FALSE, 0);
  gtk_container_set_border_width(GTK_CONTAINER(gu->gu_page_container), 0);
  gtk_box_pack_start(GTK_BOX(vbox), gu->gu_page_container, TRUE, TRUE, 0);

  prop_subscribe(0,
		 PROP_TAG_NAME_VECTOR,
		 (const char *[]){"global","nav","pages",NULL},
		 PROP_TAG_CALLBACK, gu_nav_pages, gu, 
		 PROP_TAG_COURIER, gu->gu_pc,
		 NULL);

  gtk_widget_show_all(win);
  gtk_main();
  return 0;
}



/**
 *
 */
static int
gu_dispatch_event(uii_t *uii, event_t *e)
{
  return 0;
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


