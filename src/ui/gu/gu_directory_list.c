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
#include "gu_menu.h"
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
  GtkTreeSelection *sel;

  GuDirStore *model;

  gu_tab_t *gt;

  column_t columns[GDS_COL_num];

  LIST_HEAD(, dirnode) nodes;

  gds_cell_t *currentcell;
  gds_cell_t *presscell;

  prop_t *psource;

} directory_list_t;


/**
 *
 */
typedef struct selinfo {
  directory_list_t *d;
  GtkWidget *menu;

  int num;
  int p;
  GtkTreeIter *iterv;

  int primary_action;
#define SELINFO_PA_OPEN 0x1
#define SELINFO_PA_PLAY 0x2


  char *url;
} selinfo_t;

/**
 *
 */
static void
popup_play(GtkWidget *menu_item, gpointer data)
{
  selinfo_t *si = data;
  directory_list_t *d = si->d;

  gu_tab_play_track(d->gt, gu_dir_store_get_prop(d->model, si->iterv),
		    d->psource);
  gtk_widget_destroy(si->menu);
}


/**
 *
 */
static void
popup_enqueue(GtkWidget *menu_item, gpointer data)
{
  selinfo_t *si = data;
  directory_list_t *d = si->d;

  gu_tab_play_track(d->gt, gu_dir_store_get_prop(d->model, si->iterv), NULL);
  gtk_widget_destroy(si->menu);
}


/**
 *
 */
static void
popup_open_newwin(GtkWidget *menu_item, gpointer data)
{
  selinfo_t *si = data;
  directory_list_t *d = si->d;
  
  gu_nav_open_newwin(d->gt->gt_gw->gw_gu, si->url);
  gtk_widget_destroy(si->menu);
}


/**
 *
 */
static void
popup_open_newtab(GtkWidget *menu_item, gpointer data)
{
  selinfo_t *si = data;
  directory_list_t *d = si->d;
  
  gu_nav_open_newtab(d->gt->gt_gw, si->url);
  gtk_widget_destroy(si->menu);
}


/**
 *
 */
static void
popup_delete(GtkWidget *menu_item, gpointer data)
{
  selinfo_t *si = data;
  gu_dir_store_delete_multi(si->d->model, si->iterv, si->num);

  gtk_widget_destroy(si->menu);
}


/**
 *
 */
static void
fillsel(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter,
	gpointer data)
{
  selinfo_t *si = data;
  GValue gv = {0};

  si->iterv[si->p] = *iter;
  si->p++;

  gtk_tree_model_get_value(model, iter, GDS_COL_TYPE, &gv);

  if(G_VALUE_HOLDS_STRING(&gv)) {
    const char *type = g_value_get_string(&gv);
    if(!strcmp(type, "audio") || !strcmp(type, "video")) {
      si->primary_action |= SELINFO_PA_PLAY;
    } else {
      si->primary_action |= SELINFO_PA_OPEN;
    }
    g_value_unset(&gv);

    if(si->url == NULL) {
      gtk_tree_model_get_value(model, iter, GDS_COL_URL, &gv);
      
      if(G_VALUE_HOLDS_STRING(&gv))
	si->url = strdup(g_value_get_string(&gv));
      g_value_unset(&gv);
    }
  }
}


/**
 *
 */
static void
popup_destroy(GtkWidget *menu, gpointer data)
{
  selinfo_t *si = data;
  free(si->iterv);
  free(si->url);
  free(si);
}


/**
 *
 */
static void
do_popup_menu(directory_list_t *d, GdkEventButton *event, const char *url)
{
  int button, event_time;
  selinfo_t *si = calloc(1, sizeof(selinfo_t));

  si->d = d;
  si->menu = gtk_menu_new();

  if(url != NULL) {
    si->url = strdup(url);

    gu_menu_add_item(si->menu, "Open In New _Window", 
		     popup_open_newwin, si, NULL, NULL, TRUE);

    gu_menu_add_item(si->menu, "Open In New _Tab", 
		     popup_open_newtab, si, NULL, NULL, TRUE);

    //    gu_menu_add_sep(si->menu);
    //    gu_menu_add_item(si->menu, "_Copy", popup_copy, si, NULL, NULL);

  } else {
    si->num = gtk_tree_selection_count_selected_rows(d->sel);
    si->iterv = malloc(si->num * sizeof(GtkTreeIter));
    gtk_tree_selection_selected_foreach(d->sel, fillsel, si);

    if(si->primary_action == SELINFO_PA_OPEN) {
      gu_menu_add_item(si->menu, "Open In New _Window", 
		       popup_open_newwin, si, NULL, NULL, TRUE);

      gu_menu_add_item(si->menu, "Open In New _Tab", 
		       popup_open_newtab, si, NULL, NULL, TRUE);

    } else if(si->primary_action == SELINFO_PA_PLAY) {
      gu_menu_add_item(si->menu, "_Play", popup_play, si, NULL, NULL, TRUE);
      gu_menu_add_item(si->menu, "_Enqueue", popup_enqueue, si, NULL, NULL,
		       TRUE);
    }

    // gu_menu_add_sep(si->menu);
    // gu_menu_add_item(si->menu, "_Copy", popup_copy, si, NULL, NULL);

    gu_menu_add_sep(si->menu);
    gu_menu_add_item(si->menu, "_Delete", popup_delete, si, NULL, NULL,
		     gu_dir_store_can_delete(d->model));
  }

  if(event != NULL) {
    button = event->button;
    event_time = event->time;
  } else {
    button = 0;
    event_time = gtk_get_current_event_time();
  }

  gtk_widget_show_all(si->menu);

  gtk_menu_attach_to_widget(GTK_MENU(si->menu), GTK_WIDGET(d->tree), NULL);
  gtk_menu_popup(GTK_MENU(si->menu), NULL, NULL, NULL, NULL, 
		 button, event_time);

  g_signal_connect(GTK_OBJECT(si->menu), 
		   "destroy", G_CALLBACK(popup_destroy), si);
}


/**
 * For menu popup keyboard button
 */
static gboolean
widget_popup_menu_handler(GtkWidget *widget, gpointer opaque)
{
  directory_list_t *d = opaque;
  do_popup_menu(d, NULL, NULL);
  return TRUE;
}


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
  GValue gv = { 0 };
  int how = 0;

  gtk_tree_model_get_iter(GTK_TREE_MODEL(d->model), &iter, path);

  gtk_tree_model_get_value(GTK_TREE_MODEL(d->model), &iter, GDS_COL_TYPE, &gv);
  if(G_VALUE_HOLDS_STRING(&gv)) {

    str = g_value_get_string(&gv);
    if(!strcmp(str, "audio")) {
      how = 2;
    } else {
      how = 1;
    }
  }
  
  g_value_unset(&gv);
  if(how == 1) {
    // Open by URL

    gtk_tree_model_get_value(GTK_TREE_MODEL(d->model), &iter, GDS_COL_URL, &gv);
    if(G_VALUE_HOLDS_STRING(&gv))
      gu_tab_open(d->gt, g_value_get_string(&gv));
    g_value_unset(&gv);
  }

  if(how == 2) {
    // Open by properties
    gu_tab_play_track(d->gt, gu_dir_store_get_prop(d->model, &iter), d->psource);
  }
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
#define MOUSE_RIGHTCLICK  4

static int
mouse_do(directory_list_t *d, GtkTreeView *tree_view, int action, int x, int y,
	 GdkEventButton *evb)
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
    if(col == GDS_COL_STARRED) {
      d->presscell = c;
      r = 1;
    } else if((url = gu_dir_store_url_from_cell(c)) != NULL) {
      d->presscell = c;
      r = 1;
    }
    break;

  case MOUSE_LEFTRELEASE:
    if(c == d->presscell) {
      if(col == GDS_COL_STARRED) {
	gu_dir_store_toggle_star(GU_DIR_STORE(d->model), &iter);
	r = 1;
      } else if((url = gu_dir_store_url_from_cell(c)) != NULL) {
	gu_tab_open(d->gt, url);
	r = 1;
      }
    }

    d->presscell = NULL;
    break;

  case MOUSE_RIGHTCLICK:
    // Clicked on row is selected, operate on entire selection
    r = gtk_tree_selection_iter_is_selected(d->sel, &iter);

    if(!r) {
      gtk_tree_selection_unselect_all(d->sel);
      gtk_tree_selection_select_iter(d->sel, &iter);
    }
    url = gu_dir_store_url_from_cell(c);
    do_popup_menu(d, evb, url);
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
  mouse_do(user_data, tree_view, MOUSE_MOTION, event->x, event->y, NULL);
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
  int n;
  if(event->type != GDK_BUTTON_PRESS)
    return FALSE;

  if(event->button == 1)
    n = MOUSE_LEFTCLICK;
  else if(event->button == 3)
    n = MOUSE_RIGHTCLICK;
  else
    return FALSE;

  return mouse_do(user_data, tree_view, n, event->x, event->y, event);
}

/**
 *
 */
static gboolean
mouse_release(GtkTreeView *tree_view, GdkEventButton *event, gpointer user_data)
{
  return mouse_do(user_data, tree_view, MOUSE_LEFTRELEASE, event->x, event->y,
		  event);
}


static gboolean
after_scroll(GtkTreeView *tree_view, GdkEvent *event, gpointer user_data)
{
  directory_list_t *d = user_data;
  if(event->type == GDK_SCROLL) {
    GdkEventScroll *e = (GdkEventScroll *)event;
    mouse_do(user_data, GTK_TREE_VIEW(d->tree), MOUSE_MOTION, e->x, e->y, NULL);
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
starred2pixbuf(GtkTreeViewColumn *tree_column, GtkCellRenderer *cell,
	       GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
  GValue gv = { 0, };

  gtk_tree_model_get_value(model, iter, GDS_COL_STARRED, &gv);
  if(G_VALUE_HOLDS_INT(&gv)) {
    GdkPixbuf *pb;

    if(g_value_get_int(&gv))
      pb = gu_pixbuf_get_sync(SHOWTIME_GU_RESOURCES_URL"/star.png", -1, 16);
    else
      pb = gu_pixbuf_get_sync(SHOWTIME_GU_RESOURCES_URL"/nostar.png", -1, 16);

    g_object_set(G_OBJECT(cell), "pixbuf", pb, NULL);
    if(pb != NULL)
      g_object_unref(G_OBJECT(pb));
  }
  g_value_unset(&gv);
}



/**
 *
 */
static void
init_starred_col(directory_list_t *d, const char *title, int idx)
{
  GtkCellRenderer *r;
  GtkTreeViewColumn *c;

  r = gtk_cell_renderer_pixbuf_new();
  c = gtk_tree_view_column_new_with_attributes(title, r, NULL);

  gtk_tree_view_column_set_cell_data_func(c, r, starred2pixbuf, NULL, NULL);

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
gu_directory_list_create(gu_tab_t *gt, prop_t *root, int flags)
{
  directory_list_t *d = calloc(1, sizeof(directory_list_t));
  GtkWidget *view;

  d->psource = prop_ref_inc(prop_create(root, "model"));

  d->gt = gt;

  d->model = gu_dir_store_new(gt->gt_gw->gw_gu, d->psource);
  d->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(d->model));
  d->sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(d->tree));
  gtk_tree_selection_set_mode(d->sel, GTK_SELECTION_MULTIPLE);

  gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(d->tree), TRUE);

  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(d->tree),
				    !!(flags & GU_DIR_VISIBLE_HEADERS));

  if(flags & GU_DIR_COL_TYPE)
    init_type_col(d, "", GDS_COL_TYPE);

  init_starred_col(d, "", GDS_COL_STARRED);

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

  if(flags & GU_DIR_COL_USER)
    init_text_col(d,     "User",    GDS_COL_USER, 1, 1);

  init_text_col(d, "Status", GDS_COL_STATUS, 1, 0);

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

  g_signal_connect(G_OBJECT(d->tree), "popup-menu",
		   G_CALLBACK(widget_popup_menu_handler), d);

  g_signal_connect(G_OBJECT(d->model), 
		   "column-activated", G_CALLBACK(column_activated), d);



  /* Page vbox */

  view = gtk_vbox_new(FALSE, 1);

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
