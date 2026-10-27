#include <config.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <gtk/gtk.h>

/* These tests exercise the application’s existing GtkDialog response protocol. */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
#include <gtest/gtest.h>
#include <libguile.h>

#include "googletest-glib-log-handler.hpp"
#include "test/gnome-response-test-fixture.h"
#include "Account.h"
#include "Split.h"
#include "Transaction.h"
#include "basiccell.h"
#include "cashobjects.h"
#include "gnc-backend-prov.hpp"
#include "gnc-commodity.h"
#include "gnc-component-manager.h"
#include "gnc-date.h"
#include "gnc-engine.h"
#include "gnc-gsettings.h"
#include "gnc-ledger-display.h"
#include "gnc-main-window.h"
#include "gnc-plugin-page-register.h"
#include "gnc-plugin-page.h"
#include "gnc-session.h"
#include "gnc-split-reg.h"
#include "gnucash-register.h"
#include "qof-backend.hpp"
#include "qofbook.h"
#include "qofsession.h"
#include "split-register.h"
#include "table-allgui.h"
#include "table-layout.h"

namespace
{

constexpr const char *original_description = "Lifecycle original";
constexpr const char *edited_description = "Lifecycle edited in register cursor";
constexpr const char *backend_method = "register-lifecycle";
constexpr const char *backend_uri = "register-lifecycle://fixture";

constexpr std::array<const char *, 5> process_filters
{
    "RegisterSaveCloseLifecycle.SaveClose",
    "RegisterSaveCloseLifecycle.CancelClose",
    "RegisterSaveCloseLifecycle.ValidationFailure",
    "RegisterSaveCloseLifecycle.BackendFailure",
    "RegisterSaveCloseLifecycle.ReentrantClose"
};

QofSession *sentinel_session{};
GncMainWindow *sentinel_window{};

struct BackendObservations
{
    Transaction *target{};
    QofBook *book{};
    GncPluginPage *reenter_page{};
    unsigned target_commits{};
    unsigned target_rollbacks{};
    unsigned failed_commits{};
    unsigned error_signals{};
    unsigned sync_calls{};
    unsigned reentrant_calls{};
    bool fail_next_target_commit{};
    QofBackendError last_error{ERR_BACKEND_NO_ERR};
    std::string attempted_description;
};

BackendObservations backend_observations;

class RegisterCommitBackend final : public QofBackend
{
public:
    void session_begin (QofSession *, const char *, SessionOpenMode) override {}
    void session_end () override {}
    void load (QofBook *, QofBackendLoadType) override {}

    void commit (QofInstance *instance) override
    {
        auto &observed = backend_observations;
        if (observed.target && instance == QOF_INSTANCE (observed.target))
        {
            EXPECT_TRUE (GNC_IS_TRANS (instance));
            EXPECT_EQ (qof_session_get_book (gnc_get_current_session ()), observed.book);
            EXPECT_EQ (qof_book_get_backend (observed.book), this);
            ++observed.target_commits;
            observed.attempted_description = xaccTransGetDescription (observed.target);
            if (observed.reenter_page)
            {
                auto page = observed.reenter_page;
                observed.reenter_page = nullptr;
                ++observed.reentrant_calls;
                gnc_main_window_close_page (page);
            }
            if (observed.fail_next_target_commit)
            {
                observed.fail_next_target_commit = false;
                ++observed.failed_commits;
                set_error (ERR_BACKEND_MISC);
                return;
            }
        }
        QofBackend::commit (instance);
    }

    void rollback (QofInstance *instance) override
    {
        if (backend_observations.target &&
            instance == QOF_INSTANCE (backend_observations.target))
            ++backend_observations.target_rollbacks;
        QofBackend::rollback (instance);
    }

    void sync (QofBook *) override
    {
        ++backend_observations.sync_calls;
        ADD_FAILURE () << "Register page close must not use session sync";
    }

    void safe_sync (QofBook *book) override
    {
        sync (book);
    }
};

class RegisterCommitProvider final : public QofBackendProvider
{
public:
    RegisterCommitProvider () :
        QofBackendProvider ("Register lifecycle transaction commit", backend_method) {}

    QofBackend *create_backend () override
    {
        return new RegisterCommitBackend;
    }

    bool type_check (const char *uri) override
    {
        return g_strcmp0 (uri, backend_uri) == 0;
    }
};

void
commit_error (gpointer data, QofBackendError error)
{
    auto observed = static_cast<BackendObservations *> (data);
    ++observed->error_signals;
    observed->last_error = error;
}

template<typename Predicate>
bool
wait_for (Predicate predicate)
{
    auto deadline = g_get_monotonic_time () + 3 * G_TIME_SPAN_SECOND;
    do
    {
        if (predicate ())
            return true;
        for (unsigned count = 0; count < 128; ++count)
            if (!g_main_context_iteration (nullptr, FALSE))
                break;
        g_usleep (1000);
    }
    while (g_get_monotonic_time () < deadline);
    return predicate ();
}

void
drain_ready_events ()
{
    for (unsigned count = 0; count < 512; ++count)
        if (!g_main_context_iteration (nullptr, FALSE))
            break;
}

struct PageSearch
{
    GncPluginPage *page;
    unsigned count{};
};

void
count_installed_page (GncPluginPage *page, gpointer data)
{
    auto search = static_cast<PageSearch *> (data);
    if (page == search->page)
        ++search->count;
}

unsigned
installed_count (GncPluginPage *page)
{
    PageSearch search{page};
    gnc_main_window_foreach_page (count_installed_page, &search);
    return search.count;
}

std::vector<GtkWidget *>
transient_dialogs (GncMainWindow *window)
{
    std::vector<GtkWidget *> result;
#if GTK_CHECK_VERSION(4, 0, 0)
    auto toplevels = gtk_window_get_toplevels ();
    for (guint i = 0; i < g_list_model_get_n_items (toplevels); ++i)
    {
        auto widget = GTK_WIDGET (g_list_model_get_item (toplevels, i));
        if (GTK_IS_DIALOG (widget) && gtk_widget_get_visible (widget) &&
            gtk_window_get_transient_for (GTK_WINDOW (widget)) == GTK_WINDOW (window))
            result.push_back (widget);
        g_object_unref (widget);
    }
#else
    auto toplevels = gtk_window_list_toplevels ();
    for (auto node = toplevels; node; node = node->next)
    {
        auto widget = GTK_WIDGET (node->data);
        if (GTK_IS_DIALOG (widget) && gtk_widget_get_visible (widget) &&
            gtk_window_get_transient_for (GTK_WINDOW (widget)) == GTK_WINDOW (window))
            result.push_back (widget);
    }
    g_list_free (toplevels);
#endif
    return result;
}

class RegisterSaveCloseLifecycle : public GnomeResponseTest
{
protected:
    QofSession *session{};
    QofBook *book{};
    GncMainWindow *window{};
    Account *bank{};
    Account *income{};
    Account *placeholder{};
    Transaction *transaction{};
    Split *bank_split{};
    GncGUID transaction_guid{};
    GncGUID bank_split_guid{};
    GncGUID income_split_guid{};
    GncPluginPage *page{};
    GNCSplitReg *gsr{};
    SplitRegister *reg{};
    GWeakRef page_weak{};
    GncPluginPageClass *page_class{};
    void (*real_destroy_widget) (GncPluginPage *){};
    bool weak_initialized{};
    bool original_transferred{};
    bool test_ref_owned{};
    bool cleaning_up{};
    unsigned removed_calls{};
    unsigned destroy_calls{};
    unsigned destroy_completed{};
    unsigned dialog_responses{};

    static RegisterSaveCloseLifecycle *observer;

    static void removed (GncPluginPage *removed_page, gpointer data)
    {
        auto self = static_cast<RegisterSaveCloseLifecycle *> (data);
        ++self->removed_calls;
        if (!self->cleaning_up)
        {
            EXPECT_EQ (removed_page, self->page);
            EXPECT_EQ (installed_count (removed_page), 0u);
            EXPECT_EQ (self->destroy_calls, 0u);
        }
    }

    static void observe_destroy_widget (GncPluginPage *destroyed_page)
    {
        auto self = observer;
        auto target = destroyed_page == self->page;
        if (target)
        {
            ++self->destroy_calls;
            if (!self->cleaning_up)
            {
                EXPECT_EQ (self->removed_calls, 1u);
                EXPECT_EQ (installed_count (destroyed_page), 0u);
            }
        }
        self->real_destroy_widget (destroyed_page);
        if (target)
            ++self->destroy_completed;
    }

    static void response_received (GtkDialog *, gint, gpointer data)
    {
        ++*static_cast<unsigned *> (data);
    }

    Account *make_account (Account *root, gnc_commodity *currency,
                           const char *name, GNCAccountType type)
    {
        auto account = xaccMallocAccount (book);
        xaccAccountBeginEdit (account);
        xaccAccountSetName (account, name);
        xaccAccountSetType (account, type);
        xaccAccountSetCommodity (account, currency);
        gnc_account_append_child (root, account);
        xaccAccountCommitEdit (account);
        return account;
    }

    void SetUp () override
    {
        GnomeResponseTest::SetUp ();
        ASSERT_FALSE (HasFatalFailure ());
        ASSERT_NE (sentinel_window, nullptr);
        ASSERT_EQ (gnc_get_current_session (), sentinel_session);
        backend_observations = {};
        gnc_engine_add_commit_error_callback (commit_error, &backend_observations);

        session = qof_session_new (qof_book_new ());
        gnc_set_current_session (nullptr);
        gnc_set_current_session (session);
        qof_session_begin (session, backend_uri, SESSION_NEW_STORE);
        ASSERT_EQ (qof_session_get_error (session), ERR_BACKEND_NO_ERR);
        qof_session_load (session, nullptr);
        ASSERT_EQ (qof_session_get_error (session), ERR_BACKEND_NO_ERR);
        book = qof_session_get_book (session);
        ASSERT_NE (dynamic_cast<RegisterCommitBackend *> (qof_session_get_backend (session)),
                   nullptr);
        ASSERT_EQ (qof_book_get_backend (book), qof_session_get_backend (session));

        auto commodities = gnc_commodity_table_get_table (book);
        auto currency = gnc_commodity_table_lookup (commodities, "CURRENCY", "USD");
        if (!currency)
            currency = gnc_commodity_table_insert (
                commodities, gnc_commodity_new (book, "US Dollar", "CURRENCY",
                                                "USD", "840", 100));
        ASSERT_NE (currency, nullptr);
        auto root = xaccMallocAccount (book);
        xaccAccountBeginEdit (root);
        xaccAccountSetName (root, "Lifecycle Root");
        xaccAccountSetType (root, ACCT_TYPE_ROOT);
        gnc_book_set_root_account (book, root);
        xaccAccountCommitEdit (root);
        bank = make_account (root, currency, "Lifecycle Bank", ACCT_TYPE_BANK);
        income = make_account (root, currency, "Lifecycle Income", ACCT_TYPE_INCOME);
        placeholder = make_account (root, currency, "Lifecycle Placeholder", ACCT_TYPE_INCOME);
        xaccAccountBeginEdit (placeholder);
        xaccAccountSetPlaceholder (placeholder, TRUE);
        xaccAccountCommitEdit (placeholder);

        transaction = xaccMallocTransaction (book);
        xaccTransBeginEdit (transaction);
        xaccTransSetCurrency (transaction, currency);
        xaccTransSetDatePostedSecsNormalized (transaction, gnc_time (nullptr));
        xaccTransSetDescription (transaction, original_description);
        bank_split = xaccMallocSplit (book);
        xaccSplitSetParent (bank_split, transaction);
        xaccSplitSetAccount (bank_split, bank);
        xaccSplitSetAmount (bank_split, gnc_numeric_create (10, 1));
        xaccSplitSetValue (bank_split, gnc_numeric_create (10, 1));
        auto income_split = xaccMallocSplit (book);
        xaccSplitSetParent (income_split, transaction);
        xaccSplitSetAccount (income_split, income);
        xaccSplitSetAmount (income_split, gnc_numeric_create (-10, 1));
        xaccSplitSetValue (income_split, gnc_numeric_create (-10, 1));
        xaccTransCommitEdit (transaction);
        ASSERT_FALSE (xaccTransIsOpen (transaction));
        ASSERT_EQ (xaccTransCountSplits (transaction), 2);
        transaction_guid = *qof_instance_get_guid (QOF_INSTANCE (transaction));
        bank_split_guid = *qof_instance_get_guid (QOF_INSTANCE (bank_split));
        income_split_guid = *qof_instance_get_guid (QOF_INSTANCE (income_split));

        window = gnc_main_window_new ();
        g_object_ref_sink (window);
        page = gnc_plugin_page_register_new (bank, FALSE);
        ASSERT_NE (page, nullptr);
        g_object_ref (page);
        test_ref_owned = true;
        g_weak_ref_init (&page_weak, page);
        weak_initialized = true;
        g_signal_connect (page, "removed", G_CALLBACK (removed), this);
        page_class = GNC_PLUGIN_PAGE_GET_CLASS (page);
        real_destroy_widget = page_class->destroy_widget;
        ASSERT_NE (real_destroy_widget, nullptr);
        ASSERT_EQ (observer, nullptr);
        observer = this;
        page_class->destroy_widget = observe_destroy_widget;

        gnc_main_window_open_page (window, page);
        original_transferred = installed_count (page) == 1;
        ASSERT_TRUE (original_transferred);
        gtk_widget_set_visible (GTK_WIDGET (window), TRUE);
        drain_ready_events ();
        ASSERT_EQ (gnc_main_window_get_current_page (window), page);
        gsr = gnc_plugin_page_register_get_gsr (page);
        ASSERT_NE (gsr, nullptr);
        ASSERT_FALSE (gsr->read_only);
        reg = gnc_ledger_display_get_split_register (gsr->ledger);
        ASSERT_NE (reg, nullptr);
        gnc_plugin_page_register_clear_current_filter (page);
        gnc_split_register_config (reg, reg->type, REG_STYLE_LEDGER, FALSE);
        gnc_ledger_display_set_focus (gsr->ledger, TRUE);
        gnc_ledger_display_refresh (gsr->ledger);
        drain_ready_events ();
        ASSERT_FALSE (gnc_split_register_changed (reg));
        ASSERT_TRUE (transient_dialogs (window).empty ());
        backend_observations = {};
        backend_observations.target = transaction;
        backend_observations.book = book;
    }

    void edit_cursor_cell (const char *cell_name, const char *text)
    {
        ASSERT_FALSE (cleaning_up);
        ASSERT_NE (reg, nullptr);
        VirtualLocation location{};
        ASSERT_TRUE (gnc_split_register_get_split_virt_loc (
            reg, bank_split, &location.vcell_loc));
        ASSERT_TRUE (gnc_table_get_cell_location (
            reg->table, cell_name, location.vcell_loc, &location));
        const auto &current = reg->table->current_cursor_loc;
        if (current.vcell_loc.virt_row != location.vcell_loc.virt_row ||
            current.vcell_loc.virt_col != location.vcell_loc.virt_col ||
            current.phys_row_offset != location.phys_row_offset ||
            current.phys_col_offset != location.phys_col_offset)
            gnucash_register_goto_virt_loc (gsr->reg, location);
        ASSERT_EQ (gnc_split_register_get_current_trans (reg), transaction);
        ASSERT_EQ (gnc_split_register_get_current_split (reg), bank_split);
        ASSERT_STREQ (gnc_table_get_current_cell_name (reg->table), cell_name);
        auto cell = gnc_table_layout_get_cell (reg->table->layout, cell_name);
        ASSERT_NE (cell, nullptr);
        auto old_length = static_cast<int> (std::strlen (gnc_basic_cell_get_value (cell)));
        auto new_length = static_cast<int> (std::strlen (text));
        int position = new_length;
        int start = 0;
        int end = old_length;
        gboolean cancelled = FALSE;
        auto result = gnc_table_modify_update (
            reg->table, reg->table->current_cursor_loc,
            text, new_length, text, new_length,
            &position, &start, &end, &cancelled);
        ASSERT_FALSE (cancelled);
        ASSERT_NE (result, nullptr);
        ASSERT_STREQ (gnc_basic_cell_get_value (cell), text);
        ASSERT_TRUE (gnc_basic_cell_get_changed (cell));
        ASSERT_TRUE (gnc_table_current_cursor_changed (reg->table, FALSE));
        ASSERT_TRUE (gnc_split_register_changed (reg));
        ASSERT_STREQ (xaccTransGetDescription (transaction), original_description);
        ASSERT_EQ (backend_observations.target_commits, 0u);
    }

    GtkWidget *confirmation ()
    {
        if (!wait_for ([this] { return !transient_dialogs (window).empty (); }))
            return nullptr;
        auto dialogs = transient_dialogs (window);
        if (dialogs.size () != 1)
            return nullptr;
        auto dialog = dialogs.front ();
#if GTK_CHECK_VERSION(4, 0, 0)
        if (!gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), 0) ||
            !gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), 1) ||
            !gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), 2))
#else
        if (!gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT) ||
            !gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL) ||
            !gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_REJECT))
#endif
            return nullptr;
        return dialog;
    }

    void assert_still_installed ()
    {
        ASSERT_FALSE (cleaning_up);
        ASSERT_NE (page, nullptr);
        EXPECT_EQ (installed_count (page), 1u);
        EXPECT_EQ (gnc_main_window_get_current_page (window), page);
        EXPECT_EQ (removed_calls, 0u);
        EXPECT_EQ (destroy_calls, 0u);
        EXPECT_EQ (destroy_completed, 0u);
        auto live = g_weak_ref_get (&page_weak);
        EXPECT_EQ (live, page);
        if (live)
            g_object_unref (live);
    }

    void assert_removed_and_finalize_before_cleanup ()
    {
        ASSERT_FALSE (cleaning_up);
        ASSERT_TRUE (wait_for ([this]
        {
            return removed_calls == 1 && destroy_completed == 1;
        }));
        ASSERT_EQ (installed_count (page), 0u);
        ASSERT_EQ (removed_calls, 1u);
        ASSERT_EQ (destroy_calls, 1u);
        ASSERT_EQ (destroy_completed, 1u);
        auto live = g_weak_ref_get (&page_weak);
        const bool expected_page = live == page && live != nullptr;
        if (live)
            g_object_unref (live);
        ASSERT_TRUE (expected_page);
        ASSERT_TRUE (test_ref_owned);
        auto test_ref = page;
        page = nullptr;
        test_ref_owned = false;
        reg = nullptr;
        gsr = nullptr;
        g_object_unref (test_ref);
        ASSERT_TRUE (wait_for ([this]
        {
            auto remaining = g_weak_ref_get (&page_weak);
            if (!remaining)
                return true;
            g_object_unref (remaining);
            return false;
        }));
        EXPECT_EQ (removed_calls, 1u);
        EXPECT_EQ (destroy_calls, 1u);
        EXPECT_EQ (destroy_completed, 1u);
        EXPECT_FALSE (cleaning_up);
        EXPECT_EQ (backend_observations.sync_calls, 0u);
    }

    void assert_transaction (const char *description)
    {
        auto current = xaccTransLookup (&transaction_guid, book);
        ASSERT_NE (current, nullptr);
        EXPECT_FALSE (xaccTransIsOpen (current));
        EXPECT_STREQ (xaccTransGetDescription (current), description);
        EXPECT_EQ (xaccTransCountSplits (current), 2);
        auto debit = xaccSplitLookup (&bank_split_guid, book);
        auto credit = xaccSplitLookup (&income_split_guid, book);
        ASSERT_NE (debit, nullptr);
        ASSERT_NE (credit, nullptr);
        EXPECT_EQ (xaccSplitGetParent (debit), current);
        EXPECT_EQ (xaccSplitGetParent (credit), current);
        EXPECT_EQ (xaccSplitGetAccount (debit), bank);
        EXPECT_EQ (xaccSplitGetAccount (credit), income);
        EXPECT_TRUE (gnc_numeric_equal (xaccSplitGetValue (debit), gnc_numeric_create (10, 1)));
        EXPECT_TRUE (gnc_numeric_equal (xaccSplitGetValue (credit), gnc_numeric_create (-10, 1)));
    }

    void accept_close ()
    {
        gnc_main_window_close_page (page);
        auto dialog = confirmation ();
        ASSERT_NE (dialog, nullptr);
        g_signal_connect (dialog, "response", G_CALLBACK (response_received), &dialog_responses);
#if GTK_CHECK_VERSION(4, 0, 0)
        ASSERT_NE (gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), 2), nullptr);
        gtk_dialog_response (GTK_DIALOG (dialog), 2);
#else
        gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
#endif
    }

    void TearDown () override
    {
        cleaning_up = true;
        // Remove fixture-backed callbacks and the class hook before any
        // cleanup can dispatch signals, including after a fatal SetUp assertion.
        if (weak_initialized)
        {
            auto live = g_weak_ref_get (&page_weak);
            if (live)
            {
                g_signal_handlers_disconnect_by_func (
                    live, reinterpret_cast<gpointer> (removed), this);
                g_object_unref (live);
            }
        }
        if (observer == this)
        {
            page_class->destroy_widget = real_destroy_widget;
            observer = nullptr;
        }
        backend_observations.fail_next_target_commit = false;
        backend_observations.reenter_page = nullptr;
        if (window)
        {
            for (auto dialog : transient_dialogs (window))
            {
                g_object_ref (dialog);
                g_signal_handlers_disconnect_by_func (
                    dialog, reinterpret_cast<gpointer> (response_received), &dialog_responses);
#if GTK_CHECK_VERSION(4, 0, 0)
                gtk_window_destroy (GTK_WINDOW (dialog));
#else
                gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
#endif
                g_object_unref (dialog);
            }
        }
        if (page && installed_count (page))
        {
            auto current_gsr = gnc_plugin_page_register_get_gsr (page);
            if (current_gsr && current_gsr->ledger)
            {
                auto current_reg = gnc_ledger_display_get_split_register (current_gsr->ledger);
                if (current_reg)
                    gnc_split_register_cancel_cursor_trans_changes (current_reg);
            }
            gnc_main_window_close_page (page);
        }
        GnomeResponseTest::TearDown ();
        if (page)
        {
            if (!original_transferred)
                g_object_unref (page);
            if (test_ref_owned)
                g_object_unref (page);
            page = nullptr;
            test_ref_owned = false;
        }
        if (weak_initialized)
            g_weak_ref_clear (&page_weak);
        if (window)
            g_object_unref (window);
        gnc_engine_add_commit_error_callback (nullptr, nullptr);
        if (session)
        {
            backend_observations.target = nullptr;
            gnc_clear_current_session ();
            session = nullptr;
        }
        gnc_set_current_session (sentinel_session);
    }
};

RegisterSaveCloseLifecycle *RegisterSaveCloseLifecycle::observer{};

TEST_F (RegisterSaveCloseLifecycle, SaveClose)
{
    edit_cursor_cell (DESC_CELL, edited_description);
    ASSERT_FALSE (HasFatalFailure ());
    accept_close ();
    ASSERT_FALSE (HasFatalFailure ());
    assert_removed_and_finalize_before_cleanup ();
    ASSERT_FALSE (HasFatalFailure ());
    EXPECT_EQ (dialog_responses, 1u);
    EXPECT_EQ (backend_observations.target_commits, 1u);
    EXPECT_EQ (backend_observations.failed_commits, 0u);
    EXPECT_EQ (backend_observations.error_signals, 0u);
    EXPECT_EQ (backend_observations.attempted_description, edited_description);
    assert_transaction (edited_description);
}

TEST_F (RegisterSaveCloseLifecycle, CancelClose)
{
    edit_cursor_cell (DESC_CELL, edited_description);
    ASSERT_FALSE (HasFatalFailure ());
    gnc_main_window_close_page (page);
    auto dialog = confirmation ();
    ASSERT_NE (dialog, nullptr);
    g_signal_connect (dialog, "response", G_CALLBACK (response_received), &dialog_responses);
    #if GTK_CHECK_VERSION(4, 0, 0)
        gtk_dialog_response (GTK_DIALOG (dialog), 1);
#else
        gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
#endif
    drain_ready_events ();
    assert_still_installed ();
    ASSERT_FALSE (HasFatalFailure ());
    EXPECT_TRUE (gnc_split_register_changed (reg));
    EXPECT_EQ (backend_observations.target_commits, 0u);
    EXPECT_EQ (backend_observations.error_signals, 0u);
    EXPECT_EQ (backend_observations.sync_calls, 0u);
    assert_transaction (original_description);
    accept_close ();
    ASSERT_FALSE (HasFatalFailure ());
    assert_removed_and_finalize_before_cleanup ();
    EXPECT_EQ (dialog_responses, 2u);
    EXPECT_EQ (backend_observations.target_commits, 1u);
    assert_transaction (edited_description);
}

TEST_F (RegisterSaveCloseLifecycle, ValidationFailure)
{
    auto placeholder_name = gnc_account_get_full_name (placeholder);
    std::string invalid_transfer{placeholder_name};
    g_free (placeholder_name);
    edit_cursor_cell (MXFRM_CELL, invalid_transfer.c_str ());
    ASSERT_FALSE (HasFatalFailure ());
    ASSERT_STREQ (gnc_table_get_current_cell_name (reg->table), MXFRM_CELL);
    accept_close ();
    ASSERT_FALSE (HasFatalFailure ());
    ASSERT_TRUE (wait_for ([this] { return !transient_dialogs (window).empty (); }));
    auto errors = transient_dialogs (window);
    ASSERT_EQ (errors.size (), 1u);
    auto error = errors.front ();
    ASSERT_TRUE (GTK_IS_MESSAGE_DIALOG (error));
#if !GTK_CHECK_VERSION(4, 0, 0)
    GtkMessageType message_type{};
    g_object_get (error, "message-type", &message_type, nullptr);
    EXPECT_EQ (message_type, GTK_MESSAGE_ERROR);
#endif
    assert_still_installed ();
    ASSERT_FALSE (HasFatalFailure ());
    EXPECT_TRUE (gnc_split_register_changed (reg));
    EXPECT_EQ (backend_observations.target_commits, 0u);
    EXPECT_EQ (backend_observations.failed_commits, 0u);
    EXPECT_EQ (backend_observations.target_rollbacks, 0u);
    EXPECT_EQ (backend_observations.error_signals, 0u);
    EXPECT_EQ (backend_observations.sync_calls, 0u);
    assert_transaction (original_description);
#if GTK_CHECK_VERSION(4, 0, 0)
    ASSERT_NE (gtk_dialog_get_widget_for_response (GTK_DIALOG (error), 0), nullptr);
    gtk_dialog_response (GTK_DIALOG (error), 0);
#else
    ASSERT_NE (gtk_dialog_get_widget_for_response (GTK_DIALOG (error), GTK_RESPONSE_CLOSE),
               nullptr);
    gtk_dialog_response (GTK_DIALOG (error), GTK_RESPONSE_CLOSE);
#endif
    drain_ready_events ();
    auto income_name = gnc_account_get_full_name (income);
    std::string valid_transfer{income_name};
    g_free (income_name);
    edit_cursor_cell (MXFRM_CELL, valid_transfer.c_str ());
    ASSERT_FALSE (HasFatalFailure ());
    accept_close ();
    ASSERT_FALSE (HasFatalFailure ());
    assert_removed_and_finalize_before_cleanup ();
    EXPECT_EQ (dialog_responses, 2u);
    EXPECT_EQ (backend_observations.failed_commits, 0u);
    EXPECT_EQ (backend_observations.error_signals, 0u);
    assert_transaction (original_description);
}

TEST_F (RegisterSaveCloseLifecycle, BackendFailure)
{
    RecordProperty ("observed_contract", "Transaction commit rollback signals an engine error but does not veto register page close");
    edit_cursor_cell (DESC_CELL, edited_description);
    ASSERT_FALSE (HasFatalFailure ());
    backend_observations.fail_next_target_commit = true;
    accept_close ();
    ASSERT_FALSE (HasFatalFailure ());
    EXPECT_EQ (backend_observations.target_commits, 1u);
    EXPECT_EQ (backend_observations.failed_commits, 1u);
    EXPECT_EQ (backend_observations.target_rollbacks, 1u);
    EXPECT_EQ (backend_observations.error_signals, 1u);
    EXPECT_EQ (backend_observations.last_error, ERR_BACKEND_MISC);
    EXPECT_EQ (backend_observations.attempted_description, edited_description);
    EXPECT_EQ (backend_observations.sync_calls, 0u);
    assert_transaction (original_description);
    assert_removed_and_finalize_before_cleanup ();
    EXPECT_EQ (dialog_responses, 1u);
}

TEST_F (RegisterSaveCloseLifecycle, ReentrantClose)
{
    edit_cursor_cell (DESC_CELL, edited_description);
    ASSERT_FALSE (HasFatalFailure ());
    gnc_main_window_close_page (page);
    auto dialog = confirmation ();
    ASSERT_NE (dialog, nullptr);
    g_signal_connect (dialog, "response", G_CALLBACK (response_received), &dialog_responses);
    gnc_main_window_close_page (page);
    gnc_main_window_close_page (page);
    drain_ready_events ();
    auto dialogs = transient_dialogs (window);
    ASSERT_EQ (dialogs.size (), 1u);
    EXPECT_EQ (dialogs.front (), dialog);
    assert_still_installed ();
    ASSERT_FALSE (HasFatalFailure ());
    EXPECT_EQ (backend_observations.target_commits, 0u);
    backend_observations.reenter_page = page;
    #if GTK_CHECK_VERSION(4, 0, 0)
        ASSERT_NE (gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), 2), nullptr);
        gtk_dialog_response (GTK_DIALOG (dialog), 2);
#else
        gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
#endif
    assert_removed_and_finalize_before_cleanup ();
    EXPECT_EQ (dialog_responses, 1u);
    EXPECT_EQ (backend_observations.reentrant_calls, 1u);
    EXPECT_EQ (backend_observations.target_commits, 1u);
    EXPECT_EQ (backend_observations.failed_commits, 0u);
    EXPECT_EQ (backend_observations.error_signals, 0u);
    assert_transaction (edited_description);
}

void
real_main (void *, int argc, char **argv)
{
    if (!gtk_init_check ())
    {
        g_printerr ("GTK display initialization failed; register lifecycle tests require a display.\n");
        std::exit (1);
    }
    gnc::test::initialize_logging ();
    qof_init ();
    gnc_engine_init (0, nullptr);
    cashobjects_register ();
    gnc_gsettings_load_backend ();
    gnc_component_manager_init ();
    gnucash_register_add_cell_types ();
    qof_backend_register_provider (std::make_unique<RegisterCommitProvider> ());
    sentinel_session = qof_session_new (qof_book_new ());
    gnc_set_current_session (sentinel_session);
    sentinel_window = gnc_main_window_new ();
    g_object_ref_sink (sentinel_window);
    auto result = RUN_ALL_TESTS ();
    auto tests = ::testing::UnitTest::GetInstance ();
    if (tests->test_to_run_count () != 1 || tests->successful_test_count () != 1)
        result = EXIT_FAILURE;
    std::_Exit (result);
}

}

int
main (int argc, char **argv)
{
    ::testing::InitGoogleTest (&argc, argv);
    const std::string filter = ::testing::GTEST_FLAG (filter);
    auto exact_filter = std::find_if (process_filters.begin (), process_filters.end (),
                                     [&filter] (const char *candidate)
                                     {
                                         return filter == candidate;
                                     }) != process_filters.end ();
    if (!exact_filter || ::testing::GTEST_FLAG (repeat) != 1 ||
        ::testing::GTEST_FLAG (list_tests) ||
        g_getenv ("GTEST_TOTAL_SHARDS") || g_getenv ("GTEST_SHARD_INDEX"))
    {
        g_printerr ("Select exactly one RegisterSaveCloseLifecycle case per process; repeat must be 1, list_tests false, and sharding unset.\n");
        for (auto candidate : process_filters)
            g_printerr ("--gtest_filter=%s --gtest_repeat=1\n", candidate);
        return 2;
    }
    g_setenv ("GNC_UNINSTALLED", "YES", TRUE);
    g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);
    scm_init_guile ();
    real_main (nullptr, argc, argv);
    return 1;
}
