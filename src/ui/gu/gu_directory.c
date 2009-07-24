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
static void
set_view(void *opaque, const char *str)
{
  gu_directory_t *gd = opaque;

  if(gd->gd_curview != NULL) {
    gtk_widget_destroy(gd->gd_curview);
    gd->gd_curview = NULL;
  }

  if(str == NULL)
    return;

  if(!strcmp(str, "list")) {
    gu_directory_list_create(gd);
  } else {
    TRACE(TRACE_ERROR, "GU", "Can not display directory view: %s", str);
  }
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
