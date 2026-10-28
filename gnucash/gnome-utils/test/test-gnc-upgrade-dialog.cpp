/* Copyright (C) 2026 GnuCash contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <config.h>
#include <gtk/gtk.h>
#include <gtest/gtest.h>
#include "googletest-glib-log-handler.hpp"
#include "gnc-engine.h"

/* Only linked when the sandbox test hooks are enabled. */
extern "C" void gnc_test_file_show_upgrade_error (GtkWindow *,
                                      void (*)(GtkWindow *, gboolean, gpointer),
                                      gpointer);

typedef struct { guint calls; gboolean cancelled; bool detached; } Result;

static void
completed (GtkWindow *parent, gboolean cancelled, gpointer data)
{
    Result *result = static_cast<Result *> (data);
    (void)parent;
    ++result->calls;
    result->cancelled = cancelled;
    if (result->calls == 1 && result->detached) delete result;
}

static GtkWindow *
find_child (GtkWindow *parent)
{
    GListModel *windows = gtk_window_get_toplevels ();
    for (guint i = 0; i < g_list_model_get_n_items (windows); ++i)
    {
        GtkWindow *window = GTK_WINDOW (g_list_model_get_item (windows, i));
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

class UpgradeDialogTest : public ::testing::Test
{
protected:
    GtkWindow *parent = nullptr;
    GtkWindow *dialog = nullptr;
    Result *result = new Result {0, FALSE, false};
    void SetUp () override
    {
        parent = GTK_WINDOW (gtk_window_new ());
        g_object_ref_sink (parent);
        gtk_window_present (parent);
        gnc_test_file_show_upgrade_error (parent, completed, result);
        dialog = find_child (parent);
        ASSERT_NE (dialog, nullptr);
    }
    void TearDown () override
    {
        if (dialog) { gtk_window_close (dialog); g_object_unref (dialog); }
        if (parent) { gtk_window_destroy (parent); g_object_unref (parent); }
        for (guint i = 0; i < 1000 && !result->calls; ++i)
            g_main_context_iteration (NULL, FALSE);
        if (result->calls) delete result;
        else { ADD_FAILURE () << "Upgrade callback did not finish during cleanup"; result->detached = true; }
    }
    void check (bool accept)
    {
        if (accept)
        {
            GtkWidget *button = find_choice (GTK_WIDGET (dialog), 2);
            ASSERT_NE (button, nullptr);
            g_signal_emit_by_name (button, "clicked");
        }
        else gtk_window_close (dialog);
        for (guint i = 0; i < 1000 && !result->calls; ++i)
            g_main_context_iteration (NULL, FALSE);
        EXPECT_EQ (result->calls, 1u);
        EXPECT_EQ (result->cancelled, !accept);
        for (guint i = 0; i < 20; ++i) g_main_context_iteration (NULL, FALSE);
        EXPECT_EQ (result->calls, 1u);
    }
};
TEST_F (UpgradeDialogTest, CloseCancels) { check (false); }
TEST_F (UpgradeDialogTest, OkContinues) { check (true); }

int main (int argc, char **argv)
{
    gnc::test::initialize_logging ();
    gtk_init ();
    gnc_engine_init (argc, argv);
    ::testing::InitGoogleTest (&argc, argv);
    return RUN_ALL_TESTS ();
}
