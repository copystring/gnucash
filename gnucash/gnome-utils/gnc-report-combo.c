/********************************************************************\
 * gnc-report-combo.c -- report select widget for GnuCash           *
 *                                                                  *
 * Copyright (C) 2022 Bob Fewell                                    *
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
#include <glib/gi18n.h>

#include "gnc-report-combo.h"
#include "gnc-ui-util.h"
#include "gnc-engine.h"
#include "dialog-utils.h"

/** The debugging module used by this file. */
__attribute__((unused)) static QofLogModule log_module = GNC_MOD_GUI;

static void gnc_report_combo_dispose    (GObject *object);
static void gnc_report_combo_finalize   (GObject *object);

typedef struct
{
    GObject parent_instance;
    gchar *name;
    gchar *guid;
    gboolean missing;
} GncReportComboItem;

typedef struct
{
    GObjectClass parent_class;
} GncReportComboItemClass;

G_DEFINE_TYPE (GncReportComboItem, gnc_report_combo_item, G_TYPE_OBJECT)

static void
report_combo_item_finalize (GObject *object)
{
    GncReportComboItem *item = (GncReportComboItem *)object;

    g_free (item->name);
    g_free (item->guid);
    G_OBJECT_CLASS (gnc_report_combo_item_parent_class)->finalize (object);
}

static void
gnc_report_combo_item_class_init (GncReportComboItemClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = report_combo_item_finalize;
}

static void
gnc_report_combo_item_init (GncReportComboItem *item)
{
    (void)item;
}

static GncReportComboItem *
report_combo_item_new (const gchar *name, const gchar *guid, gboolean missing)
{
    GncReportComboItem *item = g_object_new (gnc_report_combo_item_get_type (), NULL);

    item->name = g_strdup (name);
    item->guid = g_strdup (guid);
    item->missing = missing;
    return item;
}

struct _GncReportCombo
{
    GtkBox box;

    GtkMenuButton *menu_button;
    GtkSingleSelection *selection;
    GListStore *model;
    GtkWidget *warning_image;

    gboolean block_signal;
    gboolean popup_shown;

    gchar *active_report_guid;
    gchar *active_report_name;
};

G_DEFINE_TYPE (GncReportCombo, gnc_report_combo, GTK_TYPE_BOX)

enum
{
    SIGNAL_0,
    CHANGED,
    LAST_SIGNAL
};

static guint report_combo_signals [LAST_SIGNAL] = {0};

enum
{
    PROP_0,
    PROP_POPUP_SHOWN,
    N_PROPERTIES
};

static GParamSpec *report_combo_properties [N_PROPERTIES] = {NULL,};

static void
gnc_report_combo_get_property (GObject    *object,
                               guint       property_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    GncReportCombo        *grc = GNC_REPORT_COMBO(object);

    switch (property_id)
    {
    case PROP_POPUP_SHOWN:
        g_value_set_boolean (value, grc->popup_shown);
        break;

    default:
        /* We don't have any other property... */
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

/** Initialize the GncReportCombo class object.
 *
 *  @internal
 *
 *  @param klass A pointer to the newly created class object.
 */
static void
gnc_report_combo_class_init (GncReportComboClass *klass)
{
    GObjectClass   *object_class = G_OBJECT_CLASS(klass);

    object_class->get_property = gnc_report_combo_get_property;
    object_class->dispose  = gnc_report_combo_dispose;
    object_class->finalize = gnc_report_combo_finalize;

    report_combo_signals [CHANGED] =
        g_signal_new ("changed",
                      G_OBJECT_CLASS_TYPE(object_class),
                      G_SIGNAL_RUN_FIRST,
                      0,
                      NULL,
                      NULL,
                      g_cclosure_marshal_VOID__VOID,
                      G_TYPE_NONE,
                      0);

    report_combo_properties [PROP_POPUP_SHOWN] =
        g_param_spec_boolean ("popup-shown",
                              "State of PopUp",
                              "State of PopUp",
                              FALSE /* default value */,
                              G_PARAM_READABLE);

    g_object_class_install_properties (object_class,
                                       N_PROPERTIES,
                                       report_combo_properties);
}

/** Initialize a GncReportCombo object.
 *
 *  @internal
 *
 *  @param grc A pointer to the newly created object.
 */
static void
gnc_report_combo_init (GncReportCombo *grc)
{
    g_return_if_fail (grc != NULL);
    g_return_if_fail (GNC_IS_REPORT_COMBO(grc));

    // Set the name for this widget so it can be easily manipulated with css
    gtk_widget_set_name (GTK_WIDGET(grc), "gnc-id-report-combo");

    grc->block_signal = FALSE;
    grc->active_report_guid = NULL;
    grc->active_report_name = NULL;
    grc->popup_shown = FALSE;
}

/** Dispopse the GncReportCombo object. This function is called from
 *  the G_Object level to complete the destruction of the object.  It
 *  should release any memory not previously released by the destroy
 *  function (i.e. the private data structure), then chain up to the
 *  parent's destroy function.
 *
 *  @param object The object being destroyed.
 *
 *  @internal
 */
static void
gnc_report_combo_dispose (GObject *object)
{
    /* Do not free the private data structure itself. It is part of
     * a larger memory block allocated by the type system. */

    GncReportCombo *grc = GNC_REPORT_COMBO (object);

    g_clear_object (&grc->selection);
    g_clear_object (&grc->model);
    G_OBJECT_CLASS (gnc_report_combo_parent_class)->dispose (object);
}

/** Finalize the GncReportCombo object.  This function is called from
 *  the G_Object level to complete the destruction of the object.  It
 *  should release any memory not previously released by the destroy
 *  function (i.e. the private data structure), then chain up to the
 *  parent's finalize function.
 *
 *  @param object The object being finalized.
 *
 *  @internal
 */
static void
gnc_report_combo_finalize (GObject *object)
{
    GncReportCombo *grc;

    g_return_if_fail (object != NULL);
    g_return_if_fail (GNC_IS_REPORT_COMBO(object));

    grc = GNC_REPORT_COMBO(object);

    g_free (grc->active_report_guid);
    g_free (grc->active_report_name);

    G_OBJECT_CLASS (gnc_report_combo_parent_class)->finalize (object);
}

/** This function sets the active combo entry based on the private
 *  report guid and also checks to see if the report guid is in the
 *  list of reports, if not a warning image and tooltip is shown.
 *
 *  @internal
 *
 *  @param grc The report combo.
 *
 *  @return TRUE if report guid is in the list, other wise FALSE.
 */
static GncReportComboItem *
get_selected_item (GncReportCombo *grc)
{
    guint selected = gtk_single_selection_get_selected (grc->selection);

    if (selected == GTK_INVALID_LIST_POSITION)
        return NULL;
    return (GncReportComboItem *)g_list_model_get_item (G_LIST_MODEL (grc->model),
                                                         selected);
}

static void
set_selected_item (GncReportCombo *grc, guint position)
{
    GncReportComboItem *item;

    gtk_single_selection_set_selected (grc->selection, position);
    item = get_selected_item (grc);
    gtk_menu_button_set_label (grc->menu_button, item ? item->name : NULL);
    g_clear_object (&item);
}

static gboolean
select_active_and_check_exists (GncReportCombo *grc)
{
    guint n_items = g_list_model_get_n_items (G_LIST_MODEL (grc->model));

    for (guint index = 0; index < n_items; index++)
    {
        GncReportComboItem *item = (GncReportComboItem *)g_list_model_get_item (
            G_LIST_MODEL (grc->model), index);
        gboolean found = g_strcmp0 (grc->active_report_guid, item->guid) == 0;

        g_object_unref (item);
        if (found)
        {
            set_selected_item (grc, index);
            return TRUE;
        }
    }

    {
        gchar *name = grc->active_report_name
            ? g_strdup (grc->active_report_name)
            : g_strdup (_("Selected Report is Missing"));
        GncReportComboItem *item = report_combo_item_new (name,
                                                           grc->active_report_guid,
                                                           TRUE);

        g_list_store_insert (grc->model, 0, item);
        g_object_unref (item);
        g_free (name);
    }
    set_selected_item (grc, 0);
    return FALSE;
}

static gint
compare_report_list_entries (gconstpointer first, gconstpointer second)
{
    const ReportListEntry *first_entry = first;
    const ReportListEntry *second_entry = second;

    return g_utf8_collate (first_entry->report_name, second_entry->report_name);
}

static void
update_report_list (GncReportCombo *grc, GSList *report_list)
{
    g_list_store_remove_all (grc->model);
    report_list = g_slist_sort (report_list, compare_report_list_entries);

    for (GSList *node = report_list; node; node = g_slist_next (node))
    {
        ReportListEntry *entry = node->data;
        GncReportComboItem *item = report_combo_item_new (entry->report_name,
                                                           entry->report_guid,
                                                           FALSE);

        g_list_store_append (grc->model, item);
        g_object_unref (item);
        g_free (entry->report_name);
        g_free (entry->report_guid);
        g_free (entry);
    }
    g_slist_free (report_list);
}

static void
update_warning_tooltip (GncReportCombo *grc)
{
    gchar *tool_tip;

    if (grc->active_report_name)
        /* Translators: %s is the report name. */
        tool_tip = g_strdup_printf (_("'%s' is missing"),
                                    grc->active_report_name);
    else
        /* Translators: %s is the internal report guid. */
        tool_tip = g_strdup_printf (_("Report with GUID '%s' is missing"),
                                    grc->active_report_guid);

    gtk_widget_set_visible (grc->warning_image, TRUE);
    gtk_widget_set_tooltip_text (grc->warning_image, tool_tip);
    g_free (tool_tip);
}

static void
hide_warning (GncReportCombo *grc)
{
    gtk_widget_set_visible (grc->warning_image, FALSE);
    gtk_widget_set_tooltip_text (grc->warning_image, NULL);
}

void
gnc_report_combo_set_active (GncReportCombo *grc,
                             const char* active_report_guid,
                             const char* active_report_name)
{
    g_return_if_fail (grc != NULL);
    g_return_if_fail (GNC_IS_REPORT_COMBO(grc));

    g_free (grc->active_report_guid);
    grc->active_report_guid = g_strdup (active_report_guid);
    g_free (grc->active_report_name);
    grc->active_report_name = g_strdup (active_report_name);

    grc->block_signal = TRUE;
    if (!select_active_and_check_exists (grc))
        update_warning_tooltip (grc);
    else
        hide_warning (grc);
    grc->block_signal = FALSE;
}

gchar *
gnc_report_combo_get_active_guid (GncReportCombo *grc)
{
    GncReportComboItem *item;
    gchar *guid;

    g_return_val_if_fail (grc != NULL, NULL);
    g_return_val_if_fail (GNC_IS_REPORT_COMBO(grc), NULL);

    item = get_selected_item (grc);
    guid = item ? g_strdup (item->guid) : NULL;
    g_clear_object (&item);
    return guid;
}

gchar *
gnc_report_combo_get_active_name (GncReportCombo *grc)
{
    GncReportComboItem *item;
    gchar *name;

    g_return_val_if_fail (grc != NULL, NULL);
    g_return_val_if_fail (GNC_IS_REPORT_COMBO(grc), NULL);

    item = get_selected_item (grc);
    name = item ? g_strdup (item->name) : NULL;
    g_clear_object (&item);
    return name;
}

gchar*
gnc_report_combo_get_active_guid_name (GncReportCombo *grc)
{
    GncReportComboItem *item;
    gchar *report;

    g_return_val_if_fail (grc != NULL, NULL);
    g_return_val_if_fail (GNC_IS_REPORT_COMBO(grc), NULL);

    item = get_selected_item (grc);
    report = item ? g_strconcat (item->guid, "/", item->name, NULL) : NULL;
    g_clear_object (&item);
    return report;
}

void
gnc_report_combo_set_active_guid_name (GncReportCombo *grc,
                                       const gchar *guid_name)
{
    g_return_if_fail (grc != NULL);
    g_return_if_fail (GNC_IS_REPORT_COMBO(grc));

    if (guid_name && *guid_name)
    {
        gchar *guid = NULL;
        gchar *name = g_strstr_len (guid_name, -1, "/");

        if (name)
        {
            guid = g_strndup (guid_name, (name - guid_name));
            gnc_report_combo_set_active (grc, guid, name + 1);
        }
        g_free (guid);
    }
}

gboolean
gnc_report_combo_is_warning_visible_for_active (GncReportCombo *grc)
{
    g_return_val_if_fail (grc != NULL, FALSE);
    g_return_val_if_fail (GNC_IS_REPORT_COMBO(grc), FALSE);

    return gtk_widget_is_visible (GTK_WIDGET(grc->warning_image));
}

static void
report_item_setup (GtkListItemFactory *factory, GtkListItem *list_item,
                   gpointer user_data)
{
    GtkWidget *label = gtk_label_new (NULL);

    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_END);
    gtk_list_item_set_child (list_item, label);
    (void)factory;
    (void)user_data;
}

static void
report_item_bind (GtkListItemFactory *factory, GtkListItem *list_item,
                  gpointer user_data)
{
    GncReportComboItem *item = (GncReportComboItem *)gtk_list_item_get_item (
        list_item);

    gtk_label_set_text (GTK_LABEL (gtk_list_item_get_child (list_item)), item->name);
    (void)factory;
    (void)user_data;
}

static void
selection_changed_cb (GtkSelectionModel *selection, guint position, guint n_items,
                      GncReportCombo *grc)
{
    GncReportComboItem *item = get_selected_item (grc);

    if (!item)
        return;

    gtk_menu_button_set_label (grc->menu_button, item->name);
    if (item->missing)
        update_warning_tooltip (grc);
    else
        hide_warning (grc);

    if (!grc->block_signal)
        g_signal_emit (grc, report_combo_signals [CHANGED], 0);
    if (gtk_menu_button_get_active (grc->menu_button))
        gtk_menu_button_popdown (grc->menu_button);

    g_object_unref (item);
    (void)selection;
    (void)position;
    (void)n_items;
}

static void
menu_button_active_cb (GObject *object, GParamSpec *pspec, gpointer user_data)
{
    GncReportCombo *grc = GNC_REPORT_COMBO (user_data);

    grc->popup_shown = gtk_menu_button_get_active (GTK_MENU_BUTTON (object));
    g_object_notify (G_OBJECT (grc), "popup-shown");
    (void)pspec;
}

void
gnc_report_combo_refresh (GncReportCombo *grc, GSList *report_list)
{
    g_return_if_fail (grc != NULL);
    g_return_if_fail (GNC_IS_REPORT_COMBO(grc));
    g_return_if_fail (report_list != NULL);

    grc->block_signal = TRUE;
    update_report_list (grc, report_list);
    if (!select_active_and_check_exists (grc))
        update_warning_tooltip (grc);
    else
        hide_warning (grc);
    grc->block_signal = FALSE;
}

GtkWidget *
gnc_report_combo_new (GSList *report_list)
{
    GncReportCombo *grc;
    GtkListItemFactory *factory;
    GtkWidget *list_view;
    GtkWidget *scrolled_window;
    GtkWidget *popover;

    grc = g_object_new (GNC_TYPE_REPORT_COMBO, NULL);
    grc->model = g_list_store_new (gnc_report_combo_item_get_type ());
    grc->selection = gtk_single_selection_new (G_LIST_MODEL (grc->model));
    grc->menu_button = GTK_MENU_BUTTON (gtk_menu_button_new ());
    gtk_menu_button_set_always_show_arrow (grc->menu_button, TRUE);

    factory = gtk_signal_list_item_factory_new ();
    g_signal_connect (factory, "setup", G_CALLBACK (report_item_setup), NULL);
    g_signal_connect (factory, "bind", G_CALLBACK (report_item_bind), NULL);
    list_view = gtk_list_view_new (GTK_SELECTION_MODEL (g_object_ref (grc->selection)),
                                   factory);
    scrolled_window = gtk_scrolled_window_new ();
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request (scrolled_window, 280, 240);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), list_view);

    popover = gtk_popover_new ();
    gtk_popover_set_child (GTK_POPOVER (popover), scrolled_window);
    gtk_menu_button_set_popover (grc->menu_button, popover);

    gtk_box_append (GTK_BOX (grc), GTK_WIDGET (grc->menu_button));
    grc->warning_image = gtk_image_new_from_icon_name ("dialog-warning");
    gtk_image_set_icon_size (GTK_IMAGE (grc->warning_image), GTK_ICON_SIZE_NORMAL);
    gtk_box_append (GTK_BOX (grc), grc->warning_image);
    gtk_box_set_spacing (GTK_BOX (grc), 6);
    gtk_widget_set_visible (grc->warning_image, FALSE);

    g_signal_connect (grc->selection, "selection-changed",
                      G_CALLBACK (selection_changed_cb), grc);
    g_signal_connect (grc->menu_button, "notify::active",
                      G_CALLBACK (menu_button_active_cb), grc);

    update_report_list (grc, report_list);
    return GTK_WIDGET (grc);
}
