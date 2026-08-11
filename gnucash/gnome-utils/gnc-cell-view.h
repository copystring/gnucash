#ifndef __GNC_CELL_VIEW_H__
#define __GNC_CELL_VIEW_H__
#include <gtk/gtk.h>
G_BEGIN_DECLS
#define GNC_TYPE_CELL_VIEW (gnc_cell_view_get_type ())
G_DECLARE_FINAL_TYPE (GncCellView, gnc_cell_view, GNC, CELL_VIEW, GtkBox)
GtkWidget *gnc_cell_view_new (void);
void gnc_cell_view_set_text (GncCellView *view, const gchar *text);
gchar *gnc_cell_view_get_text (GncCellView *view);
G_END_DECLS
#endif