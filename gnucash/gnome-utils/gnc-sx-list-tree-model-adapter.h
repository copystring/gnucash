/********************************************************************\
 * gnc-sx-list-tree-model-adapter.h                                 *
 * GTK4 list-model adapter for scheduled transactions.              *
\********************************************************************/
#ifndef _GNC_SX_LIST_TREE_MODEL_ADAPTER_H
#define _GNC_SX_LIST_TREE_MODEL_ADAPTER_H

#include <glib-object.h>
#include <gio/gio.h>
#include "SchedXaction.h"
#include "gnc-sx-instance-model.h"

G_BEGIN_DECLS

#define GNC_TYPE_SX_LIST_TREE_MODEL_ADAPTER (gnc_sx_list_tree_model_adapter_get_type ())
G_DECLARE_FINAL_TYPE (GncSxListTreeModelAdapter, gnc_sx_list_tree_model_adapter,
                      GNC, SX_LIST_TREE_MODEL_ADAPTER, GObject)

#define GNC_TYPE_SX_LIST_ROW (gnc_sx_list_row_get_type ())
G_DECLARE_FINAL_TYPE (GncSxListRow, gnc_sx_list_row, GNC, SX_LIST_ROW, GObject)

GncSxListTreeModelAdapter* gnc_sx_list_tree_model_adapter_new (GncSxInstanceModel *instances);
GListModel* gnc_sx_list_tree_model_adapter_get_model (GncSxListTreeModelAdapter *model);
void gnc_sx_list_tree_model_adapter_refresh (GncSxListTreeModelAdapter *model);

SchedXaction* gnc_sx_list_row_get_sx (GncSxListRow *row);
const gchar* gnc_sx_list_row_get_name (GncSxListRow *row);
gboolean gnc_sx_list_row_get_enabled (GncSxListRow *row);
const gchar* gnc_sx_list_row_get_frequency (GncSxListRow *row);
guint gnc_sx_list_row_get_num_postponed (GncSxListRow *row);
const gchar* gnc_sx_list_row_get_last_occur (GncSxListRow *row);
const gchar* gnc_sx_list_row_get_next_occur (GncSxListRow *row);

G_END_DECLS

#endif /* _GNC_SX_LIST_TREE_MODEL_ADAPTER_H */