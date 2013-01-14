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
#include "showtime.h"
#include "navigator.h"
#include "gu.h"
#include "gu_directory.h"


/**
 *
 */
static void
set_title(void *opaque, const char *str)
{
  if(str != NULL) {
    char *m = g_markup_printf_escaped("<span size=\"x-large\">%s</span>", str);
    gtk_label_set_markup(GTK_LABEL(opaque), m);
    g_free(m);
  } else {
    gtk_label_set(GTK_LABEL(opaque), "");
  }
}


/**
 *
 */
static void
add_header(prop_courier_t *pc, GtkWidget *parent, prop_t *root)
{
  GtkWidget *hbox, *w;
  prop_sub_t *s;


  hbox = gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(parent), hbox, FALSE, TRUE, 0);


  /* Artist name */
  w = gtk_label_new("");
  gtk_misc_set_alignment(GTK_MISC(w), 0, 0);
  gtk_label_set_ellipsize(GTK_LABEL(w), PANGO_ELLIPSIZE_END);

  gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);

  s = prop_subscribe(0,
		     PROP_TAG_NAME("self", "model", "artist_name"),
		     PROP_TAG_CALLBACK_STRING, gu_subscription_set_label_xl, w,
		     PROP_TAG_COURIER, pc, 
		     PROP_TAG_NAMED_ROOT, root, "self",
		     NULL);

  gu_unsubscribe_on_destroy(GTK_OBJECT(w), s);
}

/**
 *
 */
typedef struct albumbrowse {
  GtkWidget *vbox;
  prop_sub_t *sub;
  gu_tab_t *gt;

  int albums;

} albumbrowse_t;


/**
 *
 */
static void
ab_destroy(GtkObject *object, gpointer user_data)
{
  albumbrowse_t *ab = user_data;

  prop_unsubscribe(ab->sub);
  free(ab);
}


/**
 *
 */
static void
album_set_art(void *opaque, const char *str)
{
  GdkPixbuf *pb;
  char path[PATH_MAX];
	   

  if(str != NULL) {
    gu_pixbuf_async_set(str, 256, -1, GTK_OBJECT(opaque));
    return;
  }

  snprintf(path, sizeof(path), "%s/guresources/cd.png", showtime_dataroot());
  pb = gu_pixbuf_get_sync(path, 256, -1);
  g_object_set(G_OBJECT(opaque), "pixbuf", pb, NULL);
  if(pb != NULL)
    g_object_unref(G_OBJECT(pb));
}


/**
 *
 */
static void
collection_albums(void *opaque, prop_event_t event, ...)
{
  albumbrowse_t *ab = opaque;
  prop_t *p;
  GtkWidget *w;
  GtkWidget *hbox;
  prop_sub_t *s;

  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_ADD_CHILD:
    p = va_arg(ap, prop_t *);
    
    /* Separator */

    if(ab->albums > 0) {
      w = gtk_hseparator_new();
      gtk_box_pack_start(GTK_BOX(ab->vbox), w, FALSE, FALSE, 5);
      gtk_widget_show(w);
    }

    ab->albums++;

    /* Album name */
    w = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(w), 0, 0);
    gtk_misc_set_padding(GTK_MISC(w), 5, 0);
    gtk_label_set_ellipsize(GTK_LABEL(w), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(ab->vbox), w, TRUE, TRUE, 0);
    gtk_widget_show(w);

    s = prop_subscribe(0,
		       PROP_TAG_NAME("self", "metadata", "album_name"),
		       PROP_TAG_CALLBACK_STRING, set_title, w,
		       PROP_TAG_COURIER, glibcourier, 
		       PROP_TAG_NAMED_ROOT, p, "self",
		       NULL);

    gu_unsubscribe_on_destroy(GTK_OBJECT(w), s);


    /* hbox */

    hbox = gtk_hbox_new(FALSE, 1);
    gtk_box_pack_start(GTK_BOX(ab->vbox), hbox, TRUE, TRUE, 0);
    
    /* Album art */

    w = gtk_image_new();
    gtk_misc_set_alignment(GTK_MISC(w), 0.5, 0.0);
    gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, TRUE, 5);

    s = prop_subscribe(0,
		       PROP_TAG_NAME("self", "metadata", "album_art"),
		       PROP_TAG_CALLBACK_STRING, album_set_art, w,
		       PROP_TAG_COURIER, glibcourier, 
		       PROP_TAG_NAMED_ROOT, p, "self",
		       NULL);

    gu_unsubscribe_on_destroy(GTK_OBJECT(w), s);
    gtk_widget_show(w);

    /* Tracklist */
    w = gu_directory_list_create(ab->gt, p,
				 GU_DIR_COL_ARTIST |
				 GU_DIR_COL_DURATION |
				 GU_DIR_COL_TRACKINDEX);
    gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);
    gtk_widget_show_all(hbox);
    break;

  case PROP_SET_DIR:
  case PROP_SET_VOID:
    break;

  default:
    fprintf(stderr, 
	    "artist_albums(): Can not handle event %d, aborting()\n", event);
    abort();
  }
}


/**
 *
 */
static void
add_albums(gu_tab_t *gt, GtkWidget *parent, prop_t *root)
{
  GtkWidget *sbox;
  albumbrowse_t *ab = calloc(1, sizeof(albumbrowse_t));

  ab->gt = gt;

  /* Scrolled window */
  sbox = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sbox),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

  gtk_box_pack_start(GTK_BOX(parent), sbox, TRUE, TRUE, 0);


  /* vbox for all albums */
  ab->vbox = gtk_vbox_new(FALSE, 1);
  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(sbox), ab->vbox);

  ab->sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "model", "nodes"),
		   PROP_TAG_CALLBACK, collection_albums, ab,
		   PROP_TAG_COURIER, glibcourier, 
		   PROP_TAG_NAMED_ROOT, root, "self",
		   NULL);

  g_signal_connect(sbox, "destroy", G_CALLBACK(ab_destroy), ab);
}

/**
 *
 */
GtkWidget *
gu_directory_albumcollection_create(gu_tab_t *gt, prop_t *root)
{
  GtkWidget *view = gtk_vbox_new(FALSE, 1);
  
  add_header(glibcourier, view, root);
  add_albums(gt, view, root);

  gtk_widget_show_all(view);
  return view;
}
