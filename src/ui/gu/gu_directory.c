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

enum {
  URL_COLUMN,
  NAME_COLUMN,
  ARTIST_COLUMN,
  DURATION_COLUMN,
  ALBUM_COLUMN,
  TRACKS_COLUMN,
  N_COLUMNS
};


/**
 *
 */
static const char **subpaths[] = {
  [URL_COLUMN]      = PNVEC("self", "url"),
  [NAME_COLUMN]     = PNVEC("self", "metadata", "title"),
  [ARTIST_COLUMN]   = PNVEC("self", "metadata", "artist"),
  [DURATION_COLUMN] = PNVEC("self", "metadata", "duration"),
  [ALBUM_COLUMN]    = PNVEC("self", "metadata", "album"),
  [TRACKS_COLUMN]   = PNVEC("self", "metadata", "tracks"),
};


/**
 *
 */
static GType coltypes[] = {
  [URL_COLUMN]      = G_TYPE_STRING,
  [NAME_COLUMN]     = G_TYPE_STRING,
  [ARTIST_COLUMN]   = G_TYPE_STRING,
  [DURATION_COLUMN] = G_TYPE_FLOAT,
  [ALBUM_COLUMN]    = G_TYPE_STRING,
  [TRACKS_COLUMN]   = G_TYPE_INT,
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
typedef struct directory {

  GtkWidget *box, *tree;
  GtkListStore *model;

  gtk_ui_t *gu;

  column_t columns[N_COLUMNS];

  prop_sub_t *node_sub;

  LIST_HEAD(, dirnode) nodes;

} directory_t;


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

  GtkTreeIter iter;
  directory_t *dir;

  cell_t cells[N_COLUMNS];

} dirnode_t;


/**
 * Make the column visible if we have any content in it
 */
static void
make_column_visible(directory_t *d, int idx)
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
  directory_t *d = dn->dir;

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
dirnode_destroy(dirnode_t *dn)
{
  int i;
  LIST_REMOVE(dn, link);

  for(i = 0; i < N_COLUMNS; i++)
    prop_unsubscribe(dn->cells[i].s);

  free(dn);
}



/**
 *
 */
static void
gu_node_sub(void *opaque, prop_event_t event, ...)
{
  directory_t *d = opaque;
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
  default:
    break;
  }
}


/**
 *
 */
static void
row_activated(GtkTreeView *tree_view, GtkTreePath *path,
	      GtkTreeViewColumn *column, gpointer user_data)
{
  directory_t *d = user_data;
  const char *str;
  GtkTreeIter iter;
  GValue gv = { 0, };

  gtk_tree_model_get_iter(GTK_TREE_MODEL(d->model), &iter, path);
  
  gtk_tree_model_get_value(GTK_TREE_MODEL(d->model), 
			   &iter, URL_COLUMN, &gv);

  if(G_VALUE_HOLDS_STRING(&gv)) {
    str = g_value_get_string(&gv);
    if(str != NULL)
      nav_open(str, NAV_OPEN_ASYNC);
  }

  g_value_unset(&gv);
}


/**
 *
 */
static void
init_text_col(directory_t *d, const char *title, int idx)
{
  GtkCellRenderer *r;
  GtkTreeViewColumn *c;

  r = gtk_cell_renderer_text_new();
  c = gtk_tree_view_column_new_with_attributes(title,
					       r,
					       "text", idx,
					       NULL);

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
init_duration_col(directory_t *d, const char *title, int idx)
{
  GtkCellRenderer *r;
  GtkTreeViewColumn *c;

  r = gtk_cell_renderer_text_new();
  c = gtk_tree_view_column_new_with_attributes(title,
					       r,
					       "text", idx,
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
gu_directory_destroy(void *opaque)
{
  directory_t *d = opaque;
  dirnode_t *dn;

  prop_unsubscribe(d->node_sub);

  while((dn = LIST_FIRST(&d->nodes)) != NULL)
    dirnode_destroy(dn);

  g_object_unref(G_OBJECT(d->model));

  gtk_widget_destroy(d->box);
  free(d);
}


/**
 *
 */
void
gu_directory_create(gu_nav_page_t *gnp)
{
  directory_t *d = calloc(1, sizeof(directory_t));
  d->model = gtk_list_store_newv(N_COLUMNS, coltypes);

  d->gu = gnp->gnp_gu;

  d->node_sub = prop_subscribe(0,
			       PROP_TAG_NAME_VECTOR, 
			       (const char *[]){"page", "nodes", NULL},
			       PROP_TAG_CALLBACK, gu_node_sub, d,
			       PROP_TAG_COURIER, gnp->gnp_gu->gu_pc, 
			       PROP_TAG_ROOT, gnp->gnp_prop, 
			       NULL);


  d->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(d->model));
  gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(d->tree), TRUE);

  init_text_col(d,     "Name",     NAME_COLUMN);
  init_text_col(d,     "Artist",   ARTIST_COLUMN);
  init_duration_col(d, "Duration", DURATION_COLUMN);
  init_text_col(d,     "Album",    ALBUM_COLUMN);
  init_text_col(d,     "Tracks",   TRACKS_COLUMN);

  g_signal_connect(G_OBJECT(d->tree), "row-activated", 
		   G_CALLBACK(row_activated), d);

  d->box = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(d->box),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(d->box),
				      GTK_SHADOW_ETCHED_IN);
  gtk_container_add(GTK_CONTAINER(d->box), d->tree);

  gtk_container_add(GTK_CONTAINER(gnp->gnp_rootbox), d->box);

  gtk_widget_show(d->tree);
  gtk_widget_show(d->box);

  gnp->gnp_destroy = gu_directory_destroy;
  gnp->gnp_opaque = d;

}
