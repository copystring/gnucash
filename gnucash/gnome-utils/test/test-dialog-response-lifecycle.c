/*
 * test-dialog-response-lifecycle.c -- Asynchronous dialog completion tests
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <config.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "dialog-utils.h"
#include "gnc-prefs.h"
#include "gnc-prefs-utils.h"

typedef struct
{
    guint calls;
    gint response;
} WarningResult;

typedef struct
{
    guint calls;
    guint destroyed_windows;
    guint denied_closes;
} CloseResult;

static void
drain_main_context (void)
{
    while (g_main_context_pending (NULL))
        g_main_context_iteration (NULL, FALSE);
}

static void
warning_completed (gint response, gpointer user_data)
{
    WarningResult *result = user_data;

    result->calls++;
    result->response = response;
}

static void
close_completed (GtkWindow *window, gboolean close_allowed, gpointer user_data)
{
    CloseResult *result = user_data;

    result->calls++;
    result->destroyed_windows += window == NULL;
    result->denied_closes += !close_allowed;
}

static GtkWindow *
find_transient_window (GtkWindow *parent)
{
    GListModel *windows = gtk_window_get_toplevels ();

    for (guint index = 0; index < g_list_model_get_n_items (windows); index++)
    {
        GtkWindow *window = g_list_model_get_item (windows, index);

        if (gtk_window_get_transient_for (window) == parent)
            return window;
        g_object_unref (window);
    }
    return NULL;
}

static void
collect_check_buttons (GtkWidget *widget, GPtrArray *buttons)
{
    if (GTK_IS_CHECK_BUTTON (widget))
        g_ptr_array_add (buttons, widget);

    for (GtkWidget *child = gtk_widget_get_first_child (widget); child;
         child = gtk_widget_get_next_sibling (child))
        collect_check_buttons (child, buttons);
}

static GtkButton *
find_button_with_label (GtkWidget *widget, const gchar *label)
{
    if (GTK_IS_BUTTON (widget) &&
        g_strcmp0 (gtk_button_get_label (GTK_BUTTON (widget)), label) == 0)
        return GTK_BUTTON (widget);

    for (GtkWidget *child = gtk_widget_get_first_child (widget); child;
         child = gtk_widget_get_next_sibling (child))
    {
        GtkButton *button = find_button_with_label (child, label);
        if (button)
            return button;
    }
    return NULL;
}

static void
test_stored_warning_response_is_deferred (void)
{
    const gchar *key = "closing-window-question";
    WarningResult result = { 0 };

    g_assert_true (gnc_prefs_set_int (GNC_PREFS_GROUP_WARNINGS_TEMP, key,
                                      GTK_RESPONSE_YES));
    gnc_warning_dialog_async (NULL, key, "Saved decision", "Use decision?",
                              "Yes", GTK_RESPONSE_YES, TRUE,
                              warning_completed, &result);

    /* The caller must finish setting up its state before a saved reply runs. */
    g_assert_cmpuint (result.calls, ==, 0);
    drain_main_context ();
    g_assert_cmpuint (result.calls, ==, 1);
    g_assert_cmpint (result.response, ==, GTK_RESPONSE_YES);
    g_assert_true (gnc_prefs_set_int (GNC_PREFS_GROUP_WARNINGS_TEMP, key, 0));
}

static void
test_warning_cancels_when_parent_is_destroyed (void)
{
    const gchar *key = "checkprinting-multi-acct";
    WarningResult result = { 0 };
    GtkWindow *parent = GTK_WINDOW (g_object_ref_sink (gtk_window_new ()));
    GtkWindow *warning;
    GPtrArray *buttons = g_ptr_array_new ();

    g_assert_true (gnc_prefs_set_int (GNC_PREFS_GROUP_WARNINGS_TEMP, key, 0));
    g_assert_true (gnc_prefs_set_int (GNC_PREFS_GROUP_WARNINGS_PERM, key, 0));
    gnc_warning_dialog_async (parent, key, "Pending decision", "Proceed?",
                              "Yes", GTK_RESPONSE_YES, TRUE,
                              warning_completed, &result);
    g_assert_cmpuint (result.calls, ==, 0);

    warning = find_transient_window (parent);
    g_assert_nonnull (warning);
    collect_check_buttons (GTK_WIDGET (warning), buttons);
    g_assert_cmpuint (buttons->len, ==, 2);
    g_assert_true (gtk_widget_get_sensitive (g_ptr_array_index (buttons, 1)));
    gtk_check_button_set_active (g_ptr_array_index (buttons, 0), TRUE);
    g_assert_false (gtk_widget_get_sensitive (g_ptr_array_index (buttons, 1)));
    gtk_check_button_set_active (g_ptr_array_index (buttons, 0), FALSE);
    g_assert_true (gtk_widget_get_sensitive (g_ptr_array_index (buttons, 1)));
    g_ptr_array_unref (buttons);
    g_object_unref (warning);

    /* The destroy signal runs after both GTK and the test release the window. */
    gtk_window_destroy (parent);
    g_object_unref (parent);
    g_assert_cmpuint (result.calls, ==, 1);
    g_assert_cmpint (result.response, ==, GTK_RESPONSE_CANCEL);
    drain_main_context ();
    g_assert_cmpuint (result.calls, ==, 1);
}

static void
test_duplicate_close_requests_finish_once_each (void)
{
    CloseResult result = { 0 };
    GtkWindow *window = GTK_WINDOW (g_object_ref_sink (gtk_window_new ()));

    gnc_ok_to_close_window_async (window, close_completed, &result);
    gnc_ok_to_close_window_async (window, close_completed, &result);
    g_assert_cmpuint (result.calls, ==, 0);

    gtk_window_destroy (window);
    g_object_unref (window);
    g_assert_cmpuint (result.calls, ==, 2);
    g_assert_cmpuint (result.destroyed_windows, ==, 2);
    g_assert_cmpuint (result.denied_closes, ==, 2);
    drain_main_context ();
    g_assert_cmpuint (result.calls, ==, 2);
}

static void
test_duplicate_close_requests_accept_once_each (void)
{
    CloseResult result = { 0 };
    GtkWindow *window = GTK_WINDOW (g_object_ref_sink (gtk_window_new ()));
    GtkWindow *dialog;
    GtkButton *yes_button;

    gnc_ok_to_close_window_async (window, close_completed, &result);
    gnc_ok_to_close_window_async (window, close_completed, &result);
    dialog = find_transient_window (window);
    g_assert_nonnull (dialog);
    yes_button = find_button_with_label (GTK_WIDGET (dialog), _("Yes"));
    g_assert_nonnull (yes_button);
    g_signal_emit_by_name (yes_button, "clicked");
    g_assert_cmpuint (result.calls, ==, 2);
    g_assert_cmpuint (result.destroyed_windows, ==, 0);
    g_assert_cmpuint (result.denied_closes, ==, 0);
    drain_main_context ();
    g_assert_cmpuint (result.calls, ==, 2);

    g_object_unref (dialog);
    gtk_window_destroy (window);
    g_object_unref (window);
}

int
main (int argc, char **argv)
{
    gint status;

    g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);
    g_test_init (&argc, &argv, NULL);
    gtk_init ();
    gnc_prefs_init ();

    g_test_add_func ("/gnome-utils/dialog-response/stored-warning",
                     test_stored_warning_response_is_deferred);
    g_test_add_func ("/gnome-utils/dialog-response/parent-destroyed",
                     test_warning_cancels_when_parent_is_destroyed);
    g_test_add_func ("/gnome-utils/dialog-response/duplicate-close",
                     test_duplicate_close_requests_finish_once_each);
    g_test_add_func ("/gnome-utils/dialog-response/duplicate-accept",
                     test_duplicate_close_requests_accept_once_each);
    status = g_test_run ();

    gnc_prefs_remove_registered ();
    return status;
}
