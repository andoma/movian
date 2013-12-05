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
#include "navigator.h"
#include "gu.h"
#include "showtime.h"


/**
 *
 */
typedef struct statusbar_entry {
  LIST_ENTRY(statusbar_entry) link;
  prop_t *p; // Originating property

  guint id;

} statusbar_entry_t;


/**
 *
 */
typedef struct statusbar {
  GtkWidget *bar;
  guint ctxid;
  prop_sub_t *sub;
  LIST_HEAD(, statusbar_entry) entries;
} statusbar_t;



/**
 *
 */
static void
notifications_update(void *opaque, prop_event_t event, ...)
{
  statusbar_t *sb = opaque;
  prop_t *p;
  statusbar_entry_t *sbe;
  char *buf;
  rstr_t *msg;
  int i, l;
  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_ADD_CHILD:
    p = va_arg(ap, prop_t *);

    if((msg = prop_get_string(p, "text", NULL)) != NULL) {
      buf = mystrdupa(rstr_get(msg));
      l = strlen(buf);
      for(i = 0; i < l; i++)
	if(buf[i] < ' ')
	  buf[i] = ' ';
      
      sbe = calloc(1, sizeof(statusbar_entry_t));
      sbe->p = prop_ref_inc(p);
      sbe->id = gtk_statusbar_push(GTK_STATUSBAR(sb->bar), sb->ctxid, buf);
      LIST_INSERT_HEAD(&sb->entries, sbe, link);
      rstr_release(msg);
    }
    break;

  case PROP_DEL_CHILD:
    p = va_arg(ap, prop_t *);

    LIST_FOREACH(sbe, &sb->entries, link)
      if(sbe->p == p)
	break;

    if(sbe == NULL)
      break;

    prop_ref_dec(sbe->p);
    gtk_statusbar_remove(GTK_STATUSBAR(sb->bar), sb->ctxid, sbe->id);
    LIST_REMOVE(sbe, link);
    free(sbe);
    break;

  default:
    break;
  }
}



/**
 *
 */
static void
notifications_dtor(GtkObject *object, gpointer user_data)
{
  statusbar_t *sb = user_data;

  prop_unsubscribe(sb->sub);
  free(sb);
}



/**
 *
 */
GtkWidget *
gu_statusbar_add(gu_window_t *gw, GtkWidget *parent)
{

  statusbar_t *sb = calloc(1, sizeof(statusbar_t));
  
  sb->bar = gtk_statusbar_new();
  gtk_statusbar_set_has_resize_grip(GTK_STATUSBAR(sb->bar), TRUE);
  gtk_box_pack_start(GTK_BOX(parent), sb->bar, FALSE, TRUE, 0);

  sb->ctxid =  gtk_statusbar_get_context_id(GTK_STATUSBAR(sb->bar),
					    "notifications");

  sb->sub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("global", "notifications", "nodes"),
		   PROP_TAG_CALLBACK, notifications_update, sb,
		   PROP_TAG_COURIER, glibcourier,
		   NULL);

  g_signal_connect(sb->bar, "destroy", G_CALLBACK(notifications_dtor), sb);

  return sb->bar;
}

