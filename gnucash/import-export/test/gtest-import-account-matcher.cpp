/********************************************************************
 * gtest-import-account-matcher.cpp --                              *
 *                        unit tests import-account-matcher.        *
 * Copyright (C) 2020 John Ralls <jralls@ceridwen.us>               *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
 *                                                                  *
 *******************************************************************/

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcpp"
#include <gtest/gtest.h>
#pragma GCC diagnostic pop

#include <config.h>
#include <import-account-matcher.h>
#include <import-backend.h>
#include <import-main-matcher.h>
#include <import-match-picker.h>
#include <import-operation-teardown.h>
#include <import-pending-matches.h>
#include <gnc-ofx-import-teardown.h>
#include <gnc-prefs.h>
#include <gnc-prefs-utils.h>
#include <gnc-session.h>
#include <gnc-ui-util.h>
#include <gnc-commodity.h>
#include <gnc-engine.h>
#include <qofbook.h>
#include <Account.h>
#include <Transaction.h>
#include <gtk/gtk.h>
#include <vector>

using AccountV = std::vector<const Account*>;
using AccountTypeV = std::vector<GNCAccountType>;
using AccountPair = std::pair<AccountV&,
                              const AccountTypeV&>;

class ImportMatcherTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite ()
    {
        gtk_init ();
        ASSERT_TRUE (gtk_is_initialized ());
        gnc_engine_init_static (0, nullptr);
        gnc_prefs_init ();
        g_log_set_always_fatal (static_cast<GLogLevelFlags> (
            G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL));
    }

    static void TearDownTestSuite ()
    {
        gnc_prefs_remove_registered ();
        gnc_engine_shutdown ();
    }

    ImportMatcherTest() :
        m_book{gnc_get_current_book()},
        m_root{gnc_book_get_root_account(m_book)},
        m_currency{gnc_commodity_table_lookup (
            gnc_commodity_table_get_table (m_book),
            GNC_COMMODITY_NS_CURRENCY, "USD")}
    {
        g_assert_nonnull (m_currency);
        auto create_account = [this](Account* parent, GNCAccountType type,
                                     const char* name,
                                     const char* online)->Account* {
            auto account = xaccMallocAccount(this->m_book);
            xaccAccountBeginEdit(account);
            xaccAccountSetType(account, type);
            xaccAccountSetName(account, name);
            xaccAccountSetCommodity(account, m_currency);
            xaccAccountBeginEdit(parent);
            gnc_account_append_child(parent, account);
            if (online)
                qof_instance_set(QOF_INSTANCE(account), "online-id", online, NULL);
            xaccAccountCommitEdit(parent);
            xaccAccountCommitEdit(account);
            return account;
        };
        m_assets = create_account(m_root, ACCT_TYPE_ASSET,
                                  "Assets", nullptr);
        auto expenses = create_account(m_root, ACCT_TYPE_EXPENSE,
                                       "Expenses", nullptr);
        m_bank = create_account(m_assets, ACCT_TYPE_BANK, "Bank", "Bank");
        auto broker = create_account(m_assets, ACCT_TYPE_ASSET,
                                     "Broker", "Broker");
        auto stocks = create_account(broker, ACCT_TYPE_STOCK,
                                     "Stocks", "BrokerStocks");
        create_account(stocks, ACCT_TYPE_STOCK, "AAPL", "BrokerStocksAAPL");
        create_account(stocks, ACCT_TYPE_STOCK, "MSFT", "BrokerStocksMSFT ");
        create_account(stocks, ACCT_TYPE_STOCK, "HPE", "BrokerStocksHPE");
        create_account(broker, ACCT_TYPE_BANK, "Cash Management",
                       "BrokerCash Management");
       create_account(expenses, ACCT_TYPE_EXPENSE, "Food", nullptr);
        create_account(expenses, ACCT_TYPE_EXPENSE, "Gas", nullptr);
        create_account(expenses, ACCT_TYPE_EXPENSE, "Rent", nullptr);
   }
    ~ImportMatcherTest()
    {
        gnc_clear_current_session();
    }

    QofBook* m_book;
    Account* m_root;
    gnc_commodity* m_currency;
    Account* m_assets;
    Account* m_bank;
};

struct AccountSelectionResult
{
    Account *account {nullptr};
    gboolean accepted {FALSE};
    guint calls {0};
};

static void
account_selected (Account *account, gboolean accepted, gpointer user_data)
{
    auto result = static_cast<AccountSelectionResult*> (user_data);

    result->account = account;
    result->accepted = accepted;
    result->calls++;
}

static GtkWidget *
find_buildable_widget (GtkWidget *widget, const gchar *buildable_id)
{
    if (GTK_IS_BUILDABLE (widget) &&
        g_strcmp0 (gtk_buildable_get_buildable_id (GTK_BUILDABLE (widget)),
                   buildable_id) == 0)
        return widget;

    for (auto child = gtk_widget_get_first_child (widget); child;
         child = gtk_widget_get_next_sibling (child))
    {
        auto result = find_buildable_widget (child, buildable_id);

        if (result)
            return result;
    }
    return nullptr;
}

static GtkWindow *
find_buildable_window (const gchar *buildable_id)
{
    auto windows = gtk_window_get_toplevels ();

    for (guint position = 0;
         position < g_list_model_get_n_items (windows); position++)
    {
        auto window = GTK_WINDOW (g_list_model_get_item (windows, position));

        if (g_strcmp0 (gtk_buildable_get_buildable_id (GTK_BUILDABLE (window)),
                       buildable_id) == 0)
            return window;
        g_object_unref (window);
    }
    return nullptr;
}

static gboolean
weak_ref_was_finalized (GWeakRef *weak_ref)
{
    auto object = G_OBJECT (g_weak_ref_get (weak_ref));
    auto finalized = object == nullptr;

    g_clear_object (&object);
    g_weak_ref_clear (weak_ref);
    return finalized;
}

struct TestTransaction
{
    Transaction *transaction;
    Split *split;
};

static TestTransaction
create_test_transaction (QofBook *book, Account *account,
                         gnc_commodity *currency, gint64 amount,
                         char reconcile, gboolean leave_open)
{
    auto transaction = xaccMallocTransaction (book);
    auto split = xaccMallocSplit (book);
    auto value = gnc_numeric_create (amount, 1);

    xaccTransBeginEdit (transaction);
    xaccTransSetCurrency (transaction, currency);
    xaccTransSetDatePostedSecsNormalized (transaction, 1000);
    xaccTransSetDescription (transaction, "selection regression");
    xaccSplitSetParent (split, transaction);
    xaccSplitSetAccount (split, account);
    xaccSplitSetAmount (split, value);
    xaccSplitSetValue (split, value);
    xaccSplitSetReconcile (split, reconcile);
    if (!leave_open)
        xaccTransCommitEdit (transaction);
    return { transaction, split };
}

static GNCImportMatchInfo *
find_match_for_split (GNCImportTransInfo *info, Split *split)
{
    for (auto node = gnc_import_TransInfo_get_match_list (info); node;
         node = g_list_next (node))
    {
        auto match = static_cast<GNCImportMatchInfo*> (node->data);

        if (gnc_import_MatchInfo_get_split (match) == split)
            return match;
    }
    return nullptr;
}

struct MatchPickerResult
{
    guint calls {0};
};

static void
match_picker_done (GNCImportTransInfo *info, gpointer user_data)
{
    auto result = static_cast<MatchPickerResult*> (user_data);

    result->calls++;
    (void)info;
}

struct OfxLifecycleMetrics
{
    guint metadata_cleanup_calls {0};
    guint payload_destroy_calls {0};
    guint reconcile_calls {0};
    GncImportOperationTeardownResult result {
        GNC_IMPORT_OPERATION_TEARDOWN_STALE};
};

struct OfxLifecyclePayload
{
    OfxLifecycleMetrics *metrics;
    GNCImportMainMatcher *matcher {nullptr};
    GList *transactions {nullptr};
};

static void
ofx_lifecycle_payload_destroyed (gpointer user_data)
{
    auto payload = static_cast<OfxLifecyclePayload *> (user_data);
    payload->metrics->payload_destroy_calls++;
    delete payload;
}

static void
ofx_lifecycle_metadata_cleanup (GncOfxImportLifecycle *lifecycle,
                                GncImportOperationTeardownResult result,
                                gpointer user_data)
{
    auto payload = static_cast<OfxLifecyclePayload *> (user_data);
    payload->metrics->metadata_cleanup_calls++;
    payload->metrics->result = result;
    EXPECT_EQ (lifecycle == nullptr, false);
}

static GncOfxImportLifecycle *
create_ofx_lifecycle (QofBook *book, GApplication *application,
                      OfxLifecycleMetrics *metrics,
                      OfxLifecyclePayload **payload_out)
{
    auto context = gnc_session_operation_context_new (
        book, QOF_SESSION_OPERATION_IMPORT);
    if (!context)
        return nullptr;
    auto payload = new OfxLifecyclePayload {metrics};
    auto lifecycle = gnc_ofx_import_lifecycle_new (
        context, application, &payload->matcher, &payload->transactions,
        ofx_lifecycle_metadata_cleanup, payload,
        ofx_lifecycle_payload_destroyed);
    gnc_session_operation_context_unref (context);
    if (payload_out)
        *payload_out = payload;
    return lifecycle;
}

static Transaction *
add_open_transaction (QofBook *book, OfxLifecyclePayload *payload)
{
    auto transaction = xaccMallocTransaction (book);
    xaccTransBeginEdit (transaction);
    payload->transactions = g_list_append (payload->transactions,
                                           transaction);
    return transaction;
}

static void
run_matcher_ofx_cancel_order (QofBook *book, gboolean matcher_first)
{
    auto application = g_application_new (nullptr, G_APPLICATION_NON_UNIQUE);
    OfxLifecycleMetrics metrics;
    OfxLifecyclePayload *payload = nullptr;
    auto lifecycle = create_ofx_lifecycle (book, application, &metrics,
                                           &payload);
    ASSERT_NE (lifecycle, nullptr);

    payload->matcher = gnc_gen_trans_list_new (nullptr, nullptr, FALSE, 42, FALSE);
    ASSERT_NE (payload->matcher, nullptr);
    ASSERT_TRUE (gnc_gen_trans_list_bind_operation_teardown (
        payload->matcher,
        gnc_ofx_import_lifecycle_get_teardown (lifecycle)));
    add_open_transaction (book, payload);

    auto save_lease = qof_session_operation_lease_acquire_for (
        gnc_get_current_session (), QOF_SESSION_OPERATION_SAVE);
    ASSERT_NE (save_lease, nullptr);
    if (matcher_first)
    {
        gnc_gen_trans_list_delete (payload->matcher);
        EXPECT_FALSE (gnc_ofx_import_lifecycle_request (lifecycle));
    }
    else
    {
        EXPECT_FALSE (gnc_ofx_import_lifecycle_request (lifecycle));
        gnc_gen_trans_list_delete (payload->matcher);
    }
    EXPECT_FALSE (gnc_ofx_import_lifecycle_request (lifecycle));
    EXPECT_EQ (metrics.metadata_cleanup_calls, 0u);

    qof_session_operation_lease_release (save_lease);
    for (guint turn = 0;
         turn < 16 && metrics.metadata_cleanup_calls == 0; ++turn)
        g_main_context_iteration (nullptr, TRUE);

    EXPECT_EQ (metrics.metadata_cleanup_calls, 1u);
    EXPECT_EQ (metrics.payload_destroy_calls, 1u);
    EXPECT_EQ (metrics.result,
               GNC_IMPORT_OPERATION_TEARDOWN_MUTATION_ALLOWED);
    for (guint turn = 0; turn < 3; ++turn)
        g_main_context_iteration (nullptr, FALSE);
    EXPECT_EQ (metrics.metadata_cleanup_calls, 1u);
    EXPECT_EQ (metrics.payload_destroy_calls, 1u);
    g_object_unref (application);
}

static void
reconcile_continuation_called (GObject *source, gpointer user_data)
{
    auto metrics = static_cast<OfxLifecycleMetrics *> (user_data);
    metrics->reconcile_calls++;
    (void)source;
}

TEST_F(ImportMatcherTest, test_simple_match)
{
    auto found = gnc_import_select_account(nullptr, "Bank", FALSE, nullptr,
                                           nullptr, ACCT_TYPE_NONE, nullptr,
                                           nullptr);
    ASSERT_NE(nullptr, found);
    EXPECT_STREQ("Bank", xaccAccountGetName(found));
}

TEST_F(ImportMatcherTest, test_async_match)
{
    AccountSelectionResult result;

    gnc_import_select_account_async(nullptr, "Bank", FALSE, nullptr,
                                    nullptr, ACCT_TYPE_NONE, nullptr,
                                    account_selected, &result);
    ASSERT_TRUE(result.accepted);
    ASSERT_NE(nullptr, result.account);
    EXPECT_STREQ("Bank", xaccAccountGetName(result.account));
}

TEST_F(ImportMatcherTest, test_async_unmatched_without_prompt)
{
    AccountSelectionResult result;

    gnc_import_select_account_async(nullptr, "Missing", FALSE, nullptr,
                                    nullptr, ACCT_TYPE_NONE, nullptr,
                                    account_selected, &result);
    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(nullptr, result.account);
}

TEST_F(ImportMatcherTest, account_picker_without_default_stays_unselected)
{
    AccountSelectionResult result;
    constexpr auto unmatched_id = "selection-regression-unmatched";

    ASSERT_EQ (xaccAccountGetOnlineID (m_assets), nullptr);
    gnc_import_select_account_async (nullptr, unmatched_id, TRUE,
                                     "Unmatched account", m_currency,
                                     ACCT_TYPE_NONE, nullptr,
                                     account_selected, &result);

    auto window = find_buildable_window ("account_picker_dialog");
    ASSERT_NE (window, nullptr);
    auto scroller = GTK_SCROLLED_WINDOW (find_buildable_widget (
        GTK_WIDGET (window), "account_tree_sw"));
    auto ok_button = find_buildable_widget (GTK_WIDGET (window), "okbutton");
    auto cancel_button = find_buildable_widget (GTK_WIDGET (window), "cancelbutton");
    ASSERT_TRUE (GTK_IS_SCROLLED_WINDOW (scroller));
    ASSERT_TRUE (GTK_IS_BUTTON (ok_button));
    ASSERT_TRUE (GTK_IS_BUTTON (cancel_button));
    auto view = GTK_COLUMN_VIEW (gtk_scrolled_window_get_child (scroller));
    ASSERT_TRUE (GTK_IS_COLUMN_VIEW (view));
    auto selection = GTK_SINGLE_SELECTION (gtk_column_view_get_model (view));
    ASSERT_TRUE (GTK_IS_SINGLE_SELECTION (selection));
    g_object_ref (selection);
    GWeakRef selection_ref;
    GWeakRef window_ref;
    g_weak_ref_init (&selection_ref, G_OBJECT (selection));
    g_weak_ref_init (&window_ref, G_OBJECT (window));

    EXPECT_EQ (gtk_single_selection_get_selected (selection),
               GTK_INVALID_LIST_POSITION);
    EXPECT_FALSE (gtk_widget_get_sensitive (ok_button));

    g_signal_emit_by_name (cancel_button, "clicked");
    EXPECT_EQ (result.calls, 1u);
    EXPECT_FALSE (result.accepted);
    EXPECT_EQ (result.account, nullptr);
    EXPECT_EQ (xaccAccountGetOnlineID (m_assets), nullptr);
    EXPECT_STREQ (xaccAccountGetOnlineID (m_bank), "Bank");
    g_object_unref (window);
    EXPECT_TRUE (weak_ref_was_finalized (&window_ref));
    gtk_single_selection_set_selected (selection, 0);
    EXPECT_EQ (result.calls, 1u);
    g_object_unref (selection);
    EXPECT_TRUE (weak_ref_was_finalized (&selection_ref));
}

TEST_F(ImportMatcherTest, account_picker_preserves_valid_default)
{
    AccountSelectionResult result;

    gnc_import_select_account_async (nullptr, nullptr, TRUE,
                                     "Existing default account", m_currency,
                                     ACCT_TYPE_NONE, m_bank,
                                     account_selected, &result);

    auto window = find_buildable_window ("account_picker_dialog");
    ASSERT_NE (window, nullptr);
    auto scroller = GTK_SCROLLED_WINDOW (find_buildable_widget (
        GTK_WIDGET (window), "account_tree_sw"));
    auto ok_button = find_buildable_widget (GTK_WIDGET (window), "okbutton");
    ASSERT_TRUE (GTK_IS_SCROLLED_WINDOW (scroller));
    ASSERT_TRUE (GTK_IS_BUTTON (ok_button));
    auto view = GTK_COLUMN_VIEW (gtk_scrolled_window_get_child (scroller));
    ASSERT_TRUE (GTK_IS_COLUMN_VIEW (view));
    auto selection = GTK_SINGLE_SELECTION (gtk_column_view_get_model (view));
    ASSERT_TRUE (GTK_IS_SINGLE_SELECTION (selection));
    g_object_ref (selection);
    GWeakRef selection_ref;
    GWeakRef window_ref;
    g_weak_ref_init (&selection_ref, G_OBJECT (selection));
    g_weak_ref_init (&window_ref, G_OBJECT (window));

    EXPECT_NE (gtk_single_selection_get_selected (selection),
               GTK_INVALID_LIST_POSITION);
    EXPECT_TRUE (gtk_widget_get_sensitive (ok_button));
    g_signal_emit_by_name (ok_button, "clicked");

    EXPECT_EQ (result.calls, 1u);
    EXPECT_TRUE (result.accepted);
    EXPECT_EQ (result.account, m_bank);
    EXPECT_STREQ (xaccAccountGetOnlineID (m_bank), "Bank");
    g_object_unref (window);
    EXPECT_TRUE (weak_ref_was_finalized (&window_ref));
    gtk_single_selection_set_selected (selection, GTK_INVALID_LIST_POSITION);
    EXPECT_EQ (result.calls, 1u);
    g_object_unref (selection);
    EXPECT_TRUE (weak_ref_was_finalized (&selection_ref));
}

TEST_F(ImportMatcherTest, account_picker_no_mutation_preserves_online_id)
{
    AccountSelectionResult result;
    constexpr auto unmatched_id = "selection-regression-no-mutation";

    ASSERT_STREQ (xaccAccountGetOnlineID (m_bank), "Bank");
    gnc_import_select_account_async_no_mutation (
        nullptr, unmatched_id, TRUE, "Existing default account", m_currency,
        ACCT_TYPE_NONE, m_bank, account_selected, &result);

    auto window = find_buildable_window ("account_picker_dialog");
    ASSERT_NE (window, nullptr);
    auto ok_button = find_buildable_widget (GTK_WIDGET (window), "okbutton");
    ASSERT_TRUE (GTK_IS_BUTTON (ok_button));
    EXPECT_TRUE (gtk_widget_get_sensitive (ok_button));
    g_signal_emit_by_name (ok_button, "clicked");

    EXPECT_EQ (result.calls, 1u);
    EXPECT_TRUE (result.accepted);
    EXPECT_EQ (result.account, m_bank);
    EXPECT_STREQ (xaccAccountGetOnlineID (m_bank), "Bank");
    g_object_unref (window);
    EXPECT_EQ (result.calls, 1u);
}

TEST_F(ImportMatcherTest, account_picker_external_destroy_finishes_once)
{
    AccountSelectionResult result;

    gnc_import_select_account_async (
        nullptr, "selection-regression-external-destroy", TRUE,
        "Externally destroyed picker", m_currency, ACCT_TYPE_NONE, nullptr,
        account_selected, &result);

    auto window = find_buildable_window ("account_picker_dialog");
    ASSERT_NE (window, nullptr);
    auto scroller = GTK_SCROLLED_WINDOW (find_buildable_widget (
        GTK_WIDGET (window), "account_tree_sw"));
    ASSERT_TRUE (GTK_IS_SCROLLED_WINDOW (scroller));
    auto view = GTK_COLUMN_VIEW (gtk_scrolled_window_get_child (scroller));
    ASSERT_TRUE (GTK_IS_COLUMN_VIEW (view));
    auto selection = GTK_SINGLE_SELECTION (gtk_column_view_get_model (view));
    ASSERT_TRUE (GTK_IS_SINGLE_SELECTION (selection));
    g_object_ref (selection);
    GWeakRef selection_ref;
    GWeakRef window_ref;
    g_weak_ref_init (&selection_ref, G_OBJECT (selection));
    g_weak_ref_init (&window_ref, G_OBJECT (window));

    gtk_window_destroy (window);
    EXPECT_EQ (result.calls, 0u);
    g_object_unref (window);

    EXPECT_TRUE (weak_ref_was_finalized (&window_ref));
    EXPECT_EQ (result.calls, 1u);
    EXPECT_FALSE (result.accepted);
    gtk_single_selection_set_selected (selection, 0);
    EXPECT_EQ (result.calls, 1u);
    g_object_unref (selection);
    EXPECT_TRUE (weak_ref_was_finalized (&selection_ref));
}

TEST_F(ImportMatcherTest, match_picker_does_not_select_first_visible_match)
{
    constexpr auto prefs_group = "dialogs.import.generic.match-picker";
    constexpr auto display_reconciled = "display-reconciled";
    auto previous_display_reconciled = gnc_prefs_get_bool (
        prefs_group, display_reconciled);
    auto imported = create_test_transaction (m_book, m_bank, m_currency,
                                             100, NREC, TRUE);
    auto visible = create_test_transaction (m_book, m_bank, m_currency,
                                            100, NREC, FALSE);
    auto hidden = create_test_transaction (m_book, m_bank, m_currency,
                                           100, YREC, FALSE);
    auto trans_info = gnc_import_TransInfo_new (imported.transaction, m_bank);

    /* split_find_match prepends: add the hidden selection last so that the
     * first displayed row is the other candidate after filtering. */
    split_find_match (trans_info, visible.split, 0, 4, 14, 0.0);
    split_find_match (trans_info, hidden.split, 0, 4, 14, 0.0);
    auto visible_match = find_match_for_split (trans_info, visible.split);
    auto hidden_match = find_match_for_split (trans_info, hidden.split);
    ASSERT_NE (visible_match, nullptr);
    ASSERT_NE (hidden_match, nullptr);
    gnc_import_TransInfo_set_selected_match_info (trans_info, hidden_match, TRUE);
    auto pending_matches = gnc_import_PendingMatches_new ();
    gnc_import_PendingMatches_add_match (pending_matches, hidden_match, TRUE);
    gnc_prefs_set_bool (prefs_group, display_reconciled, FALSE);
    MatchPickerResult result;

    gnc_import_match_picker_run (nullptr, trans_info, pending_matches,
                                 match_picker_done, &result);

    auto window = find_buildable_window ("match_picker_dialog");
    ASSERT_NE (window, nullptr);
    auto scroller = GTK_SCROLLED_WINDOW (find_buildable_widget (
        GTK_WIDGET (window), "matched_view"));
    auto downloaded_scroller = GTK_SCROLLED_WINDOW (find_buildable_widget (
        GTK_WIDGET (window), "download_view"));
    ASSERT_TRUE (GTK_IS_SCROLLED_WINDOW (scroller));
    ASSERT_TRUE (GTK_IS_SCROLLED_WINDOW (downloaded_scroller));
    auto view = GTK_COLUMN_VIEW (gtk_scrolled_window_get_child (scroller));
    auto downloaded_view = GTK_COLUMN_VIEW (
        gtk_scrolled_window_get_child (downloaded_scroller));
    ASSERT_TRUE (GTK_IS_COLUMN_VIEW (view));
    ASSERT_TRUE (GTK_IS_COLUMN_VIEW (downloaded_view));
    auto selection = GTK_SINGLE_SELECTION (gtk_column_view_get_model (view));
    auto downloaded_selection = GTK_SINGLE_SELECTION (
        gtk_column_view_get_model (downloaded_view));
    ASSERT_TRUE (GTK_IS_SINGLE_SELECTION (selection));
    ASSERT_TRUE (GTK_IS_SINGLE_SELECTION (downloaded_selection));
    auto ok_button = gtk_window_get_default_widget (window);
    ASSERT_TRUE (GTK_IS_BUTTON (ok_button));
    g_object_ref (selection);
    g_object_ref (downloaded_selection);
    GWeakRef selection_ref;
    GWeakRef downloaded_selection_ref;
    GWeakRef window_ref;
    g_weak_ref_init (&selection_ref, G_OBJECT (selection));
    g_weak_ref_init (&downloaded_selection_ref,
                     G_OBJECT (downloaded_selection));
    g_weak_ref_init (&window_ref, G_OBJECT (window));
    EXPECT_EQ (g_list_model_get_n_items (G_LIST_MODEL (selection)), 1u);
    EXPECT_EQ (gtk_single_selection_get_selected (selection),
               GTK_INVALID_LIST_POSITION);
    EXPECT_EQ (gnc_import_TransInfo_get_selected_match (trans_info),
               hidden_match);
    EXPECT_EQ (gnc_import_PendingMatches_get_match_type (
                   pending_matches, hidden_match), GNCImportPending_MANUAL);
    EXPECT_EQ (gnc_import_PendingMatches_get_match_type (
                   pending_matches, visible_match), GNCImportPending_NONE);

    g_signal_emit_by_name (ok_button, "clicked");

    EXPECT_EQ (result.calls, 1u);
    EXPECT_EQ (gnc_import_TransInfo_get_selected_match (trans_info), nullptr);
    EXPECT_EQ (gnc_import_PendingMatches_get_match_type (
                   pending_matches, hidden_match), GNCImportPending_NONE);
    EXPECT_EQ (gnc_import_PendingMatches_get_match_type (
                   pending_matches, visible_match), GNCImportPending_NONE);

    gnc_prefs_set_bool (prefs_group, display_reconciled,
                        previous_display_reconciled);
    g_object_unref (window);
    EXPECT_TRUE (weak_ref_was_finalized (&window_ref));
    gtk_single_selection_set_selected (selection, 0);
    gtk_single_selection_set_selected (downloaded_selection,
                                       GTK_INVALID_LIST_POSITION);
    EXPECT_EQ (result.calls, 1u);
    g_object_unref (downloaded_selection);
    g_object_unref (selection);
    EXPECT_TRUE (weak_ref_was_finalized (&downloaded_selection_ref));
    EXPECT_TRUE (weak_ref_was_finalized (&selection_ref));
    gnc_import_PendingMatches_delete (pending_matches);
    gnc_import_TransInfo_delete (trans_info);
}

TEST_F(ImportMatcherTest, match_picker_external_destroy_finishes_once)
{
    auto imported = create_test_transaction (m_book, m_bank, m_currency,
                                             100, NREC, TRUE);
    auto candidate = create_test_transaction (m_book, m_bank, m_currency,
                                              100, NREC, FALSE);
    auto trans_info = gnc_import_TransInfo_new (imported.transaction, m_bank);
    split_find_match (trans_info, candidate.split, 0, 4, 14, 0.0);
    auto pending_matches = gnc_import_PendingMatches_new ();
    MatchPickerResult result;

    gnc_import_match_picker_run (nullptr, trans_info, pending_matches,
                                 match_picker_done, &result);

    auto window = find_buildable_window ("match_picker_dialog");
    ASSERT_NE (window, nullptr);
    auto match_scroller = GTK_SCROLLED_WINDOW (find_buildable_widget (
        GTK_WIDGET (window), "matched_view"));
    auto downloaded_scroller = GTK_SCROLLED_WINDOW (find_buildable_widget (
        GTK_WIDGET (window), "download_view"));
    ASSERT_TRUE (GTK_IS_SCROLLED_WINDOW (match_scroller));
    ASSERT_TRUE (GTK_IS_SCROLLED_WINDOW (downloaded_scroller));
    auto match_view = GTK_COLUMN_VIEW (
        gtk_scrolled_window_get_child (match_scroller));
    auto downloaded_view = GTK_COLUMN_VIEW (
        gtk_scrolled_window_get_child (downloaded_scroller));
    ASSERT_TRUE (GTK_IS_COLUMN_VIEW (match_view));
    ASSERT_TRUE (GTK_IS_COLUMN_VIEW (downloaded_view));
    auto match_selection = GTK_SINGLE_SELECTION (
        gtk_column_view_get_model (match_view));
    auto downloaded_selection = GTK_SINGLE_SELECTION (
        gtk_column_view_get_model (downloaded_view));
    ASSERT_TRUE (GTK_IS_SINGLE_SELECTION (match_selection));
    ASSERT_TRUE (GTK_IS_SINGLE_SELECTION (downloaded_selection));
    ASSERT_GT (g_list_model_get_n_items (G_LIST_MODEL (match_selection)), 0u);
    ASSERT_GT (g_list_model_get_n_items (G_LIST_MODEL (downloaded_selection)), 0u);
    g_object_ref (match_selection);
    g_object_ref (downloaded_selection);
    GWeakRef match_selection_ref;
    GWeakRef downloaded_selection_ref;
    GWeakRef window_ref;
    g_weak_ref_init (&match_selection_ref, G_OBJECT (match_selection));
    g_weak_ref_init (&downloaded_selection_ref,
                     G_OBJECT (downloaded_selection));
    g_weak_ref_init (&window_ref, G_OBJECT (window));

    gtk_window_destroy (window);
    EXPECT_EQ (result.calls, 0u);
    g_object_unref (window);

    EXPECT_TRUE (weak_ref_was_finalized (&window_ref));
    EXPECT_EQ (result.calls, 1u);
    gtk_single_selection_set_selected (match_selection, 0);
    gtk_single_selection_set_selected (downloaded_selection,
                                       GTK_INVALID_LIST_POSITION);
    EXPECT_EQ (result.calls, 1u);
    g_object_unref (downloaded_selection);
    g_object_unref (match_selection);
    EXPECT_TRUE (weak_ref_was_finalized (&downloaded_selection_ref));
    EXPECT_TRUE (weak_ref_was_finalized (&match_selection_ref));
    gnc_import_PendingMatches_delete (pending_matches);
    gnc_import_TransInfo_delete (trans_info);
}

TEST_F(ImportMatcherTest, matcher_then_ofx_cancel_coalesces_and_cleans_once)
{
    run_matcher_ofx_cancel_order (m_book, TRUE);
}

TEST_F(ImportMatcherTest, ofx_then_matcher_cancel_coalesces_and_cleans_once)
{
    run_matcher_ofx_cancel_order (m_book, FALSE);
}

TEST_F(ImportMatcherTest, ofx_immediate_cleanup_uses_product_lifecycle)
{
    auto application = g_application_new (nullptr, G_APPLICATION_NON_UNIQUE);
    OfxLifecycleMetrics metrics;
    OfxLifecyclePayload *payload = nullptr;
    auto lifecycle = create_ofx_lifecycle (m_book, application, &metrics,
                                           &payload);
    ASSERT_NE (lifecycle, nullptr);
    add_open_transaction (m_book, payload);

    EXPECT_TRUE (gnc_ofx_import_lifecycle_request (lifecycle));
    EXPECT_EQ (metrics.metadata_cleanup_calls, 1u);
    EXPECT_EQ (metrics.payload_destroy_calls, 1u);
    EXPECT_EQ (metrics.result,
               GNC_IMPORT_OPERATION_TEARDOWN_MUTATION_ALLOWED);
    g_object_unref (application);
}

TEST_F(ImportMatcherTest, parent_abort_keeps_payload_until_async_state_releases)
{
    auto application = g_application_new (nullptr, G_APPLICATION_NON_UNIQUE);
    OfxLifecycleMetrics metrics;
    OfxLifecyclePayload *payload = nullptr;
    auto lifecycle = create_ofx_lifecycle (m_book, application, &metrics,
                                           &payload);
    ASSERT_NE (lifecycle, nullptr);
    auto state = gnc_ofx_import_async_state_new (lifecycle);
    ASSERT_NE (state, nullptr);

    EXPECT_TRUE (gnc_ofx_import_async_state_request_teardown (state));
    EXPECT_FALSE (gnc_ofx_import_async_state_is_active (state));
    EXPECT_EQ (metrics.metadata_cleanup_calls, 1u);
    EXPECT_EQ (metrics.payload_destroy_calls, 0u);
    gnc_ofx_import_async_state_unref (state);
    EXPECT_EQ (metrics.payload_destroy_calls, 1u);
    g_object_unref (application);
}

TEST_F(ImportMatcherTest, parent_abort_disconnects_reconcile_before_window_destroy)
{
    auto application = g_application_new (nullptr, G_APPLICATION_NON_UNIQUE);
    OfxLifecycleMetrics metrics;
    OfxLifecyclePayload *payload = nullptr;
    auto lifecycle = create_ofx_lifecycle (m_book, application, &metrics,
                                           &payload);
    ASSERT_NE (lifecycle, nullptr);
    auto window = gtk_window_new ();
    g_object_ref_sink (window);
    ASSERT_TRUE (gnc_ofx_import_lifecycle_connect_destroy (
        lifecycle, G_OBJECT (window), reconcile_continuation_called,
        &metrics));

    EXPECT_TRUE (gnc_ofx_import_lifecycle_request (lifecycle));
    EXPECT_EQ (metrics.metadata_cleanup_calls, 1u);
    EXPECT_EQ (metrics.payload_destroy_calls, 1u);
    gtk_window_destroy (GTK_WINDOW (window));
    EXPECT_EQ (metrics.reconcile_calls, 0u);
    g_object_unref (window);
    g_object_unref (application);
}

TEST_F(ImportMatcherTest, shutdown_destroys_retry_source_and_transfers_book_ownership)
{
    auto application = g_application_new (nullptr, G_APPLICATION_NON_UNIQUE);
    OfxLifecycleMetrics metrics;
    OfxLifecyclePayload *payload = nullptr;
    auto lifecycle = create_ofx_lifecycle (m_book, application, &metrics,
                                           &payload);
    ASSERT_NE (lifecycle, nullptr);
    auto raw_transaction = add_open_transaction (m_book, payload);

    auto save_lease = qof_session_operation_lease_acquire_for (
        gnc_get_current_session (), QOF_SESSION_OPERATION_SAVE);
    ASSERT_NE (save_lease, nullptr);
    /* No cancel/request precedes shutdown: the production owner must still
     * terminalize the multi-turn workflow and release its application hold. */
    g_signal_emit_by_name (application, "shutdown");

    EXPECT_EQ (metrics.metadata_cleanup_calls, 1u);
    EXPECT_EQ (metrics.payload_destroy_calls, 1u);
    EXPECT_EQ (metrics.result,
               GNC_IMPORT_OPERATION_TEARDOWN_BOOK_SHUTDOWN);
    ASSERT_NE (raw_transaction, nullptr);
    EXPECT_TRUE (xaccTransIsOpen (raw_transaction));
    EXPECT_EQ (qof_instance_get_book (QOF_INSTANCE (raw_transaction)), m_book);
    EXPECT_EQ (qof_collection_lookup_entity (
                   qof_book_get_collection (m_book, GNC_ID_TRANS),
                   qof_instance_get_guid (QOF_INSTANCE (raw_transaction))),
               QOF_INSTANCE (raw_transaction));
    qof_session_operation_lease_release (save_lease);

    /* BOOK_SHUTDOWN deliberately leaves the open object owned by QofBook. The
     * live-fixture test cleans it under a fresh lease instead of destroying the
     * book, proving that the owner released only non-owning references. */
    auto cleanup_lease = qof_session_operation_lease_acquire_for (
        gnc_get_current_session (), QOF_SESSION_OPERATION_IMPORT);
    ASSERT_NE (cleanup_lease, nullptr);
    xaccTransDestroy (raw_transaction);
    xaccTransCommitEdit (raw_transaction);
    qof_session_operation_lease_release (cleanup_lease);
    g_object_unref (application);
}

TEST_F(ImportMatcherTest, pending_timeout_shutdown_completes_once_and_cancels_retry)
{
    auto application = g_application_new (nullptr, G_APPLICATION_NON_UNIQUE);
    OfxLifecycleMetrics metrics;
    OfxLifecyclePayload *payload = nullptr;
    auto lifecycle = create_ofx_lifecycle (m_book, application, &metrics,
                                           &payload);
    ASSERT_NE (lifecycle, nullptr);
    auto raw_transaction = add_open_transaction (m_book, payload);

    auto save_lease = qof_session_operation_lease_acquire_for (
        gnc_get_current_session (), QOF_SESSION_OPERATION_SAVE);
    ASSERT_NE (save_lease, nullptr);
    EXPECT_FALSE (gnc_ofx_import_lifecycle_request (lifecycle));
    EXPECT_TRUE (gnc_import_operation_teardown_has_pending_retry (
        gnc_ofx_import_lifecycle_get_teardown (lifecycle)));
    EXPECT_EQ (metrics.metadata_cleanup_calls, 0u);
    EXPECT_EQ (metrics.payload_destroy_calls, 0u);

    g_signal_emit_by_name (application, "shutdown");

    EXPECT_EQ (metrics.metadata_cleanup_calls, 1u);
    EXPECT_EQ (metrics.payload_destroy_calls, 1u);
    EXPECT_EQ (metrics.result,
               GNC_IMPORT_OPERATION_TEARDOWN_BOOK_SHUTDOWN);
    ASSERT_NE (raw_transaction, nullptr);
    EXPECT_TRUE (xaccTransIsOpen (raw_transaction));
    for (guint turn = 0; turn < 4; ++turn)
        g_main_context_iteration (nullptr, FALSE);
    EXPECT_EQ (metrics.metadata_cleanup_calls, 1u);
    EXPECT_EQ (metrics.payload_destroy_calls, 1u);

    qof_session_operation_lease_release (save_lease);
    auto cleanup_lease = qof_session_operation_lease_acquire_for (
        gnc_get_current_session (), QOF_SESSION_OPERATION_IMPORT);
    ASSERT_NE (cleanup_lease, nullptr);
    xaccTransDestroy (raw_transaction);
    xaccTransCommitEdit (raw_transaction);
    qof_session_operation_lease_release (cleanup_lease);
    g_object_unref (application);
}

TEST_F(ImportMatcherTest, test_noisy_match)
{
    auto found = gnc_import_select_account(nullptr, "BankUSD", FALSE, nullptr,
                                           nullptr, ACCT_TYPE_NONE, nullptr,
                                           nullptr);
    ASSERT_NE(nullptr, found);
    EXPECT_STREQ("Bank", xaccAccountGetName(found));
}

TEST_F(ImportMatcherTest, test_match_with_subaccounts)
{
    auto found = gnc_import_select_account(nullptr, "BrokerStocks", FALSE,
                                           nullptr, nullptr, ACCT_TYPE_NONE,
                                           nullptr, nullptr);
    ASSERT_NE(nullptr, found);
    EXPECT_STREQ("Stocks", xaccAccountGetName(found));
}

TEST_F(ImportMatcherTest, test_subaccount_match)
{
    auto found = gnc_import_select_account(nullptr, "BrokerStocksHPE", FALSE,
                                           nullptr, nullptr, ACCT_TYPE_NONE,
                                           nullptr, nullptr);
    ASSERT_NE(nullptr, found);
    EXPECT_STREQ("HPE", xaccAccountGetName(found));
}

TEST_F(ImportMatcherTest, test_subaccount_match_trailing_noise)
{
    auto found = gnc_import_select_account(nullptr, "BrokerStocksHPEUSD", FALSE,
                                           nullptr, nullptr, ACCT_TYPE_NONE,
                                           nullptr, nullptr);
    ASSERT_NE(nullptr, found);
    EXPECT_STREQ("HPE", xaccAccountGetName(found));
}

TEST_F(ImportMatcherTest, test_subaccount_no_match)
{
    auto found = gnc_import_select_account(nullptr, "BrokerStocksINTC", FALSE,
                                           nullptr, nullptr, ACCT_TYPE_STOCK,
                                           nullptr, nullptr);
    ASSERT_EQ(nullptr, found);
}

TEST_F(ImportMatcherTest, test_subaccount_match_trailing_space)
{
    auto found = gnc_import_select_account(nullptr, "BrokerStocksMSFT ", FALSE,
                                           nullptr, nullptr, ACCT_TYPE_NONE,
                                           nullptr, nullptr);
    ASSERT_NE(nullptr, found);
    EXPECT_STREQ("MSFT", xaccAccountGetName(found));
}

TEST_F(ImportMatcherTest, test_subaccount_match_trim_trailing_space)
{
    auto found = gnc_import_select_account(nullptr, "BrokerStocksMSFT", FALSE,
                                           nullptr, nullptr, ACCT_TYPE_NONE,
                                           nullptr, nullptr);
    ASSERT_NE(nullptr, found);
    EXPECT_STREQ("MSFT", xaccAccountGetName(found));
}

TEST_F(ImportMatcherTest, test_subaccount_match_internal_space)
{
    auto found = gnc_import_select_account(nullptr, "BrokerCash Management",
                                           FALSE, nullptr, nullptr,
                                           ACCT_TYPE_NONE, nullptr, nullptr);
    ASSERT_NE(nullptr, found);
    EXPECT_STREQ("Cash Management", xaccAccountGetName(found));
}
