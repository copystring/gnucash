/*
 * test-assistant-hierarchy-layout.c -- GTK4 hierarchy assistant layout tests
 * Copyright (C) 2026 GnuCash Developers
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <config.h>

#include <gtk/gtk.h>

#include "dialog-utils.h"

typedef struct
{
    GtkBuilder *builder;
    GtkWindow *window;
    GtkPaned *paned;
    GtkStack *stack;
} HierarchyAssistant;

static void
drain_main_context (void)
{
    while (g_main_context_pending (NULL))
        g_main_context_iteration (NULL, FALSE);
}

static gboolean
wait_for_paned_width (HierarchyAssistant *assistant, int minimum_width)
{
    gint64 deadline = g_get_monotonic_time () + G_TIME_SPAN_SECOND;

    do
    {
        drain_main_context ();
        if (gtk_widget_get_mapped (GTK_WIDGET (assistant->window)) &&
            gtk_widget_get_mapped (GTK_WIDGET (assistant->paned)) &&
            gtk_widget_get_width (GTK_WIDGET (assistant->paned)) >= minimum_width)
            return TRUE;
        g_usleep (1000);
    }
    while (g_get_monotonic_time () < deadline);

    return FALSE;
}

static GtkWidget *
find_widget_of_type (GtkWidget *widget, GType type)
{
    GtkWidget *child;

    if (G_TYPE_CHECK_INSTANCE_TYPE (widget, type))
        return widget;

    for (child = gtk_widget_get_first_child (widget); child != NULL;
         child = gtk_widget_get_next_sibling (child))
    {
        GtkWidget *result = find_widget_of_type (child, type);
        if (result != NULL)
            return result;
    }

    return NULL;
}

static gboolean
contains_widget (GtkWidget *widget, GtkWidget *needle)
{
    GtkWidget *child;

    if (widget == needle)
        return TRUE;

    for (child = gtk_widget_get_first_child (widget); child != NULL;
         child = gtk_widget_get_next_sibling (child))
        if (contains_widget (child, needle))
            return TRUE;

    return FALSE;
}

static HierarchyAssistant
load_hierarchy_assistant (void)
{
    HierarchyAssistant assistant = { 0 };

    assistant.builder = gtk_builder_new ();
    g_assert_true (gnc_builder_add_from_file (assistant.builder,
                                               "assistant-hierarchy.glade",
                                               "hierarchy_assistant"));

    assistant.window = GTK_WINDOW (gtk_builder_get_object (assistant.builder,
                                                            "hierarchy_assistant"));
    assistant.stack = GTK_STACK (gtk_builder_get_object (assistant.builder,
                                                          "hierarchy_stack"));
    g_assert_nonnull (assistant.window);
    g_assert_nonnull (assistant.stack);
    assistant.paned = GTK_PANED (find_widget_of_type (GTK_WIDGET (assistant.stack),
                                                       GTK_TYPE_PANED));
    g_assert_nonnull (assistant.paned);
    return assistant;
}

static void
free_hierarchy_assistant (HierarchyAssistant *assistant)
{
    gtk_window_destroy (assistant->window);
    g_clear_object (&assistant->builder);
}

static GtkWidget *
find_hierarchy_page (GtkStack *stack, GtkWidget *paned)
{
    GtkSelectionModel *pages = gtk_stack_get_pages (stack);

    for (guint index = 0;
         index < g_list_model_get_n_items (G_LIST_MODEL (pages)); index++)
    {
        GtkStackPage *page = g_list_model_get_item (G_LIST_MODEL (pages), index);
        GtkWidget *child = gtk_stack_page_get_child (page);
        gboolean found = contains_widget (child, paned);

        g_object_unref (page);
        if (found)
        {
            g_object_unref (pages);
            return child;
        }
    }

    g_object_unref (pages);
    return NULL;
}

static GtkWidget *
find_other_page (GtkStack *stack, GtkWidget *current)
{
    GtkSelectionModel *pages = gtk_stack_get_pages (stack);

    for (guint index = 0;
         index < g_list_model_get_n_items (G_LIST_MODEL (pages)); index++)
    {
        GtkStackPage *page = g_list_model_get_item (G_LIST_MODEL (pages), index);
        GtkWidget *child = gtk_stack_page_get_child (page);

        g_object_unref (page);
        if (child != current)
        {
            g_object_unref (pages);
            return child;
        }
    }

    g_object_unref (pages);
    return NULL;
}

static void
get_paned_position_bounds (GtkPaned *paned, int *min_position, int *max_position)
{
    g_object_get (paned,
                  "min-position", min_position,
                  "max-position", max_position,
                  NULL);
}

static void
assert_paned_position_is_valid (GtkPaned *paned)
{
    int min_position = 0;
    int max_position = 0;
    int position = gtk_paned_get_position (paned);

    get_paned_position_bounds (paned, &min_position, &max_position);
    g_assert_cmpint (position, >=, min_position);
    g_assert_cmpint (position, <=, max_position);
}

static int
present_hierarchy_page (HierarchyAssistant *assistant, int width, int height)
{
    GtkWidget *hierarchy_page = find_hierarchy_page (assistant->stack,
                                                      GTK_WIDGET (assistant->paned));

    g_assert_nonnull (hierarchy_page);
    gtk_stack_set_visible_child (assistant->stack, hierarchy_page);
    gtk_window_set_default_size (assistant->window, width, height);
    gtk_window_present (assistant->window);
    g_assert_true (wait_for_paned_width (assistant, 1));

    g_assert_true (gtk_widget_get_mapped (GTK_WIDGET (assistant->window)));
    g_assert_true (gtk_widget_get_mapped (hierarchy_page));
    g_assert_true (gtk_widget_get_mapped (GTK_WIDGET (assistant->paned)));
    return gtk_widget_get_width (GTK_WIDGET (assistant->paned));
}

static void
test_hierarchy_paned_builder_contract (void)
{
    HierarchyAssistant assistant = load_hierarchy_assistant ();
    GtkWidget *start = gtk_paned_get_start_child (assistant.paned);
    GtkWidget *end = gtk_paned_get_end_child (assistant.paned);
    gboolean position_set = FALSE;
    int width_request = 0;
    int height_request = 0;
    int minimum = 0;
    int natural = 0;
    int small_allocation;
    int large_allocation;
    int large_width;

    g_assert_nonnull (start);
    g_assert_nonnull (end);
    g_object_get (assistant.paned, "position-set", &position_set, NULL);
    g_assert_true (position_set);
    g_assert_cmpint (gtk_paned_get_position (assistant.paned), ==, 300);
    g_assert_true (gtk_paned_get_resize_start_child (assistant.paned));
    g_assert_true (gtk_paned_get_resize_end_child (assistant.paned));
    g_assert_false (gtk_paned_get_shrink_start_child (assistant.paned));
    g_assert_false (gtk_paned_get_shrink_end_child (assistant.paned));

    g_object_get (start,
                  "width-request", &width_request,
                  "height-request", &height_request,
                  NULL);
    g_assert_cmpint (width_request, ==, -1);
    g_assert_cmpint (height_request, ==, -1);

    gtk_widget_measure (GTK_WIDGET (assistant.paned), GTK_ORIENTATION_HORIZONTAL,
                        -1, &minimum, &natural, NULL, NULL);
    g_assert_cmpint (minimum, >=, 0);
    g_assert_cmpint (natural, >=, minimum);

    /* The 400px default must respect the real minimum, not an artificial width. */
    small_allocation = present_hierarchy_page (&assistant, 400, 550);
    g_assert_cmpint (small_allocation, >=, minimum);
    assert_paned_position_is_valid (assistant.paned);

    large_width = MAX (natural + 200, 800);
    gtk_window_set_default_size (assistant.window, large_width, 550);
    gtk_window_present (assistant.window);
    g_assert_true (wait_for_paned_width (&assistant, small_allocation + 1));
    large_allocation = gtk_widget_get_width (GTK_WIDGET (assistant.paned));
    g_assert_cmpint (large_allocation, >=, minimum);
    g_assert_cmpint (large_allocation, >=, small_allocation);
    assert_paned_position_is_valid (assistant.paned);
    free_hierarchy_assistant (&assistant);
}

static void
test_hierarchy_paned_survives_page_change (void)
{
    HierarchyAssistant assistant = load_hierarchy_assistant ();
    GtkWidget *hierarchy_page = find_hierarchy_page (assistant.stack,
                                                      GTK_WIDGET (assistant.paned));
    GtkWidget *other_page;
    int minimum = 0;
    int natural = 0;
    int min_position = 0;
    int max_position = 0;
    int small_allocation;
    int target_position;
    int position_before;
    int initial_width;

    g_assert_nonnull (hierarchy_page);
    other_page = find_other_page (assistant.stack, hierarchy_page);
    g_assert_nonnull (other_page);
    gtk_widget_measure (GTK_WIDGET (assistant.paned), GTK_ORIENTATION_HORIZONTAL,
                        -1, &minimum, &natural, NULL, NULL);
    g_assert_cmpint (natural, >=, minimum);
    /* Leave room to move the divider even when translated text is wider. */
    initial_width = MAX (natural + 200, 800);
    small_allocation = present_hierarchy_page (&assistant, initial_width, 550);
    assert_paned_position_is_valid (assistant.paned);

    get_paned_position_bounds (assistant.paned, &min_position, &max_position);
    g_assert_cmpint (max_position, >, min_position);
    target_position = gtk_paned_get_position (assistant.paned) == min_position
        ? max_position : min_position;
    gtk_paned_set_position (assistant.paned, target_position);
    drain_main_context ();
    g_assert_cmpint (gtk_paned_get_position (assistant.paned), ==, target_position);
    assert_paned_position_is_valid (assistant.paned);

    gtk_window_set_default_size (assistant.window, initial_width + 300, 550);
    gtk_window_present (assistant.window);
    g_assert_true (wait_for_paned_width (&assistant, small_allocation + 1));
    assert_paned_position_is_valid (assistant.paned);
    position_before = gtk_paned_get_position (assistant.paned);

    gtk_stack_set_visible_child (assistant.stack, other_page);
    drain_main_context ();
    g_assert_true (gtk_stack_get_visible_child (assistant.stack) == other_page);
    gtk_stack_set_visible_child (assistant.stack, hierarchy_page);
    g_assert_true (wait_for_paned_width (&assistant, 1));
    g_assert_true (gtk_stack_get_visible_child (assistant.stack) == hierarchy_page);
    g_assert_true (gtk_widget_get_mapped (hierarchy_page));
    assert_paned_position_is_valid (assistant.paned);
    g_assert_cmpint (gtk_paned_get_position (assistant.paned), ==, position_before);

    free_hierarchy_assistant (&assistant);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    gtk_init ();

    g_test_add_func ("/gtkbuilder/assistant-hierarchy/paned-builder-contract",
                     test_hierarchy_paned_builder_contract);
    g_test_add_func ("/gtkbuilder/assistant-hierarchy/paned-page-change",
                     test_hierarchy_paned_survives_page_change);
    return g_test_run ();
}
