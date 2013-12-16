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
	       gtk_ui_t *gu,
	       int flags)
{
  assert(nodesize >= sizeof(gu_cloner_node_t));

  gc->gc_opaque = opaque;
  gc->gc_add = addfunc;
  gc->gc_del = delfunc;
  gc->gc_nodesize = nodesize;
  gc->gc_gu = gu;
  TAILQ_INIT(&gc->gc_nodes);
  gc->gc_flags = flags;
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
  gu_cloner_node_t *gcn, *b, *x;
  int pos;

  gcn = calloc(1, gc->gc_nodesize);
  gcn->gcn_prop = prop_ref_inc(p);

  b = before ? cloner_node_find(gc, before) : NULL;
  
  if(b == NULL) {
    TAILQ_INSERT_TAIL(&gc->gc_nodes, gcn, gcn_link);
  } else {
    TAILQ_INSERT_BEFORE(b, gcn, gcn_link);
  }

  if(gc->gc_flags & GU_CLONER_TRACK_POSITION) {

    x = TAILQ_PREV(gcn, gu_cloner_node_queue, gcn_link);
    pos = gcn->gcn_position = x != NULL ? x->gcn_position + 1 : 0;
    x = gcn;
    while((x = TAILQ_NEXT(x, gcn_link)) != NULL)
      x->gcn_position = ++pos;
  }
  gc->gc_add(gc->gc_gu, gc->gc_opaque, p, gcn, b, gcn->gcn_position);
}


/**
 *
 */
static void
cloner_del(gu_cloner_t *gc, gu_cloner_node_t *gcn, int updatepos)
{
  int pos = gcn->gcn_position;
  gu_cloner_node_t *x = gcn;

  gc->gc_del(gc->gc_gu, gc->gc_opaque, gcn);
  
  prop_ref_dec(gcn->gcn_prop);

  if(updatepos) {
    while((x = TAILQ_NEXT(x, gcn_link)) != NULL)
      x->gcn_position = pos++;
  }

  TAILQ_REMOVE(&gc->gc_nodes, gcn, gcn_link);
  free(gcn);


}


/**
 *
 */
void
gu_cloner_destroy(gu_cloner_t *gc)
{
  gu_cloner_node_t *gcn;

  while((gcn = TAILQ_FIRST(&gc->gc_nodes)) != NULL)
    cloner_del(gc, gcn, 0);
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
    cloner_del(gc, cloner_node_find(gc, va_arg(ap, prop_t *)),
	       !!(gc->gc_flags & GU_CLONER_TRACK_POSITION));
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





/**
 * Based on a content type string, return a GdkPixbuf
 * Returns a reference to be free'd by caller
 */
GdkPixbuf *
gu_contentstr_to_icon(const char *str, int height)
{
  char buf[PATH_MAX];

  if(str == NULL)
    return NULL;

  snprintf(buf, sizeof(buf), 
	   "%s/guresources/content-%s.png", showtime_dataroot(), str);
  return gu_pixbuf_get_sync(buf, -1, height);
}

/**
 *
 */
void
gu_set_icon_by_type(void *opaque, const char *str)
{
  GdkPixbuf *pb = gu_contentstr_to_icon(str, 16);
  g_object_set(G_OBJECT(opaque), "pixbuf", pb, NULL);
  if(pb != NULL)
    g_object_unref(G_OBJECT(pb));
}
