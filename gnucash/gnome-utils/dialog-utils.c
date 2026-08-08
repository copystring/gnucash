/********************************************************************\
 * dialog-utils.c -- utility functions for creating dialogs         *
 *                   for GnuCash                                    *
 * Copyright (C) 1999-2000 Linas Vepstas                            *
 * Copyright (C) 2005 David Hampton <hampton@employees.org>         *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
 *                                                                  *
\********************************************************************/

#include <config.h>

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <gmodule.h>
#ifdef HAVE_DLFCN_H
# include <dlfcn.h>
#endif

#include "dialog-utils.h"
#include "gnc-commodity.h"
#include "gnc-date.h"
#include "gnc-path.h"
#include "gnc-engine.h"
#include "gnc-euro.h"
#include "gnc-ui.h"
#include "gnc-ui-util.h"
#include "gnc-prefs.h"
#include "gnc-main-window.h"

/* This static indicates the debugging module that this .o belongs to. */
static QofLogModule log_module = GNC_MOD_GUI;

#define GNC_PREF_LAST_GEOMETRY "last-geometry"

/********************************************************************\
 * gnc_set_label_color                                              *
 *   sets the color of the label given the value                    *
 *                                                                  *
 * Args: label - gtk label widget                                   *
 *       value - value to use to set color                          *
 * Returns: none                                                    *
 \*******************************************************************/
void
gnc_set_label_color(GtkWidget *label, gnc_numeric value)
{
    gboolean deficit;

    if (!gnc_prefs_get_bool(GNC_PREFS_GROUP_GENERAL, GNC_PREF_NEGATIVE_IN_RED))
        return;

    deficit = gnc_numeric_negative_p (value);

    if (deficit)
    {
        gnc_widget_style_context_remove_class (GTK_WIDGET(label), "gnc-class-default-color");
        gnc_widget_style_context_add_class (GTK_WIDGET(label), "gnc-class-negative-numbers");
    }
    else
    {
        gnc_widget_style_context_remove_class (GTK_WIDGET(label), "gnc-class-negative-numbers");
        gnc_widget_style_context_add_class (GTK_WIDGET(label), "gnc-class-default-color");
    }
}


/********************************************************************\
 * gnc_restore_window_size                                          *
 *   restores the position and size of the given window, if these   *
 *   these parameters have been saved earlier. Does nothing if no   *
 *   saved values are found.                                        *
 *                                                                  *
 * Args: group - the preferences group to look in for saved coords  *
 *       window - the window for which the coords are to be         *
 *                restored                                          *
 *       parent - the parent window that can be used to position    *
 *                 window when it still has default entries         *
 * Returns: nothing                                                 *
 \*******************************************************************/
void
gnc_restore_window_size(const char *group, GtkWindow *window, GtkWindow *parent)
{
    gint wpos[2], wsize[2];
    GVariant *geometry;

    ENTER("");

    g_return_if_fail(group != NULL);
    g_return_if_fail(window != NULL);
    g_return_if_fail(parent != NULL);

    if (!gnc_prefs_get_bool(GNC_PREFS_GROUP_GENERAL, GNC_PREF_SAVE_GEOMETRY))
        return;

    geometry = gnc_prefs_get_value (group, GNC_PREF_LAST_GEOMETRY);

    if (g_variant_is_of_type (geometry, (const GVariantType *) "(iiii)") )
    {
        GdkWindow *win = gtk_widget_get_window (GTK_WIDGET(parent));
        GdkRectangle monitor_size;
        GdkDisplay *display = gdk_window_get_display (win);
        GdkMonitor *mon;

        g_variant_get (geometry, "(iiii)",
                       &wpos[0],  &wpos[1],
                       &wsize[0], &wsize[1]);

        mon = gdk_display_get_monitor_at_point (display, wpos[0], wpos[1]);
        gdk_monitor_get_geometry (mon, &monitor_size);

        DEBUG("monitor left top corner x: %d, y: %d, width: %d, height: %d",
              monitor_size.x, monitor_size.y, monitor_size.width, monitor_size.height);
        DEBUG("geometry from preferences - group, %s, wpos[0]: %d, wpos[1]: %d, wsize[0]: %d, wsize[1]: %d",
              group, wpos[0],  wpos[1], wsize[0], wsize[1]);

        /* (-1, -1) means no geometry was saved (default preferences value) */
        if ((wpos[0] != -1) && (wpos[1] != -1))
        {
            /* Keep the window on screen if possible */
            if (wpos[0] - monitor_size.x + wsize[0] > monitor_size.x + monitor_size.width)
                wpos[0] = monitor_size.x + monitor_size.width - wsize[0];

            if (wpos[1] - monitor_size.y + wsize[1] > monitor_size.y + monitor_size.height)
                wpos[1] = monitor_size.y + monitor_size.height - wsize[1];

            /* make sure the coordinates have not left the monitor */
            if (wpos[0] < monitor_size.x)
                wpos[0] = monitor_size.x;

            if (wpos[1] < monitor_size.y)
                wpos[1] = monitor_size.y;

            DEBUG("geometry after screen adaption - wpos[0]: %d, wpos[1]: %d, wsize[0]: %d, wsize[1]: %d",
                  wpos[0],  wpos[1], wsize[0], wsize[1]);

            gtk_window_move(window, wpos[0], wpos[1]);
        }
        else
        {
            /* preference is at default value -1,-1,-1,-1 */
            if (parent != NULL)
            {
                gint parent_wpos[2], parent_wsize[2], window_wsize[2];
                gtk_window_get_position (GTK_WINDOW(parent), &parent_wpos[0], &parent_wpos[1]);
                gtk_window_get_size (GTK_WINDOW(parent), &parent_wsize[0], &parent_wsize[1]);
                gtk_window_get_size (GTK_WINDOW(window), &window_wsize[0], &window_wsize[1]);

                DEBUG("parent window - wpos[0]: %d, wpos[1]: %d, wsize[0]: %d, wsize[1]: %d - window size is %dx%d",
                      parent_wpos[0],  parent_wpos[1], parent_wsize[0], parent_wsize[1],
                      window_wsize[0], window_wsize[1]);

                /* check for gtk default size, no window size specified, let gtk decide location */
                if ((window_wsize[0] == 200) && (window_wsize[1] == 200))
                    DEBUG("window size not specified, let gtk locate it");
                else
                    gtk_window_move (window, parent_wpos[0] + (parent_wsize[0] - window_wsize[0])/2,
                                             parent_wpos[1] + (parent_wsize[1] - window_wsize[1])/2);
            }
        }

        /* Don't attempt to restore invalid sizes */
        if ((wsize[0] > 0) && (wsize[1] > 0))
        {
            wsize[0] = MIN(wsize[0], monitor_size.width - 10);
            wsize[1] = MIN(wsize[1], monitor_size.height - 10);

            gtk_window_resize(window, wsize[0], wsize[1]);
        }
    }
    g_variant_unref (geometry);
    LEAVE("");
}


/********************************************************************\
 * gnc_save_window_size                                             *
 *   save the window position and size into options whose names are *
 *   prefixed by the group name.                                    *
 *                                                                  *
 * Args: group - preferences group to save the options in           *
 *       window - the window for which current position and size    *
 *                are to be saved                                   *
 * Returns: nothing                                                 *
\********************************************************************/
void
gnc_save_window_size(const char *group, GtkWindow *window)
{
    gint wpos[2], wsize[2];
    GVariant *geometry;

    ENTER("");

    g_return_if_fail(group != NULL);
    g_return_if_fail(window != NULL);

    if (!gnc_prefs_get_bool(GNC_PREFS_GROUP_GENERAL, GNC_PREF_SAVE_GEOMETRY))
        return;

    gtk_window_get_position(GTK_WINDOW(window), &wpos[0], &wpos[1]);
    gtk_window_get_size(GTK_WINDOW(window), &wsize[0], &wsize[1]);

    DEBUG("save geometry - wpos[0]: %d, wpos[1]: %d, wsize[0]: %d, wsize[1]: %d",
                  wpos[0],  wpos[1], wsize[0], wsize[1]);

    geometry = g_variant_new ("(iiii)", wpos[0],  wpos[1],
                              wsize[0], wsize[1]);
    gnc_prefs_set_value (group, GNC_PREF_LAST_GEOMETRY, geometry);
    /* Don't unref geometry here, it is consumed by gnc_prefs_set_value */
    LEAVE("");
}


/********************************************************************\
 * gnc_window_adjust_for_screen                                     *
 *   adjust the window size if it is bigger than the screen size.   *
 *                                                                  *
 * Args: window - the window to adjust                              *
 * Returns: nothing                                                 *
\********************************************************************/
void
gnc_window_adjust_for_screen(GtkWindow * window)
{
    GdkWindow *win;
    GdkDisplay *display;
    GdkMonitor *mon;
    GdkRectangle monitor_size;
    gint wpos[2];
    gint width;
    gint height;

    ENTER("");

    if (window == NULL)
        return;

    g_return_if_fail(GTK_IS_WINDOW(window));
    if (gtk_widget_get_window (GTK_WIDGET(window)) == NULL)
        return;

    win = gtk_widget_get_window (GTK_WIDGET(window));
    display = gdk_window_get_display (win);

    gtk_window_get_position(GTK_WINDOW(window), &wpos[0], &wpos[1]);
    gtk_window_get_size(GTK_WINDOW(window), &width, &height);

    mon = gdk_display_get_monitor_at_point (display, wpos[0], wpos[1]);
    gdk_monitor_get_geometry (mon, &monitor_size);

    DEBUG("monitor width is %d, height is %d; wwindow width is %d, height is %d",
           monitor_size.width, monitor_size.height, width, height);

    if ((width <= monitor_size.width) && (height <= monitor_size.height))
        return;

    /* Keep the window on screen if possible */
    if (wpos[0] - monitor_size.x + width > monitor_size.x + monitor_size.width)
        wpos[0] = monitor_size.x + monitor_size.width - width;

    if (wpos[1] - monitor_size.y + height > monitor_size.y + monitor_size.height)
        wpos[1] = monitor_size.y + monitor_size.height - height;

    /* make sure the coordinates have not left the monitor */
    if (wpos[0] < monitor_size.x)
        wpos[0] = monitor_size.x;

    if (wpos[1] < monitor_size.y)
        wpos[1] = monitor_size.y;

    DEBUG("move window to position %d, %d", wpos[0], wpos[1]);

    gtk_window_move(window, wpos[0], wpos[1]);

    /* if window is bigger, set it to monitor sizes */
    width = MIN(width, monitor_size.width - 10);
    height = MIN(height, monitor_size.height - 10);

    DEBUG("resize window to width %d, height %d", width, height);

    gtk_window_resize(GTK_WINDOW(window), width, height);
    gtk_widget_queue_resize(GTK_WIDGET(window));
    LEAVE("");
}

/********************************************************************\
 * Sets the alignment of a Label Widget, GTK3 version specific.    *
 *                                                                  *
 * Args: widget - the label widget to set alignment on              *
 *       xalign - x alignment                                       *
 *       yalign - y alignment                                       *
 * Returns: nothing                                                 *
\********************************************************************/
void
gnc_label_set_alignment (GtkWidget *widget, gfloat xalign, gfloat yalign)
{
    gtk_label_set_xalign (GTK_LABEL (widget), xalign);
    gtk_label_set_yalign (GTK_LABEL (widget), yalign);
}

/********************************************************************\
 * Get the preference for showing tree view grid lines              *
 *                                                                  *
 * Args: none                                                       *
 * Returns:  GtkTreeViewGridLines setting                           *
\********************************************************************/
GtkTreeViewGridLines
gnc_tree_view_get_grid_lines_pref (void)
{
    GtkTreeViewGridLines grid_lines;
    gboolean h_lines = gnc_prefs_get_bool (GNC_PREFS_GROUP_GENERAL, GNC_PREF_GRID_LINES_HORIZONTAL);
    gboolean v_lines = gnc_prefs_get_bool (GNC_PREFS_GROUP_GENERAL, GNC_PREF_GRID_LINES_VERTICAL);

    if (h_lines)
    {
        if (v_lines)
            grid_lines = GTK_TREE_VIEW_GRID_LINES_BOTH;
        else
            grid_lines =  GTK_TREE_VIEW_GRID_LINES_HORIZONTAL;
    }
    else if (v_lines)
        grid_lines = GTK_TREE_VIEW_GRID_LINES_VERTICAL;
    else
        grid_lines = GTK_TREE_VIEW_GRID_LINES_NONE;
    return grid_lines;
}

/********************************************************************\
 * Add a style context to a Widget so it can be altered with css    *
 *                                                                  *
 * Args:    widget - widget to add css style too                    *
 *       gnc_class - character string for css class name            *
 * Returns:  nothing                                                *
\********************************************************************/
void
gnc_widget_style_context_add_class (GtkWidget *widget, const char *gnc_class)
{
    GtkStyleContext *context = gtk_widget_get_style_context (widget);
    gtk_style_context_add_class (context, gnc_class);
}

/********************************************************************\
 * Remove a style context class from a Widget                       *
 *                                                                  *
 * Args:    widget - widget to remove style class from              *
 *       gnc_class - character string for css class name            *
 * Returns:  nothing                                                *
\********************************************************************/
void
gnc_widget_style_context_remove_class (GtkWidget *widget, const char *gnc_class)
{
    GtkStyleContext *context = gtk_widget_get_style_context (widget);

    if (gtk_style_context_has_class (context, gnc_class))
        gtk_style_context_remove_class (context, gnc_class);
}

/********************************************************************\
 * Draw an arrow on a Widget so it can be altered with css          *
 *                                                                  *
 * Args:     widget - widget to add arrow to in the draw callback   *
 *               cr - cairo context for the draw callback           *
 *        direction - 0 for up, 1 for down                          *
 * Returns:  TRUE, stop other handlers being invoked for the event  *
\********************************************************************/
gboolean
gnc_draw_arrow_cb (GtkWidget *widget, cairo_t *cr, gpointer direction)
{
    GtkStyleContext *context = gtk_widget_get_style_context (widget);
    gint width = gtk_widget_get_allocated_width (widget);
    gint height = gtk_widget_get_allocated_height (widget);
    gint size;

    gtk_render_background (context, cr, 0, 0, width, height);
    gtk_style_context_add_class (context, GTK_STYLE_CLASS_ARROW);

    size = MIN(width / 2, height / 2);

    if (GPOINTER_TO_INT(direction) == 0)
        gtk_render_arrow (context, cr, 0,
                         (width - size)/2, (height - size)/2, size);
    else
        gtk_render_arrow (context, cr, G_PI,
                         (width - size)/2, (height - size)/2, size);

    return TRUE;
}


gboolean
gnc_gdate_in_valid_range (GDate *test_date, gboolean warn)
{
    gboolean use_autoreadonly = qof_book_uses_autoreadonly (gnc_get_current_book());
    GDate *max_date = g_date_new_dmy (1,1,10000);
    GDate *min_date;
    gboolean ret = FALSE;
    gboolean max_date_ok = FALSE;
    gboolean min_date_ok = FALSE;

    if (use_autoreadonly)
        min_date = qof_book_get_autoreadonly_gdate (gnc_get_current_book());
    else
        min_date = g_date_new_dmy (1,1,1400);

    // max date
    if (g_date_compare (max_date, test_date) > 0)
        max_date_ok = TRUE;

    // min date
    if (g_date_compare (min_date, test_date) <= 0)
        min_date_ok = TRUE;

    if (use_autoreadonly && warn)
        ret = max_date_ok;
    else
        ret = min_date_ok & max_date_ok;

    if (warn && !ret)
    {
            // Translators: Use your locale date format here!
        gchar *dialog_msg = _("The entered date is out of the range "
                  "01/01/1400 - 31/12/9999, resetting to this year");
        gchar *dialog_title = _("Date out of range");
        GtkWidget *dialog = gtk_message_dialog_new (gnc_ui_get_main_window (NULL),
                               0,
                               GTK_MESSAGE_ERROR,
                               GTK_BUTTONS_OK,
                               "%s", dialog_title);
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG(dialog),
                             "%s", dialog_msg);
        gtk_dialog_run (GTK_DIALOG(dialog));
        gtk_widget_destroy (dialog);
    }
    g_date_free (max_date);
    g_date_free (min_date);
    return ret;
}


gboolean
gnc_handle_date_accelerator (GdkEventKey *event,
                             struct tm *tm,
                             const char *date_str)
{
    GDate gdate;

    g_return_val_if_fail (event != NULL, FALSE);
    g_return_val_if_fail (tm != NULL, FALSE);
    g_return_val_if_fail (date_str != NULL, FALSE);

    if (event->type != GDK_KEY_PRESS)
        return FALSE;

    if ((tm->tm_mday <= 0) || (tm->tm_mon == -1) || (tm->tm_year == -1))
        return FALSE;

    // Make sure we have a valid date before we proceed
    if (!g_date_valid_dmy (tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900))
        return FALSE;

    g_date_set_dmy (&gdate,
                    tm->tm_mday,
                    tm->tm_mon + 1,
                    tm->tm_year + 1900);

    /*
     * Check those keys where the code does different things depending
     * upon the modifiers.
     */
    switch (event->keyval)
    {
    case GDK_KEY_KP_Add:
    case GDK_KEY_plus:
    case GDK_KEY_equal:
    case GDK_KEY_semicolon: // See https://bugs.gnucash.org/show_bug.cgi?id=798386
         if (event->state & GDK_SHIFT_MASK)
            g_date_add_days (&gdate, 7);
        else if (event->state & GDK_MOD1_MASK)
            g_date_add_months (&gdate, 1);
        else if (event->state & GDK_CONTROL_MASK)
            g_date_add_years (&gdate, 1);
        else
            g_date_add_days (&gdate, 1);

        if (gnc_gdate_in_valid_range (&gdate, FALSE))
            g_date_to_struct_tm (&gdate, tm);
        return TRUE;

    case GDK_KEY_minus:
    case GDK_KEY_KP_Subtract:
    case GDK_KEY_underscore:
        if ((strlen (date_str) != 0) && (dateSeparator () == '-'))
        {
            const char *c;
            gunichar uc;
            int count = 0;

            /* rough check for existing date */
            c = date_str;
            while (*c)
            {
                uc = g_utf8_get_char (c);
                if (uc == '-')
                    count++;
                c = g_utf8_next_char (c);
            }

            if (count < 2)
                return FALSE;
        }

        if (event->state & GDK_SHIFT_MASK)
            g_date_subtract_days (&gdate, 7);
        else if (event->state & GDK_MOD1_MASK)
            g_date_subtract_months (&gdate, 1);
        else if (event->state & GDK_CONTROL_MASK)
            g_date_subtract_years (&gdate, 1);
        else
            g_date_subtract_days (&gdate, 1);

        if (gnc_gdate_in_valid_range (&gdate, FALSE))
            g_date_to_struct_tm (&gdate, tm);
        return TRUE;

    default:
        break;
    }

    /*
     * Control and Alt key combinations should be ignored by this
     * routine so that the menu system gets to handle them.  This
     * prevents weird behavior of the menu accelerators (i.e. work in
     * some widgets but not others.)
     */
    if (event->state & (GDK_MODIFIER_INTENT_DEFAULT_MOD_MASK))
        return FALSE;

    /* Now check for the remaining keystrokes. */
    switch (event->keyval)
    {
    case GDK_KEY_braceright:
    case GDK_KEY_bracketright:
        /* increment month */
        g_date_add_months (&gdate, 1);
        break;

    case GDK_KEY_braceleft:
    case GDK_KEY_bracketleft:
        /* decrement month */
        g_date_subtract_months (&gdate, 1);
        break;

    case GDK_KEY_M:
    case GDK_KEY_m:
        /* beginning of month */
        g_date_set_day (&gdate, 1);
        break;

    case GDK_KEY_H:
    case GDK_KEY_h:
        /* end of month */
        g_date_set_day (&gdate, 1);
        g_date_add_months (&gdate, 1);
        g_date_subtract_days (&gdate, 1);
        break;

    case GDK_KEY_Y:
    case GDK_KEY_y:
        /* beginning of year */
        g_date_set_day (&gdate, 1);
        g_date_set_month (&gdate, 1);
        break;

    case GDK_KEY_R:
    case GDK_KEY_r:
        /* end of year */
        g_date_set_day (&gdate, 1);
        g_date_set_month (&gdate, 1);
        g_date_add_years (&gdate, 1);
        g_date_subtract_days (&gdate, 1);
        break;

    case GDK_KEY_T:
    case GDK_KEY_t:
        /* today */
        gnc_gdate_set_today (&gdate);
        break;

    default:
        return FALSE;
    }
    if (gnc_gdate_in_valid_range (&gdate, FALSE))
        g_date_to_struct_tm (&gdate, tm);

    return TRUE;
}


/*--------------------------------------------------------------------------
 *   GtkBuilder support functions
 *-------------------------------------------------------------------------*/

GModule *allsymbols = NULL;

typedef struct
{
    gchar *object_id;
    gchar *signal_name;
    gchar *handler_name;
    gchar *connect_object_id;
    GConnectFlags flags;
} GncBuilderSignal;

typedef struct
{
    GPtrArray *signals;
    GHashTable *seen;
} GncBuilderSignalRegistry;

typedef struct
{
    GncBuilderSignalRegistry *registry;
    GPtrArray *object_stack;
} GncBuilderSignalCollector;

static GQuark builder_signal_registry_quark;

static void
gnc_builder_signal_free (GncBuilderSignal *signal)
{
    g_free (signal->object_id);
    g_free (signal->signal_name);
    g_free (signal->handler_name);
    g_free (signal->connect_object_id);
    g_free (signal);
}

static void
gnc_builder_signal_registry_free (GncBuilderSignalRegistry *registry)
{
    g_ptr_array_unref (registry->signals);
    g_hash_table_unref (registry->seen);
    g_free (registry);
}

static GncBuilderSignalRegistry *
gnc_builder_get_signal_registry (GtkBuilder *builder)
{
    GncBuilderSignalRegistry *registry;

    if (G_UNLIKELY (builder_signal_registry_quark == 0))
        builder_signal_registry_quark =
            g_quark_from_static_string ("gnc-builder-signal-registry");

    registry = g_object_get_qdata (G_OBJECT (builder),
                                   builder_signal_registry_quark);
    if (registry)
        return registry;

    registry = g_new0 (GncBuilderSignalRegistry, 1);
    registry->signals = g_ptr_array_new_with_free_func
        ((GDestroyNotify)gnc_builder_signal_free);
    registry->seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    g_object_set_qdata_full (G_OBJECT (builder), builder_signal_registry_quark,
                             registry,
                             (GDestroyNotify)gnc_builder_signal_registry_free);
    return registry;
}

static const gchar *
gnc_builder_attribute (const gchar **attribute_names,
                       const gchar **attribute_values,
                       const gchar *name)
{
    for (guint i = 0; attribute_names[i] != NULL; i++)
        if (g_str_equal (attribute_names[i], name))
            return attribute_values[i];
    return NULL;
}

static void
gnc_builder_collect_start_element (GMarkupParseContext *context,
                                   const gchar *element_name,
                                   const gchar **attribute_names,
                                   const gchar **attribute_values,
                                   gpointer user_data,
                                   GError **error)
{
    GncBuilderSignalCollector *collector = user_data;

    if (g_str_equal (element_name, "object"))
    {
        const gchar *id = gnc_builder_attribute (attribute_names,
                                                 attribute_values, "id");
        g_ptr_array_add (collector->object_stack, g_strdup (id ? id : ""));
        return;
    }

    if (!g_str_equal (element_name, "signal") ||
        collector->object_stack->len == 0)
        return;

    const gchar *signal_name = gnc_builder_attribute (attribute_names,
                                                      attribute_values, "name");
    const gchar *handler_name = gnc_builder_attribute (attribute_names,
                                                       attribute_values, "handler");
    if (!signal_name || !handler_name)
        return;

    const gchar *object_id = g_ptr_array_index
        (collector->object_stack, collector->object_stack->len - 1);
    const gchar *connect_object_id = gnc_builder_attribute (attribute_names,
                                                             attribute_values,
                                                             "object");
    const gchar *swapped = gnc_builder_attribute (attribute_names,
                                                  attribute_values, "swapped");
    const gchar *after = gnc_builder_attribute (attribute_names,
                                                attribute_values, "after");
    GConnectFlags flags = 0;
    if (g_strcmp0 (swapped, "yes") == 0 || g_strcmp0 (swapped, "true") == 0)
        flags |= G_CONNECT_SWAPPED;
    if (g_strcmp0 (after, "yes") == 0 || g_strcmp0 (after, "true") == 0)
        flags |= G_CONNECT_AFTER;

    gchar *key = g_strdup_printf ("%s\x1f%s\x1f%s\x1f%s\x1f%u", object_id,
                                  signal_name, handler_name,
                                  connect_object_id ? connect_object_id : "", flags);
    if (g_hash_table_contains (collector->registry->seen, key))
    {
        g_free (key);
        return;
    }

    GncBuilderSignal *signal = g_new0 (GncBuilderSignal, 1);
    signal->object_id = g_strdup (object_id);
    signal->signal_name = g_strdup (signal_name);
    signal->handler_name = g_strdup (handler_name);
    signal->connect_object_id = g_strdup (connect_object_id);
    signal->flags = flags;
    g_hash_table_add (collector->registry->seen, key);
    g_ptr_array_add (collector->registry->signals, signal);
}

static void
gnc_builder_collect_end_element (GMarkupParseContext *context,
                                 const gchar *element_name,
                                 gpointer user_data,
                                 GError **error)
{
    GncBuilderSignalCollector *collector = user_data;

    if (g_str_equal (element_name, "object") && collector->object_stack->len)
        g_ptr_array_remove_index (collector->object_stack,
                                  collector->object_stack->len - 1);
}

static gboolean
gnc_builder_collect_signals (GtkBuilder *builder, const gchar *contents,
                             gssize length, GError **error)
{
    GncBuilderSignalCollector collector =
    {
        .registry = gnc_builder_get_signal_registry (builder),
        .object_stack = g_ptr_array_new_with_free_func (g_free),
    };
    GMarkupParser parser =
    {
        .start_element = gnc_builder_collect_start_element,
        .end_element = gnc_builder_collect_end_element,
    };
    GMarkupParseContext *context = g_markup_parse_context_new
        (&parser, G_MARKUP_TREAT_CDATA_AS_TEXT, &collector, NULL);
    gboolean result = g_markup_parse_context_parse (context, contents, length, error) &&
        g_markup_parse_context_end_parse (context, error);

    g_markup_parse_context_free (context);
    g_ptr_array_unref (collector.object_stack);
    return result;
}

static gchar *
gnc_builder_without_signals (const gchar *contents, GError **error)
{
    GRegex *signal_pattern = g_regex_new
        ("(?s)<signal\\b[^>]*(?:/>|>.*?</signal>)", 0, 0, error);
    if (!signal_pattern)
        return NULL;

    gchar *without_signals = g_regex_replace_literal (signal_pattern, contents,
                                                      -1, 0, "", 0, error);
    g_regex_unref (signal_pattern);
    return without_signals;
}

/* gnc_builder_add_from_file:
 *
 *   a convenience wrapper for gtk_builder_add_objects_from_string.
 *   It takes care of finding the directory for glade files and prints a
 *   warning message in case of an error.
 */
gboolean
gnc_builder_add_from_file (GtkBuilder *builder, const char *filename, const char *root)
{
    GError* error = NULL;
    char *fname;
    gchar *gnc_builder_dir;
    gchar *contents = NULL;
    gchar *without_signals = NULL;
    gsize contents_length = 0;
    gboolean result;

    g_return_val_if_fail (builder != NULL, FALSE);
    g_return_val_if_fail (filename != NULL, FALSE);
    g_return_val_if_fail (root != NULL, FALSE);

    gnc_builder_dir = gnc_path_get_gtkbuilderdir ();
    fname = g_build_filename(gnc_builder_dir, filename, (char *)NULL);
    g_free (gnc_builder_dir);

    if (!g_file_get_contents (fname, &contents, &contents_length, &error))
    {
        PWARN ("Couldn't load builder file: %s", error->message);
        g_error_free (error);
        g_free (fname);
        return FALSE;
    }

    if (!gnc_builder_collect_signals (builder, contents, contents_length, &error))
    {
        PWARN ("Couldn't parse builder signals: %s", error->message);
        g_error_free (error);
        g_free (contents);
        g_free (fname);
        return FALSE;
    }

    without_signals = gnc_builder_without_signals (contents, &error);
    g_free (contents);
    if (!without_signals)
    {
        PWARN ("Couldn't prepare builder file: %s", error->message);
        g_error_free (error);
        g_free (fname);
        return FALSE;
    }

    {
        gchar *localroot = g_strdup(root);
        gchar *objects[] = { localroot, NULL };
        result = gtk_builder_add_objects_from_string (builder, without_signals,
                                                      -1, objects, &error);
        if (!result)
        {
            PWARN ("Couldn't load builder file: %s", error->message);
            g_error_free (error);
        }
        g_free (localroot);
    }

    g_free (without_signals);
    g_free (fname);

    return result;
}

void
gnc_builder_connect_signals_full (GtkBuilder *builder,
                                  GncBuilderConnectFunc connect_func,
                                  gpointer user_data)
{
    GncBuilderSignalRegistry *registry = gnc_builder_get_signal_registry (builder);

    g_return_if_fail (connect_func != NULL);
    for (guint i = 0; i < registry->signals->len; i++)
    {
        GncBuilderSignal *signal = g_ptr_array_index (registry->signals, i);
        GObject *signal_object = gtk_builder_get_object (builder, signal->object_id);
        if (!signal_object)
            continue;

        GObject *connect_object = NULL;
        if (signal->connect_object_id)
        {
            connect_object = gtk_builder_get_object (builder,
                                                     signal->connect_object_id);
            if (!connect_object)
            {
                PWARN ("Couldn't find signal object '%s' for handler '%s'.",
                       signal->connect_object_id, signal->handler_name);
                continue;
            }
        }

        connect_func (builder, signal_object, signal->signal_name,
                      signal->handler_name, connect_object,
                      signal->flags, user_data);
    }
}

void
gnc_builder_connect_signals (GtkBuilder *builder, gpointer user_data)
{
    gnc_builder_connect_signals_full (builder, gnc_builder_connect_full_func,
                                      user_data);
}


/*---------------------------------------------------------------------
 * The following function is built from a couple of glade functions.
 *--------------------------------------------------------------------*/
void
gnc_builder_connect_full_func(GtkBuilder *builder,
                              GObject *signal_object,
                              const gchar *signal_name,
                              const gchar *handler_name,
                              GObject *connect_object,
                              GConnectFlags flags,
                              gpointer user_data)
{
    GCallback func;
    GCallback *p_func = &func;

    if (allsymbols == NULL)
    {
        /* get a handle on the main executable -- use this to find symbols */
        allsymbols = g_module_open(NULL, 0);
    }

    if (!g_module_symbol(allsymbols, handler_name, (gpointer *)p_func))
    {
#ifdef HAVE_DLSYM
        /* Fallback to dlsym -- necessary for *BSD linkers */
        func = dlsym(RTLD_DEFAULT, handler_name);
#else
        func = NULL;
#endif
        if (func == NULL)
        {
            PWARN("ggaff: could not find signal handler '%s'.", handler_name);
            return;
        }
    }

    if (connect_object)
        g_signal_connect_object (signal_object, signal_name, func,
                                 connect_object, flags);
    else
        g_signal_connect_data(signal_object, signal_name, func,
                              user_data, NULL , flags);
}
/*--------------------------------------------------------------------------
 * End of GtkBuilder utilities
 *-------------------------------------------------------------------------*/


void
gnc_gtk_dialog_add_button (GtkWidget *dialog, const gchar *label, const gchar *icon_name, guint response)
{
    GtkWidget *button;

    button = gtk_button_new_with_mnemonic(label);
    if (icon_name)
    {
        GtkWidget *image;

        image = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image (GTK_BUTTON(button), image);
        g_object_set (button, "always-show-image", TRUE, NULL);
    }
    g_object_set (button, "can-default", TRUE, NULL);
    gtk_widget_show_all(button);
    gtk_dialog_add_action_widget(GTK_DIALOG(dialog), button, response);
}

static void
gnc_perm_button_cb (GtkButton *perm, gpointer user_data)
{
    gboolean perm_active;

    perm_active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(perm));
    gtk_widget_set_sensitive(user_data, !perm_active);
}

gint
gnc_dialog_run (GtkDialog *dialog, const gchar *pref_name)
{
    GtkWidget *perm, *temp;
    gboolean ask = TRUE;
    gint response;

    /* Does the user want to see this question? If not, return the
     * previous answer. */
    response = gnc_prefs_get_int(GNC_PREFS_GROUP_WARNINGS_PERM, pref_name);
    if (response != 0)
        return response;
    response = gnc_prefs_get_int(GNC_PREFS_GROUP_WARNINGS_TEMP, pref_name);
    if (response != 0)
        return response;

    /* Add in the checkboxes to find out if the answer should be remembered. */
    if (GTK_IS_MESSAGE_DIALOG(dialog))
    {
        GtkMessageType type;
        g_object_get(dialog, "message-type", &type, (gchar*)NULL);
        ask = (type == GTK_MESSAGE_QUESTION || type == GTK_MESSAGE_WARNING);
    }
    perm = gtk_check_button_new_with_mnemonic
           (ask
            ? _("Remember and don't _ask me again.")
            : _("Don't _tell me again."));
    temp = gtk_check_button_new_with_mnemonic
           (ask
            ? _("Remember and don't ask me again this _session.")
            : _("Don't tell me again this _session."));
    gtk_widget_show(perm);
    gtk_widget_show(temp);
    gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (dialog)), perm, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (dialog)), temp, TRUE, TRUE, 0);
    g_signal_connect(perm, "clicked", G_CALLBACK(gnc_perm_button_cb), temp);

    /* OK. Present the dialog. */
    GtkWidget *button_cancel = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    gtk_widget_grab_focus(button_cancel);
    response = gtk_dialog_run(dialog);
    if ((response == GTK_RESPONSE_NONE) || (response == GTK_RESPONSE_DELETE_EVENT))
    {
        return GTK_RESPONSE_CANCEL;
    }

    if (response != GTK_RESPONSE_CANCEL)
    {
        /* Save the answer? */
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(perm)))
        {
            gnc_prefs_set_int(GNC_PREFS_GROUP_WARNINGS_PERM, pref_name, response);
        }
        else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(temp)))
        {
            gnc_prefs_set_int(GNC_PREFS_GROUP_WARNINGS_TEMP, pref_name, response);
        }
    }
    return response;
}

/* If this is a new book, this function can be used to display book options
 * dialog so user can specify options, before any transactions can be
 * imported/entered, since the book options can affect how transactions are
 * created. Note: This dialog is modal! */
gboolean
gnc_new_book_option_display (GtkWidget *parent)
{
    GtkWidget *window;
    gint result = GTK_RESPONSE_HELP;

    window = gnc_book_options_dialog_cb (TRUE, _( "New Book Options"),
                                         GTK_WINDOW (parent));
    if (window)
    {
        /* close dialog and proceed unless help button selected */
        while (result == GTK_RESPONSE_HELP)
        {
            result = gtk_dialog_run(GTK_DIALOG(window));
        }
        return FALSE;
    }
    return TRUE;
}

gchar*
gnc_get_negative_color (void)
{
    GdkRGBA color;
    GtkWidget *label = gtk_label_new ("Color");
    GtkStyleContext *context = gtk_widget_get_style_context (GTK_WIDGET(label));
    gtk_style_context_add_class (context, "gnc-class-negative-numbers");
    gtk_style_context_get_color (context, GTK_STATE_FLAG_NORMAL, &color);

    return gdk_rgba_to_string (&color);
}

void
gnc_owner_window_set_title (GtkWindow *window, const char *header,
                            GtkWidget *owner_entry, GtkWidget *id_entry)
{
    const char *name = gtk_entry_get_text (GTK_ENTRY (owner_entry));
    if (!name || *name == '\0')
        name = _("<No name>");

    const char *id = gtk_entry_get_text (GTK_ENTRY (id_entry));

    char *title = (id && *id) ?
        g_strdup_printf ("%s - %s (%s)", header, name, id) :
        g_strdup_printf ("%s - %s", header, name);

    gtk_window_set_title (window, title);

    g_free (title);
}
