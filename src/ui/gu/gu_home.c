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
#include "gu.h"

TAILQ_HEAD(source_queue, source);

/**
 *
 */
typedef struct home {

  gtk_ui_t *h_gu;

  prop_sub_t *h_src_sub;

  struct source_queue h_sources;

  GtkWidget *h_sourcebox;

} home_t;


/**
 *
 */
typedef struct source {
  prop_t *s_originator;

} source_t;


/**
 *
 */
static void
home_set_icon(void *opaque, const char *str)
{
  if(str == NULL)
    return;
  
  gu_pixbuf_async_set(str, 72, -1, GTK_OBJECT(opaque));
}



/**
 *
 */
static void
source_add(home_t *h, prop_t *p, source_t *before)
{
  source_t *s = calloc(1, sizeof(source_t));
  prop_sub_t *sub;
  GtkWidget *hbox, *w, *vbox, *bbox;

  s->s_originator = p;
  prop_ref_inc(p);

  hbox = gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(h->h_sourcebox), hbox, FALSE, TRUE, 5);

  w = gtk_hseparator_new();
  gtk_box_pack_start(GTK_BOX(h->h_sourcebox), w, FALSE, TRUE, 0);
  gtk_widget_show(w);

  /* Icon */
  w = gtk_image_new();
  gtk_misc_set_alignment(GTK_MISC(w), 0.5, 0.0);
  gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, TRUE, 5);

  sub = prop_subscribe(0,
		       PROP_TAG_NAME("self", "icon"),
		       PROP_TAG_CALLBACK_STRING, home_set_icon, w,
		       PROP_TAG_COURIER, h->h_gu->gu_pc, 
		       PROP_TAG_NAMED_ROOT, p, "self",
		       NULL);

  gu_unsubscribe_on_destroy(GTK_OBJECT(w), sub);

  /* vbox */
  vbox = gtk_vbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(hbox), vbox, TRUE, TRUE, 0);

  /* Title */
  w = gtk_label_new("");
  gtk_misc_set_alignment(GTK_MISC(w), 0, 0);
  gtk_label_set_ellipsize(GTK_LABEL(w), PANGO_ELLIPSIZE_END);
  gtk_box_pack_start(GTK_BOX(vbox), w, FALSE, TRUE, 0);

  sub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "title"),
		   PROP_TAG_CALLBACK_STRING, gu_subscription_set_label_xl, w,
		   PROP_TAG_COURIER, h->h_gu->gu_pc, 
		   PROP_TAG_NAMED_ROOT, p, "self",
		   NULL);

  gu_unsubscribe_on_destroy(GTK_OBJECT(w), sub);


  /* Status */
  w = gtk_label_new("");
  gtk_misc_set_alignment(GTK_MISC(w), 0, 0);
  gtk_label_set_ellipsize(GTK_LABEL(w), PANGO_ELLIPSIZE_END);
  gtk_box_pack_start(GTK_BOX(vbox), w, FALSE, TRUE, 0);

  sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "status"),
		   PROP_TAG_CALLBACK_STRING, gu_subscription_set_label, w,
		   PROP_TAG_COURIER, h->h_gu->gu_pc, 
		   PROP_TAG_NAMED_ROOT, p, "self",
		   NULL);

  gu_unsubscribe_on_destroy(GTK_OBJECT(w), sub);


  /* Spacer */

  w = gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(vbox), w, TRUE, FALSE, 0);

  /* Links */

  bbox = gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 0);


  

  w = gtk_button_new_with_label("Playlists");
  gtk_box_pack_start(GTK_BOX(bbox), w, FALSE, FALSE, 0);

  w = gtk_button_new_with_label("New albums");
  gtk_box_pack_start(GTK_BOX(bbox), w, FALSE, FALSE, 0);


  /* Finalize */
  gtk_widget_show_all(hbox);
}


/**
 *
 */
static void
gu_home_sources(void *opaque, prop_event_t event, ...)
{
  home_t *h = opaque;
  prop_t *p;

  va_list ap;
  va_start(ap, event);
  
  switch(event) {
  case PROP_ADD_CHILD:
    p = va_arg(ap, prop_t *);
    prop_print_tree(p, 0);

    source_add(h, p, NULL);
    break;

  case PROP_SET_DIR:
  case PROP_SET_VOID:
    break;

  default:
    fprintf(stderr, 
	    "gu_home_sources(): Can not handle event %d, aborting()\n", event);
    abort();
  }
}

/**
 *
 */
static void
home_destroy(GtkObject *object, gpointer opaque)
{
  home_t *h = opaque;

  prop_unsubscribe(h->h_src_sub);
  free(h);
}


/**
 *
 */
void
gu_home_create(gu_nav_page_t *gnp)
{
  GtkWidget *vbox;
  home_t *h = calloc(1, sizeof(home_t));
  h->h_gu = gnp->gnp_gu;

  /* Scrolled window */
  gnp->gnp_pageroot = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(gnp->gnp_pageroot),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

  /* Vbox */
  vbox = gtk_vbox_new(FALSE, 1);
  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(gnp->gnp_pageroot),
					vbox);

  h->h_sourcebox = vbox;

  h->h_src_sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("global", "sources"),
		   PROP_TAG_CALLBACK, gu_home_sources, h,
		   PROP_TAG_COURIER, h->h_gu->gu_pc, 
		   NULL);


  g_signal_connect(GTK_OBJECT(gnp->gnp_pageroot), 
		   "destroy", G_CALLBACK(home_destroy), h);

  gtk_container_add(GTK_CONTAINER(gnp->gnp_pagebin), gnp->gnp_pageroot);
  gtk_widget_show_all(gnp->gnp_pageroot);
}
