/*
 * test-dialog-transfer-model-lifecycle.c -- transfer account model ownership
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <config.h>

#include <gtk/gtk.h>

#include "Account.h"
#include "dialog-transfer.h"
#include "gnc-commodity.h"
#include "gnc-component-manager.h"
#include "gnc-engine.h"
#include "gnc-prefs-utils.h"
#include "gnc-session.h"
#include "qof.h"

static void
object_finalized (gpointer data, GObject *where_the_object_was)
{
    gboolean *finalized = data;

    *finalized = TRUE;
    (void)where_the_object_was;
}

static void
collect_account_views (GtkWidget *widget, GPtrArray *views)
{
    GtkWidget *child;

    if (GTK_IS_COLUMN_VIEW (widget))
        g_ptr_array_add (views, widget);
    for (child = gtk_widget_get_first_child (widget); child;
         child = gtk_widget_get_next_sibling (child))
        collect_account_views (child, views);
}

static GtkWindow *
find_transfer_window (void)
{
    GListModel *toplevels = gtk_window_get_toplevels ();

    for (guint index = 0; index < g_list_model_get_n_items (toplevels); index++)
    {
        GtkWindow *window = GTK_WINDOW (g_list_model_get_item (toplevels, index));
        GPtrArray *views = g_ptr_array_new ();

        collect_account_views (GTK_WIDGET (window), views);
        if (views->len == 2)
        {
            g_ptr_array_unref (views);
            return window;
        }
        g_ptr_array_unref (views);
        g_object_unref (window);
    }
    return NULL;
}

static void
hold_transfer_selections (GtkWindow *window, GtkSelectionModel **first,
                          GtkSelectionModel **second)
{
    GPtrArray *views = g_ptr_array_new ();

    collect_account_views (GTK_WIDGET (window), views);
    g_assert_cmpuint (views->len, ==, 2);
    *first = gtk_column_view_get_model (GTK_COLUMN_VIEW (g_ptr_array_index (views, 0)));
    *second = gtk_column_view_get_model (GTK_COLUMN_VIEW (g_ptr_array_index (views, 1)));
    g_assert_nonnull (*first);
    g_assert_nonnull (*second);
    *first = g_object_ref (*first);
    *second = g_object_ref (*second);
    g_ptr_array_unref (views);
}

static gulong
find_dialog_handler (GtkSelectionModel *selection, XferDialog *dialog)
{
    return g_signal_handler_find (selection, G_SIGNAL_MATCH_DATA, 0, 0,
                                  NULL, NULL, dialog);
}

static void
change_held_selection (GtkSelectionModel *selection)
{
    GListModel *model = gtk_single_selection_get_model (GTK_SINGLE_SELECTION (selection));

    g_assert_cmpuint (g_list_model_get_n_items (model), >, 0);
    gtk_single_selection_set_can_unselect (GTK_SINGLE_SELECTION (selection), TRUE);
    gtk_single_selection_set_selected (GTK_SINGLE_SELECTION (selection),
                                       GTK_INVALID_LIST_POSITION);
    gtk_single_selection_set_selected (GTK_SINGLE_SELECTION (selection), 0);
}

static void
test_transfer_account_models_rebuild_and_close (void)
{
    QofSession *session = qof_session_new (qof_book_new ());
    QofBook *book = qof_session_get_book (session);
    gnc_commodity_table *table;
    gnc_commodity *commodity;
    Account *root;
    Account *account;
    XferDialog *dialog;
    GtkWindow *window;
    GtkSelectionModel *first_selection;
    GtkSelectionModel *second_selection;
    GtkSelectionModel *old_from_selection;
    GtkSelectionModel *old_to_selection;
    GtkSelectionModel *current_first_selection;
    GtkSelectionModel *current_second_selection;
    gboolean window_finalized = FALSE;
    gboolean old_from_finalized = FALSE;
    gboolean old_to_finalized = FALSE;
    gboolean current_first_finalized = FALSE;
    gboolean current_second_finalized = FALSE;

    gnc_set_current_session (session);
    gnc_account_create_root (book);
    table = gnc_commodity_table_get_table (book);
    commodity = gnc_commodity_table_lookup (table, GNC_COMMODITY_NS_CURRENCY, "USD");
    g_assert_nonnull (commodity);
    root = gnc_book_get_root_account (book);
    account = xaccMallocAccount (book);
    xaccAccountBeginEdit (account);
    xaccAccountSetName (account, "Transfer model account");
    xaccAccountSetType (account, ACCT_TYPE_BANK);
    xaccAccountSetCommodity (account, commodity);
    gnc_account_append_child (root, account);
    xaccAccountCommitEdit (account);

    dialog = gnc_xfer_dialog (NULL, account);
    g_assert_nonnull (dialog);
    window = find_transfer_window ();
    g_assert_nonnull (window);
    g_object_weak_ref (G_OBJECT (window), object_finalized, &window_finalized);
    hold_transfer_selections (window, &first_selection, &second_selection);
    g_object_unref (window);

    /* The From rebuild disconnects exactly the replaced selection. */
    gnc_xfer_dialog_set_from_show_button_active (dialog, TRUE);
    if (find_dialog_handler (first_selection, dialog) == 0)
    {
        old_from_selection = first_selection;
        old_to_selection = second_selection;
    }
    else
    {
        old_from_selection = second_selection;
        old_to_selection = first_selection;
    }
    g_assert_cmpuint (find_dialog_handler (old_from_selection, dialog), ==, 0);
    g_assert_cmpuint (find_dialog_handler (old_to_selection, dialog), !=, 0);

    /* Also replace To and then From again, leaving both current selections
     * held for the close-path lifetime assertion below. */
    gnc_xfer_dialog_set_to_show_button_active (dialog, TRUE);
    g_assert_cmpuint (find_dialog_handler (old_to_selection, dialog), ==, 0);
    gnc_xfer_dialog_set_from_show_button_active (dialog, FALSE);
    window = find_transfer_window ();
    g_assert_nonnull (window);
    hold_transfer_selections (window, &current_first_selection,
                              &current_second_selection);
    g_object_unref (window);
    g_object_weak_ref (G_OBJECT (old_from_selection), object_finalized,
                       &old_from_finalized);
    g_object_weak_ref (G_OBJECT (old_to_selection), object_finalized,
                       &old_to_finalized);
    g_object_weak_ref (G_OBJECT (current_first_selection), object_finalized,
                       &current_first_finalized);
    g_object_weak_ref (G_OBJECT (current_second_selection), object_finalized,
                       &current_second_finalized);

    gnc_xfer_dialog_close (dialog);

    g_assert_true (window_finalized);
    g_assert_cmpuint (find_dialog_handler (old_from_selection, dialog), ==, 0);
    g_assert_cmpuint (find_dialog_handler (old_to_selection, dialog), ==, 0);
    g_assert_cmpuint (find_dialog_handler (current_first_selection, dialog), ==, 0);
    g_assert_cmpuint (find_dialog_handler (current_second_selection, dialog), ==, 0);
    change_held_selection (old_from_selection);
    change_held_selection (old_to_selection);
    change_held_selection (current_first_selection);
    change_held_selection (current_second_selection);
    g_assert_false (old_from_finalized);
    g_assert_false (old_to_finalized);
    g_assert_false (current_first_finalized);
    g_assert_false (current_second_finalized);
    g_object_unref (old_from_selection);
    g_object_unref (old_to_selection);
    g_object_unref (current_first_selection);
    g_object_unref (current_second_selection);
    g_assert_true (old_from_finalized);
    g_assert_true (old_to_finalized);
    g_assert_true (current_first_finalized);
    g_assert_true (current_second_finalized);

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
    gnc_prefs_init ();
    gnc_component_manager_init ();

    g_test_add_func ("/gnome-utils/dialog-transfer/account-model-lifecycle",
                     test_transfer_account_models_rebuild_and_close);
    status = g_test_run ();

    gnc_component_manager_shutdown ();
    gnc_prefs_remove_registered ();
    gnc_engine_shutdown ();
    return status;
}
