#ifndef __GNC_TREE_VIEW_H
#define __GNC_TREE_VIEW_H
#include <gtk/gtk.h>
G_BEGIN_DECLS
#define GNC_TYPE_TREE_VIEW (gnc_tree_view_get_type ())
G_DECLARE_DERIVABLE_TYPE (GncTreeView, gnc_tree_view, GNC, TREE_VIEW, GtkBox)
#define GNC_TREE_VIEW_NAME "GncTreeView"
struct _GncTreeViewClass { GtkBoxClass parent_class; };
GtkColumnView *gnc_tree_view_get_column_view (GncTreeView *view);
void gnc_column_view_bind_grid_line_preferences (GtkColumnView *view);
void gnc_column_view_unbind_grid_line_preferences (GtkColumnView *view);
void gnc_tree_view_set_state_section (GncTreeView *view, const gchar *section);
const gchar *gnc_tree_view_get_state_section (GncTreeView *view);
void gnc_tree_view_set_show_column_menu (GncTreeView *view, gboolean visible);
gboolean gnc_tree_view_get_show_column_menu (GncTreeView *view);
G_END_DECLS
#endif
