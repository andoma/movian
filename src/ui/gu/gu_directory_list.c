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
#include "navigator.h"
#include "gu.h"
#include "gu_directory.h"

enum {
  URL_COLUMN,
  TYPE_COLUMN,
  NAME_COLUMN,
  ARTIST_COLUMN,
  DURATION_COLUMN,
  ALBUM_COLUMN,
  TRACKS_COLUMN,
  TRACKINDEX_COLUMN,
  N_COLUMNS
};


/**
 *
 */
static const char **subpaths[] = {
  [URL_COLUMN]      = PNVEC("self", "url"),
  [TYPE_COLUMN]     = PNVEC("self", "type"),
  [NAME_COLUMN]     = PNVEC("self", "metadata", "title"),
  [ARTIST_COLUMN]   = PNVEC("self", "metadata", "artist"),
  [DURATION_COLUMN] = PNVEC("self", "metadata", "duration"),
  [ALBUM_COLUMN]    = PNVEC("self", "metadata", "album"),
  [TRACKS_COLUMN]   = PNVEC("self", "metadata", "tracks"),
  [TRACKINDEX_COLUMN] = PNVEC("self", "metadata", "trackindex"),
};


/**
 *
 */
static GType coltypes[] = {
  [URL_COLUMN]      = G_TYPE_STRING,
  [TYPE_COLUMN]     = G_TYPE_STRING,
  [NAME_COLUMN]     = G_TYPE_STRING,
  [ARTIST_COLUMN]   = G_TYPE_STRING,
  [DURATION_COLUMN] = G_TYPE_FLOAT,
  [ALBUM_COLUMN]    = G_TYPE_STRING,
  [TRACKS_COLUMN]   = G_TYPE_INT,
  [TRACKINDEX_COLUMN] = G_TYPE_INT,
};


/**
 *
 */
typedef struct column {
  GtkTreeViewColumn *col;
  int contents; // Cells with actual contents
} column_t;



/**
 *
 */
typedef struct directory_list {

  GtkWidget *scrollbox;
  GtkWidget *tree;

  GtkListStore *model;

  gtk_ui_t *gu;

  column_t columns[N_COLUMNS];

  prop_sub_t *node_sub;

  LIST_HEAD(, dirnode) nodes;

  char **parenturlptr;

} directory_list_t;


/**
 *
 */
typedef struct cell {
  prop_sub_t *s;
  int idx;
  struct dirnode *dn; // points back to struct
} cell_t;


/**
 *
 */
typedef struct dirnode {
  LIST_ENTRY(dirnode) link;
  prop_t *p;
  GtkTreeIter iter;
  directory_list_t *dir;

  cell_t cells[N_COLUMNS];

} dirnode_t;


/**
 * Based on a content type string, return a GdkPixbuf
 * Returns a reference to be free'd by caller
 */
static GdkPixbuf *
contentstr_to_icon(const char *str, int height)
{
  char buf[100];

  if(str == NULL)
    return NULL;

  snprintf(buf, sizeof(buf), 
	   SHOWTIME_GU_RESOURCES_URL"/content-%s.png", str);
  return gu_pixbuf_get_sync(buf, -1, height);
}



/**
 * Make the column visible if we have any content in it
 */
static void
make_column_visible(directory_list_t *d, int idx)
{
  column_t *c;
  c = &d->columns[idx];
  if(c->col != NULL && c->contents == 0) {
    gtk_tree_view_column_set_visible(c->col, TRUE);
    c->contents = 1;
  }
}


/**
 *
 */
static void
gu_col_set(void *opaque, prop_event_t event, ...)
{
  cell_t *c = opaque;
  dirnode_t *dn = c->dn;
  directory_list_t *d = dn->dir;

  GValue gv = { 0, };

  va_list ap;
  va_start(ap, event);

  switch(event) {
  case PROP_SET_STRING:
    g_value_init(&gv, G_TYPE_STRING);
    g_value_set_string(&gv, va_arg(ap, char *));
    break;
  case PROP_SET_INT:
    g_value_init(&gv, G_TYPE_INT);
    g_value_set_int(&gv, va_arg(ap, int));
    break;
  case PROP_SET_FLOAT:
    g_value_init(&gv, G_TYPE_FLOAT);
    g_value_set_float(&gv, va_arg(ap, double));
    break;
  default:
    return;
  }

  gtk_list_store_set_value(dn->dir->model, &dn->iter, c->idx, &gv);
  g_value_unset(&gv);
  make_column_visible(d, c->idx);
}


/**
 *
 */
static void
dirnode_destroy(dirnode_t *dn, int remove)
{
  int i;

  if(remove)
    gtk_list_store_remove(dn->dir->model, &dn->iter);

  LIST_REMOVE(dn, link);

  for(i = 0; i < N_COLUMNS; i++)
    if(dn->cells[i].s != NULL)
      prop_unsubscribe(dn->cells[i].s);

  free(dn);
}



/**
 *
 */
static void
gu_node_sub(void *opaque, prop_event_t event, ...)
{
  directory_list_t *d = opaque;
  prop_t *p;
  int flags, i;
  dirnode_t *dn;
  cell_t *c;

  va_list ap;
  va_start(ap, event);
  
  switch(event) {
  case PROP_ADD_CHILD:
    p = va_arg(ap, prop_t *);
    flags = va_arg(ap, int);

    dn = calloc(1, sizeof(dirnode_t));
    dn->dir = d;
    dn->p = p;
    prop_ref_inc(p);

    LIST_INSERT_HEAD(&d->nodes, dn, link);

    gtk_list_store_append(d->model, &dn->iter);

    for(i = 0; i < N_COLUMNS; i++) {
      c = &dn->cells[i];
      c->idx = i;
      c->dn = dn;

      c->s = prop_subscribe(0, 
			    PROP_TAG_NAME_VECTOR, subpaths[i],
			    PROP_TAG_CALLBACK, gu_col_set, c,
			    PROP_TAG_COURIER, d->gu->gu_pc,
			    PROP_TAG_NAMED_ROOT, p, "self",
			    NULL);
    }
    break;

  case PROP_DEL_CHILD:
    p = va_arg(ap, prop_t *);

    LIST_FOREACH(dn, &d->nodes, link)
      if(dn->p == p)
	break;

    if(dn != NULL)
      dirnode_destroy(dn, 1);
    break;

  case PROP_SET_DIR:
  case PROP_SET_VOID:
    break;

  default:
    fprintf(stderr, 
	    "gu_node_sub(): Can not handle event %d, aborting()\n", event);
    abort();
  }
}


/**
 *
 */
static void
row_activated(GtkTreeView *tree_view, GtkTreePath *path,
	      GtkTreeViewColumn *column, gpointer user_data)
{
  directory_list_t *d = user_data;
  const char *str;
  GtkTreeIter iter;
  GValue gv = { 0, };

  gtk_tree_model_get_iter(GTK_TREE_MODEL(d->model), &iter, path);
  
  gtk_tree_model_get_value(GTK_TREE_MODEL(d->model), 
			   &iter, URL_COLUMN, &gv);

  if(G_VALUE_HOLDS_STRING(&gv)) {
    str = g_value_get_string(&gv);
    if(str != NULL)
      nav_open(str, NULL, *d->parenturlptr, NAV_OPEN_ASYNC);
  }

  g_value_unset(&gv);
}


/**
 *
 */
static void
init_text_col(directory_list_t *d, const char *title, int idx, int autosize)
{
  GtkCellRenderer *r;
  GtkTreeViewColumn *c;

  r = gtk_cell_renderer_text_new();
  if(autosize)
    g_object_set(G_OBJECT(r), "ellipsize", PANGO_ELLIPSIZE_END, NULL);

  c = gtk_tree_view_column_new_with_attributes(title, r,
					       "text", idx,
					       NULL);
  if(autosize) {

    //  gtk_tree_view_column_set_min_width(c, 180);
    //  gtk_tree_view_column_set_max_width(c, 200);
    gtk_tree_view_column_set_expand(c, TRUE);
    //  gtk_tree_view_column_set_fixed_width(c, 180);
    //  gtk_tree_view_column_set_sizing(c, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_resizable(c, TRUE);
  }

  gtk_tree_view_append_column(GTK_TREE_VIEW(d->tree), c);
  gtk_tree_view_column_set_visible(c, FALSE);
  d->columns[idx].col = c;
}


/**
 *
 */
static void
duration2txt(GtkTreeViewColumn *tree_column, GtkCellRenderer *cell,
	     GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
  char buf[20];
  int i;
  GValue gv = { 0, };

  gtk_tree_model_get_value(model, iter, DURATION_COLUMN, &gv);
  
  if(G_VALUE_HOLDS_FLOAT(&gv)) {
    i = g_value_get_float(&gv);
    if(i == 0)
      buf[0] = 0;
    else
      snprintf(buf, sizeof(buf), "%d:%02d", i / 60, i % 60);
    g_object_set(cell, "text", buf, NULL);
  }
  g_value_unset(&gv);
}


/**
 *
 */
static void
init_duration_col(directory_list_t *d, const char *title, int idx)
{
  GtkCellRenderer *r;
  GtkTreeViewColumn *c;

  r = gtk_cell_renderer_text_new();
  c = gtk_tree_view_column_new_with_attributes(title, r,
					       NULL);

  gtk_tree_view_column_set_cell_data_func(c, r, duration2txt, NULL, NULL);


  gtk_tree_view_append_column(GTK_TREE_VIEW(d->tree), c);
  gtk_tree_view_column_set_visible(c, FALSE);
  d->columns[idx].col = c;
}


/**
 *
 */
static void
type2pixbuf(GtkTreeViewColumn *tree_column, GtkCellRenderer *cell,
	     GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
  GValue gv = { 0, };

  gtk_tree_model_get_value(model, iter, TYPE_COLUMN, &gv);
  if(G_VALUE_HOLDS_STRING(&gv)) {
    GdkPixbuf *pb = contentstr_to_icon(g_value_get_string(&gv), 16);
    g_object_set(G_OBJECT(cell), 
		 "pixbuf", pb,
		 NULL);
    if(pb != NULL)
      g_object_unref(G_OBJECT(pb));

  }
  g_value_unset(&gv);
}


/**
 *
 */
static void
init_type_col(directory_list_t *d, const char *title, int idx)
{
  GtkCellRenderer *r;
  GtkTreeViewColumn *c;

  r = gtk_cell_renderer_pixbuf_new();
  c = gtk_tree_view_column_new_with_attributes(title,
					       r,
					       NULL);

  gtk_tree_view_column_set_cell_data_func(c, r, type2pixbuf, NULL, NULL);

  gtk_tree_view_append_column(GTK_TREE_VIEW(d->tree), c);
  gtk_tree_view_column_set_visible(c, TRUE);
  d->columns[idx].col = c;
}


/**
 *
 */
static void
view_list_header_set_title(void *opaque, const char *str)
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
static void
view_list_header_set_icon(void *opaque, const char *str)
{
  GdkPixbuf *pb = contentstr_to_icon(str, 16);
  g_object_set(G_OBJECT(opaque), "pixbuf", pb, NULL);
  if(pb != NULL)
    g_object_unref(G_OBJECT(pb));
}


/**
 *
 */
static void
add_headers(gtk_ui_t *gu, GtkWidget *parent, prop_t *root)
{
  GtkWidget *hbox, *w;
  prop_sub_t *s;

  hbox = gtk_hbox_new(FALSE, 1);
  gtk_box_pack_start(GTK_BOX(parent), hbox, FALSE, TRUE, 0);

  /* Image */
  w = gtk_image_new();
  gtk_misc_set_alignment(GTK_MISC(w), 0.5, 0.5);
  gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, TRUE, 0);

  s = prop_subscribe(0,
		     PROP_TAG_NAME("self", "type"),
		     PROP_TAG_CALLBACK_STRING, view_list_header_set_icon, w,
		     PROP_TAG_COURIER, gu->gu_pc, 
		     PROP_TAG_NAMED_ROOT, root, "self",
		     NULL);

  gu_unsubscribe_on_destroy(GTK_OBJECT(w), s);


  /* Title */
  w = gtk_label_new("");
  gtk_misc_set_alignment(GTK_MISC(w), 0, 0);
  gtk_label_set_ellipsize(GTK_LABEL(w), PANGO_ELLIPSIZE_END);

  gtk_box_pack_start(GTK_BOX(hbox), w, TRUE, TRUE, 0);

  s = prop_subscribe(0,
		     PROP_TAG_NAME("self", "title"),
		     PROP_TAG_CALLBACK_STRING, view_list_header_set_title, w,
		     PROP_TAG_COURIER, gu->gu_pc, 
		     PROP_TAG_NAMED_ROOT, root, "self",
		     NULL);

  gu_unsubscribe_on_destroy(GTK_OBJECT(w), s);
}


/**
 *
 */
static void
directory_list_destroy(GtkObject *object, gpointer opaque)
{
  directory_list_t *d = opaque;
  dirnode_t *dn;

  prop_unsubscribe(d->node_sub);

  while((dn = LIST_FIRST(&d->nodes)) != NULL)
    dirnode_destroy(dn, 0);

  g_object_unref(G_OBJECT(d->model));
  free(d);
}




/**
 *
 */
GtkWidget *
gu_directory_list_create(gtk_ui_t *gu, prop_t *root,
			 char **parenturlptr, int flags)
{
  directory_list_t *d = calloc(1, sizeof(directory_list_t));
  GtkWidget *view;
  d->model = gtk_list_store_newv(N_COLUMNS, coltypes);

  d->gu = gu;
  d->parenturlptr = parenturlptr;

  d->node_sub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "nodes"),
		   PROP_TAG_CALLBACK, gu_node_sub, d,
		   PROP_TAG_COURIER, gu->gu_pc, 
		   PROP_TAG_NAMED_ROOT, root, "self",
		   NULL);


  d->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(d->model));
  gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(d->tree), TRUE);

  if(flags & GU_DIR_COL_TYPE)
    init_type_col(d,     "",         TYPE_COLUMN);

  if(flags & GU_DIR_COL_TRACKINDEX)
    init_text_col(d, "#",        TRACKINDEX_COLUMN, 0);

  init_text_col(d,     "Name",     NAME_COLUMN, 1);

  if(flags & GU_DIR_COL_DURATION)
    init_duration_col(d, "Duration", DURATION_COLUMN);

  if(flags & GU_DIR_COL_ARTIST)
    init_text_col(d,     "Artist",   ARTIST_COLUMN, 1);

  if(flags & GU_DIR_COL_ALBUM)
    init_text_col(d,     "Album",    ALBUM_COLUMN, 1);

  if(flags & GU_DIR_COL_NUM_TRACKS)
    init_text_col(d,     "Tracks",   TRACKS_COLUMN, 0);

  g_signal_connect(G_OBJECT(d->tree), "row-activated", 
		   G_CALLBACK(row_activated), d);


  /* Page vbox */

  view = gtk_vbox_new(FALSE, 1);

  if(flags & GU_DIR_HEADERS)
    add_headers(d->gu, view, root);

  /* Scrollbox with tree */

  d->scrollbox = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(d->scrollbox),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(d->scrollbox),
				      GTK_SHADOW_ETCHED_IN);
  gtk_container_add(GTK_CONTAINER(d->scrollbox), d->tree);
  gtk_box_pack_start(GTK_BOX(view), d->scrollbox, TRUE, TRUE, 0);

  /* Attach to parent */

  gtk_widget_show_all(view);

  g_signal_connect(GTK_OBJECT(view), 
		   "destroy", G_CALLBACK(directory_list_destroy), d);
  return view;
}
