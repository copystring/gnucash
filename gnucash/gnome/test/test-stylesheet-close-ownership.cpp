/* Copyright (C) 2026 GnuCash contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <config.h>
#include <gtk/gtk.h>
#include <libguile.h>
#include <cstring>
#include <gtest/gtest.h>
#include "dialog-report-style-sheet.h"
#include "gnc-optiondb.h"
#include "gnc-report.h"
#include "gnc-engine.h"

static GtkWidget *find_widget(GtkWidget *widget, const char *id)
{
    if (GTK_IS_BUILDABLE(widget) && g_strcmp0(gtk_buildable_get_buildable_id(GTK_BUILDABLE(widget)), id) == 0)
        return widget;
    for (auto child = gtk_widget_get_first_child(widget); child; child = gtk_widget_get_next_sibling(child))
        if (auto found = find_widget(child, id)) return found;
    return nullptr;
}

static GtkWindow *find_window(GtkWindow *parent, bool child)
{
    auto windows = gtk_window_get_toplevels();
    for (guint i = 0; i < g_list_model_get_n_items(windows); ++i)
    {
        auto window = GTK_WINDOW(g_list_model_get_item(windows, i));
        auto match = child ? gtk_window_get_transient_for(window) == parent :
            g_strcmp0(gtk_widget_get_name(GTK_WIDGET(window)), "gnc-id-style-sheet-select") == 0;
        if (match) return window;
        g_object_unref(window);
    }
    return nullptr;
}

static void finalized(gpointer data, GObject *) { *static_cast<bool *>(data) = true; }
class StylesheetOwnershipTest : public ::testing::Test {};
TEST_F(StylesheetOwnershipTest, CloseDoesNotDestroyOwnedOptions)
{
    gnc_style_sheet_dialog_open(nullptr);
    auto parent = find_window(nullptr, false);
    g_assert_nonnull(parent);
    auto view = GTK_COLUMN_VIEW(find_widget(GTK_WIDGET(parent), "style_sheet_list_view"));
    auto edit = find_widget(GTK_WIDGET(parent), "edit_button");
    g_assert_nonnull(view); g_assert_nonnull(edit);
    auto model = gtk_column_view_get_model(view);
    g_assert_nonnull(model);
    g_assert_cmpuint(g_list_model_get_n_items(G_LIST_MODEL(model)), >, 0);
    g_assert_true(gtk_selection_model_select_item(model, 0, TRUE));
    auto sheets = scm_c_eval_string("(gnc:get-html-style-sheets)");
    g_assert_true(scm_is_pair(sheets));
    auto sheet = scm_car(sheets);
    scm_gc_protect_object(sheet);
    auto options = scm_call_1(scm_c_eval_string("gnc:html-style-sheet-options"), sheet);
    auto odb = gnc_get_optiondb_from_dispatcher(options);
    g_assert_nonnull(odb);
    for (int i = 0; i < 3; ++i)
    {
        g_signal_emit_by_name(edit, "clicked");
        auto window = find_window(parent, true);
        g_assert_nonnull(window);
        bool freed = false;
        g_object_weak_ref(G_OBJECT(window), finalized, &freed);
        gtk_window_close(window);
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_object_weak_unref(G_OBJECT(window), finalized, &freed);
        g_object_unref(window);
        scm_gc();
        options = scm_call_1(scm_c_eval_string("gnc:html-style-sheet-options"), sheet);
        g_assert_true(gnc_get_optiondb_from_dispatcher(options) == odb);
    }
    scm_gc_unprotect_object(sheet);
    gtk_window_destroy(parent);
    g_object_unref(parent);
}
int main(int argc, char **argv)
{
    gtk_init();
    scm_init_guile();
    qof_init(); gnc_engine_init(0, nullptr);
    scm_c_use_module("gnucash reports");
    scm_c_use_module("gnucash report html-style-sheet");
    scm_c_use_module("gnucash report stylesheets plain");
    scm_c_use_module("gnucash report report-core");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
