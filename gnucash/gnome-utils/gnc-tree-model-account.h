/* gnc-tree-model-account.h -- GTK4 account list-model facade. */
#ifndef __GNC_TREE_MODEL_ACCOUNT_H
#define __GNC_TREE_MODEL_ACCOUNT_H

#include <gtk/gtk.h>
#include "Account.h"

G_BEGIN_DECLS

#define GNC_TYPE_TREE_MODEL_ACCOUNT (gnc_tree_model_account_get_type ())
G_DECLARE_FINAL_TYPE (GncTreeModelAccount, gnc_tree_model_account, GNC,
                      TREE_MODEL_ACCOUNT, GObject)
#define GNC_TREE_MODEL_ACCOUNT_NAME "GncTreeModelAccount"

typedef enum
{
    GNC_TREE_MODEL_ACCOUNT_COL_NAME,
    GNC_TREE_MODEL_ACCOUNT_COL_TYPE,
    GNC_TREE_MODEL_ACCOUNT_COL_COMMODITY,
    GNC_TREE_MODEL_ACCOUNT_COL_CODE,
    GNC_TREE_MODEL_ACCOUNT_COL_DESCRIPTION,
    GNC_TREE_MODEL_ACCOUNT_COL_LASTNUM,
    GNC_TREE_MODEL_ACCOUNT_COL_PRESENT,
    GNC_TREE_MODEL_ACCOUNT_COL_PRESENT_REPORT,
    GNC_TREE_MODEL_ACCOUNT_COL_BALANCE,
    GNC_TREE_MODEL_ACCOUNT_COL_BALANCE_REPORT,
    GNC_TREE_MODEL_ACCOUNT_COL_BALANCE_PERIOD,
    GNC_TREE_MODEL_ACCOUNT_COL_BALANCE_LIMIT,
    GNC_TREE_MODEL_ACCOUNT_COL_BALANCE_LIMIT_EXPLANATION,
    GNC_TREE_MODEL_ACCOUNT_COL_CLEARED,
    GNC_TREE_MODEL_ACCOUNT_COL_CLEARED_REPORT,
    GNC_TREE_MODEL_ACCOUNT_COL_RECONCILED,
    GNC_TREE_MODEL_ACCOUNT_COL_RECONCILED_REPORT,
    GNC_TREE_MODEL_ACCOUNT_COL_RECONCILED_DATE,
    GNC_TREE_MODEL_ACCOUNT_COL_EARLIEST_DATE,
    GNC_TREE_MODEL_ACCOUNT_COL_FUTURE_MIN,
    GNC_TREE_MODEL_ACCOUNT_COL_FUTURE_MIN_REPORT,
    GNC_TREE_MODEL_ACCOUNT_COL_TOTAL,
    GNC_TREE_MODEL_ACCOUNT_COL_TOTAL_REPORT,
    GNC_TREE_MODEL_ACCOUNT_COL_TOTAL_PERIOD,
    GNC_TREE_MODEL_ACCOUNT_COL_NOTES,
    GNC_TREE_MODEL_ACCOUNT_COL_TAX_INFO,
    GNC_TREE_MODEL_ACCOUNT_COL_TAX_INFO_SUB_ACCT,
    GNC_TREE_MODEL_ACCOUNT_COL_HIDDEN,
    GNC_TREE_MODEL_ACCOUNT_COL_PLACEHOLDER,
    GNC_TREE_MODEL_ACCOUNT_COL_OPENING_BALANCE,
    GNC_TREE_MODEL_ACCOUNT_COL_LAST_VISIBLE = GNC_TREE_MODEL_ACCOUNT_COL_OPENING_BALANCE,
    GNC_TREE_MODEL_ACCOUNT_COL_COLOR_PRESENT,
    GNC_TREE_MODEL_ACCOUNT_COL_COLOR_ACCOUNT,
    GNC_TREE_MODEL_ACCOUNT_COL_COLOR_BALANCE,
    GNC_TREE_MODEL_ACCOUNT_COL_COLOR_BALANCE_PERIOD,
    GNC_TREE_MODEL_ACCOUNT_COL_COLOR_CLEARED,
    GNC_TREE_MODEL_ACCOUNT_COL_COLOR_RECONCILED,
    GNC_TREE_MODEL_ACCOUNT_COL_COLOR_FUTURE_MIN,
    GNC_TREE_MODEL_ACCOUNT_COL_COLOR_TOTAL,
    GNC_TREE_MODEL_ACCOUNT_COL_COLOR_TOTAL_PERIOD,
    GNC_TREE_MODEL_ACCOUNT_NUM_COLUMNS
} GncTreeModelAccountColumn;

typedef gboolean (*GncTreeModelAccountFilterFunc) (Account *account,
                                                     gpointer user_data);

GncTreeModelAccount *gnc_tree_model_account_new (Account *root,
                                                   gboolean show_root);
GListModel *gnc_tree_model_account_get_roots (GncTreeModelAccount *model);
GListModel *gnc_tree_model_account_create_children (GncTreeModelAccount *model,
                                                      Account *account);
void gnc_tree_model_account_set_filter (GncTreeModelAccount *model,
                                         GncTreeModelAccountFilterFunc filter,
                                         gpointer user_data,
                                         GDestroyNotify destroy);
void gnc_tree_model_account_set_sort_column (GncTreeModelAccount *model,
                                              GncTreeModelAccountColumn column,
                                              GtkSortType order);
void gnc_tree_model_account_clear_cache (GncTreeModelAccount *model);
gchar *gnc_tree_model_account_get_string (GncTreeModelAccount *model,
                                           Account *account,
                                           GncTreeModelAccountColumn column,
                                           gchar **foreground);
gboolean gnc_tree_model_account_get_boolean (Account *account,
                                              GncTreeModelAccountColumn column);

G_END_DECLS
#endif
