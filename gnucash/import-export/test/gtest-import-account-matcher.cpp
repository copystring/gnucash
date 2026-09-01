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
#include <import-operation-teardown.h>
#include <gnc-ofx-import-teardown.h>
#include <gnc-session.h>
#include <gnc-ui-util.h>
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
        g_log_set_always_fatal (static_cast<GLogLevelFlags> (
            G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL));
    }

    ImportMatcherTest() :
        m_book{gnc_get_current_book()}, m_root{gnc_account_create_root(m_book)}
    {
        auto create_account = [this](Account* parent, GNCAccountType type,
                                     const char* name,
                                     const char* online)->Account* {
            auto account = xaccMallocAccount(this->m_book);
            xaccAccountBeginEdit(account);
            xaccAccountSetType(account, type);
            xaccAccountSetName(account, name);
            xaccAccountBeginEdit(parent);
            gnc_account_append_child(parent, account);
            if (online)
                qof_instance_set(QOF_INSTANCE(account), "online-id", online, NULL);
            xaccAccountCommitEdit(parent);
            xaccAccountCommitEdit(account);
            return account;
        };
        auto assets = create_account(m_root, ACCT_TYPE_ASSET,
                                     "Assets", nullptr);
        auto expenses = create_account(m_root, ACCT_TYPE_EXPENSE,
                                       "Expenses", nullptr);
        create_account(assets, ACCT_TYPE_BANK, "Bank", "Bank");
        auto broker = create_account(assets, ACCT_TYPE_ASSET,
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
        xaccAccountBeginEdit(m_root);
        xaccAccountDestroy(m_root); //It does the commit
        gnc_clear_current_session();
    }

    QofBook* m_book;
    Account* m_root;
};

struct AccountSelectionResult
{
    Account *account {nullptr};
    gboolean accepted {FALSE};
};

static void
account_selected (Account *account, gboolean accepted, gpointer user_data)
{
    auto result = static_cast<AccountSelectionResult*> (user_data);

    result->account = account;
    result->accepted = accepted;
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
