/**
 * @file gnc-tree-view-sx-list.h
 * @brief GTK4 ColumnView implementation for Scheduled Transaction List.
 */
#ifndef __GNC_TREE_VIEW_SX_LIST_H
#define __GNC_TREE_VIEW_SX_LIST_H

#include <gtk/gtk.h>
#include "SchedXaction.h"
#include "gnc-sx-instance-model.h"

G_BEGIN_DECLS

GtkColumnView* gnc_sx_list_view_new (GncSxInstanceModel *sx_instances);
GtkSelectionModel* gnc_sx_list_view_get_selection (GtkColumnView *view);
GList* gnc_sx_list_view_get_selected_sxes (GtkColumnView *view);
void gnc_sx_list_view_select_sxes (GtkColumnView *view, GList *sxs);
void gnc_sx_list_view_refresh (GtkColumnView *view);
gboolean gnc_sx_list_view_enabled_column_visible (GtkColumnView *view);

G_END_DECLS

#endif /* __GNC_TREE_VIEW_SX_LIST_H */