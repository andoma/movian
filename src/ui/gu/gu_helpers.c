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
#include "gu.h"
#include "showtime.h"


/**
 *
 */
void
gu_subscription_set_label(void *opaque, const char *str)
{
  GtkLabel *l = opaque;
  gtk_label_set_text(l, str);
}



/**
 *
 */
void
gu_subscription_set_label_xl(void *opaque, const char *str)
{
  if(str != NULL) {
    char *m = g_markup_printf_escaped("<span size=\"x-large\">%s</span>", str);
    gtk_label_set_markup(GTK_LABEL(opaque), m);
    g_free(m);
  } else {
    gtk_label_set(GTK_LABEL(opaque), "");
  }
}


/**
 *
 */
void
gu_subscription_set_sensitivity(void *opaque, int on)
{
  GtkWidget *w = opaque;
  gtk_widget_set_sensitive(w, on);
}


/**
 *
 */
static void
gu_unsubscribe_callback(GtkObject *object, gpointer user_data)
{
  prop_unsubscribe(user_data);
}


/**
 *
 */
void
gu_unsubscribe_on_destroy(GtkObject *o, prop_sub_t *s)
{
  assert(s != NULL);
  g_signal_connect(o, "destroy", G_CALLBACK(gu_unsubscribe_callback), s);
}

