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
  g_object_set(G_OBJECT(opaque), "label", str ?: "", NULL);
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


/**
 *
 */
void
gu_cloner_init(gu_cloner_t *gc,
	       void *opaque,
	       void *addfunc,
	       void *delfunc,
	       size_t nodesize,
	       gtk_ui_t *gu)
{
  assert(nodesize >= sizeof(gu_cloner_node_t));

  gc->gc_opaque = opaque;
  gc->gc_add = addfunc;
  gc->gc_del = delfunc;
  gc->gc_nodesize = nodesize;
  gc->gc_gu = gu;
  TAILQ_INIT(&gc->gc_nodes);
}

/**
 *
 */
static gu_cloner_node_t *
cloner_node_find(gu_cloner_t *gc, prop_t *p)
{
  gu_cloner_node_t *gcn;
  TAILQ_FOREACH(gcn, &gc->gc_nodes, gcn_link)
    if(gcn->gcn_prop == p)
      return gcn;
  abort();
}


/**
 *
 */
static void
cloner_add(gu_cloner_t *gc, prop_t *p, prop_t *before)
{
  gu_cloner_node_t *gcn, *b;
  gcn = calloc(1, gc->gc_nodesize);
  gcn->gcn_prop = p;
  prop_ref_inc(p);

  b = before ? cloner_node_find(gc, before) : NULL;
  
  if(b == NULL) {
    TAILQ_INSERT_TAIL(&gc->gc_nodes, gcn, gcn_link);
  } else {
    TAILQ_INSERT_BEFORE(b, gcn, gcn_link);
  }

  gc->gc_add(gc->gc_gu, gc->gc_opaque, p, gcn, b);
}


/**
 *
 */
static void
cloner_del(gu_cloner_t *gc, prop_t *p)
{
  gu_cloner_node_t *gcn = cloner_node_find(gc, p);

  gc->gc_del(gc->gc_gu, gc->gc_opaque, gcn);
  
  prop_ref_dec(gcn->gcn_prop);
  TAILQ_REMOVE(&gc->gc_nodes, gcn, gcn_link);
  free(gcn);
}


/**
 *
 */
void
gu_cloner_subscription(void *opaque, prop_event_t event, ...)
{
  gu_cloner_t *gc = opaque;
  prop_t *p1, *p2;

  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_ADD_CHILD:
    cloner_add(gc, va_arg(ap, prop_t *), NULL);
    break;

  case PROP_ADD_CHILD_BEFORE:
    p1 = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    cloner_add(gc, p1, p2);
    break;

  case PROP_DEL_CHILD:
    cloner_del(gc, va_arg(ap, prop_t *));
    break;

  case PROP_SET_DIR:
  case PROP_SET_VOID:
    break;

  default:
    fprintf(stderr, 
	    "gu_home_sources(): Can not handle event %d, aborting()\n", event);
    abort();
  }
}




