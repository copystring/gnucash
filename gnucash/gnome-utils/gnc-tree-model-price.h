#ifndef __GNC_TREE_MODEL_PRICE_H
#define __GNC_TREE_MODEL_PRICE_H
#include <gtk/gtk.h>
#include "gnc-tree-model.h"
#include "gnc-pricedb.h"
G_BEGIN_DECLS
#define GNC_TYPE_TREE_MODEL_PRICE (gnc_tree_model_price_get_type ())
G_DECLARE_FINAL_TYPE (GncTreeModelPrice, gnc_tree_model_price, GNC, TREE_MODEL_PRICE, GncTreeModel)
#define GNC_TREE_MODEL_PRICE_NAME "GncTreeModelPrice"
#define GNC_TYPE_TREE_MODEL_PRICE_ROW (gnc_tree_model_price_row_get_type ())
G_DECLARE_FINAL_TYPE (GncTreeModelPriceRow, gnc_tree_model_price_row, GNC, TREE_MODEL_PRICE_ROW, GObject)
typedef enum { GNC_TREE_MODEL_PRICE_ROW_NAMESPACE, GNC_TREE_MODEL_PRICE_ROW_COMMODITY, GNC_TREE_MODEL_PRICE_ROW_PRICE } GncTreeModelPriceRowKind;
typedef enum { GNC_TREE_MODEL_PRICE_COL_COMMODITY, GNC_TREE_MODEL_PRICE_COL_CURRENCY, GNC_TREE_MODEL_PRICE_COL_DATE, GNC_TREE_MODEL_PRICE_COL_SOURCE, GNC_TREE_MODEL_PRICE_COL_TYPE, GNC_TREE_MODEL_PRICE_COL_VALUE, GNC_TREE_MODEL_PRICE_COL_LAST_VISIBLE = GNC_TREE_MODEL_PRICE_COL_VALUE } GncTreeModelPriceColumn;
GncTreeModelPrice *gnc_tree_model_price_new (QofBook *book, GNCPriceDB *price_db);
GListModel *gnc_tree_model_price_get_roots (GncTreeModelPrice *model);
GListModel *gnc_tree_model_price_row_get_children (GncTreeModelPriceRow *row);
GncTreeModelPriceRowKind gnc_tree_model_price_row_get_kind (GncTreeModelPriceRow *row);
gnc_commodity_namespace *gnc_tree_model_price_row_get_namespace (GncTreeModelPriceRow *row);
gnc_commodity *gnc_tree_model_price_row_get_commodity (GncTreeModelPriceRow *row);
GNCPrice *gnc_tree_model_price_row_get_price (GncTreeModelPriceRow *row);
const gchar *gnc_tree_model_price_row_get_id (GncTreeModelPriceRow *row);
gchar *gnc_tree_model_price_row_get_string (GncTreeModelPriceRow *row, GncTreeModelPriceColumn column);
G_END_DECLS
#endif