/*
 * test-dialog-response-lifecycle.c -- Asynchronous dialog completion tests
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <config.h>

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

    g_assert_true (gnc_prefs_set_int (GNC_PREFS_GROUP_WARNINGS_TEMP, key, 0));
    g_assert_true (gnc_prefs_set_int (GNC_PREFS_GROUP_WARNINGS_PERM, key, 0));
    gnc_warning_dialog_async (parent, key, "Pending decision", "Proceed?",
                              "Yes", GTK_RESPONSE_YES, TRUE,
                              warning_completed, &result);
    g_assert_cmpuint (result.calls, ==, 0);

    gtk_window_destroy (parent);
    g_assert_cmpuint (result.calls, ==, 1);
    g_assert_cmpint (result.response, ==, GTK_RESPONSE_CANCEL);
    drain_main_context ();
    g_assert_cmpuint (result.calls, ==, 1);
    g_object_unref (parent);
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
    g_assert_cmpuint (result.calls, ==, 2);
    g_assert_cmpuint (result.destroyed_windows, ==, 2);
    g_assert_cmpuint (result.denied_closes, ==, 2);
    drain_main_context ();
    g_assert_cmpuint (result.calls, ==, 2);
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
    status = g_test_run ();

    gnc_prefs_remove_registered ();
    return status;
}
