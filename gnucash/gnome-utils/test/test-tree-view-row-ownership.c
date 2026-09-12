/* test-tree-view-row-ownership.c -- TreeListRow item ownership regression tests. */

#include <config.h>

#include <gtk/gtk.h>

#include "Account.h"
#include "gnc-commodity.h"
#include "gnc-component-manager.h"
#include "gnc-engine.h"
#include "gnc-prefs-utils.h"
#include "gnc-pricedb.h"
#include "gnc-session.h"
#include "gnc-query-view.h"
#include "search-param.h"
#include "gnc-tree-view-account.h"
#include "gnc-tree-view-commodity.h"
#include "gnc-tree-view-owner.h"
#include "gnc-tree-view-price.h"
#include "qof.h"
#include "qofquery-p.h"
#include "Split.h"

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

typedef struct
{
    GMainLoop *loop;
    gboolean frame_seen;
    gboolean timed_out;
    guint tick_id;
    GdkFrameClock *clock;
    gulong after_paint_id;
} FrameWait;

static void
frame_wait_after_paint_cb (GdkFrameClock *clock, gpointer user_data)
{
    FrameWait *wait = user_data;

    wait->frame_seen = TRUE;
    if (g_main_loop_is_running (wait->loop))
        g_main_loop_quit (wait->loop);
    (void)clock;
}

static gboolean
frame_wait_tick_cb (GtkWidget *widget, GdkFrameClock *clock, gpointer user_data)
{
    FrameWait *wait = user_data;

    wait->tick_id = 0;
    wait->clock = GDK_FRAME_CLOCK (g_object_ref (clock));
    wait->after_paint_id = g_signal_connect (clock, "after-paint",
                                              G_CALLBACK (frame_wait_after_paint_cb), wait);
    gdk_frame_clock_request_phase (clock, GDK_FRAME_CLOCK_PHASE_AFTER_PAINT);
    (void)widget;
    return G_SOURCE_REMOVE;
}

static gboolean
frame_wait_timeout_cb (gpointer user_data)
{
    FrameWait *wait = user_data;

    wait->timed_out = TRUE;
    if (g_main_loop_is_running (wait->loop))
        g_main_loop_quit (wait->loop);
    return G_SOURCE_REMOVE;
}

static void
present_and_wait_for_frame (GtkWindow *window)
{
    GMainLoop *loop = g_main_loop_new (NULL, FALSE);
    FrameWait wait = { loop, FALSE, FALSE, 0, NULL, 0 };
    guint timeout_id = 0;

    wait.tick_id = gtk_widget_add_tick_callback (GTK_WIDGET (window), frame_wait_tick_cb,
                                                  &wait, NULL);
    gtk_window_present (window);
    if (!wait.frame_seen)
    {
        timeout_id = g_timeout_add (1000, frame_wait_timeout_cb, &wait);
        g_main_loop_run (loop);
    }
    if (wait.tick_id)
        gtk_widget_remove_tick_callback (GTK_WIDGET (window), wait.tick_id);
    if (timeout_id && !wait.timed_out)
        g_source_remove (timeout_id);
    if (wait.after_paint_id)
        g_signal_handler_disconnect (wait.clock, wait.after_paint_id);
    g_clear_object (&wait.clock);
    g_main_loop_unref (loop);
    if (wait.timed_out)
        g_test_message ("Account tree frame timeout: window mapped=%d realized=%d size=%dx%d",
                        gtk_widget_get_mapped (GTK_WIDGET (window)),
                        gtk_widget_get_realized (GTK_WIDGET (window)),
                        gtk_widget_get_width (GTK_WIDGET (window)),
                        gtk_widget_get_height (GTK_WIDGET (window)));
    g_assert_true (wait.frame_seen);
    g_assert_false (wait.timed_out);
}

static GtkColumnView *
find_column_view (GtkWidget *widget)
{
    if (GTK_IS_COLUMN_VIEW (widget))
        return GTK_COLUMN_VIEW (widget);

    for (GtkWidget *child = gtk_widget_get_first_child (widget); child;
         child = gtk_widget_get_next_sibling (child))
    {
        GtkColumnView *view = find_column_view (child);

        if (view)
            return view;
    }
    return NULL;
}

static void
query_row_selected (GNCQueryView *view, gpointer count, gpointer user_data)
{
    guint *emissions = user_data;

    (*emissions)++;
    (void)view;
    (void)count;
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

static GtkTreeListRow *
selected_tree_row (GtkSelectionModel *selection)
{
    guint position;

    for (position = 0;
         position < g_list_model_get_n_items (G_LIST_MODEL (selection));
         position++)
        if (gtk_selection_model_is_selected (selection, position))
            return GTK_TREE_LIST_ROW (g_list_model_get_item
                                      (G_LIST_MODEL (selection), position));
    g_assert_not_reached ();
    return NULL;
}

/* The view drops its selection model during dispose. Keeping the model alive
 * externally proves that its tree-list model still owns the selected item
 * until that final external reference is released. Calling dispose twice also
 * exercises the view's idempotent cleanup while restore_state is still queued.
 * The caller determines whether the underlying item is model- or book-owned.
 */
static void
dispose_with_retained_selection (GtkWidget *widget, GtkSelectionModel *selection,
                                 GtkTreeListRow *parent,
                                 gboolean *item_finalized)
{
    gboolean selection_finalized = FALSE;
    guint parent_position = gtk_tree_list_row_get_position (parent);

    g_object_ref (selection);
    g_object_weak_ref (G_OBJECT (selection), object_finalized, &selection_finalized);
    g_object_run_dispose (G_OBJECT (widget));
    g_object_run_dispose (G_OBJECT (widget));
    gtk_tree_list_row_set_expanded (parent, FALSE);
    gtk_tree_list_row_set_expanded (parent, TRUE);
    g_assert_true (gtk_selection_model_select_item (selection, parent_position, TRUE));
    drain_main_context ();
    g_assert_false (selection_finalized);
    g_assert_false (*item_finalized);

    g_object_unref (widget);
    gtk_tree_list_row_set_expanded (parent, FALSE);
    gtk_tree_list_row_set_expanded (parent, TRUE);
    g_assert_true (gtk_selection_model_select_item (selection, parent_position, TRUE));
    drain_main_context ();
    g_assert_false (selection_finalized);
    g_assert_false (*item_finalized);

    g_object_unref (selection);
    g_object_unref (parent);
    drain_main_context ();
    g_assert_true (selection_finalized);
}

static void
test_account_lookup_releases_tree_item (void)
{
    QofSession *session = qof_session_new (qof_book_new ());
    QofBook *book = qof_session_get_book (session);
    Account *root = gnc_account_create_root (book);
    Account *child = xaccMallocAccount (book);
    GtkWidget *widget;
    GncTreeViewAccount *view;
    GObject *item;
    GtkTreeListRow *selected_row;
    GtkTreeListRow *parent_row;
    gboolean finalized = FALSE;

    gnc_set_current_session (session);
    xaccAccountSetName (child, "Ownership child");
    xaccAccountSetType (child, ACCT_TYPE_BANK);
    gnc_account_append_child (root, child);
    widget = gnc_tree_view_account_new_with_root (root, TRUE);
    g_object_ref_sink (widget);
    view = GNC_TREE_VIEW_ACCOUNT (widget);
    gnc_tree_view_account_set_selected_account (view, child);
    drain_main_context ();
    g_assert_true (gnc_tree_view_account_get_cursor_account (view) == child);
    for (guint round = 0; round < 8; round++)
    {
        gnc_tree_view_account_set_selection_mode (view, GTK_SELECTION_MULTIPLE);
        gnc_tree_view_account_set_selected_account (view, child);
        drain_main_context ();
        g_assert_true (gnc_tree_view_account_get_cursor_account (view) == child);
        gnc_tree_view_account_set_selection_mode (view, GTK_SELECTION_SINGLE);
        gnc_tree_view_account_set_selected_account (view, child);
        drain_main_context ();
        g_assert_true (gnc_tree_view_account_get_account_at (view, 1) == child);
    }

    gnc_tree_view_account_refilter (view);
    drain_main_context ();
    item = selected_tree_row_item (gnc_tree_view_account_get_selection_model (view));
    g_object_weak_ref (item, object_finalized, &finalized);
    g_object_unref (item);
    selected_row = selected_tree_row (gnc_tree_view_account_get_selection_model (view));
    parent_row = gtk_tree_list_row_get_parent (selected_row);
    g_object_unref (selected_row);
    g_assert_nonnull (parent_row);
    gnc_tree_view_account_set_selected_account (view, child);
    dispose_with_retained_selection (widget,
                                    gnc_tree_view_account_get_selection_model (view),
                                    parent_row,
                                    &finalized);
    g_assert_false (finalized);
    gnc_clear_current_session ();
    g_assert_true (finalized);
}

static void
test_account_dispose_quiesces_retained_row (void)
{
    QofSession *session = qof_session_new (qof_book_new ());
    QofBook *book = qof_session_get_book (session);
    Account *root = gnc_account_create_root (book);
    Account *child = xaccMallocAccount (book);
    Account *grandchild = xaccMallocAccount (book);
    GtkWidget *widget;
    GtkWindow *window;
    GncTreeViewAccount *view;
    GtkSelectionModel *selection;
    GtkTreeListRow *selected_row;
    GtkTreeListRow *parent_row;
    GtkTreeListRow *root_row;

    gnc_set_current_session (session);
    xaccAccountSetName (child, "Dispose callback child");
    xaccAccountSetType (child, ACCT_TYPE_BANK);
    gnc_account_append_child (root, child);
    xaccAccountSetName (grandchild, "Dispose callback grandchild");
    xaccAccountSetType (grandchild, ACCT_TYPE_BANK);
    gnc_account_append_child (child, grandchild);
    widget = gnc_tree_view_account_new_with_root (root, TRUE);
    g_object_ref_sink (widget);
    view = GNC_TREE_VIEW_ACCOUNT (widget);
    selection = gnc_tree_view_account_get_selection_model (view);
    gnc_tree_view_account_set_selected_account (view, grandchild);
    drain_main_context ();
    selected_row = selected_tree_row (selection);
    parent_row = gtk_tree_list_row_get_parent (selected_row);
    g_object_unref (selected_row);
    g_assert_nonnull (parent_row);
    root_row = gtk_tree_list_row_get_parent (parent_row);
    g_assert_nonnull (root_row);
    gtk_tree_list_row_set_expanded (root_row, TRUE);
    gtk_tree_list_row_set_expanded (parent_row, TRUE);

    window = GTK_WINDOW (g_object_ref_sink (gtk_window_new ()));
    gtk_window_set_default_size (window, 640, 480);
    gtk_window_set_child (window, widget);
    present_and_wait_for_frame (window);
    g_assert_nonnull (g_object_get_data (G_OBJECT (parent_row),
                                         "gnc-account-expansion-listener"));
    gtk_window_set_child (window, NULL);
    gtk_window_destroy (window);
    g_object_unref (window);

    gtk_tree_list_row_set_expanded (root_row, FALSE);
    drain_main_context ();
    g_assert_cmpuint (g_list_model_get_n_items (G_LIST_MODEL (selection)), ==, 1);

    g_object_ref (selection);
    g_object_run_dispose (G_OBJECT (widget));
    g_object_run_dispose (G_OBJECT (widget));
    gtk_tree_list_row_set_expanded (parent_row, FALSE);
    gtk_tree_list_row_set_expanded (parent_row, TRUE);
    g_assert_true (gtk_selection_model_select_item (selection, 0, TRUE));
    drain_main_context ();

    g_object_unref (widget);
    gtk_tree_list_row_set_expanded (parent_row, FALSE);
    gtk_tree_list_row_set_expanded (parent_row, TRUE);
    g_object_unref (selection);
    g_object_unref (root_row);
    g_object_unref (parent_row);
    gnc_clear_current_session ();
}

static void
test_account_selection_modes_preserve_semantics (void)
{
    QofSession *session = qof_session_new (qof_book_new ());
    QofBook *book = qof_session_get_book (session);
    Account *root = gnc_account_create_root (book);
    Account *first = xaccMallocAccount (book);
    Account *second = xaccMallocAccount (book);
    GtkWidget *widget;
    GncTreeViewAccount *view;
    GtkSelectionModel *selection;
    GList *accounts = NULL;
    GList *selected;

    gnc_set_current_session (session);
    xaccAccountSetName (first, "First account");
    xaccAccountSetType (first, ACCT_TYPE_BANK);
    gnc_account_append_child (root, first);
    xaccAccountSetName (second, "Second account");
    xaccAccountSetType (second, ACCT_TYPE_BANK);
    gnc_account_append_child (root, second);
    widget = gnc_tree_view_account_new_with_root (root, FALSE);
    g_object_ref_sink (widget);
    view = GNC_TREE_VIEW_ACCOUNT (widget);
    drain_main_context ();

    selection = gnc_tree_view_account_get_selection_model (view);
    g_assert_true (GTK_IS_SINGLE_SELECTION (selection));
    g_assert_false (gtk_single_selection_get_autoselect (
                        GTK_SINGLE_SELECTION (selection)));
    g_assert_true (gtk_single_selection_get_can_unselect (
                       GTK_SINGLE_SELECTION (selection)));
    g_assert_null (gnc_tree_view_account_get_selected_account (view));

    gnc_tree_view_account_set_selected_account (view, second);
    gnc_tree_view_account_set_selection_mode (view, GTK_SELECTION_MULTIPLE);
    drain_main_context ();
    g_assert_true (gnc_tree_view_account_get_selected_account (view) == second);
    gnc_tree_view_account_set_selection_mode (view, GTK_SELECTION_SINGLE);
    drain_main_context ();
    g_assert_true (gnc_tree_view_account_get_selected_account (view) == second);

    gnc_tree_view_account_set_selected_account (view, first);
    drain_main_context ();
    g_assert_true (gnc_tree_view_account_get_selected_account (view) == first);

    gnc_tree_view_account_set_selection_mode (view, GTK_SELECTION_MULTIPLE);
    drain_main_context ();
    selection = gnc_tree_view_account_get_selection_model (view);
    g_assert_true (GTK_IS_MULTI_SELECTION (selection));
    g_assert_true (gnc_tree_view_account_get_selected_account (view) == first);

    gnc_tree_view_account_set_selection_mode (view, GTK_SELECTION_SINGLE);
    drain_main_context ();
    g_assert_true (gnc_tree_view_account_get_selected_account (view) == first);
    gnc_tree_view_account_set_selected_account (view, NULL);
    drain_main_context ();
    g_assert_null (gnc_tree_view_account_get_selected_account (view));

    gnc_tree_view_account_set_selection_mode (view, GTK_SELECTION_BROWSE);
    drain_main_context ();
    selection = gnc_tree_view_account_get_selection_model (view);
    g_assert_true (GTK_IS_SINGLE_SELECTION (selection));
    g_assert_true (gtk_single_selection_get_autoselect (
                       GTK_SINGLE_SELECTION (selection)));
    g_assert_false (gtk_single_selection_get_can_unselect (
                        GTK_SINGLE_SELECTION (selection)));
    g_assert_nonnull (gnc_tree_view_account_get_selected_account (view));
    gnc_tree_view_account_set_selected_account (view, second);
    drain_main_context ();
    g_assert_true (gnc_tree_view_account_get_selected_account (view) == second);
    gnc_tree_view_account_set_selected_account (view, NULL);
    drain_main_context ();
    g_assert_true (gnc_tree_view_account_get_selected_account (view) == second);

    gnc_tree_view_account_set_selection_mode (view, GTK_SELECTION_MULTIPLE);
    drain_main_context ();
    g_assert_true (gnc_tree_view_account_get_selected_account (view) == second);
    gnc_tree_view_account_set_selection_mode (view, GTK_SELECTION_BROWSE);
    drain_main_context ();
    g_assert_true (gnc_tree_view_account_get_selected_account (view) == second);
    gnc_tree_view_account_set_selected_account (view, NULL);
    drain_main_context ();
    g_assert_true (gnc_tree_view_account_get_selected_account (view) == second);

    gnc_tree_view_account_set_selection_mode (view, GTK_SELECTION_SINGLE);
    drain_main_context ();
    g_assert_true (gnc_tree_view_account_get_selected_account (view) == second);

    gnc_tree_view_account_set_selection_mode (view, GTK_SELECTION_NONE);
    drain_main_context ();
    g_assert_true (GTK_IS_NO_SELECTION (
        gnc_tree_view_account_get_selection_model (view)));
    g_assert_null (gnc_tree_view_account_get_selected_account (view));
    gnc_tree_view_account_set_selected_account (view, first);
    drain_main_context ();
    g_assert_null (gnc_tree_view_account_get_selected_account (view));

    gnc_tree_view_account_set_selection_mode (view, GTK_SELECTION_MULTIPLE);
    drain_main_context ();
    selection = gnc_tree_view_account_get_selection_model (view);
    g_assert_true (GTK_IS_MULTI_SELECTION (selection));
    g_assert_null (gnc_tree_view_account_get_selected_account (view));
    accounts = g_list_append (accounts, first);
    accounts = g_list_append (accounts, second);
    gnc_tree_view_account_set_selected_accounts (view, accounts, FALSE);
    g_list_free (accounts);
    drain_main_context ();
    selected = gnc_tree_view_account_get_selected_accounts (view);
    g_assert_cmpuint (g_list_length (selected), ==, 2);
    g_list_free (selected);

    /* The GTK4 model has no GTK3 anchor object. Keep a deterministic account
     * from the existing selection instead of inventing an unrelated row. */
    gnc_tree_view_account_set_selection_mode (view, GTK_SELECTION_SINGLE);
    drain_main_context ();
    selection = gnc_tree_view_account_get_selection_model (view);
    g_assert_true (GTK_IS_SINGLE_SELECTION (selection));
    g_assert_true (gnc_tree_view_account_get_selected_account (view) == first);
    gnc_tree_view_account_set_selected_account (view, second);
    drain_main_context ();
    g_assert_true (gnc_tree_view_account_get_selected_account (view) == second);

    g_object_unref (widget);
    gnc_clear_current_session ();
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
    GtkTreeListRow *selected_row;
    GtkTreeListRow *parent_row;
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
    for (guint round = 0; round < 8; round++)
        g_assert_true (gnc_tree_view_commodity_get_cursor_commodity (view) == commodity);

    gnc_tree_view_commodity_refilter (view);
    drain_main_context ();
    item = selected_tree_row_item (selection);
    g_object_weak_ref (item, object_finalized, &finalized);
    g_object_unref (item);
    selected_row = selected_tree_row (selection);
    parent_row = gtk_tree_list_row_get_parent (selected_row);
    g_object_unref (selected_row);
    g_assert_nonnull (parent_row);
    gnc_tree_view_commodity_select_commodity (view, commodity);
    dispose_with_retained_selection (widget, selection, parent_row, &finalized);
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
    GtkTreeListRow *selected_row;
    GtkTreeListRow *parent_row;
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
    for (guint round = 0; round < 8; round++)
        g_assert_true (gnc_tree_view_price_get_cursor_price (view) == expected_price);

    gnc_tree_view_price_set_filter (view, NULL, NULL, NULL, NULL, NULL);
    drain_main_context ();
    item = selected_tree_row_item (selection);
    g_object_weak_ref (item, object_finalized, &finalized);
    g_object_unref (item);
    selected_row = selected_tree_row (selection);
    parent_row = gtk_tree_list_row_get_parent (selected_row);
    g_object_unref (selected_row);
    g_assert_nonnull (parent_row);
    gnc_tree_view_price_set_selected_price (view, expected_price);
    dispose_with_retained_selection (widget, selection, parent_row, &finalized);
    g_assert_true (finalized);
    gnc_clear_current_session ();
}

static void
test_owner_selection_survives_view_dispose (void)
{
    GtkWidget *widget = gnc_tree_view_owner_new (GNC_OWNER_CUSTOMER);
    GtkSelectionModel *selection;
    gboolean finalized = FALSE;

    g_object_ref_sink (widget);
    selection = g_object_ref (gnc_tree_view_owner_get_selection_model
                              (GNC_TREE_VIEW_OWNER (widget)));
    g_object_weak_ref (G_OBJECT (selection), object_finalized, &finalized);
    g_object_run_dispose (G_OBJECT (widget));
    g_object_run_dispose (G_OBJECT (widget));
    g_signal_emit_by_name (selection, "selection-changed", 0u, 0u);
    g_object_unref (widget);
    g_signal_emit_by_name (selection, "selection-changed", 0u, 0u);
    g_object_unref (selection);
    drain_main_context ();
    g_assert_true (finalized);
}

static void
test_query_selection_switch_disconnects_old_model (void)
{
    GtkWidget *widget = GTK_WIDGET (g_object_new (GNC_TYPE_QUERY_VIEW, NULL));
    GtkColumnView *column_view;
    GtkSelectionModel *old_selection;
    gboolean finalized = FALSE;
    guint emissions = 0;

    g_object_ref_sink (widget);
    column_view = find_column_view (widget);
    g_assert_nonnull (column_view);
    old_selection = g_object_ref (gtk_column_view_get_model (column_view));
    g_object_weak_ref (G_OBJECT (old_selection), object_finalized, &finalized);
    g_signal_connect (widget, "row-selected", G_CALLBACK (query_row_selected),
                      &emissions);
    gnc_query_view_set_selection_mode (GNC_QUERY_VIEW (widget),
                                       GTK_SELECTION_MULTIPLE);
    g_signal_emit_by_name (old_selection, "selection-changed", 0u, 0u);
    g_assert_cmpuint (emissions, ==, 0);
    g_object_run_dispose (G_OBJECT (widget));
    g_object_run_dispose (G_OBJECT (widget));
    g_signal_emit_by_name (old_selection, "selection-changed", 0u, 0u);
    g_assert_cmpuint (emissions, ==, 0);
    g_object_unref (widget);
    g_object_unref (old_selection);
    drain_main_context ();
    g_assert_true (finalized);
}

static void
assert_query_primary_sort (GNCQueryView *view, gboolean increasing)
{
    QofQuerySort *primary = NULL;

    qof_query_get_sorts (view->query, &primary, NULL, NULL);
    g_assert_nonnull (primary);
    g_assert_cmpint (qof_query_sort_get_increasing (primary), ==, increasing);
}

static void
test_query_native_column_sorting (void)
{
    QofSession *session = qof_session_new (qof_book_new ());
    QofBook *book = qof_session_get_book (session);
    GNCSearchParamSimple *memo_param = gnc_search_param_simple_new ();
    GNCSearchParamSimple *amount_param = gnc_search_param_simple_new ();
    GNCSearchParamSimple *passive_param = gnc_search_param_simple_new ();
    GSList *path = NULL;
    GList *params = NULL;
    Query *query = qof_query_create_for (GNC_ID_SPLIT);
    GtkWidget *widget;
    GNCQueryView *view;
    GtkColumnView *column_view;
    GListModel *columns;
    GtkColumnViewColumn *memo_column;
    GtkColumnViewColumn *amount_column;
    GtkColumnViewColumn *passive_column;
    GtkSorter *view_sorter;

    gnc_set_current_session (session);
    qof_query_set_book (query, book);

    path = g_slist_append (path, (gpointer)SPLIT_MEMO);
    gnc_search_param_set_param_path (memo_param, GNC_ID_SPLIT, path);
    g_slist_free (path);
    gnc_search_param_set_title (GNC_SEARCH_PARAM (memo_param), "Memo");

    path = g_slist_append (NULL, (gpointer)SPLIT_AMOUNT);
    gnc_search_param_set_param_path (amount_param, GNC_ID_SPLIT, path);
    g_slist_free (path);
    gnc_search_param_set_title (GNC_SEARCH_PARAM (amount_param), "Amount");

    path = g_slist_append (NULL, (gpointer)SPLIT_MEMO);
    gnc_search_param_set_param_path (passive_param, GNC_ID_SPLIT, path);
    g_slist_free (path);
    gnc_search_param_set_title (GNC_SEARCH_PARAM (passive_param), "Passive");
    gnc_search_param_set_passive (GNC_SEARCH_PARAM (passive_param), TRUE);

    params = g_list_append (params, memo_param);
    params = g_list_append (params, amount_param);
    params = g_list_append (params, passive_param);
    widget = gnc_query_view_new (params, query);
    g_object_ref_sink (widget);
    view = GNC_QUERY_VIEW (widget);
    column_view = find_column_view (widget);
    g_assert_nonnull (column_view);
    g_assert_null (gtk_column_view_get_header_factory (column_view));

    columns = gtk_column_view_get_columns (column_view);
    memo_column = g_list_model_get_item (columns, 0);
    amount_column = g_list_model_get_item (columns, 1);
    passive_column = g_list_model_get_item (columns, 2);
    g_assert_nonnull (gtk_column_view_column_get_sorter (memo_column));
    g_assert_nonnull (gtk_column_view_column_get_sorter (amount_column));
    g_assert_null (gtk_column_view_column_get_sorter (passive_column));
    g_assert_cmpint (gtk_sorter_compare
                     (gtk_column_view_column_get_sorter (memo_column),
                      memo_param, amount_param), ==, GTK_ORDERING_EQUAL);

    gtk_column_view_sort_by_column (column_view, memo_column,
                                    GTK_SORT_ASCENDING);
    g_assert_cmpint (view->sort_column, ==, 0);
    g_assert_true (view->increasing);
    assert_query_primary_sort (view, TRUE);
    gtk_column_view_sort_by_column (column_view, memo_column,
                                    GTK_SORT_DESCENDING);
    g_assert_false (view->increasing);
    assert_query_primary_sort (view, FALSE);

    gnc_query_view_set_numerics (view, FALSE, TRUE);
    gtk_column_view_sort_by_column (column_view, amount_column,
                                    GTK_SORT_ASCENDING);
    g_assert_cmpint (view->sort_column, ==, 1);
    g_assert_true (view->increasing);
    assert_query_primary_sort (view, FALSE);

    gnc_query_sort_order (view, 1, GTK_SORT_ASCENDING);
    view_sorter = g_object_ref (gtk_column_view_get_sorter (column_view));
    g_assert_true (gtk_column_view_sorter_get_primary_sort_column
                   (GTK_COLUMN_VIEW_SORTER (view_sorter)) == memo_column);
    g_assert_cmpint (gtk_column_view_sorter_get_primary_sort_order
                     (GTK_COLUMN_VIEW_SORTER (view_sorter)), ==,
                     GTK_SORT_ASCENDING);
    g_assert_true (view->increasing);
    assert_query_primary_sort (view, TRUE);

    g_object_run_dispose (G_OBJECT (widget));
    g_object_run_dispose (G_OBJECT (widget));
    gtk_sorter_changed (view_sorter, GTK_SORTER_CHANGE_DIFFERENT);
    g_object_unref (view_sorter);
    g_object_unref (memo_column);
    g_object_unref (amount_column);
    g_object_unref (passive_column);
    g_object_unref (widget);
    qof_query_destroy (query);
    g_list_free_full (params, g_object_unref);
    gnc_clear_current_session ();
}

int
main (int argc, char **argv)
{
    int status;

    g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);
    g_test_init (&argc, &argv, NULL);
    gtk_init ();
    qof_log_init_filename_special ("stderr");
    qof_log_set_level ("gnc", (QofLogLevel)G_LOG_LEVEL_DEBUG);
    gnc_engine_init_static (argc, argv);
    gnc_prefs_init ();
    gnc_component_manager_init ();

    g_test_add_func ("/gnome-utils/tree-view-row-ownership/account",
                     test_account_lookup_releases_tree_item);
    g_test_add_func ("/gnome-utils/tree-view-row-ownership/account-dispose-row-callback",
                     test_account_dispose_quiesces_retained_row);
    g_test_add_func ("/gnome-utils/tree-view-row-ownership/account-selection-modes",
                     test_account_selection_modes_preserve_semantics);
    g_test_add_func ("/gnome-utils/tree-view-row-ownership/commodity",
                     test_commodity_lookup_releases_tree_item);
    g_test_add_func ("/gnome-utils/tree-view-row-ownership/price",
                     test_price_lookup_releases_tree_item);
    g_test_add_func ("/gnome-utils/tree-view-row-ownership/owner",
                     test_owner_selection_survives_view_dispose);
    g_test_add_func ("/gnome-utils/tree-view-row-ownership/query",
                     test_query_selection_switch_disconnects_old_model);
    g_test_add_func ("/gnome-utils/tree-view-row-ownership/query-native-sorting",
                     test_query_native_column_sorting);
    status = g_test_run ();

    gnc_component_manager_shutdown ();
    gnc_prefs_remove_registered ();
    gnc_engine_shutdown ();
    return status;
}
