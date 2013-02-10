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

TAILQ_HEAD(setting_queue, setting);

/**
 *
 */
typedef struct settinglist {

  gu_tab_t *sl_gt;

  prop_sub_t *sl_node_sub;
  GtkWidget *sl_box;

  gu_cloner_t sl_nodes;

} settinglist_t;


/**
 *
 */
typedef struct setting {
  gu_cloner_node_t s_gcn;
  gu_cloner_t s_links;

  GtkWidget *s_bbox;
  GtkWidget *s_hbox;
  GtkWidget *s_separator;

  prop_sub_t *s_linksub;

  settinglist_t *s_settinglist;

  char *s_url;

} setting_t;



/**
 *
 */
static void
set_icon(void *opaque, const char *str)
{
  if(str == NULL)
    return;
  gint width = 64;
  gtk_icon_size_lookup(GTK_ICON_SIZE_DIALOG, &width, NULL);
  gu_pixbuf_async_set(str, width, -1, GTK_OBJECT(opaque));
}


/**
 *
 */
static void
source_add(gtk_ui_t *gu, settinglist_t *sl,
	   prop_t *p, setting_t *s, setting_t *before,
	   int position)
{
  prop_sub_t *sub;
  GtkWidget *w, *vbox;

  s->s_settinglist = sl;
  s->s_hbox = gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(sl->sl_box), s->s_hbox, FALSE, TRUE, 5);
  gtk_box_reorder_child(GTK_BOX(sl->sl_box), s->s_hbox, position * 2);

  s->s_separator = gtk_hseparator_new();
  gtk_box_pack_start(GTK_BOX(sl->sl_box), s->s_separator, FALSE, TRUE, 0);
  gtk_box_reorder_child(GTK_BOX(sl->sl_box), s->s_separator, 
			position * 2 + 1);
  gtk_widget_show(s->s_separator);
  
  

  gtk_widget_set_size_request(s->s_hbox, -1, 64);

  /* Icon */
  w = gtk_image_new();
  gtk_box_pack_start(GTK_BOX(s->s_hbox), w, FALSE, TRUE, 5);

  sub = prop_subscribe(0,
		       PROP_TAG_NAME("self", "model", "metadata", "icon"),
		       PROP_TAG_CALLBACK_STRING, set_icon, w,
		       PROP_TAG_COURIER, glibcourier,
		       PROP_TAG_NAMED_ROOT, p, "self",
		       NULL);

  gu_unsubscribe_on_destroy(GTK_OBJECT(w), sub);

  gtk_image_set_from_stock(GTK_IMAGE(w), GTK_STOCK_PROPERTIES,
			   GTK_ICON_SIZE_DIALOG);

  gtk_misc_set_alignment(GTK_MISC(w), 0.5, 0.5);
  gtk_misc_set_padding(GTK_MISC(w), 8, 0);

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
		   PROP_TAG_NAME("self", "model", "metadata", "title"),
		   PROP_TAG_CALLBACK_STRING, gu_subscription_set_label_xl, w,
		   PROP_TAG_COURIER, glibcourier,
		   PROP_TAG_NAMED_ROOT, p, "self",
		   NULL);

  gu_unsubscribe_on_destroy(GTK_OBJECT(w), sub);


  /* Short description */
  w = gtk_label_new("");
  gtk_misc_set_alignment(GTK_MISC(w), 0, 0);
  gtk_label_set_ellipsize(GTK_LABEL(w), PANGO_ELLIPSIZE_END);
  gtk_box_pack_start(GTK_BOX(vbox), w, FALSE, TRUE, 0);

  sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "model", "metadata", "shortdesc"),
		   PROP_TAG_CALLBACK_STRING, gu_subscription_set_label, w,
		   PROP_TAG_COURIER, glibcourier,
		   PROP_TAG_NAMED_ROOT, p, "self",
		   NULL);

  gu_unsubscribe_on_destroy(GTK_OBJECT(w), sub);


  /* Spacer */

  w = gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(vbox), w, TRUE, FALSE, 0);

  /* Link */


#if 0
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
#endif

  /* Finalize */
  gtk_widget_show_all(s->s_hbox);
}


/**
 *
 */
static void
source_del(gtk_ui_t *gu, settinglist_t *sl, setting_t *s)
{
  gtk_widget_destroy(s->s_hbox);
  gtk_widget_destroy(s->s_separator);
}


/**
 *
 */
static void
settinglist_destroy(GtkObject *object, gpointer opaque)
{
  settinglist_t *sl = opaque;

  prop_unsubscribe(sl->sl_node_sub);

  gu_cloner_destroy(&sl->sl_nodes);

  free(sl);
}


/**
 *
 */
void
gu_settings_create(gu_nav_page_t *gnp)
{
  GtkWidget *vbox;
  gu_tab_t *gt = gnp->gnp_gt;
  gtk_ui_t *gu = gt->gt_gw->gw_gu;

  settinglist_t *sl = calloc(1, sizeof(settinglist_t));
  sl->sl_gt = gnp->gnp_gt;

  /* Scrolled window */
  gnp->gnp_pageroot = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(gnp->gnp_pageroot),
				 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

  /* Vbox */
  vbox = gtk_vbox_new(FALSE, 1);
  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(gnp->gnp_pageroot),
					vbox);

  sl->sl_box = vbox;


  gu_cloner_init(&sl->sl_nodes, sl, source_add, source_del, sizeof(setting_t),
		 gu, GU_CLONER_TRACK_POSITION);

  sl->sl_node_sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("page", "model", "nodes"),
		   PROP_TAG_CALLBACK, gu_cloner_subscription, &sl->sl_nodes,
		   PROP_TAG_ROOT, gnp->gnp_prop, 
		   PROP_TAG_COURIER, glibcourier,
		   NULL);


  g_signal_connect(GTK_OBJECT(gnp->gnp_pageroot), 
		   "destroy", G_CALLBACK(settinglist_destroy), sl);

  gtk_container_add(GTK_CONTAINER(gnp->gnp_pagebin), gnp->gnp_pageroot);
  gtk_widget_show_all(gnp->gnp_pageroot);
}
