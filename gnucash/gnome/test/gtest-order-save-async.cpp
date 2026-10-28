#include <config.h>
#include <gtk/gtk.h>
#include <libguile.h>
#include <gtest/gtest.h>
#include "googletest-glib-log-handler.hpp"
#include "cashobjects.h"
#include "gncOrder.h"
#include "gncEntry.h"
extern "C" {
#include "gncEntryLedger.h"
#include "gncEntryLedgerP.h"
#include "cellblock.h"
#include "gnc-component-manager.h"
GObject *gnc_test_order_save_begin (GtkWidget *, GncEntryLedger *, QofBook *, GncOrder *);
void gnc_test_order_save_owner_free (void);
}
#include "basiccell.h"
#include "gnc-gsettings.h"
#include "gnc-session.h"
#include "gnucash-register.h"
#include "qof.h"

namespace {
struct Probe { int finalized{}; };
void finalized (gpointer p, GObject *) { static_cast<Probe *>(p)->finalized++; }
TEST (OrderSaveAsync, DestroyWindowAndLedgerBeforeDelayedAnswer)
{
    auto *book = qof_book_new ();
    ASSERT_NE (book, nullptr);
    auto *session = qof_session_new (book);
    ASSERT_NE (session, nullptr);
    gnc_set_current_session (session);
    auto *order = gncOrderCreate (book);
    ASSERT_NE (order, nullptr);
    auto *ledger = gnc_entry_ledger_new (book, GNCENTRY_ORDER_ENTRY);
    ASSERT_NE (ledger, nullptr);
    auto *window = gtk_window_new ();
    g_object_ref_sink (window);
    gtk_widget_realize (window);
    gnc_entry_ledger_set_parent (ledger, window);
    gnc_entry_ledger_set_default_order (ledger, order);
    /* Order ledgers do not create an invoice-style blank row: load an actual
     * entry and position the cursor there before editing. */
    auto *entry = gncEntryCreate (book);
    ASSERT_NE (entry, nullptr);
    gncOrderAddEntry (order, entry);
    GList *entries = g_list_append (nullptr, entry);
    gnc_entry_ledger_load (ledger, entries);
    g_list_free (entries);
    VirtualCellLocation cell_location;
    ASSERT_TRUE (gnc_entry_ledger_get_entry_virt_loc (ledger, entry, &cell_location));
    VirtualLocation location = ledger->table->current_cursor_loc;
    location.vcell_loc = cell_location;
    gnc_table_move_cursor_gui (ledger->table, location);
    ASSERT_EQ (gnc_entry_ledger_get_current_entry (ledger), entry);
    auto *cell = gnc_cellblock_get_cell_by_name (ledger->table->current_cursor, ENTRY_DESC_CELL, nullptr, nullptr);
    ASSERT_NE (cell, nullptr);
    gnc_basic_cell_set_value (cell, "pending order edit");
    gnc_basic_cell_set_changed (cell, TRUE);
    ASSERT_TRUE (gnc_entry_ledger_changed (ledger));
    auto *request = gnc_test_order_save_begin (window, ledger, book, order);
    ASSERT_NE (request, nullptr);
    /* Keep probe valid even on assertion failure and late completion. */
    auto *probe = new Probe;
    g_object_weak_ref (request, finalized, probe);
    EXPECT_EQ (probe->finalized, 0);
    GtkWindow *decision = nullptr;
    auto *windows = gtk_window_get_toplevels ();
    for (guint i = 0; i < g_list_model_get_n_items (windows); ++i)
    {
        auto *candidate = GTK_WINDOW (g_list_model_get_item (windows, i));
        if (gtk_window_get_transient_for (candidate) == GTK_WINDOW (window))
        {
            ASSERT_EQ (decision, nullptr);
            decision = candidate;
        }
        else g_object_unref (candidate);
    }
    ASSERT_NE (decision, nullptr);
    gnc_entry_ledger_destroy (ledger);
    /* Keep the alert alive, detach it before destroying its parent, then
     * explicitly supply the delayed cancel response. This avoids GTK
     * destroying the same dialog twice when its parent disappears. */
    gtk_window_set_transient_for (decision, nullptr);
    gtk_window_destroy (GTK_WINDOW (window));
    g_object_unref (window);
    EXPECT_EQ (probe->finalized, 0);
    g_signal_emit_by_name (decision, "response", GTK_RESPONSE_CANCEL);
    g_object_unref (decision);
    auto deadline = g_get_monotonic_time () + 5 * G_TIME_SPAN_SECOND;
    while (!probe->finalized && g_get_monotonic_time () < deadline)
    {
        g_main_context_iteration (nullptr, FALSE);
        g_usleep (1000);
    }
    EXPECT_EQ (probe->finalized, 1);
    auto settle = g_get_monotonic_time () + 100 * G_TIME_SPAN_MILLISECOND;
    while (g_get_monotonic_time () < settle)
    {
        g_main_context_iteration (nullptr, FALSE);
        g_usleep (1000);
    }
    EXPECT_EQ (probe->finalized, 1);
    gnc_test_order_save_owner_free ();
    gnc_clear_current_session ();
}
}
int main (int argc, char **argv)
{
    g_setenv ("GNC_UNINSTALLED", "YES", TRUE);
    g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);
    ::testing::InitGoogleTest (&argc, argv);
    if (!gtk_init_check ()) g_error ("GTK display required");
    scm_init_guile ();
    qof_init ();
    if (!cashobjects_register ()) g_error ("cashobjects registration failed");
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
