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
#ifndef _custom_cell_renderer_progressbar_included_
#define _custom_cell_renderer_progressbar_included_

#include <gtk/gtk.h>

/* Some boilerplate GObject type check and type cast macros.
 *  'klass' is used here instead of 'class', because 'class'
 *  is a c++ keyword */

#define CUSTOM_TYPE_CELL_RENDERER_PROGRESS             (custom_cell_renderer_progress_get_type())
#define CUSTOM_CELL_RENDERER_PROGRESS(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),  CUSTOM_TYPE_CELL_RENDERER_PROGRESS, CustomCellRendererProgress))
#define CUSTOM_CELL_RENDERER_PROGRESS_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass),  CUSTOM_TYPE_CELL_RENDERER_PROGRESS, CustomCellRendererProgressClass))
#define CUSTOM_IS_CELL_PROGRESS_PROGRESS(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CUSTOM_TYPE_CELL_RENDERER_PROGRESS))
#define CUSTOM_IS_CELL_PROGRESS_PROGRESS_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass),  CUSTOM_TYPE_CELL_RENDERER_PROGRESS))
#define CUSTOM_CELL_RENDERER_PROGRESS_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),  CUSTOM_TYPE_CELL_RENDERER_PROGRESS, CustomCellRendererProgressClass))

typedef struct _CustomCellRendererProgress CustomCellRendererProgress;
typedef struct _CustomCellRendererProgressClass CustomCellRendererProgressClass;

/* CustomCellRendererProgress: Our custom cell renderer
 *   structure. Extend according to need */

struct _CustomCellRendererProgress
{
  GtkCellRenderer   parent;

  gdouble           progress;
};


struct _CustomCellRendererProgressClass
{
  GtkCellRendererClass  parent_class;
};


GType                custom_cell_renderer_progress_get_type (void);

GtkCellRenderer     *custom_cell_renderer_progress_new (void);


#endif /* _custom_cell_renderer_progressbar_included_ */
