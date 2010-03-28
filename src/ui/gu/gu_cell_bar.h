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
