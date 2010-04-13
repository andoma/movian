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
#include "gu_directory_store.h"

#include "gu_cell_bar.h"


/**
 *
 */
typedef struct column {
  GtkTreeViewColumn *col;
  GtkCellRenderer *r;
  struct directory_list *d;
  int idx;
} column_t;



/**
 *
 */
typedef struct directory_list {

  GtkWidget *scrollbox;
  GtkWidget *tree;

  GuDirStore *model;

  gtk_ui_t *gu;

  column_t columns[GDS_COL_num];

  LIST_HEAD(, dirnode) nodes;

  gds_cell_t *currentcell;
  gds_cell_t *presscell;

  prop_t *psource;

} directory_list_t;


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
column_activated(GuDirStore *gds, int col, gpointer user_data)
{
  directory_list_t *d = user_data;
  column_t *c = &d->columns[col];
  if(c->col != NULL)
    gtk_tree_view_column_set_visible(c->col, TRUE);
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

  gtk_tree_model_get_value(GTK_TREE_MODEL(d->model), &iter, GDS_COL_URL, &gv);
  if(G_VALUE_HOLDS_STRING(&gv)) {
    str = g_value_get_string(&gv);
    if(str != NULL)
      nav_open(str, NULL, d->psource);
  }

  g_value_unset(&gv);
}


/**
 *
 */
static void
repaint_cell(directory_list_t *d, gds_cell_t *c)
{
  GtkTreeIter iter;
  GtkTreePath *path;

  gu_dir_store_iter_from_cell(c, &iter);
  path = gtk_tree_model_get_path(GTK_TREE_MODEL(d->model),  &iter);
  gtk_tree_model_row_changed(GTK_TREE_MODEL(d->model), path, &iter);
  gtk_tree_path_free(path);
}


/**
 *
 */

#define MOUSE_MOTION      1
#define MOUSE_LEFTCLICK   2
#define MOUSE_LEFTRELEASE 3

static int
mouse_do(directory_list_t *d, GtkTreeView *tree_view, int action, int x, int y)
{
  GtkTreePath *path;
  GtkTreeViewColumn *column;
  int cx, cy, col;
  GtkTreeIter iter;
  int r = 0;
  gds_cell_t *c;
  const char *url;

  if(!gtk_tree_view_get_path_at_pos(tree_view, x, y, &path, &column, &cx, &cy))
    return 0;
  
  for(col = 0; col < GDS_COL_num; col++) {
    if(d->columns[col].col == column)
      break;
  }

  gtk_tree_model_get_iter(GTK_TREE_MODEL(d->model), &iter, path);
  c = gu_dir_store_get_cell(GU_DIR_STORE(d->model), &iter, col);

  switch(action) {

  case MOUSE_MOTION:
    if(d->currentcell != c) {
      if(d->currentcell)
	repaint_cell(d, d->currentcell);
      
      url = gu_dir_store_url_from_cell(c);
      if(url != NULL) {
	d->currentcell = c;
	repaint_cell(d, d->currentcell);
      } else {
	d->currentcell = NULL;
      }
    }
    break;
      
  case MOUSE_LEFTCLICK:
    url = gu_dir_store_url_from_cell(c);
    if(url != NULL) {
      d->presscell = c;
      r = 1;
    }
    break;

  case MOUSE_LEFTRELEASE:
    url = gu_dir_store_url_from_cell(c);
    if(url != NULL && c == d->presscell) {
      nav_open(url, NULL, NULL);
      r = 1;
    }
    d->presscell = NULL;
    break;
  }
  gtk_tree_path_free(path);
  return r;
}


/**
 *
 */
static void
mouse_motion(GtkTreeView *tree_view, GdkEventMotion *event, gpointer user_data)
{
  mouse_do(user_data, tree_view, MOUSE_MOTION, event->x, event->y);
}


/**
 *
 */
static void
mouse_leave(GtkTreeView *tree_view, GdkEventCrossing *event, gpointer user_data)
{
  directory_list_t *d = user_data;

  if(d->currentcell)
    repaint_cell(d, d->currentcell);
  
  d->currentcell = NULL;
}



/**
 *
 */
static gboolean
mouse_press(GtkTreeView *tree_view, GdkEventButton *event, gpointer user_data)
{
  return mouse_do(user_data, tree_view, MOUSE_LEFTCLICK, event->x, event->y);
}

/**
 *
 */
static gboolean
mouse_release(GtkTreeView *tree_view, GdkEventButton *event, gpointer user_data)
{
  return mouse_do(user_data, tree_view, MOUSE_LEFTRELEASE, event->x, event->y);
}


static gboolean
after_scroll(GtkTreeView *tree_view, GdkEvent *event, gpointer user_data)
{
  directory_list_t *d = user_data;
  if(event->type == GDK_SCROLL) {
    GdkEventScroll *e = (GdkEventScroll *)event;
    mouse_do(user_data, GTK_TREE_VIEW(d->tree), MOUSE_MOTION, e->x, e->y);
  }

  return 0;
}

/**
 *
 */
static void
link2txt(GtkTreeViewColumn *tree_column, GtkCellRenderer *cell,
	 GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
  column_t *col = data;
  directory_list_t *d = col->d;
  GValue gv = { 0, };
  PangoUnderline ul;

  void *c = gu_dir_store_get_cell(GU_DIR_STORE(model), iter, col->idx);

  gtk_tree_model_get_value(model, iter, col->idx, &gv);

  ul = d->currentcell == c ? PANGO_UNDERLINE_SINGLE : PANGO_UNDERLINE_NONE;

  g_object_set(cell, "underline", ul, NULL);

  if(G_VALUE_HOLDS_STRING(&gv))
    g_object_set(cell, "text", g_value_get_string(&gv), NULL);

  g_value_unset(&gv);
}

/**
 *
 */
static void
init_text_col(directory_list_t *d, const char *title, int idx, int autosize,
	      int link)
{
  GtkCellRenderer *r;
  GtkTreeViewColumn *c;

  r = gtk_cell_renderer_text_new();
  if(autosize)
    g_object_set(G_OBJECT(r), "ellipsize", PANGO_ELLIPSIZE_END, NULL);

  if(link) {

    c = gtk_tree_view_column_new_with_attributes(title, r, NULL);
    
    gtk_tree_view_column_set_cell_data_func(c, r, link2txt, 
					    &d->columns[idx], NULL);
    
  } else {
    c = gtk_tree_view_column_new_with_attributes(title, r, 
						 "text", idx, 
						 NULL);
  }

  if(autosize) {
    gtk_tree_view_column_set_expand(c, TRUE);
    gtk_tree_view_column_set_resizable(c, TRUE);
  }

  gtk_tree_view_append_column(GTK_TREE_VIEW(d->tree), c);
  gtk_tree_view_column_set_visible(c, FALSE);
  d->columns[idx].col = c;
  d->columns[idx].idx = idx;
  d->columns[idx].d   = d;
  d->columns[idx].r   = r;
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

  gtk_tree_model_get_value(model, iter, GDS_COL_DURATION, &gv);
  
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
init_bar_col(directory_list_t *d, const char *title, int idx)
{
  GtkCellRenderer *r;
  GtkTreeViewColumn *c;

  r = custom_cell_renderer_progress_new();
  c = gtk_tree_view_column_new_with_attributes(title, r, NULL);
  
  gtk_tree_view_column_add_attribute(c, r, "percentage", idx);

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

  gtk_tree_model_get_value(model, iter, GDS_COL_TYPE, &gv);
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

  g_object_unref(G_OBJECT(d->model));
  prop_ref_dec(d->psource);
  free(d);
}




/**
 *
 */
GtkWidget *
gu_directory_list_create(gtk_ui_t *gu, prop_t *root, int flags)
{
  directory_list_t *d = calloc(1, sizeof(directory_list_t));
  GtkWidget *view;

  d->psource = prop_create(root, "source");
  prop_ref_inc(d->psource);

  d->gu = gu;

  d->model = gu_dir_store_new(gu, d->psource);
  d->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(d->model));

  gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(d->tree), TRUE);

  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(d->tree),
				    !!(flags & GU_DIR_VISIBLE_HEADERS));

  if(flags & GU_DIR_COL_TYPE)
    init_type_col(d, "", GDS_COL_TYPE);

  if(flags & GU_DIR_COL_TRACKINDEX)
    init_text_col(d, "#", GDS_COL_TRACKINDEX, 0, 0);

  init_text_col(d, "Name", GDS_COL_NAME, 1, 0);

  if(flags & GU_DIR_COL_DURATION)
    init_duration_col(d, "Duration", GDS_COL_DURATION);

  if(flags & GU_DIR_COL_ARTIST)
    init_text_col(d,     "Artist",   GDS_COL_ARTIST, 1, 1);

  if(flags & GU_DIR_COL_ALBUM)
    init_text_col(d,     "Album",    GDS_COL_ALBUM, 1, 1);

  if(flags & GU_DIR_COL_NUM_TRACKS)
    init_text_col(d,     "Tracks",   GDS_COL_TRACKS, 0, 0);

  if(flags & GU_DIR_COL_POPULARITY)
    init_bar_col(d, "Popularity", GDS_COL_POPULARITY);

  g_signal_connect(G_OBJECT(d->tree), "row-activated", 
		   G_CALLBACK(row_activated), d);

  g_signal_connect(G_OBJECT(d->tree), "button-press-event", 
		   G_CALLBACK(mouse_press), d);

  g_signal_connect(G_OBJECT(d->tree), "button-release-event", 
		   G_CALLBACK(mouse_release), d);

  g_signal_connect(G_OBJECT(d->tree), "motion-notify-event", 
		   G_CALLBACK(mouse_motion), d);

  g_signal_connect(G_OBJECT(d->tree), "leave-notify-event", 
		   G_CALLBACK(mouse_leave), d);

  g_signal_connect(G_OBJECT(d->model), 
		   "column-activated", G_CALLBACK(column_activated), d);



  /* Page vbox */

  view = gtk_vbox_new(FALSE, 1);

  if(flags & GU_DIR_HEADERS)
    add_headers(d->gu, view, root);

  if(flags & GU_DIR_SCROLLBOX) {

    /* Scrollbox with tree */

    d->scrollbox = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(d->scrollbox),
				   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(d->scrollbox),
					GTK_SHADOW_ETCHED_IN);
    gtk_container_add(GTK_CONTAINER(d->scrollbox), d->tree);

    gtk_box_pack_start(GTK_BOX(view), d->scrollbox, TRUE, TRUE, 0);

    g_signal_connect_after(G_OBJECT(d->scrollbox), "event-after", 
			   G_CALLBACK(after_scroll), d);

  } else {
    gtk_box_pack_start(GTK_BOX(view), d->tree, TRUE, TRUE, 0);
  }

  gtk_widget_show_all(view);
  g_signal_connect(GTK_OBJECT(view), 
		   "destroy", G_CALLBACK(directory_list_destroy), d);
  return view;
}
