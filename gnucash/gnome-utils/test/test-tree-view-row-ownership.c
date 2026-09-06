/* test-tree-view-row-ownership.c -- TreeListRow item ownership regression tests. */

#include <config.h>

#include <gtk/gtk.h>

#include "Account.h"
#include "gnc-commodity.h"
#include "gnc-engine.h"
#include "gnc-pricedb.h"
#include "gnc-session.h"
#include "gnc-tree-view-account.h"
#include "gnc-tree-view-commodity.h"
#include "gnc-tree-view-price.h"

static void
object_finalized (gpointer data, GObject *object)
{
    gboolean *finalized = data;

    *finalized = TRUE;
    (void)object;
}

static void
drain_main_context (void)
{
    while (g_main_context_pending (NULL))
        g_main_context_iteration (NULL, FALSE);
}

static GObject *
selected_tree_row_item (GtkSelectionModel *selection)
{
    guint position;
    GObject *tree_row = NULL;
    GObject *item;

    for (position = 0;
         position < g_list_model_get_n_items (G_LIST_MODEL (selection));
         position++)
        if (gtk_selection_model_is_selected (selection, position))
        {
            tree_row = g_list_model_get_item (G_LIST_MODEL (selection), position);
            break;
        }
    g_assert_nonnull (tree_row);
    item = gtk_tree_list_row_get_item (GTK_TREE_LIST_ROW (tree_row));
    g_object_unref (tree_row);
    return item;
}

static void
test_account_lookup_releases_tree_item (void)
{
    QofSession *session = qof_session_new (qof_book_new ());
    QofBook *book = qof_session_get_book (session);
    Account *root = gnc_account_create_root (book);
    GtkWidget *widget;
    GncTreeViewAccount *view;
    GObject *item;
    gboolean finalized = FALSE;

    gnc_set_current_session (session);
    widget = gnc_tree_view_account_new_with_root (root, TRUE);
    g_object_ref_sink (widget);
    view = GNC_TREE_VIEW_ACCOUNT (widget);
    gnc_tree_view_account_set_selected_account (view, root);
    drain_main_context ();
    g_assert_true (gnc_tree_view_account_get_cursor_account (view) == root);
    item = selected_tree_row_item (gnc_tree_view_account_get_selection_model (view));
    g_object_weak_ref (item, object_finalized, &finalized);
    g_object_unref (item);

    for (guint round = 0; round < 8; round++)
        g_assert_true (gnc_tree_view_account_get_account_at (view, 0) == root);

    gnc_tree_view_account_refilter (view);
    drain_main_context ();
    g_object_unref (widget);
    drain_main_context ();
    gnc_clear_current_session ();
    g_assert_true (finalized);
}

static void
test_commodity_lookup_releases_tree_item (void)
{
    QofSession *session = qof_session_new (qof_book_new ());
    QofBook *book = qof_session_get_book (session);
    gnc_commodity_table *table = gnc_commodity_table_get_table (book);
    gnc_commodity *commodity;
    GtkWidget *widget;
    GncTreeViewCommodity *view;
    GtkSelectionModel *selection;
    GObject *item;
    gboolean finalized = FALSE;

    gnc_set_current_session (session);
    commodity = gnc_commodity_new (book, "Ownership currency",
                                   GNC_COMMODITY_NS_CURRENCY, "OWN", "", 100);
    gnc_commodity_table_insert (table, commodity);
    widget = gnc_tree_view_commodity_new (book, NULL);
    g_object_ref_sink (widget);
    view = GNC_TREE_VIEW_COMMODITY (widget);
    selection = gnc_tree_view_commodity_get_selection_model (view);
    gnc_tree_view_commodity_select_commodity (view, commodity);
    drain_main_context ();
    g_assert_true (gnc_tree_view_commodity_get_cursor_commodity (view) == commodity);
    item = selected_tree_row_item (selection);
    g_object_weak_ref (item, object_finalized, &finalized);
    g_object_unref (item);

    for (guint round = 0; round < 8; round++)
        g_assert_true (gnc_tree_view_commodity_get_cursor_commodity (view) == commodity);

    gnc_tree_view_commodity_refilter (view);
    drain_main_context ();
    g_object_unref (widget);
    drain_main_context ();
    g_assert_true (finalized);
    gnc_clear_current_session ();
}

static void
test_price_lookup_releases_tree_item (void)
{
    QofSession *session = qof_session_new (qof_book_new ());
    QofBook *book = qof_session_get_book (session);
    gnc_commodity_table *table = gnc_commodity_table_get_table (book);
    gnc_commodity *currency;
    gnc_commodity *security;
    GNCPrice *price;
    GtkWidget *widget;
    GncTreeViewPrice *view;
    GtkSelectionModel *selection;
    GObject *item;
    GNCPrice *expected_price;
    gboolean finalized = FALSE;

    gnc_set_current_session (session);
    currency = gnc_commodity_new (book, "Ownership currency", GNC_COMMODITY_NS_CURRENCY,
                                  "OWN", "", 100);
    security = gnc_commodity_new (book, "Ownership security", "NYSE", "OWS", "", 1000);
    gnc_commodity_table_insert (table, currency);
    gnc_commodity_table_insert (table, security);
    price = gnc_price_create (book);
    gnc_price_begin_edit (price);
    gnc_price_set_commodity (price, security);
    gnc_price_set_currency (price, currency);
    gnc_price_set_time64 (price, 1);
    gnc_price_set_value (price, gnc_numeric_create (1, 1));
    gnc_pricedb_add_price (gnc_pricedb_get_db (book), price);
    gnc_price_commit_edit (price);

    widget = gnc_tree_view_price_new (book, NULL);
    g_object_ref_sink (widget);
    view = GNC_TREE_VIEW_PRICE (widget);
    selection = gnc_tree_view_price_get_selection_model (view);
    gnc_tree_view_price_set_selected_price (view, price);
    drain_main_context ();
    g_assert_true (gnc_tree_view_price_get_cursor_price (view) == price);
    expected_price = price;
    gnc_price_unref (price);
    item = selected_tree_row_item (selection);
    g_object_weak_ref (item, object_finalized, &finalized);
    g_object_unref (item);

    for (guint round = 0; round < 8; round++)
        g_assert_true (gnc_tree_view_price_get_cursor_price (view) == expected_price);

    gnc_tree_view_price_set_filter (view, NULL, NULL, NULL, NULL, NULL);
    drain_main_context ();
    g_object_unref (widget);
    drain_main_context ();
    g_assert_true (finalized);
    gnc_clear_current_session ();
}

int
main (int argc, char **argv)
{
    int status;

    g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);
    g_test_init (&argc, &argv, NULL);
    gtk_init ();
    gnc_engine_init_static (argc, argv);

    g_test_add_func ("/gnome-utils/tree-view-row-ownership/account",
                     test_account_lookup_releases_tree_item);
    g_test_add_func ("/gnome-utils/tree-view-row-ownership/commodity",
                     test_commodity_lookup_releases_tree_item);
    g_test_add_func ("/gnome-utils/tree-view-row-ownership/price",
                     test_price_lookup_releases_tree_item);
    status = g_test_run ();

    gnc_engine_shutdown ();
    return status;
}
