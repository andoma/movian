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
#include "navigator.h"
#include "gu.h"


enum {
  URL_COLUMN,
  TITLE_COLUMN,
  ALBUM_COLUMN,
  ARTIST_COLUMN,
  N_COLUMNS
};


typedef struct dirnodecol {
  prop_sub_t *s;
  int col;
  struct dirnode *dn; // points back to struct
} dirnodecol_t;

typedef struct dirnode {
  GtkTreeIter iter;
  gu_nav_page_t *gnp;

  dirnodecol_t sub[N_COLUMNS];

} dirnode_t;


static const char **subpaths[] = {
  (const char *[]){"self", "url", NULL},
  (const char *[]){"self", "metadata", "title", NULL},
  (const char *[]){"self", "metadata", "album", NULL},
  (const char *[]){"self", "metadata", "artist", NULL},
};



/**
 *
 */
static void
gu_col_sub(void *opaque, prop_event_t event, ...)
{
  const char *str;
  dirnodecol_t *dnc = opaque;
  dirnode_t *dn = dnc->dn;

  va_list ap;
  va_start(ap, event);

  GValue gv = { 0, };
 
  switch(event) {
  case PROP_SET_STRING:
    g_value_init(&gv, G_TYPE_STRING);
    str = va_arg(ap, const char *);
    g_value_set_string(&gv, str);
    break;

  default:
    g_value_init(&gv, G_TYPE_STRING);
    g_value_set_string(&gv, "");
    break;
  }
  gtk_list_store_set_value(dn->gnp->gnp_list_store, &dn->iter, dnc->col, &gv);
  g_value_unset(&gv);
}

/**
 *
 */
static void
gu_node_sub(void *opaque, prop_event_t event, ...)
{
  gu_nav_page_t *gnp = opaque;
  prop_t *p;
  int flags, i;
  dirnode_t *dn;

  va_list ap;
  va_start(ap, event);
  
  switch(event) {
  case PROP_ADD_CHILD:
    p = va_arg(ap, prop_t *);
    flags = va_arg(ap, int);

    dn = calloc(1, sizeof(dirnode_t));
    dn->gnp = gnp;

    gtk_list_store_append(gnp->gnp_list_store, &dn->iter);

    for(i = 0; i < N_COLUMNS; i++) {
      dn->sub[i].col = i;
      dn->sub[i].dn = dn;
      dn->sub[i].s =
	prop_subscribe(0, 
		       PROP_TAG_NAME_VECTOR, subpaths[i],
		       PROP_TAG_CALLBACK, gu_col_sub, &dn->sub[i],
		       PROP_TAG_COURIER, gnp->gnp_gu->gu_pc, 
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
  gu_nav_page_t *gnp = user_data;
  GtkTreeIter iter;
  GValue gv = { 0, };

  gtk_tree_model_get_iter(GTK_TREE_MODEL(gnp->gnp_list_store), &iter, path);
  
  gtk_tree_model_get_value(GTK_TREE_MODEL(gnp->gnp_list_store), 
			   &iter, URL_COLUMN, &gv);

  if(G_VALUE_HOLDS_STRING(&gv))
    nav_open(g_value_get_string(&gv), NAV_OPEN_ASYNC);

  g_value_unset(&gv);
}


/**
 *
 */
void
gu_directory_create(gu_nav_page_t *gnp)
{
  GtkContainer *parent = GTK_CONTAINER(gnp->gnp_rootbox);
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkWidget *tv, *sw;

  gnp->gnp_list_store = gtk_list_store_new(N_COLUMNS,
					   G_TYPE_STRING,
					   G_TYPE_STRING,
					   G_TYPE_STRING,
					   G_TYPE_STRING
					   );


  prop_subscribe(0,
		 PROP_TAG_NAME_VECTOR, 
		 (const char *[]){"page", "nodes", NULL},
		 PROP_TAG_CALLBACK, gu_node_sub, gnp,
		 PROP_TAG_COURIER, gnp->gnp_gu->gu_pc, 
		 PROP_TAG_ROOT, gnp->gnp_prop, 
		 NULL);


  tv = gtk_tree_view_new_with_model(GTK_TREE_MODEL(gnp->gnp_list_store));

  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes("Title",
						    renderer,
						    "text", 
						    TITLE_COLUMN,
						    NULL);

  gtk_tree_view_append_column(GTK_TREE_VIEW(tv), column);


  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes("Album",
						    renderer,
						    "text", 
						    ALBUM_COLUMN,
						    NULL);

  gtk_tree_view_append_column(GTK_TREE_VIEW(tv), column);


  renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new_with_attributes("Artist",
						    renderer,
						    "text", 
						    ARTIST_COLUMN,
						    NULL);

  //  gtk_tree_view_set_headers_clickable(GTK_TREE_VIEW(gnp->gnp_root), TRUE);
  //  gtk_tree_view_set_enable_search(GTK_TREE_VIEW(gnp->gnp_root), TRUE);

  gtk_tree_view_append_column(GTK_TREE_VIEW(tv), column);

  g_signal_connect(G_OBJECT(tv), "row-activated", 
		   G_CALLBACK(row_activated), gnp);

  sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sw),
				      GTK_SHADOW_ETCHED_IN);
  gtk_container_add(GTK_CONTAINER(sw), tv);

  gnp->gnp_view = sw;
  gtk_container_add(parent, gnp->gnp_view);

  gtk_widget_show(tv);

  gtk_widget_show(sw);
 }
