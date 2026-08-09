/*
 * test-gnc-account-type-list.c -- tests for the GTK4 account type list.
 */

#include <glib.h>

#include "Account.h"
#include "gnc-tree-model-account-types.h"

static void
test_account_type_list (void)
{
    guint32 types = (1u << ACCT_TYPE_BANK) | (1u << ACCT_TYPE_CASH) |
                    (1u << ACCT_TYPE_INCOME);
    GListModel *model = gnc_account_type_list_new (types);
    gboolean saw_bank = FALSE;
    gboolean saw_cash = FALSE;
    gboolean saw_income = FALSE;
    gchar *previous_name = NULL;

    g_assert_cmpuint (g_list_model_get_n_items (model), ==, 3);
    for (guint position = 0;
         position < g_list_model_get_n_items (model); position++)
    {
        GncAccountTypeItem *item = GNC_ACCOUNT_TYPE_ITEM (
            g_list_model_get_item (model, position));
        GNCAccountType type = gnc_account_type_item_get_account_type (item);

        g_assert_nonnull (gnc_account_type_item_get_name (item));
        if (previous_name)
            g_assert_cmpint (g_utf8_collate (previous_name,
                                            gnc_account_type_item_get_name (item)), <=, 0);
        g_free (previous_name);
        previous_name = g_strdup (gnc_account_type_item_get_name (item));
        if (type == ACCT_TYPE_BANK)
        {
            saw_bank = TRUE;
            gnc_account_type_item_set_selected (item, TRUE);
            g_assert_true (gnc_account_type_item_get_selected (item));
        }
        else if (type == ACCT_TYPE_CASH)
        {
            saw_cash = TRUE;
        }
        else if (type == ACCT_TYPE_INCOME)
        {
            saw_income = TRUE;
        }
        else
        {
            g_assert_not_reached ();
        }
        g_object_unref (item);
    }

    g_free (previous_name);
    g_assert_true (saw_bank);
    g_assert_true (saw_cash);
    g_assert_true (saw_income);
    g_object_unref (model);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/gnome-utils/account-type-list", test_account_type_list);

    return g_test_run ();
}
