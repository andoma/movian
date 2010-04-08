/*
 *  Showtime GTK frontend
 *  Copyright (C) 2009, 2010 Andreas Öman
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

#include <gdk/gdkx.h>

#include "showtime.h"
#include "navigator.h"
#include "gu.h"

#include "ui/linux/x11_common.h"


/**
 *
 */
static void
video_destroy(GtkObject *object, gpointer opaque)
{
  x11_vo_destroy(opaque);
}


/**
 *
 */
static gboolean
configure_event_callback(GtkWidget *w, GdkEventConfigure *e,
			 gpointer user_data)
{
  x11_vo_position(user_data, 0, 0, e->width, e->height);
  return FALSE;
}

/**
 *
 */
static gboolean 
expose_event_callback(GtkWidget *w, GdkEventExpose *e,
			 gpointer user_data)
{
  GdkColor col = {0};
  GdkGC *gc = gdk_gc_new(w->window);

  gdk_gc_set_foreground(gc, &col);
  gdk_draw_rectangle(w->window, gc, TRUE,
		     0, 0, w->allocation.width, w->allocation.height);

  g_object_unref(G_OBJECT(gc));
  return FALSE;
}



/**
 *
 */
void
gu_video_create(gu_nav_page_t *gnp)
{
  gtk_ui_t *gu = gnp->gnp_gu;
  struct video_output *vo;
  char errbuf[256];
  char buf[512];

  gnp->gnp_pageroot = gtk_drawing_area_new();
  gtk_container_add(GTK_CONTAINER(gnp->gnp_pagebin), gnp->gnp_pageroot);
  gtk_widget_show_all(gnp->gnp_pageroot);

  vo = x11_vo_create(GDK_WINDOW_XDISPLAY(gnp->gnp_pageroot->window),
		     GDK_WINDOW_XID(gnp->gnp_pageroot->window),
		     gu->gu_pc, gnp->gnp_prop, errbuf, sizeof(errbuf));

  if(vo == NULL) {
    gtk_widget_destroy(gnp->gnp_pageroot);

    snprintf(buf, sizeof(buf), "Unable to start video: %s", errbuf);

    gnp->gnp_pageroot = gtk_label_new(buf);
    gtk_container_add(GTK_CONTAINER(gnp->gnp_pagebin), gnp->gnp_pageroot);
    gtk_widget_show_all(gnp->gnp_pageroot);
    return;
  }

  g_signal_connect(G_OBJECT(gnp->gnp_pageroot), 
		   "expose_event",
                    G_CALLBACK(expose_event_callback), NULL);

  g_signal_connect(G_OBJECT(gnp->gnp_pageroot), 
		   "configure_event",
                    G_CALLBACK(configure_event_callback), vo);

  g_signal_connect(GTK_OBJECT(gnp->gnp_pageroot), 
		   "destroy", G_CALLBACK(video_destroy), vo);
}


