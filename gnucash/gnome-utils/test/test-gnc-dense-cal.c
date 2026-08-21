/*
 * test-gnc-dense-cal.c -- GtkDrawingArea resize regression tests
 * Copyright (C) 2026 GnuCash Developers
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <config.h>

#include <gtk/gtk.h>

#include "gnc-dense-cal.h"

static GtkWidget *
find_widget (GtkWidget *widget, GType type)
{
    if (G_TYPE_CHECK_INSTANCE_TYPE (widget, type))
        return widget;

    for (GtkWidget *child = gtk_widget_get_first_child (widget); child;
         child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *found = find_widget (child, type);

        if (found)
            return found;
    }

    return NULL;
}

static gboolean
quit_main_loop (gpointer user_data)
{
    g_main_loop_quit (user_data);
    return G_SOURCE_REMOVE;
}

static void
settle_layout (void)
{
    GMainLoop *loop = g_main_loop_new (NULL, FALSE);

    g_timeout_add (10, quit_main_loop, loop);
    g_main_loop_run (loop);
    g_main_loop_unref (loop);
}

static void
test_resize_rebuilds_calendar_scene (void)
{
    GtkWindow *window = GTK_WINDOW (gtk_window_new ());
    GtkWidget *calendar = gnc_dense_cal_new (window);
    GtkWidget *background;

    gtk_window_set_default_size (window, 640, 360);
    gtk_window_set_child (window, calendar);
    gtk_window_present (window);
    settle_layout ();

    background = find_widget (calendar, GTK_TYPE_FIXED);
    g_assert_nonnull (background);
    g_assert_nonnull (gtk_widget_get_first_child (background));

    gtk_window_set_default_size (window, 900, 520);
    settle_layout ();
    g_assert_nonnull (gtk_widget_get_first_child (background));

    gtk_window_destroy (window);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_log_set_always_fatal (G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL);
    gtk_init ();

    g_test_add_func ("/gnome-utils/dense-cal/resize-rebuilds-scene",
                     test_resize_rebuilds_calendar_scene);

    return g_test_run ();
}
