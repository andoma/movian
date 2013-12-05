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

#include <string.h>
#include <assert.h>
#include "navigator.h"
#include "gu.h"
#include "gu_directory.h"


typedef struct albuminfo {
  char *artist;
  char *title;
  int year;

  prop_sub_t *artist_sub;
  prop_sub_t *title_sub;
  prop_sub_t *year_sub;

  GtkWidget *widget;

} albuminfo_t;


/**
 *
 */
static void
ai_destroy(GtkObject *object, gpointer user_data)
{
  albuminfo_t *ai = user_data;

  prop_unsubscribe(ai->artist_sub);
  prop_unsubscribe(ai->title_sub);
  prop_unsubscribe(ai->year_sub);

  free(ai->artist);
  free(ai->title);
}

/**
 *
 */
static void
ai_update_widget(albuminfo_t *ai)
{
  char *m;
  char yeartxt[20];
  if(ai->title == NULL)
    return;

  if(ai->year) {
    snprintf(yeartxt, sizeof(yeartxt), " (%d)", ai->year);
  } else {
    yeartxt[0] = 0;
  }

  m = g_markup_printf_escaped("<span size=\"xx-large\">%s%s%s</span>"
			      "<span>%s</span>", 
			      ai->artist ?: "",
			      ai->artist ? " - " : "",
			      ai->title,
			      yeartxt);

  gtk_label_set_markup(GTK_LABEL(ai->widget), m);
  g_free(m);
}


/**
 *
 */
static void
album_set_title(void *opaque, const char *str)
{
  albuminfo_t *ai = opaque;

  free(ai->title);
  ai->title = str ? strdup(str) : NULL;
  ai_update_widget(ai);
}


/**
 *
 */
static void
album_set_artist(void *opaque, const char *str)
{
  albuminfo_t *ai = opaque;

  free(ai->artist);
  ai->artist = str ? strdup(str) : NULL;
  ai_update_widget(ai);
}


/**
 *
 */
static void
album_set_year(void *opaque, int year)
{
  albuminfo_t *ai = opaque;
  ai->year = year;
  ai_update_widget(ai);
}




/**
 *
 */
static void
add_header(GtkWidget *parent, prop_t *root)
{
  GtkWidget *hbox, *w;
  albuminfo_t *ai = calloc(1, sizeof(albuminfo_t));



  hbox = gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(parent), hbox, FALSE, TRUE, 0);


  /* Title */
  w = gtk_label_new("");
  gtk_misc_set_alignment(GTK_MISC(w), 0, 0);
  gtk_label_set_ellipsize(GTK_LABEL(w), PANGO_ELLIPSIZE_END);

  gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);

  ai->title_sub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "model", "album_name"),
		   PROP_TAG_CALLBACK_STRING, album_set_title, ai,
		   PROP_TAG_COURIER, glibcourier,
		   PROP_TAG_NAMED_ROOT, root, "self",
		   NULL);

  ai->artist_sub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "model", "artist_name"),
		   PROP_TAG_CALLBACK_STRING, album_set_artist, ai,
		   PROP_TAG_COURIER, glibcourier,
		   PROP_TAG_NAMED_ROOT, root, "self",
		   NULL);

  ai->year_sub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "model", "album_year"),
		   PROP_TAG_CALLBACK_INT, album_set_year, ai,
		   PROP_TAG_COURIER, glibcourier,
		   PROP_TAG_NAMED_ROOT, root, "self",
		   NULL);

  ai->widget = w;

  g_signal_connect(GTK_OBJECT(w), "destroy", G_CALLBACK(ai_destroy), ai);
}


/**
 *
 */
static void
album_set_art(void *opaque, const char *str)
{
  if(str == NULL)
    return;

  gu_pixbuf_async_set(str, 256, -1, GTK_OBJECT(opaque));
}


/**
 *
 */
static void
gu_add_album(gu_tab_t *gt, GtkWidget *parent, prop_t *root)
{
  GtkWidget *hbox;
  GtkWidget *w;
  prop_sub_t *s;

  hbox = gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(parent), hbox, TRUE, TRUE, 0);

  /* Album art */

  w = gtk_image_new();
  gtk_misc_set_alignment(GTK_MISC(w), 0.5, 0.0);
  gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, TRUE, 0);

  s = prop_subscribe(0,
		     PROP_TAG_NAME("self", "model", "album_art"),
		     PROP_TAG_CALLBACK_STRING, album_set_art, w,
		     PROP_TAG_COURIER, glibcourier, 
		     PROP_TAG_NAMED_ROOT, root, "self",
		     NULL);

  gu_unsubscribe_on_destroy(GTK_OBJECT(w), s);
  
  /* Tracklist */

  w = gu_directory_list_create(gt, root,
			       GU_DIR_SCROLLBOX |
			       GU_DIR_VISIBLE_HEADERS |
			       GU_DIR_COL_ARTIST |
			       GU_DIR_COL_DURATION |
			       GU_DIR_COL_TRACKINDEX);
  gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
}



/**
 *
 */
GtkWidget *
gu_directory_album_create(gu_tab_t *gt, prop_t *root)
{
  GtkWidget *view = gtk_vbox_new(FALSE, 1);
  
  add_header(view, root);
  gu_add_album(gt, view, root);
  gtk_widget_show_all(view);

  return view;
}
