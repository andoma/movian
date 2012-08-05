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

TAILQ_HEAD(source_queue, source);
TAILQ_HEAD(link_queue,   links);
/**
 *
 */
typedef struct home {

  gu_tab_t *h_gt;

  prop_sub_t *h_src_sub;
  GtkWidget *h_sourcebox;

  gu_cloner_t h_sources;

} home_t;


/**
 *
 */
typedef struct source {
  gu_cloner_node_t s_gcn;
  gu_cloner_t s_links;

  GtkWidget *s_bbox;
  GtkWidget *s_hbox;
  GtkWidget *s_separator;

  prop_sub_t *s_linksub;

  home_t *s_home;

  char *s_url;

} source_t;



/**
 *
 */
static void
source_clicked(GtkObject *object, gpointer opaque)
{
  source_t *s = opaque;

  if(s->s_url != NULL)
    gu_tab_open(s->s_home->h_gt, s->s_url);
}


/**
 *
 */
static void
source_set_url(void *opaque, const char *str)
{
  source_t *s = opaque;
  free(s->s_url);
  s->s_url = str ? strdup(str) : NULL;
}




/**
 *
 */
static void
home_set_icon(void *opaque, const char *str)
{
  if(str == NULL)
    return;
  
  gtk_misc_set_padding(GTK_MISC(opaque), 0, 0);
  gu_pixbuf_async_set(str, 72, -1, GTK_OBJECT(opaque));
}


/**
 *
 */
static void
source_add(gtk_ui_t *gu, home_t *h, prop_t *p, source_t *s, source_t *before,
	   int position)
{
  prop_sub_t *sub;
  GtkWidget *w, *vbox, *hbox;
  prop_courier_t *pc = gu->gu_pc;

  s->s_home = h;
  s->s_hbox = gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(h->h_sourcebox), s->s_hbox, FALSE, TRUE, 5);
  gtk_box_reorder_child(GTK_BOX(h->h_sourcebox), s->s_hbox, position * 2);

  s->s_separator = gtk_hseparator_new();
  gtk_box_pack_start(GTK_BOX(h->h_sourcebox), s->s_separator, FALSE, TRUE, 0);
  gtk_box_reorder_child(GTK_BOX(h->h_sourcebox), s->s_separator, 
			position * 2 + 1);
  gtk_widget_show(s->s_separator);

  /* Icon */
  w = gtk_image_new();
  gtk_box_pack_start(GTK_BOX(s->s_hbox), w, FALSE, TRUE, 5);

  sub = prop_subscribe(0,
		       PROP_TAG_NAME("self", "icon"),
		       PROP_TAG_CALLBACK_STRING, home_set_icon, w,
		       PROP_TAG_COURIER, pc, 
		       PROP_TAG_NAMED_ROOT, p, "self",
		       NULL);

  gu_unsubscribe_on_destroy(GTK_OBJECT(w), sub);

  gtk_image_set_from_stock(GTK_IMAGE(w), GTK_STOCK_DIRECTORY,
			   GTK_ICON_SIZE_DIALOG);

  gtk_misc_set_alignment(GTK_MISC(w), 0.5, 0.5);
  gtk_misc_set_padding(GTK_MISC(w), 12, 0);

  /* vbox */
  vbox = gtk_vbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(s->s_hbox), vbox, TRUE, TRUE, 0);

  /* Title */
  w = gtk_label_new("");
  gtk_misc_set_alignment(GTK_MISC(w), 0, 0);
  gtk_label_set_ellipsize(GTK_LABEL(w), PANGO_ELLIPSIZE_END);
  gtk_box_pack_start(GTK_BOX(vbox), w, FALSE, TRUE, 0);

  sub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "title"),
		   PROP_TAG_CALLBACK_STRING, gu_subscription_set_label_xl, w,
		   PROP_TAG_COURIER, pc, 
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
		   PROP_TAG_COURIER, pc, 
		   PROP_TAG_NAMED_ROOT, p, "self",
		   NULL);

  gu_unsubscribe_on_destroy(GTK_OBJECT(w), sub);


  /* Spacer */

  w = gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(vbox), w, TRUE, FALSE, 0);

  /* Link */



  hbox = gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, TRUE, FALSE, 0);

  w = gtk_button_new();
  gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);

  g_signal_connect(GTK_OBJECT(w),
		   "clicked", G_CALLBACK(source_clicked), s);

  g_object_set(G_OBJECT(w), "label", "Open", NULL);


  sub = prop_subscribe(0,
		       PROP_TAG_NAME("self", "url"),
		       PROP_TAG_CALLBACK_STRING, source_set_url, s,
		       PROP_TAG_COURIER, pc, 
		       PROP_TAG_NAMED_ROOT, p, "self",
		       NULL);

  gu_unsubscribe_on_destroy(GTK_OBJECT(w), sub);

  /* Finalize */
  gtk_widget_show_all(s->s_hbox);
}


/**
 *
 */
static void
source_del(gtk_ui_t *gu, home_t *h, source_t *s)
{
  gtk_widget_destroy(s->s_hbox);
  gtk_widget_destroy(s->s_separator);
}


/**
 *
 */
static void
home_destroy(GtkObject *object, gpointer opaque)
{
  home_t *h = opaque;

  prop_unsubscribe(h->h_src_sub);

  gu_cloner_destroy(&h->h_sources);

  free(h);
}


/**
 *
 */
void
gu_home_create(gu_nav_page_t *gnp)
{
  GtkWidget *vbox;
  gu_tab_t *gt = gnp->gnp_gt;
  gtk_ui_t *gu = gt->gt_gw->gw_gu;

  home_t *h = calloc(1, sizeof(home_t));
  h->h_gt = gnp->gnp_gt;

  /* Scrolled window */
  gnp->gnp_pageroot = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(gnp->gnp_pageroot),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

  /* Vbox */
  vbox = gtk_vbox_new(FALSE, 1);
  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(gnp->gnp_pageroot),
					vbox);

  h->h_sourcebox = vbox;


  gu_cloner_init(&h->h_sources, h, source_add, source_del, sizeof(source_t),
		 gu, GU_CLONER_TRACK_POSITION);

  h->h_src_sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("global", "services", "enabled"),
		   PROP_TAG_CALLBACK, gu_cloner_subscription, &h->h_sources,
		   PROP_TAG_COURIER, gu->gu_pc,
		   NULL);


  g_signal_connect(GTK_OBJECT(gnp->gnp_pageroot), 
		   "destroy", G_CALLBACK(home_destroy), h);

  gtk_container_add(GTK_CONTAINER(gnp->gnp_pagebin), gnp->gnp_pageroot);
  gtk_widget_show_all(gnp->gnp_pageroot);
}
