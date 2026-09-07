/*
 * test-gnc-date-edit-calendar.c -- GtkCalendar signal regression tests
 *
 * Copyright (C) 2026 GnuCash Developers
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <config.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#include "gnc-date-edit.h"

static GtkEventController *
find_controller (GtkWidget *widget, const char *name)
{
    GListModel *controllers = gtk_widget_observe_controllers (widget);
    GtkEventController *found = NULL;

    for (guint index = 0;
         index < g_list_model_get_n_items (controllers) && !found;
         index++)
    {
        GtkEventController *controller =
            g_list_model_get_item (controllers, index);

        if (g_strcmp0 (gtk_event_controller_get_name (controller), name) == 0)
            found = controller;
        else
            g_object_unref (controller);
    }

    g_object_unref (controllers);
    return found;
}

static void
test_calendar_selection_and_keys (void)
{
    GNCDateEdit *date_edit = GNC_DATE_EDIT
        (gnc_date_edit_new (0, FALSE, FALSE));
    GtkEventController *key_controller;
    GDate selected_date;
    gboolean handled = FALSE;

    g_object_ref_sink (date_edit);

    /* GtkCalendar has no GTK4 "activate" signal. Construction must not
     * attempt to connect it, and day-selected remains the update path. */
    g_assert_cmpuint (g_signal_lookup ("activate", GTK_TYPE_CALENDAR), ==, 0);

    gtk_calendar_set_day (GTK_CALENDAR (date_edit->calendar), 7);
    gtk_calendar_set_year (GTK_CALENDAR (date_edit->calendar), 2026);
    gtk_calendar_set_month (GTK_CALENDAR (date_edit->calendar), 8);
    g_signal_emit_by_name (date_edit->calendar, "day-selected");

    gnc_date_edit_get_gdate (date_edit, &selected_date);
    g_assert_cmpuint (g_date_get_day (&selected_date), ==, 7);
    g_assert_cmpuint (g_date_get_month (&selected_date), ==, 9);
    g_assert_cmpuint (g_date_get_year (&selected_date), ==, 2026);

    key_controller = find_controller (date_edit->cal_popup,
                                      "gnc-date-edit-popup-key");
    g_assert_nonnull (key_controller);

    g_signal_emit_by_name (key_controller, "key-pressed", GDK_KEY_Return,
                           0, (GdkModifierType)0, &handled);
    g_assert_true (handled);

    handled = FALSE;
    g_signal_emit_by_name (key_controller, "key-pressed", GDK_KEY_Escape,
                           0, (GdkModifierType)0, &handled);
    g_assert_true (handled);

    g_object_unref (key_controller);
    g_object_unref (date_edit);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    gtk_init ();

    g_test_add_func ("/gnome-utils/date-edit/calendar-selection-and-keys",
                     test_calendar_selection_and_keys);

    return g_test_run ();
}
