/*
 *  Showtime GTK frontend
 *  Copyright (C) 2010 Andreas Öman
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

#ifndef GU_DIRECTORY_STORE_H__
#define GU_DIRECTORY_STORE_H__

#include "gu.h"

#define GU_TYPE_DIR_STORE (gu_dir_store_get_type())

#define GU_DIR_STORE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GU_TYPE_DIR_STORE, GuDirStore))

typedef struct _GuDirStore       GuDirStore;
typedef struct _GuDirStoreClass  GuDirStoreClass;
typedef struct gds_cell          gds_cell_t;

struct _GuDirStoreClass {
  GObjectClass parent_class;

  /* Padding for future expansion */
  void (*_gtk_reserved1) (void);
  void (*_gtk_reserved2) (void);
  void (*_gtk_reserved3) (void);
  void (*_gtk_reserved4) (void);
};

enum {
  GDS_COL_URL,
  GDS_COL_TYPE,
  GDS_COL_NAME,
  GDS_COL_ARTIST,
  GDS_COL_DURATION,
  GDS_COL_ALBUM,
  GDS_COL_TRACKS,
  GDS_COL_TRACKINDEX,
  GDS_COL_POPULARITY,
  GDS_COL_STARRED,
  GDS_COL_USER,
  GDS_COL_STATUS,
  GDS_COL_num,
};

TAILQ_HEAD(gds_row_queue, gds_row);

struct _GuDirStore {
  GObject parent;

  /*< private >*/
  struct gtk_ui *gu;

  struct prop_sub *node_sub;
  struct gds_row_queue rows;
  int num_rows;

  // Cache for fast row <> index translations
  struct gds_row *cache_ptr;
  int cache_pos;

  int active[GDS_COL_num];


  struct prop_sub *canDelete_sub;
  int canDelete;
};


GType gu_dir_store_get_type(void) G_GNUC_CONST;

GuDirStore *gu_dir_store_new(gtk_ui_t *gu, prop_t *source);

gds_cell_t *gu_dir_store_get_cell(GuDirStore *gds,
				  GtkTreeIter *iter, int column);

void gu_dir_store_iter_from_cell(gds_cell_t *c, GtkTreeIter *iter);

const char *gu_dir_store_url_from_cell(gds_cell_t *c);

gboolean gu_dir_store_can_delete(GuDirStore *gds);

void gu_dir_store_delete(GuDirStore *gds, GtkTreeIter *iter);

void gu_dir_store_delete_multi(GuDirStore *gds, GtkTreeIter *iter, int niter);

void gu_dir_store_toggle_star(GuDirStore *gds, GtkTreeIter *iter);

prop_t *gu_dir_store_get_prop(GuDirStore *gds, GtkTreeIter *iter);

#endif // GU_DIRECTORY_STORE_H__
