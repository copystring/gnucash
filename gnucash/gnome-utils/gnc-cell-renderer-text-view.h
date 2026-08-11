#ifndef __GNC_CELL_RENDERER_TEXT_VIEW_H__
#define __GNC_CELL_RENDERER_TEXT_VIEW_H__
#include <gtk/gtk.h>
G_BEGIN_DECLS
/* GTK4 factory for multiline editable presentation. */
GtkListItemFactory *gnc_cell_renderer_text_view_new (void);
G_END_DECLS
#endif