/* Copyright (C) 2026 GnuCash contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <config.h>
#include <gtk/gtk.h>
#include <libguile.h>
#include <cstring>
#include <gtest/gtest.h>
#include "googletest-glib-log-handler.hpp"
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

class StylesheetOwnershipTest : public ::testing::Test
{
protected:
    GtkWindow *parent = nullptr;
    GtkWindow *child = nullptr;
    bool child_finalized = false;
    SCM sheet = SCM_UNDEFINED;
    bool protected_sheet = false;
    static void finalized(gpointer data, GObject *) { *static_cast<bool *>(data) = true; }
    void SetUp() override
    {
        gnc_style_sheet_dialog_open(nullptr);
        parent = find_window(nullptr, false);
        ASSERT_NE(parent, nullptr);
    }
    void TearDown() override
    {
        if (child)
        {
            g_object_weak_unref(G_OBJECT(child), finalized, &child_finalized);
            gtk_window_destroy(child);
            g_object_unref(child);
        }
        if (protected_sheet) scm_gc_unprotect_object(sheet);
        if (parent) { gtk_window_destroy(parent); g_object_unref(parent); }
        while (g_main_context_iteration(nullptr, FALSE)) {}
    }
};
TEST_F(StylesheetOwnershipTest, CloseDoesNotDestroyOwnedOptions)
{
    auto view = GTK_COLUMN_VIEW(find_widget(GTK_WIDGET(parent), "style_sheet_list_view"));
    auto edit = find_widget(GTK_WIDGET(parent), "edit_button");
    ASSERT_NE(view, nullptr);
    ASSERT_NE(edit, nullptr);
    auto model = gtk_column_view_get_model(view);
    ASSERT_NE(model, nullptr);
    ASSERT_GT(g_list_model_get_n_items(G_LIST_MODEL(model)), 0u);
    ASSERT_TRUE(gtk_selection_model_select_item(model, 0, TRUE));
    auto sheets = scm_c_eval_string("(gnc:get-html-style-sheets)");
    ASSERT_TRUE(scm_is_pair(sheets));
    sheet = scm_car(sheets);
    scm_gc_protect_object(sheet);
    protected_sheet = true;
    auto options = scm_call_1(scm_c_eval_string("gnc:html-style-sheet-options"), sheet);
    auto odb = gnc_get_optiondb_from_dispatcher(options);
    ASSERT_NE(odb, nullptr);
    for (int i = 0; i < 3; ++i)
    {
        g_signal_emit_by_name(edit, "clicked");
        child = find_window(parent, true);
        ASSERT_NE(child, nullptr);
        child_finalized = false;
        g_object_weak_ref(G_OBJECT(child), finalized, &child_finalized);
        gtk_window_close(child);
        GtkWindow *observed = child;
        g_object_unref(child);
        child = nullptr;
        for (int turn = 0; turn < 1000 && !child_finalized; ++turn)
            g_main_context_iteration(nullptr, FALSE);
        if (!child_finalized)
            g_object_weak_unref(G_OBJECT(observed), finalized, &child_finalized);
        ASSERT_TRUE(child_finalized);
        scm_gc();
        options = scm_call_1(scm_c_eval_string("gnc:html-style-sheet-options"), sheet);
        EXPECT_EQ(gnc_get_optiondb_from_dispatcher(options), odb);
    }
}

int main(int argc, char **argv)
{
    gnc::test::initialize_logging ();
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
