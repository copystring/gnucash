/* GTK4 ColumnView for business owners. */
#ifndef __GNC_TREE_VIEW_OWNER_H
#define __GNC_TREE_VIEW_OWNER_H

#include <gtk/gtk.h>
#include "gncOwner.h"
#include "gnc-plugin-page.h"

G_BEGIN_DECLS

#define GNC_TYPE_TREE_VIEW_OWNER (gnc_tree_view_owner_get_type ())
G_DECLARE_FINAL_TYPE (GncTreeViewOwner, gnc_tree_view_owner, GNC, TREE_VIEW_OWNER, GtkColumnView)

typedef struct
{
    GtkWidget *dialog;
    GncTreeViewOwner *tree_view;
    gboolean show_inactive;
    gboolean original_show_inactive;
    gboolean show_zero_total;
    gboolean original_show_zero_total;
} OwnerFilterDialog;

typedef gboolean (*gnc_tree_view_owner_filter_func)(GncOwner *owner, gpointer data);

GtkWidget *gnc_tree_view_owner_new (GncOwnerType owner_type);
GtkSelectionModel *gnc_tree_view_owner_get_selection_model (GncTreeViewOwner *view);
GncOwner *gnc_tree_view_owner_get_selected_owner (GncTreeViewOwner *view);
void gnc_tree_view_owner_set_selected_owner (GncTreeViewOwner *view, GncOwner *owner);
void gnc_tree_view_owner_set_filter (GncTreeViewOwner *view,
                                     gnc_tree_view_owner_filter_func func,
                                     gpointer data, GDestroyNotify destroy);
void gnc_tree_view_owner_refilter (GncTreeViewOwner *view);
void gnc_tree_view_owner_save (GncTreeViewOwner *view, OwnerFilterDialog *fd,
                               GKeyFile *key_file, const gchar *group_name);
void gnc_tree_view_owner_restore (GncTreeViewOwner *view, OwnerFilterDialog *fd,
                                  GKeyFile *key_file, const gchar *group_name,
                                  GncOwnerType owner_type);
void owner_filter_dialog_create (OwnerFilterDialog *fd, GncPluginPage *page);
gboolean gnc_plugin_page_owner_tree_filter_owners (GncOwner *owner, gpointer data);
void gppot_filter_show_inactive_toggled_cb (GtkToggleButton *button,
                                            OwnerFilterDialog *fd);
void gppot_filter_show_zero_toggled_cb (GtkToggleButton *button,
                                        OwnerFilterDialog *fd);
void gppot_filter_apply_cb (GtkButton *button, OwnerFilterDialog *fd);
void gppot_filter_cancel_cb (GtkButton *button, OwnerFilterDialog *fd);

G_END_DECLS
#endif