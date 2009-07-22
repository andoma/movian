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
static int
gu_start(ui_t *ui, int argc, char **argv, int primary)
{
  GtkWidget *win;
  GtkWidget *vbox;
  GtkWidget *menubar;
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
  gu_toolbar_add(gu, vbox);

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

  /* Playback controls */
  gu_playdeck_add(gu, vbox);

  /* Statusbar */
  gu_statusbar_add(gu, vbox);

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


