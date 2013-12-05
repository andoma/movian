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

#include <gdk/gdkx.h>

#include "showtime.h"
#include "navigator.h"
#include "gu.h"

#include "ui/linux/x11_common.h"

typedef struct gu_video {
  prop_t *gv_closeprop;
  prop_sub_t *gv_status_sub;

} gu_video_t;


/**
 *
 */
static void
video_set_playstatus(gu_video_t *gv, const char *v)
{
  if(v != NULL && !strcmp(v, "stop"))
    prop_set_int(gv->gv_closeprop, 1);
}

/**
 *
 */
static void
video_destroy(GtkWidget *w, gu_video_t *gv)
{
  prop_unsubscribe(gv->gv_status_sub);
  prop_ref_dec(gv->gv_closeprop);
  free(gv);
}

/**
 *
 */
static void
video_unrealize(GtkWidget *w, gpointer opaque)
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
  x11_vo_dimension(user_data, 0, 0, e->width, e->height);
  return FALSE;
}

/**
 *
 */
static gboolean 
expose_event_callback(GtkWidget *w, GdkEventExpose *e,
			 gpointer user_data)
{
  x11_vo_exposed(user_data);
  return FALSE;
}


/**
 *
 */
static gboolean
mouse_press(GtkWidget *w, GdkEventButton *event, gpointer user_data)
{
  gu_nav_page_t *gnp = user_data;

  if(event->button == 1) {
    gu_page_set_fullwindow(gnp, !gnp->gnp_fullwindow);
    return TRUE;
  }
  return FALSE;
}


/**
 *
 */
void
gu_video_create(gu_nav_page_t *gnp)
{
  struct video_output *vo;
  char errbuf[256];
  char buf[512];
  gu_video_t *gv;
  

  gnp->gnp_pageroot = gtk_drawing_area_new();
  gtk_container_add(GTK_CONTAINER(gnp->gnp_pagebin), gnp->gnp_pageroot);
  gtk_widget_show_all(gnp->gnp_pageroot);

  vo = x11_vo_create(GDK_WINDOW_XDISPLAY(gnp->gnp_pageroot->window),
		     GDK_WINDOW_XID(gnp->gnp_pageroot->window),
		     glibcourier, gnp->gnp_prop, errbuf, sizeof(errbuf));

  if(vo == NULL) {
    gtk_widget_destroy(gnp->gnp_pageroot);

    snprintf(buf, sizeof(buf), "Unable to start video: %s", errbuf);

    gnp->gnp_pageroot = gtk_label_new(buf);
    gtk_container_add(GTK_CONTAINER(gnp->gnp_pagebin), gnp->gnp_pageroot);
    gtk_widget_show_all(gnp->gnp_pageroot);
    return;
  }

  gv = calloc(1, sizeof(gu_video_t));

  gv->gv_closeprop = prop_ref_inc(prop_create(gnp->gnp_prop, "close"));

  gv->gv_status_sub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "media", "playstatus"),
		   PROP_TAG_CALLBACK_STRING, video_set_playstatus, gv,
		   PROP_TAG_COURIER, glibcourier,
		   PROP_TAG_NAMED_ROOT, gnp->gnp_prop, "self",
		   NULL);

  g_signal_connect(GTK_OBJECT(gnp->gnp_pageroot), 
		   "destroy", G_CALLBACK(video_destroy), gv);

  g_signal_connect(G_OBJECT(gnp->gnp_pageroot), 
		   "expose_event",
                    G_CALLBACK(expose_event_callback), vo);

  g_signal_connect_after(G_OBJECT(gnp->gnp_pageroot), 
		   "configure_event",
                    G_CALLBACK(configure_event_callback), vo);

  g_signal_connect(GTK_OBJECT(gnp->gnp_pageroot), 
		   "unrealize", G_CALLBACK(video_unrealize), vo);

  g_signal_connect(G_OBJECT(gnp->gnp_pageroot), 
		   "button-press-event", 
		   G_CALLBACK(mouse_press), gnp);

  gtk_widget_add_events(gnp->gnp_pageroot, 
			GDK_BUTTON_PRESS_MASK |
			GDK_BUTTON_RELEASE_MASK);

  gu_page_set_fullwindow(gnp, 1);
}


