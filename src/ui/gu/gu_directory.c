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
#include "gu_directory.h"

/**
 *
 */
typedef struct gu_directory {
  GtkWidget *gd_curview;       /* Root widget for current view.
				  Must always be the only child of
				  gnp->gnp_pageroot (which is a vbox) */

  gtk_ui_t *gd_gu;
  gu_nav_page_t *gd_gnp;
  prop_sub_t *gd_view_sub;
} gu_directory_t;




/**
 *
 */
static void
set_view(void *opaque, const char *str)
{
  gu_directory_t *gd = opaque;
  gu_nav_page_t *gnp = gd->gd_gnp;
  GtkWidget *w;

  if(gd->gd_curview != NULL) {
    gtk_widget_destroy(gd->gd_curview);
    gd->gd_curview = NULL;
  }

  if(str == NULL)
    return;

  if(!strcmp(str, "list")) {
    w = gu_directory_list_create(gd->gd_gu, gnp->gnp_prop, &gnp->gnp_url,
				 GU_DIR_VISIBLE_HEADERS |
				 GU_DIR_HEADERS |
				 GU_DIR_SCROLLBOX |
				 GU_DIR_COL_TYPE |
				 GU_DIR_COL_ARTIST |
				 GU_DIR_COL_DURATION |
				 GU_DIR_COL_NUM_TRACKS |
				 GU_DIR_COL_ALBUM);
  } else if(!strcmp(str, "album")) {
    w = gu_directory_album_create(gd->gd_gu, gnp->gnp_prop, &gnp->gnp_url);
  } else if(!strcmp(str, "albumcollection")) {
    w = gu_directory_albumcollection_create(gd->gd_gu, gnp->gnp_prop, 
					    &gnp->gnp_url);
  } else {
    TRACE(TRACE_ERROR, "GU", "Can not display directory view: %s", str);
    return;
  }

  gd->gd_curview = w;
  gtk_container_add(GTK_CONTAINER(gnp->gnp_pageroot), w);
}


/**
 *
 */
static void
gu_directory_destroy(GtkObject *object, gpointer opaque)
{
  gu_directory_t *gd = opaque;

  prop_unsubscribe(gd->gd_view_sub);
  free(gd);
}


/**
 *
 */
void
gu_directory_create(gu_nav_page_t *gnp)
{
  gu_directory_t *gd = calloc(1, sizeof(gu_directory_t));

  gd->gd_gu = gnp->gnp_gu;
  gd->gd_gnp = gnp;

  gd->gd_view_sub =
    prop_subscribe(0,
		   PROP_TAG_NAME("page", "view"),
		   PROP_TAG_CALLBACK_STRING, set_view, gd,
		   PROP_TAG_COURIER, gnp->gnp_gu->gu_pc, 
		   PROP_TAG_ROOT, gnp->gnp_prop, 
		   NULL);

  gnp->gnp_pageroot = gtk_vbox_new(FALSE, 1);

  gtk_container_add(GTK_CONTAINER(gnp->gnp_pagebin), gnp->gnp_pageroot);
  gtk_widget_show_all(gnp->gnp_pageroot);

  g_signal_connect(GTK_OBJECT(gnp->gnp_pageroot), 
		   "destroy", G_CALLBACK(gu_directory_destroy), gd);
}
