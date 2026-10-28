/* Copyright (C) 2026 GnuCash contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <config.h>
#include <cstdint>
#include <gtk/gtk.h>
#include <libguile.h>
#include <gtest/gtest.h>
#include "googletest-glib-log-handler.hpp"

#include "Account.h"
#include "cashobjects.h"
extern "C" {
#include "gncEntryLedger.h"
#include "gncEntryLedgerP.h"
#include "cellblock.h"
}
#include "basiccell.h"
#include "gnc-component-manager.h"
#include "gnc-gsettings.h"
#include "gncInvoice.h"
#include "gncTaxTable.h"
#include "gnc-session.h"
#include "combocell.h"
#include "gnucash-register.h"
#include "qof.h"

namespace
{
class EntryLedgerCloseTest : public ::testing::Test
{
protected:
    void SetUp () override
    {
        book = qof_book_new ();
        ASSERT_NE (book, nullptr);
        if (!gnc_book_get_root_account (book))
            gnc_account_create_root (book);
        session = qof_session_new (book);
        ASSERT_NE (session, nullptr);
        gnc_set_current_session (session);
        invoice = gncInvoiceCreate (book);
        ASSERT_NE (invoice, nullptr);
        ledger = gnc_entry_ledger_new (book, GNCENTRY_INVOICE_ENTRY);
        ASSERT_NE (ledger, nullptr);
        parent = gtk_window_new ();
        g_object_ref_sink (parent);
        gtk_widget_realize (parent);
        gnc_entry_ledger_set_parent (ledger, parent);
        gnc_entry_ledger_set_default_invoice (ledger, invoice);
    }

    void TearDown () override
    {
        if (result == 0 && parent)
        {
            gtk_window_destroy (GTK_WINDOW (parent));
            const std::int64_t deadline =
                g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
            while (result == 0 && g_get_monotonic_time () < deadline)
            {
                while (g_main_context_iteration (nullptr, FALSE))
                    ;
                g_usleep (1000);
            }
        }
        if (ledger)
            gnc_entry_ledger_destroy (ledger);
        if (parent)
        {
            gtk_window_destroy (GTK_WINDOW (parent));
            g_object_unref (parent);
        }
        auto current = gnc_current_session_exist () ? gnc_get_current_session () : nullptr;
        if (current)
            gnc_clear_current_session ();
        if (session && session != current)
            qof_session_destroy (session);
        if (other_session && other_session != current &&
            other_session != session)
            qof_session_destroy (other_session);
    }

    QofSession *session{};
    QofBook *book{};
    GncInvoice *invoice{};
    GncEntryLedger *ledger{};
    GtkWidget *parent{};
    QofSession *other_session{};
    std::int32_t result{};
};

static void completed (GncEntryLedger *, gboolean accepted, gpointer data)
{
    *static_cast<std::int32_t *> (data) = accepted ? 1 : -1;
}
TEST_F (EntryLedgerCloseTest, UnchangedLedgerCompletesImmediately)
{
    gnc_entry_ledger_check_close_async (parent, ledger, completed, &result);
    EXPECT_EQ (result, 1);
}
struct RequestProbe
{
    int callbacks = 0;
    int destroys = 0;
};

static void probe_completed (GncEntryLedger *, gboolean, gpointer data)
{
    static_cast<RequestProbe *> (data)->callbacks++;
}

static void probe_destroyed (gpointer data)
{
    static_cast<RequestProbe *> (data)->destroys++;
}

TEST_F (EntryLedgerCloseTest, DestroyLedgerWhileSaveDecisionPending)
{
    auto *cursor = ledger->table->current_cursor;
    ASSERT_NE (cursor, nullptr);
    BasicCell *cell = gnc_cellblock_get_cell_by_name (cursor, ENTRY_DESC_CELL, nullptr, nullptr);
    ASSERT_NE (cell, nullptr);
    gnc_basic_cell_set_value (cell, "Pending edit");
    gnc_basic_cell_set_changed (cell, TRUE);
    ASSERT_TRUE (gnc_entry_ledger_changed (ledger));

    RequestProbe probe;
    gnc_entry_ledger_check_close_async_full (parent, ledger, probe_completed,
                                             &probe, probe_destroyed);
    ASSERT_EQ (probe.callbacks, 0);
    ASSERT_EQ (probe.destroys, 0);
    GtkWindow *decision = nullptr;
    auto *windows = gtk_window_get_toplevels ();
    for (guint i = 0; i < g_list_model_get_n_items (windows); ++i)
    {
        auto *candidate = GTK_WINDOW (g_list_model_get_item (windows, i));
        if (gtk_window_get_transient_for (candidate) == GTK_WINDOW (parent))
        {
            ASSERT_EQ (decision, nullptr);
            decision = candidate;
        }
        else
            g_object_unref (candidate);
    }
    ASSERT_NE (decision, nullptr);
    gnc_entry_ledger_destroy (ledger);
    ledger = nullptr;
    /* Complete the still-owned alert explicitly. Destroying its parent does
     * not guarantee cancellation while another reference keeps it alive. */
    gtk_window_close (decision);
    g_object_unref (decision);
    const auto deadline = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
    while (!probe.destroys && g_get_monotonic_time () < deadline)
    {
        while (g_main_context_iteration (nullptr, FALSE)) ;
        g_usleep (1000);
    }
    EXPECT_EQ (probe.callbacks, 0);
    EXPECT_EQ (probe.destroys, 1);
    result = 1;
}
}

int
main (int argc, char **argv)
{
    g_setenv ("GNC_UNINSTALLED", "YES", TRUE);
    g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);
    ::testing::InitGoogleTest (&argc, argv);
    if (!gtk_init_check ())
        g_error ("GTK display is required for the entry-ledger response tests");
    scm_init_guile ();
    qof_init ();
    if (!cashobjects_register ())
        g_error ("Failed to register cash objects for entry-ledger tests");
    gnc_gsettings_load_backend ();
    gnc_component_manager_init ();
    gnucash_register_add_cell_types ();
    gnc::test::initialize_logging ();
    auto result = RUN_ALL_TESTS ();
    gnc_gsettings_shutdown ();
    gnc_component_manager_shutdown ();
    gnc_clear_current_session ();
    qof_close ();
    return result;
}
