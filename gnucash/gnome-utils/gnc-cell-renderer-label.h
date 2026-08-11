#ifndef __GNC_CELL_RENDERER_LABEL_H__
#define __GNC_CELL_RENDERER_LABEL_H__
#include <gtk/gtk.h>
G_BEGIN_DECLS
/* GTK4 list-item factory used by views that need selectable text. */
GtkListItemFactory *gnc_cell_renderer_label_new (void);
G_END_DECLS
#endif