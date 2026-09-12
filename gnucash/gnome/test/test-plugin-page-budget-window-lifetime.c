/*
 * test-plugin-page-budget-window-lifetime.c -- budget page window signal lifetime
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <config.h>

#include <gtk/gtk.h>

#include "gnc-budget.h"
#include "gnc-budget-view.h"
#include "gnc-component-manager.h"
#include "gnc-engine.h"
#include "gnc-plugin-page-budget.h"
#include "gnc-plugin-page.h"
#include "gnc-prefs-utils.h"
#include "gnc-session.h"
#include "qof.h"

typedef struct
{
    guint count;
} DefaultWidthNotification;

static gboolean
weak_ref_is_finalized (GWeakRef *weak_ref)
{
    GObject *object = g_weak_ref_get (weak_ref);

    if (!object)
        return TRUE;

    g_object_unref (object);
    return FALSE;
}

static void
default_width_changed (GtkWindow *window, GParamSpec *pspec,
                       DefaultWidthNotification *notification)
{
    notification->count++;
    (void)window;
    (void)pspec;
}

static gboolean
window_has_budget_resize_handler (GtkWindow *window)
{
    return g_signal_handler_find (window, G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
                                  G_CALLBACK (gnc_budget_view_resized_cb), NULL) != 0;
}

static void
set_default_size_and_assert_notification (GtkWindow *window,
                                          DefaultWidthNotification *notification,
                                          gint width, gint height)
{
    guint old_count = notification->count;
    gint actual_width;
    gint actual_height;

    gtk_window_set_default_size (window, width, height);
    gtk_window_get_default_size (window, &actual_width, &actual_height);

    g_assert_cmpint (actual_width, ==, width);
    g_assert_cmpint (actual_height, ==, height);
    g_assert_cmpuint (notification->count, ==, old_count + 1);
}

static void
test_budget_page_window_handler_ends_with_view (void)
{
    QofSession *session = qof_session_new (qof_book_new ());
    QofBook *book = qof_session_get_book (session);
    GncBudget *budget;
    GncPluginPage *page;
    GtkWindow *window;
    GtkWidget *budget_view;
    GWeakRef weak_view;
    DefaultWidthNotification notification = { 0 };
    gulong notify_id;

    gnc_set_current_session (session);
    gnc_account_create_root (book);
    budget = gnc_budget_new (book);
    gnc_budget_set_num_periods (budget, 2);

    page = gnc_plugin_page_budget_new (budget);
    window = GTK_WINDOW (gtk_window_new ());
    g_object_ref (window);
    page->window = GTK_WIDGET (window);
    budget_view = gnc_plugin_page_create_widget (page);
    g_assert_nonnull (budget_view);
    g_weak_ref_init (&weak_view, budget_view);
    gtk_window_set_child (window, budget_view);

    notify_id = g_signal_connect (window, "notify::default-width",
                                  G_CALLBACK (default_width_changed),
                                  &notification);
    g_assert_true (window_has_budget_resize_handler (window));
    set_default_size_and_assert_notification (window, &notification, 701, 503);

    /* Match the window close order: detach the child before page teardown. */
    gtk_window_set_child (window, NULL);
    gnc_plugin_page_destroy_widget (page);
    g_assert_true (weak_ref_is_finalized (&weak_view));
    g_assert_false (window_has_budget_resize_handler (window));

    /* The still-live window must not retain the former budget view callback. */
    set_default_size_and_assert_notification (window, &notification, 743, 521);

    g_signal_handler_disconnect (window, notify_id);
    page->window = NULL;
    gtk_window_destroy (window);
    g_object_unref (window);
    g_weak_ref_clear (&weak_view);
    g_object_unref (page);
    gnc_budget_destroy (budget);
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
    g_test_add_func ("/gnome/plugin-page-budget/window-handler-ends-with-view",
                     test_budget_page_window_handler_ends_with_view);
    status = g_test_run ();
    gnc_component_manager_shutdown ();
    gnc_prefs_remove_registered ();
    gnc_engine_shutdown ();
    return status;
}
