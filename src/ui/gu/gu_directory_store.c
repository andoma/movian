/*
 *  Copyright (C) 2007-2015 Lonelycoder AB
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
#include <assert.h>
#include "gu_directory_store.h"
#include "prop/prop.h"



static const GType coltypes[] = {
  [GDS_COL_URL]        = G_TYPE_STRING,
  [GDS_COL_TYPE]       = G_TYPE_STRING,
  [GDS_COL_NAME]       = G_TYPE_STRING,
  [GDS_COL_ARTIST]     = G_TYPE_STRING,
  [GDS_COL_DURATION]   = G_TYPE_FLOAT,
  [GDS_COL_ALBUM]      = G_TYPE_STRING,
  [GDS_COL_TRACKS]     = G_TYPE_INT,
  [GDS_COL_TRACKINDEX] = G_TYPE_INT,
  [GDS_COL_POPULARITY] = G_TYPE_FLOAT,
  [GDS_COL_STARRED]    = G_TYPE_INT,
  [GDS_COL_USER]       = G_TYPE_STRING,
  [GDS_COL_STATUS]     = G_TYPE_STRING,
};

/**
 *
 */
static const char **subpaths[] = {
  [GDS_COL_URL]        = PNVEC("self", "url"),
  [GDS_COL_TYPE]       = PNVEC("self", "type"),
  [GDS_COL_NAME]       = PNVEC("self", "metadata", "title"),
  [GDS_COL_ARTIST]     = PNVEC("self", "metadata", "artist"),
  [GDS_COL_DURATION]   = PNVEC("self", "metadata", "duration"),
  [GDS_COL_ALBUM]      = PNVEC("self", "metadata", "album"),
  [GDS_COL_TRACKS]     = PNVEC("self", "metadata", "tracks"),
  [GDS_COL_TRACKINDEX] = PNVEC("self", "metadata", "trackindex"),
  [GDS_COL_POPULARITY] = PNVEC("self", "metadata", "popularity"),
  [GDS_COL_STARRED]    = PNVEC("self", "metadata", "starred"),
  [GDS_COL_USER]       = PNVEC("self", "metadata", "user", "name"),
  [GDS_COL_STATUS]     = PNVEC("self", "metadata", "status"),
};


enum {
  COLUMN_ACTIVATED,
  LAST_SIGNAL
};

static guint gds_signals[LAST_SIGNAL] = { 0 };


struct gds_cell {
  prop_sub_t *s;
  struct gds_row *r;
  int type;

  union {
    float f;
    int i;
    rstr_t *rstr;
    const char *cstr;
    struct {
      rstr_t *title;
      rstr_t *url;
    } rlink;
  };
};


typedef struct gds_row {
  GuDirStore *gr_gds;
  TAILQ_ENTRY(gds_row) gr_link;
  
  prop_t *gr_root;
  gds_cell_t gr_cells[GDS_COL_num];
  char gr_noupdates;
  int gr_tmp;
} gds_row_t;








static void clear_model(GuDirStore *gds, int notify);


static GtkTreeModelFlags gu_dir_store_get_flags  (GtkTreeModel   *tree_model);
static gint         gu_dir_store_get_n_columns   (GtkTreeModel   *tree_model);
static GType        gu_dir_store_get_column_type (GtkTreeModel   *tree_model,
						  gint            index);
static gboolean     gu_dir_store_get_iter        (GtkTreeModel   *tree_model,
						  GtkTreeIter    *iter,
						  GtkTreePath    *path);
static GtkTreePath *gu_dir_store_get_path        (GtkTreeModel   *tree_model,
						  GtkTreeIter    *iter);
static void         gu_dir_store_get_value       (GtkTreeModel   *tree_model,
						  GtkTreeIter    *iter,
						  gint            column,
						  GValue         *value);
static gboolean     gu_dir_store_iter_next       (GtkTreeModel   *tree_model,
						  GtkTreeIter    *iter);
static gboolean     gu_dir_store_iter_children   (GtkTreeModel   *tree_model,
						  GtkTreeIter    *iter,
						  GtkTreeIter    *parent);
static gboolean     gu_dir_store_iter_has_child  (GtkTreeModel   *tree_model,
						  GtkTreeIter    *iter);
static gint         gu_dir_store_iter_n_children (GtkTreeModel   *tree_model,
						  GtkTreeIter    *iter);
static gboolean     gu_dir_store_iter_nth_child  (GtkTreeModel   *tree_model,
						  GtkTreeIter    *iter,
						  GtkTreeIter    *parent,
						  gint            n);
static gboolean     gu_dir_store_iter_parent     (GtkTreeModel   *tree_model,
						  GtkTreeIter    *iter,
						  GtkTreeIter    *child);



static void gu_dir_store_tree_model_init(GtkTreeModelIface *iface);

static void gu_dir_store_class_init(GuDirStoreClass *class);
static void gu_dir_store_finalize(GObject *object);

G_DEFINE_TYPE_WITH_CODE(GuDirStore, gu_dir_store, G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_MODEL,
					      gu_dir_store_tree_model_init)
			)

static void
gu_dir_store_class_init(GuDirStoreClass *class)
{
  static gboolean initialized = FALSE;
  GObjectClass *object_class;
  object_class = (GObjectClass*) class;
  object_class->finalize = gu_dir_store_finalize;

  if(!initialized) {
    initialized = TRUE;
    
    gds_signals[COLUMN_ACTIVATED] =
      g_signal_new ("column-activated",
		    GU_TYPE_DIR_STORE,
		    G_SIGNAL_RUN_LAST, 
		    0,
		    NULL, NULL,
		    g_cclosure_marshal_VOID__INT,
		    G_TYPE_NONE, 1,
		    G_TYPE_INT);

  }
}



static void
gu_dir_store_finalize(GObject *object)
{
  GuDirStore *gds = GU_DIR_STORE(object);

  prop_unsubscribe(gds->node_sub);
  clear_model(gds, 0);
}


static void
gu_dir_store_tree_model_init(GtkTreeModelIface *iface)
{
  iface->get_flags = gu_dir_store_get_flags;
  iface->get_n_columns = gu_dir_store_get_n_columns;
  iface->get_column_type = gu_dir_store_get_column_type;
  iface->get_iter = gu_dir_store_get_iter;
  iface->get_path = gu_dir_store_get_path;
  iface->get_value = gu_dir_store_get_value;
  iface->iter_next = gu_dir_store_iter_next;
  iface->iter_children = gu_dir_store_iter_children;
  iface->iter_has_child = gu_dir_store_iter_has_child;
  iface->iter_n_children = gu_dir_store_iter_n_children;
  iface->iter_nth_child = gu_dir_store_iter_nth_child;
  iface->iter_parent = gu_dir_store_iter_parent;
}


static void
gu_dir_store_init(GuDirStore *store)
{

}


/**
 *
 */
static void
gds_set_cache(GuDirStore *gds, gds_row_t *gr, int index)
{
  gds->cache_ptr = gr;
  gds->cache_pos = index;
}

/**
 *
 */
static gds_row_t *
get_row_by_index(GuDirStore *gds, unsigned int index)
{
  gds_row_t *gr;
  int i = 0;

  assert(gds->num_rows > 0);

  if(index == gds->num_rows - 1) {
    return TAILQ_LAST(&gds->rows, gds_row_queue);
  }

  if(gds->cache_ptr != NULL) {

    if(gds->cache_pos == index) {
      return gds->cache_ptr;
    }

    if(gds->cache_pos + 1 == index) {
      gds_set_cache(gds, TAILQ_NEXT(gds->cache_ptr, gr_link), index);
      return gds->cache_ptr;
    }

    if(gds->cache_pos - 1 == index) {
      gds_set_cache(gds, TAILQ_PREV(gds->cache_ptr, gds_row_queue, gr_link),
		    index);
      return gds->cache_ptr;
    }
  }

  TAILQ_FOREACH(gr, &gds->rows, gr_link) {
    if(index == i) {
      gds_set_cache(gds, gr, i);
      return gr;
    }
    i++;
  }
  abort();
}


/**
 *
 */
static int
get_index_by_row(GuDirStore *gds, gds_row_t *gr)
{
  gds_row_t *x;
  int i = 0;
  assert(gds->num_rows > 0);

  if(gr == TAILQ_LAST(&gds->rows, gds_row_queue)) {
    return gds->num_rows - 1;
  }

  if(gds->cache_ptr != NULL) {

    if(gds->cache_ptr == gr) {
      assert(gds->cache_pos >= 0);
      return gds->cache_pos;
    }

    if(TAILQ_NEXT(gds->cache_ptr, gr_link) == gr) {
      gds_set_cache(gds, gr, gds->cache_pos + 1);
      return gds->cache_pos;
    }

    if(TAILQ_PREV(gds->cache_ptr, gds_row_queue, gr_link) == gr) {
      gds_set_cache(gds, gr, gds->cache_pos - 1);
      return gds->cache_pos;
    }
  }

  TAILQ_FOREACH(x, &gds->rows, gr_link) {
    if(gr == x) {
      gds_set_cache(gds, gr, i);
      return i;
    }
    i++;
  }
  abort();
}


/**
 *
 */
static void
gds_iter_init(GtkTreeIter *iter)
{
  iter->stamp = 1;
}


/**
 *
 */
static GtkTreeModelFlags 
gu_dir_store_get_flags(GtkTreeModel *tree_model)
{
  return GTK_TREE_MODEL_LIST_ONLY;
}


/**
 *
 */
static gint
gu_dir_store_get_n_columns(GtkTreeModel *tree_model)
{
  return GDS_COL_num;
}


/**
 *
 */
static GType
gu_dir_store_get_column_type(GtkTreeModel   *tree_model,
			     gint            index)
{
  return coltypes[index];
}


/**
 *
 */
static gboolean
gu_dir_store_get_iter(GtkTreeModel *tree_model,
		      GtkTreeIter  *iter,
		      GtkTreePath  *path)
{
  GuDirStore *gds = (GuDirStore *)tree_model;
  int i;

  i = gtk_tree_path_get_indices(path)[0];
  
  if(i >= gds->num_rows)
    return FALSE;
  
  gds_iter_init(iter);
  iter->user_data = get_row_by_index(gds, i);
  return TRUE;
}


/**
 *
 */
static GtkTreePath *
gu_dir_store_get_path(GtkTreeModel *tree_model,
		      GtkTreeIter  *iter)
{
  GuDirStore *gds = (GuDirStore *)tree_model;
  gds_row_t *gr = iter->user_data;
  GtkTreePath *path = gtk_tree_path_new();
  gtk_tree_path_append_index(path, get_index_by_row(gds, gr));
  return path;
}


/**
 *
 */
static void
gu_dir_store_get_value(GtkTreeModel   *tree_model,
		       GtkTreeIter    *iter,
		       gint            column,
		       GValue         *value)
{
  gds_row_t *gr = iter->user_data;
  gds_cell_t *c = &gr->gr_cells[column];
 
  g_value_init(value, coltypes[column]);
    
  if(coltypes[column] == G_TYPE_STRING && c->type == PROP_SET_RSTRING) {
    g_value_set_string(value, rstr_get(c->rstr));
  }
  else if(coltypes[column] == G_TYPE_STRING && c->type == PROP_SET_CSTRING) {
    g_value_set_string(value, c->cstr);
  }
  else if(coltypes[column] == G_TYPE_STRING && c->type == PROP_SET_URI) {
    g_value_set_string(value, rstr_get(c->rlink.title));
  }
  else if(coltypes[column] == G_TYPE_INT && c->type == PROP_SET_INT) {
    g_value_set_int(value, c->i);
  }
  else if(coltypes[column] == G_TYPE_FLOAT && c->type == PROP_SET_FLOAT) {
    g_value_set_float(value, c->f);
  }
}


/**
 *
 */
static gboolean     
gu_dir_store_iter_next(GtkTreeModel *tree_model,
		       GtkTreeIter  *iter)
{
  gds_row_t *gr = iter->user_data;
  iter->user_data = TAILQ_NEXT(gr, gr_link);
  return iter->user_data != NULL;
}

static gboolean     gu_dir_store_iter_children   (GtkTreeModel   *tree_model,
						  GtkTreeIter    *iter,
						  GtkTreeIter    *parent)
{
  printf("%s\n", __FUNCTION__);
  abort();
}


static gboolean     gu_dir_store_iter_has_child  (GtkTreeModel   *tree_model,
						  GtkTreeIter    *iter)
{
  printf("%s\n", __FUNCTION__);
  abort();
}


static gint
gu_dir_store_iter_n_children(GtkTreeModel   *tree_model,
			     GtkTreeIter    *iter)
{
  GuDirStore *gds = (GuDirStore *)tree_model;

  if(iter == NULL)
    return gds->num_rows;
  return 0;
}

static gboolean
gu_dir_store_iter_nth_child(GtkTreeModel   *tree_model,
			    GtkTreeIter    *iter,
			    GtkTreeIter    *parent,
			    gint            n)
{
  GuDirStore *gds = (GuDirStore *)tree_model;
  if(parent != NULL)
    return FALSE;
  
  if(n >= gds->num_rows)
    return FALSE;
  
  gds_iter_init(iter);
  iter->user_data = get_row_by_index(gds, n);

  return TRUE;
}


static gboolean     gu_dir_store_iter_parent     (GtkTreeModel   *tree_model,
						  GtkTreeIter    *iter,
						  GtkTreeIter    *child)
{
  printf("%s\n", __FUNCTION__);
  abort();
}


/**
 *
 */
static void
gds_cell_free_payload(gds_cell_t *c)
{
  switch(c->type) {
  case PROP_SET_RSTRING:
    rstr_release(c->rstr);
    break;
  case PROP_SET_URI:
    rstr_release(c->rlink.title);
    rstr_release(c->rlink.url);
    break;
  default:
    break;
  }
}

/**
 *
 */
static void
gds_cell_set(void *opaque, prop_event_t event, ...)
{
  gds_cell_t *c = opaque;
  gds_row_t *gr = c->r;
  GuDirStore *gds = gr->gr_gds;
  GtkTreeIter iter;
  GtkTreePath *path;
  int col = c - &gr->gr_cells[0];
  int s = 1;
  va_list ap;
  va_start(ap, event);

  gds_cell_free_payload(c);

  c->type = event;

  switch(event) {
  case PROP_SET_URI:
    c->rlink.title = rstr_dup(va_arg(ap, rstr_t *));
    c->rlink.url   = rstr_dup(va_arg(ap, rstr_t *));
    break;

  case PROP_SET_RSTRING:
    c->rstr = rstr_dup(va_arg(ap, rstr_t *));
    break;

  case PROP_SET_CSTRING:
    c->cstr = va_arg(ap, const char *);
    break;

  case PROP_SET_INT:
    c->i = va_arg(ap, int);
    break;

  case PROP_SET_FLOAT:
    c->f = va_arg(ap, double);
    break;

  default:
    s = 0;
    break;
  }

  if(!gr->gr_noupdates) {

    gds_iter_init(&iter);
    iter.user_data = gr;

    path = gtk_tree_path_new();
    gtk_tree_path_append_index(path, get_index_by_row(gds, gr));
    gtk_tree_model_row_changed(GTK_TREE_MODEL(gds), path, &iter);
    gtk_tree_path_free(path);
  }

  if(s && !gds->active[col]) {
    gds->active[col] = 1;
    g_signal_emit(gds, gds_signals[COLUMN_ACTIVATED], 0, col);
  }
}

/**
 *
 */
static void
gds_row_add(GuDirStore *gds, prop_t *p, prop_t *p_before)
{
  GtkTreeIter iter;
  GtkTreePath *path;
  gds_row_t *b;
  gds_row_t *gr = malloc(sizeof(gds_row_t));
  int i, pos;

  gr->gr_gds = gds;
  gr->gr_root = prop_ref_inc(p);
  
  if(p_before == NULL) {
    // Add at tail, common operation
    pos = gds->num_rows;
    TAILQ_INSERT_TAIL(&gds->rows, gr, gr_link);
  } else {
    pos = 0;
    TAILQ_FOREACH(b, &gds->rows, gr_link) {
      if(b->gr_root == p_before)
	break;
      pos++;
    }

    assert(b != NULL);
    TAILQ_INSERT_BEFORE(b, gr, gr_link);
  }

  gds->num_rows++;
  gr->gr_noupdates = 1;

  gds_set_cache(gds, gr, pos);

  for(i = 0; i < GDS_COL_num; i++) {
    gds_cell_t *c = &gr->gr_cells[i];
    c->type = PROP_SET_VOID;
    c->r = gr;
    c->s = prop_subscribe(PROP_SUB_DIRECT_UPDATE,
			  PROP_TAG_NAME_VECTOR, subpaths[i],
			  PROP_TAG_CALLBACK, gds_cell_set, c,
			  PROP_TAG_COURIER, glibcourier,
			  PROP_TAG_NAMED_ROOT, p, "self",
			  NULL);
  }

  gds_iter_init(&iter);
  iter.user_data = gr;

  path = gtk_tree_path_new();
  gtk_tree_path_append_index(path, pos);
  gtk_tree_model_row_inserted(GTK_TREE_MODEL(gds), path, &iter);
  gtk_tree_path_free(path);

  gr->gr_noupdates = 0;

}


/**
 *
 */
static void
gds_row_delete0(GuDirStore *gds, gds_row_t *gr, int notify)
{
  int i;
  int pos = get_index_by_row(gds, gr);

  for(i = 0; i < GDS_COL_num; i++) {
    gds_cell_t *c = &gr->gr_cells[i];
    prop_unsubscribe(c->s);
    gds_cell_free_payload(c);
  }

  
  gds_set_cache(gds, NULL, 0);
  TAILQ_REMOVE(&gds->rows, gr, gr_link);
  gds->num_rows--;

  if(notify) {
    GtkTreePath *path = gtk_tree_path_new();
    gtk_tree_path_append_index(path, pos);
    gtk_tree_model_row_deleted(GTK_TREE_MODEL(gds), path);
    gtk_tree_path_free(path);
  }
  prop_ref_dec(gr->gr_root);
  free(gr);
}

/**
 *
 */
static gds_row_t *
prop_to_row(GuDirStore *gds, prop_t *p)
{
  gds_row_t *gr;
  TAILQ_FOREACH(gr, &gds->rows, gr_link) {
    if(gr->gr_root == p)
      return gr;
  }
  return NULL;
}


/**
 *
 */
static void
gds_row_delete(GuDirStore *gds, prop_t *p)
{
  gds_row_t *gr = prop_to_row(gds, p);
  assert(gr != NULL);
  gds_row_delete0(gds, gr, 1);
}

/**
 *
 */
static void 
gds_row_move(GuDirStore *gds, gds_row_t *gr, gds_row_t *before)
{
  GtkTreePath *path;
  gds_row_t *x;
  int i = 0;
  gint *new_order = malloc(gds->num_rows * sizeof(gint));
  
  gds_set_cache(gds, NULL, 0);

  TAILQ_FOREACH(x, &gds->rows, gr_link)
    x->gr_tmp = i++;
  
  TAILQ_REMOVE(&gds->rows, gr, gr_link);

  if(before == NULL) {
    TAILQ_INSERT_TAIL(&gds->rows, gr, gr_link);
  } else {
    TAILQ_INSERT_BEFORE(before, gr, gr_link);
  }

  i = 0;
  TAILQ_FOREACH(x, &gds->rows, gr_link)
    new_order[i++] = x->gr_tmp;
  
  path = gtk_tree_path_new();
  gtk_tree_model_rows_reordered(GTK_TREE_MODEL(gds), path, NULL, new_order);
  gtk_tree_path_free(path);
  free(new_order);
}



/**
 *
 */
static void
gds_node_sub(void *opaque, prop_event_t event, ...)
{
  GuDirStore *gds = opaque;
  prop_t *p, *p2;
  gds_row_t *gr;

  va_list ap;
  va_start(ap, event);
  
  switch(event) {
  case PROP_ADD_CHILD:
    gds_row_add(gds, va_arg(ap, prop_t *), NULL);
    break;

  case PROP_ADD_CHILD_BEFORE:
    p  = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    gds_row_add(gds, p, p2);
    break;

  case PROP_MOVE_CHILD:
    p = va_arg(ap, prop_t *);
    p2 = va_arg(ap, prop_t *);
    gr = p2 ? prop_to_row(gds, p2) : NULL;
    gds_row_move(gds, prop_to_row(gds, p), gr);
    break;

  case PROP_DEL_CHILD:
    gds_row_delete(gds, va_arg(ap, prop_t *));
    break;

  case PROP_SET_DIR:
    break;

  case PROP_SET_VOID:
    clear_model(gds, 1);
    break;

  case PROP_REQ_DELETE_VECTOR:
  case PROP_REQ_DELETE:
  case PROP_WANT_MORE_CHILDS:
  case PROP_HAVE_MORE_CHILDS_YES:
  case PROP_HAVE_MORE_CHILDS_NO:
    break;

  default:
    fprintf(stderr, 
	    "gds_node_sub(): Can not handle event %d, aborting()\n", event);
    abort();
  }
}


/**
 *
 */
static void
clear_model(GuDirStore *gds, int notify)
{
  gds_row_t *gr;

  while((gr = TAILQ_FIRST(&gds->rows)) != NULL)
    gds_row_delete0(gds, gr, notify);
}


/**
 *
 */
static void
gds_canDelete_sub(void *opaque, int v)
{
  GuDirStore *gds = opaque;
  gds->canDelete = !!v;
}

/**
 *
 */
GuDirStore *
gu_dir_store_new(gtk_ui_t *gu, prop_t *source)
{
  GuDirStore *gds = g_object_new(GU_TYPE_DIR_STORE, NULL);

  TAILQ_INIT(&gds->rows);
  gds->num_rows = 0;
  gds->cache_ptr = NULL;
  gds->gu = gu;

  gds->node_sub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "nodes"),
		   PROP_TAG_CALLBACK, gds_node_sub, gds,
		   PROP_TAG_COURIER, glibcourier,
		   PROP_TAG_NAMED_ROOT, source, "self",
		   NULL);

  gds->canDelete_sub = 
    prop_subscribe(0,
		   PROP_TAG_NAME("self", "canDelete"),
		   PROP_TAG_CALLBACK_INT, gds_canDelete_sub, gds,
		   PROP_TAG_COURIER, glibcourier,
		   PROP_TAG_NAMED_ROOT, source, "self",
		   NULL);

  return gds;
}

const char *
gu_dir_store_url_from_cell(gds_cell_t *c)
{
  return c->type == PROP_SET_URI ? rstr_get(c->rlink.url) : NULL;
}


/**
 *
 */
gds_cell_t *
gu_dir_store_get_cell(GuDirStore *gds, GtkTreeIter *iter, int column)
{
  gds_row_t *gr = iter->user_data;
  gds_cell_t *c = &gr->gr_cells[column];
  return c;
}


void
gu_dir_store_iter_from_cell(gds_cell_t *c, GtkTreeIter *iter)
{
  gds_iter_init(iter);

  gds_row_t *gr = c->r;
  iter->user_data = gr;
}

gboolean
gu_dir_store_can_delete(GuDirStore *gds)
{
  return gds->canDelete;
}

void
gu_dir_store_delete_multi(GuDirStore *gds, GtkTreeIter *iter, int len)
{
  int i;
  gds_row_t *gr;

  prop_vec_t *pv = prop_vec_create(len);

  for(i = 0; i < len; i++) {
    gr = iter[i].user_data;
    pv = prop_vec_append(pv, gr->gr_root);
  }

  prop_request_delete_multi(pv);
  prop_vec_release(pv);
}

void
gu_dir_store_toggle_star(GuDirStore *gds, GtkTreeIter *iter)
{
  gds_row_t *gr = iter->user_data;
  event_t *e = event_create_action_str("starToggle");
  prop_send_ext_event(gr->gr_root, e);
  event_release(e);
}


prop_t *
gu_dir_store_get_prop(GuDirStore *gds, GtkTreeIter *iter)
{
  gds_row_t *gr = iter->user_data;
  return gr->gr_root;
}
