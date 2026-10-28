/* Copyright (C) 2026 GnuCash contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <config.h>
#include <gtk/gtk.h>
#include "gnc-engine.h"

/* Only linked when the sandbox test hooks are enabled. */
void gnc_test_file_show_upgrade_error (GtkWindow *,
                                      void (*)(GtkWindow *, gboolean, gpointer),
                                      gpointer);

typedef struct { guint calls; gboolean cancelled; } Result;

static void
completed (GtkWindow *parent, gboolean cancelled, gpointer data)
{
    Result *result = data;
    g_assert_nonnull (parent);
    ++result->calls;
    result->cancelled = cancelled;
}

static GtkWindow *
find_child (GtkWindow *parent)
{
    GListModel *windows = gtk_window_get_toplevels ();
    for (guint i = 0; i < g_list_model_get_n_items (windows); ++i)
    {
        GtkWindow *window = g_list_model_get_item (windows, i);
        if (window != parent && gtk_window_get_transient_for (window) == parent)
            return window; /* caller owns the reference */
        g_object_unref (window);
    }
    return NULL;
}

static GtkWidget *
find_choice (GtkWidget *widget, gint index)
{
    if (GTK_IS_BUTTON (widget) &&
        GPOINTER_TO_INT (g_object_get_data (G_OBJECT (widget), "gnc-choice")) == index)
        return widget;
    for (GtkWidget *child = gtk_widget_get_first_child (widget); child;
         child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = find_choice (child, index);
        if (found) return found;
    }
    return NULL;
}

static void
run_response (gconstpointer data)
{
    gboolean accept = GPOINTER_TO_INT (data);
    GtkWindow *parent = GTK_WINDOW (gtk_window_new ());
    Result result = {0, FALSE};
    GtkWindow *dialog;
    g_object_ref_sink (parent);
    gtk_window_present (parent);
    gnc_test_file_show_upgrade_error (parent, completed, &result);
    dialog = find_child (parent);
    g_assert_nonnull (dialog);
    if (accept)
    {
        GtkWidget *button = find_choice (GTK_WIDGET (dialog), 2);
        g_assert_nonnull (button);
        g_signal_emit_by_name (button, "clicked");
    }
    else
        gtk_window_close (dialog);
    for (guint i = 0; i < 1000 && !result.calls; ++i)
        g_main_context_iteration (NULL, FALSE);
    g_assert_cmpuint (result.calls, ==, 1);
    g_assert_cmpint (result.cancelled, ==, !accept);
    for (guint i = 0; i < 20; ++i)
        g_main_context_iteration (NULL, FALSE);
    g_assert_cmpuint (result.calls, ==, 1);
    g_object_unref (dialog);
    gtk_window_destroy (parent);
    g_object_unref (parent);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    gtk_init ();
    gnc_engine_init (argc, argv);
    g_test_add_data_func ("/upgrade/close-cancels", GINT_TO_POINTER (FALSE), run_response);
    g_test_add_data_func ("/upgrade/ok-continues", GINT_TO_POINTER (TRUE), run_response);
    return g_test_run ();
}
