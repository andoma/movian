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

#include "gu_cell_bar.h"

enum {
  URL_COLUMN,
  TYPE_COLUMN,
  NAME_COLUMN,
  ARTIST_COLUMN,
  DURATION_COLUMN,
  ALBUM_COLUMN,
  TRACKS_COLUMN,
  TRACKINDEX_COLUMN,
  POPULARITY_COLUMN,
  N_COLUMNS
};

#define NODE_COLUMN N_COLUMNS // Pointer to the node


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
  [POPULARITY_COLUMN] = PNVEC("self", "metadata", "popularity"),
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
  [POPULARITY_COLUMN] = G_TYPE_FLOAT,
  [NODE_COLUMN] = G_TYPE_POINTER,
};


/**
 *
 */
typedef struct column {
  GtkTreeViewColumn *col;
  GtkCellRenderer *r;
  int contents; // Cells with actual contents
  struct directory_list *d;
  int idx;
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

  struct cell *currentcell;
  struct cell *presscell;

} directory_list_t;


/**
 *
 */
typedef struct cell {
  prop_sub_t *s;
  int16_t idx;
  rstr_t *url;
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
  case PROP_SET_RLINK:
    g_value_init(&gv, G_TYPE_STRING);
    g_value_set_string(&gv, rstr_get(va_arg(ap, const rstr_t *)));

    rstr_release(c->url);
    c->url = rstr_dup(va_arg(ap, rstr_t *));
    break;

  case PROP_SET_RSTRING:
    g_value_init(&gv, G_TYPE_STRING);
    g_value_set_string(&gv, rstr_get(va_arg(ap, const rstr_t *)));
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
dirnode_destroy(directory_list_t *d, dirnode_t *dn, int remove)
{
  int i;

  if(remove)
    gtk_list_store_remove(dn->dir->model, &dn->iter);

  LIST_REMOVE(dn, link);

  for(i = 0; i < N_COLUMNS; i++) {
    if(dn->cells[i].s != NULL)
      prop_unsubscribe(dn->cells[i].s);
    rstr_release(dn->cells[i].url);
    if(d->currentcell == &dn->cells[i])
      d->currentcell = NULL;
    if(d->presscell == &dn->cells[i])
      d->presscell = NULL;
  }

  prop_ref_dec(dn->p);
  free(dn);
}


/**
 *
 */
static dirnode_t *
gu_node_find(directory_list_t *d, prop_t *p)
{
  dirnode_t *dn;

  LIST_FOREACH(dn, &d->nodes, link)
    if(dn->p == p)
      return dn;
  return NULL;
}


/**
 *
 */
static void
gu_node_add(directory_list_t *d, prop_t *p, dirnode_t *before)
{
  dirnode_t *dn;
  int i;
  cell_t *c;
  GValue gv = { 0, };

  dn = calloc(1, sizeof(dirnode_t));
  dn->dir = d;
  dn->p = p;
  prop_ref_inc(p);
  
  LIST_INSERT_HEAD(&d->nodes, dn, link);

  if(before) {
    gtk_list_store_insert_before(d->model, &dn->iter, &before->iter);
  } else {
    gtk_list_store_append(d->model, &dn->iter);
  }

  g_value_init(&gv, G_TYPE_POINTER);
  g_value_set_pointer(&gv, dn);
  gtk_list_store_set_value(dn->dir->model, &dn->iter, NODE_COLUMN, &gv);
  g_value_unset(&gv);

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
}


/**
 *
 */
static void
dirnode_move(directory_list_t *d, dirnode_t *dn, dirnode_t *before)
{
  gtk_list_store_move_before(d->model, &dn->iter,
			     before ? &before->iter : NULL);
}


/**
 *
 */
static void
gu_node_sub(void *opaque, prop_event_t event, ...)
{
  dirnode_t *dn;
  directory_list_t *d = opaque;
  prop_t *p;
  prop_t *p2;

  va_list ap;
  va_start(ap, event);
  
  switch(event) {
  case PROP_ADD_CHILD:
    gu_node_add(d, va_arg(ap, prop_t *), NULL);
    break;

  case PROP_ADD_CHILD_BEFORE:
    p = va_arg(ap, prop_t *);
    dn = gu_node_find(d, va_arg(ap, prop_t *));
    assert(dn != NULL);
    gu_node_add(d, p, dn);
    break;

  case PROP_MOVE_CHILD:
    p = va_arg(ap, prop_t *);
    
    p2 = va_arg(ap, prop_t *);
    dn = p2 ? gu_node_find(d, p2) : NULL; // if p2 == NULL then move to tail

    dirnode_move(d, gu_node_find(d, p), dn);
    break;


  case PROP_DEL_CHILD:
    dn = gu_node_find(d, va_arg(ap, prop_t *));
    assert(dn != NULL);
    dirnode_destroy(d, dn, 1);
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

  gtk_tree_model_get_value(GTK_TREE_MODEL(d->model), &iter, URL_COLUMN, &gv);
  if(G_VALUE_HOLDS_STRING(&gv)) {
    str = g_value_get_string(&gv);
    if(str != NULL)
      nav_open(str, NULL, *d->parenturlptr);
  }

  g_value_unset(&gv);
}


/**
 *
 */
static void
repaint_cell(cell_t *c)
{
  dirnode_t *dn = c->dn;
  directory_list_t *dir = dn->dir;

  GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(dir->model), 
					      &dn->iter);

  gtk_tree_model_row_changed(GTK_TREE_MODEL(dir->model), path, &dn->iter);
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
  GValue gv = { 0, };
  int r = 0;

  if(!gtk_tree_view_get_path_at_pos(tree_view, x, y,
				    &path, &column, &cx, &cy))
    return 0;
  
  for(col = 0; col < N_COLUMNS; col++) {
    if(d->columns[col].col == column)
      break;
  }

  gtk_tree_model_get_iter(GTK_TREE_MODEL(d->model), &iter, path);

  gtk_tree_model_get_value(GTK_TREE_MODEL(d->model), &iter, NODE_COLUMN, &gv);

  if(G_VALUE_HOLDS_POINTER(&gv)) {
    dirnode_t *dn = g_value_get_pointer(&gv);
    cell_t *c = &dn->cells[col];

    switch(action) {

    case MOUSE_MOTION:
      if(c->url != NULL && d->currentcell != c) {
	if(d->currentcell)
	  repaint_cell(d->currentcell);
	
	d->currentcell = c;
	repaint_cell(d->currentcell);
      }
      break;
      
    case MOUSE_LEFTCLICK:
      if(c->url != NULL) {
	d->presscell = c;
	r = 1;
      }
      break;

    case MOUSE_LEFTRELEASE:
      if(c->url != NULL && c == d->presscell) {
	nav_open(rstr_get(c->url), NULL, NULL);
	r = 1;
      }
      d->presscell = NULL;
      break;
    }
  }
  g_value_unset(&gv);
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
    repaint_cell(d->currentcell);
  
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
  GValue ptr = { 0, };
  dirnode_t *dn;
  cell_t *c;
  PangoUnderline ul;

  gtk_tree_model_get_value(model, iter, NODE_COLUMN, &ptr);
  dn = g_value_get_pointer(&ptr);

  c = &dn->cells[col->idx];

  gtk_tree_model_get_value(model, iter, col->idx, &gv);

  ul = d->currentcell == c ? PANGO_UNDERLINE_SINGLE : PANGO_UNDERLINE_NONE;

  g_object_set(cell, "underline", ul, NULL);

  if(G_VALUE_HOLDS_STRING(&gv))
    g_object_set(cell, "text", g_value_get_string(&gv), NULL);

  g_value_unset(&gv);
  g_value_unset(&ptr);
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
    dirnode_destroy(d, dn, 0);

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
  d->model = gtk_list_store_newv(N_COLUMNS + 1, coltypes);

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

  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(d->tree),
				    !!(flags & GU_DIR_VISIBLE_HEADERS));

  if(flags & GU_DIR_COL_TYPE)
    init_type_col(d,     "",         TYPE_COLUMN);

  if(flags & GU_DIR_COL_TRACKINDEX)
    init_text_col(d, "#",        TRACKINDEX_COLUMN, 0, 0);

  init_text_col(d,     "Name",     NAME_COLUMN, 1, 0);

  if(flags & GU_DIR_COL_DURATION)
    init_duration_col(d, "Duration", DURATION_COLUMN);

  if(flags & GU_DIR_COL_ARTIST)
    init_text_col(d,     "Artist",   ARTIST_COLUMN, 1, 1);

  if(flags & GU_DIR_COL_ALBUM)
    init_text_col(d,     "Album",    ALBUM_COLUMN, 1, 1);

  if(flags & GU_DIR_COL_NUM_TRACKS)
    init_text_col(d,     "Tracks",   TRACKS_COLUMN, 0, 0);

  if(flags & GU_DIR_COL_POPULARITY)
    init_bar_col(d, "Popularity", POPULARITY_COLUMN);

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
