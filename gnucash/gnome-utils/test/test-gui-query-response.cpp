/* Copyright (C) 2026 GnuCash contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <config.h>
#include <gtk/gtk.h>
#include "gnc-ui.h"

static gboolean display_available;

struct Completion
{
    guint count = 0;
    gint response = GTK_RESPONSE_NONE;
    GtkWindow *parent = nullptr;
};

static void
completed (GtkWindow *parent, gint response, gpointer user_data)
{
    auto result = static_cast<Completion *> (user_data);
    ++result->count;
    result->response = response;
    result->parent = parent;
}

static GtkWidget *
find_query (GtkWindow *parent)
{
    auto windows = gtk_window_list_toplevels ();
    GtkWidget *dialog = nullptr;
    for (auto node = windows; node; node = node->next)
        if (GTK_IS_MESSAGE_DIALOG (node->data) &&
            gtk_window_get_transient_for (GTK_WINDOW (node->data)) == parent)
        {
            g_assert_null (dialog);
            dialog = GTK_WIDGET (node->data);
        }
    g_list_free (windows);
    return dialog;
}

static void
test_query (gconstpointer data)
{
    if (!display_available)
    {
        g_test_skip ("No graphical display is available");
        return;
    }
    auto scenario = GPOINTER_TO_INT (data);
    auto family = scenario / 6;
    auto action = scenario % 6;
    auto parent = GTK_WINDOW (gtk_window_new (GTK_WINDOW_TOPLEVEL));
    Completion result;
    gint accept = GTK_RESPONSE_OK;
    gint cancel = GTK_RESPONSE_CANCEL;
    if (family == 0)
        gnc_ok_cancel_dialog_async (parent, GTK_RESPONSE_CANCEL, completed,
                                    &result, "query %d", scenario);
    else if (family == 1)
    {
        accept = GTK_RESPONSE_YES;
        cancel = GTK_RESPONSE_NO;
        gnc_verify_dialog_async (parent, FALSE, completed, &result,
                                 "query %d", scenario);
    }
    else
    {
        accept = GTK_RESPONSE_ACCEPT;
        gnc_action_dialog_async (parent, "Action", FALSE, completed,
                                 &result, "query %d", scenario);
    }
    g_assert_cmpuint (result.count, ==, 0);
    auto dialog = find_query (parent);
    g_assert_nonnull (dialog);
    g_assert_true (gtk_window_get_modal (GTK_WINDOW (dialog)));
    g_assert_true (gtk_window_get_destroy_with_parent (GTK_WINDOW (dialog)));

    /* Retain only the object, not its callback state: a late response after
     * completion must not call the consumer or touch the freed request. */
    g_object_ref (dialog);
    switch (action)
    {
    case 0: gtk_dialog_response (GTK_DIALOG (dialog), accept); break;
    case 1: gtk_dialog_response (GTK_DIALOG (dialog), cancel); break;
    case 2: gtk_window_close (GTK_WINDOW (dialog)); break;
    case 3: gtk_widget_destroy (GTK_WIDGET (parent)); break;
    case 4: gtk_widget_destroy (dialog); break;
    case 5: gtk_dialog_response (GTK_DIALOG (dialog), 12345); break;
    }
    for (guint attempt = 0; result.count == 0 && attempt < 1000; ++attempt)
    {
        while (g_main_context_iteration (nullptr, FALSE))
            ;
        if (result.count == 0)
            g_usleep (1000);
    }
    g_assert_cmpuint (result.count, ==, 1);
    g_assert_cmpint (result.response, ==, action == 0 ? accept : cancel);
    if (action == 3)
        g_assert_null (result.parent);
    else
        g_assert_true (result.parent == parent);
    gtk_dialog_response (GTK_DIALOG (dialog), accept);
    g_assert_cmpuint (result.count, ==, 1);
    g_object_unref (dialog);
    if (action != 3)
        gtk_widget_destroy (GTK_WIDGET (parent));
}

static void
notice_destroyed (GtkWidget *, gpointer user_data)
{
    ++*static_cast<guint *> (user_data);
}

static void
test_error_notice (gconstpointer data)
{
    if (!display_available)
    {
        g_test_skip ("No graphical display is available");
        return;
    }
    auto action = GPOINTER_TO_INT (data);
    auto parent = GTK_WINDOW (gtk_window_new (GTK_WINDOW_TOPLEVEL));
    auto detail = g_strdup ("borrowed detail");
    gnc_error_dialog_async (parent, "Error %d: %s", 42, detail);
    g_free (detail);
    auto dialog = find_query (parent);
    g_assert_nonnull (dialog);
    g_assert_true (gtk_window_get_modal (GTK_WINDOW (dialog)));
    g_assert_true (gtk_window_get_destroy_with_parent (GTK_WINDOW (dialog)));
    gchar *message = nullptr;
    gint type = GTK_MESSAGE_OTHER;
    g_object_get (dialog, "text", &message, "message-type", &type, nullptr);
    g_assert_cmpstr (message, ==, "Error 42: borrowed detail");
    g_assert_cmpint (type, ==, GTK_MESSAGE_ERROR);
    g_free (message);

    guint count = 0;
    g_signal_connect (dialog, "destroy", G_CALLBACK (notice_destroyed), &count);
    g_object_ref (dialog);
    GtkWidget *weak_dialog = dialog;
    g_object_add_weak_pointer (G_OBJECT (dialog),
                               reinterpret_cast<gpointer *> (&weak_dialog));
    switch (action)
    {
    case 0: gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE); break;
    case 1: gtk_window_close (GTK_WINDOW (dialog)); break;
    case 2: gtk_widget_destroy (GTK_WIDGET (parent)); break;
    case 3: gtk_widget_destroy (dialog); break;
    }
    for (guint attempt = 0; count == 0 && attempt < 1000; ++attempt)
    {
        while (g_main_context_iteration (nullptr, FALSE))
            ;
        if (count == 0)
            g_usleep (1000);
    }
    g_assert_cmpuint (count, ==, 1);
    gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);
    g_assert_cmpuint (count, ==, 1);
    g_object_unref (dialog);
    g_assert_null (weak_dialog);
    if (action != 2)
        gtk_widget_destroy (GTK_WIDGET (parent));
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, nullptr);
    display_available = gtk_init_check (&argc, &argv);
    if (g_getenv ("GNC_REQUIRE_DISPLAY"))
        g_assert_true (display_available);
    const char *families[] = {"ok-cancel", "verify", "action"};
    const char *actions[] = {"accept", "cancel", "window-close", "parent-destroy",
                             "dialog-destroy", "invalid-response"};
    for (guint family = 0; family < G_N_ELEMENTS (families); ++family)
        for (guint action = 0; action < G_N_ELEMENTS (actions); ++action)
        {
            auto path = g_strdup_printf ("/gnome-utils/query/%s/%s",
                                         families[family], actions[action]);
            g_test_add_data_func (path, GINT_TO_POINTER (family * 6 + action), test_query);
            g_free (path);
        }
    const char *notice_actions[] = {"close", "window-close", "parent-destroy",
                                    "dialog-destroy"};
    for (guint action = 0; action < G_N_ELEMENTS (notice_actions); ++action)
    {
        auto path = g_strdup_printf ("/gnome-utils/query/error-notice/%s",
                                     notice_actions[action]);
        g_test_add_data_func (path, GINT_TO_POINTER (action), test_error_notice);
        g_free (path);
    }
    return g_test_run ();
}
