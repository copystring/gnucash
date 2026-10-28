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
#include "gnc-ui.h"
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
GtkWindow *find_decision (GtkWindow *parent)
{
    auto *windows = gtk_window_get_toplevels ();
    for (guint i = 0; i < g_list_model_get_n_items (windows); i++)
    {
        auto *candidate = GTK_WINDOW (g_list_model_get_item (windows, i));
        if (gtk_window_get_transient_for (candidate) == parent)
            return candidate;
        g_object_unref (candidate);
    }
    return nullptr;
}
GtkWidget *find_button (GtkWidget *widget, const char *label)
{
    if (GTK_IS_BUTTON (widget) &&
        g_strcmp0 (gtk_button_get_label (GTK_BUTTON (widget)), label) == 0)
        return widget;
    for (auto *child = gtk_widget_get_first_child (widget); child;
         child = gtk_widget_get_next_sibling (child))
        if (auto *button = find_button (child, label))
            return button;
    return nullptr;
}
void drain ()
{
    while (g_main_context_iteration (nullptr, FALSE))
        ;
}
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
    auto *decision = find_decision (GTK_WINDOW (window));
    ASSERT_NE (decision, nullptr);
    ASSERT_TRUE (gtk_window_get_destroy_with_parent (decision));
    auto *button = find_button (GTK_WIDGET (decision), "Yes");
    ASSERT_NE (button, nullptr);
    g_object_ref (button);
    gnc_entry_ledger_destroy (ledger);
    /* Native GTK parent destruction: no detachment or synthetic response. */
    gtk_window_destroy (GTK_WINDOW (window));
    g_object_unref (window);
    g_signal_emit_by_name (button, "clicked");
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
    g_signal_emit_by_name (button, "clicked");
    drain ();
    EXPECT_EQ (probe->finalized, 1);
    g_object_unref (button);
    g_object_unref (decision);
    delete probe;
    gnc_test_order_save_owner_free ();
    gnc_clear_current_session ();
}

struct ChoiceProbe
{
    int calls{};
    int response{99};
    bool reentrant{};
    bool initiating{true};
    GtkWindow *parent{};
};
void choice_finished (GtkWindow *parent, gint response, gpointer data)
{
    auto *probe = static_cast<ChoiceProbe *>(data);
    probe->calls++;
    probe->response = response;
    probe->reentrant = probe->initiating;
    probe->parent = parent;
}
class ChoiceLifetime : public ::testing::TestWithParam<int> {};
TEST_P (ChoiceLifetime, CompletesExactlyOnce)
{
    auto *parent = GTK_WINDOW (gtk_window_new ());
    g_object_ref_sink (parent);
    gtk_widget_realize (GTK_WIDGET (parent));
    ChoiceProbe probe;
    GList *buttons = nullptr;
    buttons = g_list_append (buttons, const_cast<char *> ("_Yes"));
    buttons = g_list_append (buttons, const_cast<char *> ("_No"));
    buttons = g_list_append (buttons, const_cast<char *> ("_Cancel"));
    gnc_choose_option_dialog_async (parent, "Decision", "Lifetime regression",
                                    buttons, 0, choice_finished, &probe);
    g_list_free (buttons);
    EXPECT_EQ (probe.calls, 0);
    auto *decision = find_decision (parent);
    ASSERT_NE (decision, nullptr);
    EXPECT_TRUE (gtk_window_get_destroy_with_parent (decision));
    auto *button = find_button (GTK_WIDGET (decision), "_Yes");
    ASSERT_NE (button, nullptr);
    g_object_ref (button);
    switch (GetParam ())
    {
    case 0: /* Native parent destruction, then delayed retained button. */
        gtk_window_destroy (parent);
        break;
    case 1: /* Real action button. */
        g_signal_emit_by_name (button, "clicked");
        break;
    case 2: /* Native WM close. */
        gtk_window_close (decision);
        break;
    case 3: /* Direct dialog destroy, parent still alive. */
        gtk_window_destroy (decision);
        break;
    case 4: /* Answer queued, parent destroyed before continuation. */
        g_signal_emit_by_name (button, "clicked");
        gtk_window_destroy (parent);
        break;
    case 5: /* Escape, including a delayed duplicate on retained controller. */
    {
        auto *controllers = gtk_widget_observe_controllers (GTK_WIDGET (decision));
        bool found = false;
        for (guint i = 0; i < g_list_model_get_n_items (controllers); i++)
        {
            auto *controller = g_list_model_get_item (controllers, i);
            if (GTK_IS_EVENT_CONTROLLER_KEY (controller))
            {
                gboolean handled = FALSE;
                g_signal_emit_by_name (controller, "key-pressed", GDK_KEY_Escape,
                                       0, static_cast<GdkModifierType> (0), &handled);
                EXPECT_TRUE (handled);
                handled = FALSE;
                g_signal_emit_by_name (controller, "key-pressed", GDK_KEY_Escape,
                                       0, static_cast<GdkModifierType> (0), &handled);
                found = true;
            }
            g_object_unref (controller);
        }
        g_object_unref (controllers);
        EXPECT_TRUE (found);
        break;
    }
    }
    probe.initiating = false;
    drain ();
    EXPECT_EQ (probe.calls, 1);
    EXPECT_FALSE (probe.reentrant);
    EXPECT_EQ (probe.response, GetParam () == 1 ? 0 : -1);
    EXPECT_EQ (probe.parent, GetParam () == 0 || GetParam () == 4 ? nullptr : parent);
    g_signal_emit_by_name (button, "clicked");
    gtk_window_destroy (parent);
    drain ();
    EXPECT_EQ (probe.calls, 1);
    g_object_unref (button);
    g_object_unref (decision);
    g_object_unref (parent);
}
INSTANTIATE_TEST_SUITE_P (NativeLifecycle, ChoiceLifetime,
                         ::testing::Range (0, 6));
TEST (VerifyLifetime, ParentDestroyAndLateButton)
{
    auto *parent = GTK_WINDOW (gtk_window_new ());
    g_object_ref_sink (parent);
    gtk_widget_realize (GTK_WIDGET (parent));
    ChoiceProbe probe;
    gnc_verify_dialog_async (parent, TRUE, choice_finished, &probe, "%s", "Verify");
    auto *decision = find_decision (parent);
    ASSERT_NE (decision, nullptr);
    auto *button = find_button (GTK_WIDGET (decision), "Yes");
    ASSERT_NE (button, nullptr);
    g_object_ref (button);
    gtk_window_destroy (parent);
    probe.initiating = false;
    drain ();
    EXPECT_EQ (probe.calls, 1);
    EXPECT_EQ (probe.response, GTK_RESPONSE_NO);
    EXPECT_EQ (probe.parent, nullptr);
    g_signal_emit_by_name (button, "clicked");
    drain ();
    EXPECT_EQ (probe.calls, 1);
    g_object_unref (button);
    g_object_unref (decision);
    g_object_unref (parent);
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
