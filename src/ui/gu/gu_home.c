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

  gtk_ui_t *h_gu;

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
} source_t;


/**
 *
 */
typedef struct link {
  gu_cloner_node_t l_gcn;

  GtkWidget *l_btn;

  prop_sub_t *l_title_sub;
  prop_sub_t *l_url_sub;

  char *l_url;

} link_t;


/**
 *
 */
static void
link_clicked(GtkObject *object, gpointer opaque)
{
  link_t *l = opaque;

  if(l->l_url != NULL)
    nav_open(l->l_url, NULL, NULL, NAV_OPEN_ASYNC);
}


/**
 *
 */
static void
link_set_url(void *opaque, const char *str)
{
  link_t *l = opaque;
  free(l->l_url);
  l->l_url = str ? strdup(str) : NULL;
}


/**
 *
 */
static void
link_add(gtk_ui_t *gu, source_t *s, prop_t *p, link_t *l, source_t *before)
{
  l->l_btn = gtk_button_new();
  
  gtk_box_pack_start(GTK_BOX(s->s_bbox), l->l_btn, FALSE, FALSE, 0);

  g_signal_connect(GTK_OBJECT(l->l_btn),
		   "clicked", G_CALLBACK(link_clicked), l);

  l->l_title_sub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "title"),
		   PROP_TAG_CALLBACK_STRING, 
		   gu_subscription_set_label, l->l_btn,
		   PROP_TAG_COURIER, gu->gu_pc, 
		   PROP_TAG_NAMED_ROOT, p, "self",
		   NULL);

  l->l_url_sub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "url"),
		   PROP_TAG_CALLBACK_STRING, link_set_url, l,
		   PROP_TAG_COURIER, gu->gu_pc, 
		   PROP_TAG_NAMED_ROOT, p, "self",
		   NULL);


  gtk_widget_show(l->l_btn);
}


/**
 *
 */
static void
link_del(gtk_ui_t *gu, source_t *s, link_t *l)
{
  prop_unsubscribe(l->l_title_sub);
  prop_unsubscribe(l->l_url_sub);

  gtk_widget_destroy(l->l_btn);
  free(l->l_url);
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
source_add(gtk_ui_t *gu, home_t *h, prop_t *p, source_t *s, source_t *before)
{
  prop_sub_t *sub;
  GtkWidget *w, *vbox;

  s->s_hbox = gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(h->h_sourcebox), s->s_hbox, FALSE, TRUE, 5);

  s->s_separator = gtk_hseparator_new();
  gtk_box_pack_start(GTK_BOX(h->h_sourcebox), s->s_separator, FALSE, TRUE, 0);
  gtk_widget_show(s->s_separator);

  /* Icon */
  w = gtk_image_new();
  gtk_box_pack_start(GTK_BOX(s->s_hbox), w, FALSE, TRUE, 5);

  sub = prop_subscribe(0,
		       PROP_TAG_NAME("self", "icon"),
		       PROP_TAG_CALLBACK_STRING, home_set_icon, w,
		       PROP_TAG_COURIER, h->h_gu->gu_pc, 
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

  s->s_bbox = gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(vbox), s->s_bbox, FALSE, FALSE, 0);

  gu_cloner_init(&s->s_links, s, link_add, link_del, sizeof(link_t), gu);

  s->s_linksub =
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "links"),
		   PROP_TAG_CALLBACK, gu_cloner_subscription, &s->s_links,
		   PROP_TAG_COURIER, h->h_gu->gu_pc, 
		   PROP_TAG_NAMED_ROOT, p, "self",
		   NULL);

  /* Finalize */
  gtk_widget_show_all(s->s_hbox);
}


/**
 *
 */
static void
source_del(gtk_ui_t *gu, home_t *h, source_t *s)
{
  prop_unsubscribe(s->s_linksub);
  gu_cloner_destroy(&s->s_links);
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


  gu_cloner_init(&h->h_sources, h, source_add, source_del, sizeof(source_t),
		 h->h_gu);

  h->h_src_sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("global", "sources"),
		   PROP_TAG_CALLBACK, gu_cloner_subscription, &h->h_sources,
		   PROP_TAG_COURIER, h->h_gu->gu_pc,
		   NULL);


  g_signal_connect(GTK_OBJECT(gnp->gnp_pageroot), 
		   "destroy", G_CALLBACK(home_destroy), h);

  gtk_container_add(GTK_CONTAINER(gnp->gnp_pagebin), gnp->gnp_pageroot);
  gtk_widget_show_all(gnp->gnp_pageroot);
}
