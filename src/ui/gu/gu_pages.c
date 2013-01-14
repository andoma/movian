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

#include <assert.h>
#include <string.h>
#include "gu.h"
#include "showtime.h"

static void gu_openerror_create(gu_nav_page_t *gnp);


/**
 *
 */
static void
gu_nav_page_set_type(void *opaque, const char *type)
{
  gu_nav_page_t *gnp = opaque;

  if(gnp->gnp_pageroot != NULL)
    gtk_widget_destroy(gnp->gnp_pageroot);

  if(type == NULL)
    return;

  if(!strcmp(type, "directory") || !strcmp(type, "playlist")) {
    gu_directory_create(gnp);
  } else if(!strcmp(type, "playqueue")) {
    gu_directory_create(gnp);
  } else if(!strcmp(type, "home")) {
    gu_home_create(gnp);
  } else if(!strcmp(type, "video")) {
    gu_video_create(gnp);
  } else if(!strcmp(type, "openerror")) {
    gu_openerror_create(gnp);
  } else if(!strcmp(type, "settings")) {
    gu_settings_create(gnp);
  } else {
    GtkWidget *l;
    char str[256];

    snprintf(str, sizeof(str), "Can not display page type: %s", type);

    gnp->gnp_pageroot = gtk_vbox_new(FALSE, 3);
    gtk_container_add(GTK_CONTAINER(gnp->gnp_pagebin), gnp->gnp_pageroot);
  
    l = gtk_label_new(str);
    gtk_box_pack_start(GTK_BOX(gnp->gnp_pageroot), l, FALSE, FALSE, 0);

    gtk_widget_show_all(gnp->gnp_pageroot);
  }
}


/**
 *
 */
static void
gu_nav_page_set_url(void *opaque, const char *url)
{
  gu_nav_page_t *gnp = opaque;
  free(gnp->gnp_url);
  gnp->gnp_url = url ? strdup(url) : NULL;
}




/**
 *
 */
static void
gnp_dtor(GtkWidget *w, gu_nav_page_t *gnp)
{
  if(gnp->gnp_gt->gt_page_current == gnp) 
    gnp->gnp_gt->gt_page_current = NULL;

  prop_unsubscribe(gnp->gnp_sub_type);
  prop_unsubscribe(gnp->gnp_sub_url);

  LIST_REMOVE(gnp, gnp_link);
  
  prop_ref_dec(gnp->gnp_prop);
  free(gnp->gnp_url);
  free(gnp);
}


/**
 *
 */
static gu_nav_page_t *
gu_nav_page_create(gu_tab_t *gt, prop_t *p)
{
  gu_nav_page_t *gnp = calloc(1, sizeof(gu_nav_page_t));

  gnp->gnp_gt = gt;
  gnp->gnp_prop = prop_ref_inc(p);

  gnp->gnp_pagebin = gtk_vbox_new(FALSE, 0);
  gtk_widget_show(gnp->gnp_pagebin);
  gtk_container_set_border_width(GTK_CONTAINER(gnp->gnp_pagebin), 0);

  gtk_notebook_append_page(GTK_NOTEBOOK(gt->gt_notebook), gnp->gnp_pagebin,
			   NULL);

  LIST_INSERT_HEAD(&gt->gt_pages, gnp, gnp_link);

  gnp->gnp_sub_type = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "model", "type"),
		   PROP_TAG_CALLBACK_STRING, gu_nav_page_set_type, gnp,
		   PROP_TAG_COURIER, glibcourier,
		   PROP_TAG_NAMED_ROOT, p, "self",
		   NULL);

  gnp->gnp_sub_url =
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "url"),
		   PROP_TAG_CALLBACK_STRING, gu_nav_page_set_url, gnp,
		   PROP_TAG_COURIER, glibcourier,
		   PROP_TAG_NAMED_ROOT, p, "self",
		   NULL);

  g_signal_connect(gnp->gnp_pagebin, "destroy", G_CALLBACK(gnp_dtor), gnp);
  return gnp;
}


/**
 *
 */
static gu_nav_page_t *
gu_nav_page_find(gu_tab_t *gt, prop_t *p)
{
  gu_nav_page_t *gnp;

  LIST_FOREACH(gnp, &gt->gt_pages, gnp_link)
    if(p == gnp->gnp_prop)
      break;
  return gnp;
}


/**
 *
 */
static void
gu_nav_page_display(gu_tab_t *gt, gu_nav_page_t *gnp)
{
  int i, n;
  GtkWidget *c;

  if(gt->gt_page_current == gnp)
    return;

  n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(gt->gt_notebook));

  for(i = 0; i < n; i++) {
    c = gtk_notebook_get_nth_page(GTK_NOTEBOOK(gt->gt_notebook), i);
    if(c == gnp->gnp_pagebin)
      break;
  }

  if(i == n)
    abort();

  gtk_notebook_set_current_page(GTK_NOTEBOOK(gt->gt_notebook), i);

  gt->gt_page_current = gnp;
  gu_fullwindow_update(gnp->gnp_gt->gt_gw);
}



/**
 *
 */
void
gu_nav_pages(void *opaque, prop_event_t event, ...)
{
  gu_tab_t *gt = opaque;
  prop_t *p;
  int flags;
  gu_nav_page_t *gnp;

  va_list ap;
  va_start(ap, event);
  
  switch(event) {
  case PROP_ADD_CHILD:
    p = va_arg(ap, prop_t *);
    flags = va_arg(ap, int);
    gnp = gu_nav_page_create(gt, p);

    if(flags & PROP_ADD_SELECTED)
      gu_nav_page_display(gt, gnp);
    break;

  case PROP_SELECT_CHILD:
    p = va_arg(ap, prop_t *);
    gnp = gu_nav_page_find(gt, p);
    assert(gnp != NULL);
    gu_nav_page_display(gt, gnp);
    break;

  case PROP_DEL_CHILD:
    p = va_arg(ap, prop_t *);
    gnp = gu_nav_page_find(gt, p);
    assert(gnp != NULL);
    gtk_widget_destroy(gnp->gnp_pagebin);
    break;

  case PROP_SET_DIR:
  case PROP_SET_VOID:
    break;

  default:
    fprintf(stderr, 
	    "gu_nav_pages(): Can not handle event %d, aborting()\n", event);
    abort();
  }
}


/**
 *
 */
void
gu_page_set_fullwindow(gu_nav_page_t *gnp, int enable)
{
  gnp->gnp_fullwindow = enable;
  gu_fullwindow_update(gnp->gnp_gt->gt_gw);
}


/**
 *
 */
static void
gu_openerror_create(gu_nav_page_t *gnp)
{
  prop_sub_t *s;
  GtkWidget *l;

  gnp->gnp_pageroot = gtk_vbox_new(FALSE, 3);
  gtk_container_add(GTK_CONTAINER(gnp->gnp_pagebin), gnp->gnp_pageroot);
  
  l = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(l),
		       "<span size=\"x-large\">Unable to open page</span>");
  gtk_box_pack_start(GTK_BOX(gnp->gnp_pageroot), l, FALSE, FALSE, 0);
  gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);



  l = gtk_label_new(NULL);
  gtk_box_pack_start(GTK_BOX(gnp->gnp_pageroot), l, FALSE, FALSE, 0);
  gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);

 s = prop_subscribe(PROP_SUB_DIRECT_UPDATE,
		    PROP_TAG_NAME("self", "url"),
		    PROP_TAG_CALLBACK_STRING, gu_subscription_set_label, l,
		    PROP_TAG_COURIER, glibcourier,
		    PROP_TAG_NAMED_ROOT, gnp->gnp_prop, "self",
		    NULL);
  gu_unsubscribe_on_destroy(GTK_OBJECT(l), s);



  l = gtk_label_new(NULL);
  gtk_box_pack_start(GTK_BOX(gnp->gnp_pageroot), l, FALSE, FALSE, 0);
  gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_END);

 s = prop_subscribe(PROP_SUB_DIRECT_UPDATE,
		    PROP_TAG_NAME("self", "model", "error"),
		    PROP_TAG_CALLBACK_STRING, gu_subscription_set_label, l,
		    PROP_TAG_COURIER, glibcourier,
		    PROP_TAG_NAMED_ROOT, gnp->gnp_prop, "self",
		    NULL);
  gu_unsubscribe_on_destroy(GTK_OBJECT(l), s);

  gtk_widget_show_all(gnp->gnp_pageroot);
}
