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

/**
 *
 */
static void
m_quit(gpointer callback_data, guint callback_action, GtkWidget *menu_item)
{
  showtime_shutdown(0);
}


/**
 *
 */
static void
m_about(gpointer callback_data, guint callback_action, GtkWidget *menu_item)
{
  gtk_ui_t *gu = callback_data;
  extern const char *htsversion;

  gtk_show_about_dialog(GTK_WINDOW(gu->gu_window),
			"program-name", "HTS Showtime",
			"version", htsversion,
			"website", "http://www.lonelycoder.com/hts",
			"copyright", "2006 - 2009 Andreas Öman, et al.",
			NULL);
}


/**
 *
 */
static GtkItemFactoryEntry menu_items[] = {
  { "/_File",         NULL,       NULL,           0, "<Branch>" },
  { "/File/_Quit",    "<CTRL>Q",  m_quit, 0, "<StockItem>", GTK_STOCK_QUIT },
  { "/_Options",      NULL,       NULL,           0, "<Branch>" },
  { "/Options/Check", NULL,       print_toggle,   1, "<CheckItem>" },
  { "/Options/sep",   NULL,       NULL,           0, "<Separator>" },
  { "/Options/Rad1",  NULL,       print_selected, 1, "<RadioItem>" },
  { "/Options/Rad2",  NULL,       print_selected, 2, "/Options/Rad1" },
  { "/Options/Rad3",  NULL,       print_selected, 3, "/Options/Rad1" },
  { "/_Help",         NULL,       NULL,           0, "<LastBranch>" },
  { "/_Help/About",   NULL,       m_about,        0, "<Item>" },
};

static gint nmenu_items = sizeof (menu_items) / sizeof (menu_items[0]);

/**
 *
 */
void
gu_menubar_add(gtk_ui_t *gu, GtkWidget *parent)
{
  GtkItemFactory *item_factory;
  GtkAccelGroup *accel_group;
  GtkWidget *menubar;

  accel_group = gtk_accel_group_new();

  item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<main>",
				      accel_group);

  gtk_item_factory_create_items(item_factory, nmenu_items, menu_items, gu);

  gtk_window_add_accel_group(GTK_WINDOW(gu->gu_window), accel_group);

  menubar = gtk_item_factory_get_widget(item_factory, "<main>");
  gtk_box_pack_start(GTK_BOX(parent), menubar, FALSE, TRUE, 0);

}




