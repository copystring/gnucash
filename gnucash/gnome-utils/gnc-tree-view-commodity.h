#ifndef __GNC_TREE_VIEW_COMMODITY_H
#define __GNC_TREE_VIEW_COMMODITY_H
#include <gtk/gtk.h>
#include "gnc-tree-view.h"
#include "gnc-pricedb.h"
G_BEGIN_DECLS
#define GNC_TYPE_TREE_VIEW_COMMODITY (gnc_tree_view_commodity_get_type ())
G_DECLARE_FINAL_TYPE (GncTreeViewCommodity, gnc_tree_view_commodity, GNC, TREE_VIEW_COMMODITY, GncTreeView)
typedef gboolean (*gnc_tree_view_commodity_ns_filter_func) (gnc_commodity_namespace*, gpointer data);
typedef gboolean (*gnc_tree_view_commodity_cm_filter_func) (gnc_commodity*, gpointer data);
GtkWidget *gnc_tree_view_commodity_new (QofBook *book, const gchar *first_property_name, ...);
GtkColumnView *gnc_tree_view_commodity_get_column_view (GncTreeViewCommodity *view);
GtkSelectionModel *gnc_tree_view_commodity_get_selection_model (GncTreeViewCommodity *view);
void gnc_tree_view_commodity_configure_columns (GncTreeViewCommodity *view, GSList *column_names);
void gnc_tree_view_commodity_set_filter (GncTreeViewCommodity *view, gnc_tree_view_commodity_ns_filter_func ns_func, gnc_tree_view_commodity_cm_filter_func cm_func, gpointer data, GDestroyNotify destroy);
void gnc_tree_view_commodity_refilter (GncTreeViewCommodity *view);
gnc_commodity *gnc_tree_view_commodity_get_cursor_commodity (GncTreeViewCommodity *view);
gnc_commodity *gnc_tree_view_commodity_get_selected_commodity (GncTreeViewCommodity *view);
gnc_commodity_namespace *gnc_tree_view_commodity_get_selected_namespace (GncTreeViewCommodity *view);
void gnc_tree_view_commodity_select_commodity (GncTreeViewCommodity *view, gnc_commodity *commodity);
void gnc_tree_view_commodity_select_subcommodities (GncTreeViewCommodity *view, gnc_commodity *commodity);
G_END_DECLS
#endif