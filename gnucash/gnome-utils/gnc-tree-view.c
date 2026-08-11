#include <config.h>
#include "gnc-tree-view.h"
typedef struct
{
    GtkColumnView *column_view;
    gchar *state_section;
    gboolean show_column_menu;
} GncTreeViewPrivate;
enum
{
    PROP_0, PROP_STATE_SECTION, PROP_SHOW_COLUMN_MENU, N_PROPERTIES
};
static GParamSpec *properties[N_PROPERTIES];
G_DEFINE_TYPE_WITH_PRIVATE (GncTreeView, gnc_tree_view, GTK_TYPE_BOX)

static void
get_property (GObject *object, guint id, GValue *value, GParamSpec *pspec)
{
    GncTreeViewPrivate *p = gnc_tree_view_get_instance_private (GNC_TREE_VIEW (object));
    if (id == PROP_STATE_SECTION) g_value_set_string (value, p->state_section);
    else if (id == PROP_SHOW_COLUMN_MENU) g_value_set_boolean (value, p->show_column_menu);
    else G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
}
static void
set_property (GObject *object, guint id, const GValue *value, GParamSpec *pspec)
{
    if (id == PROP_STATE_SECTION) gnc_tree_view_set_state_section (GNC_TREE_VIEW (object), g_value_get_string (value));
    else if (id == PROP_SHOW_COLUMN_MENU) gnc_tree_view_set_show_column_menu (GNC_TREE_VIEW (object), g_value_get_boolean (value));
    else G_OBJECT_WARN_INVALID_PROPERTY_ID (object, id, pspec);
}
static void
dispose (GObject *object)
{
    GncTreeViewPrivate *p = gnc_tree_view_get_instance_private (GNC_TREE_VIEW (object));
    g_clear_pointer (&p->state_section, g_free);
    g_clear_object (&p->column_view);
    G_OBJECT_CLASS (gnc_tree_view_parent_class)->dispose (object);
}
static void
gnc_tree_view_class_init (GncTreeViewClass *klass)
{
    GObjectClass *c = G_OBJECT_CLASS (klass);
    c->get_property = get_property;
    c->set_property = set_property;
    c->dispose = dispose;
    properties[PROP_STATE_SECTION] = g_param_spec_string ("state-section", "State section", "Preference section used to persist this view", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_SHOW_COLUMN_MENU] = g_param_spec_boolean ("show-column-menu", "Show column menu", "Expose the GTK4 column chooser affordance", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_properties (c, N_PROPERTIES, properties);
}
static void
gnc_tree_view_init (GncTreeView *view)
{
    GncTreeViewPrivate *p = gnc_tree_view_get_instance_private (view);
    gtk_orientable_set_orientation (GTK_ORIENTABLE (view), GTK_ORIENTATION_VERTICAL);
    p->column_view = GTK_COLUMN_VIEW (gtk_column_view_new (NULL));
    gtk_column_view_set_show_row_separators (p->column_view, TRUE);
    gtk_column_view_set_show_column_separators (p->column_view, TRUE);
    gtk_column_view_set_reorderable (p->column_view, TRUE);
    gtk_widget_set_hexpand (GTK_WIDGET (p->column_view), TRUE);
    gtk_widget_set_vexpand (GTK_WIDGET (p->column_view), TRUE);
    gtk_box_append (GTK_BOX (view), GTK_WIDGET (p->column_view));
}
GtkColumnView *
gnc_tree_view_get_column_view (GncTreeView *view)
{
    GncTreeViewPrivate *p;
    g_return_val_if_fail (GNC_IS_TREE_VIEW (view), NULL);
    p = gnc_tree_view_get_instance_private (view);
    return p->column_view;
}
void
gnc_tree_view_set_state_section (GncTreeView *view, const gchar *section)
{
    GncTreeViewPrivate *p;
    g_return_if_fail (GNC_IS_TREE_VIEW (view));
    p = gnc_tree_view_get_instance_private (view);
    if (g_strcmp0 (p->state_section, section) == 0) return;
    g_free (p->state_section);
    p->state_section = g_strdup (section);
    g_object_notify_by_pspec (G_OBJECT (view), properties[PROP_STATE_SECTION]);
}
const gchar *
gnc_tree_view_get_state_section (GncTreeView *view)
{
    GncTreeViewPrivate *p;
    g_return_val_if_fail (GNC_IS_TREE_VIEW (view), NULL);
    p = gnc_tree_view_get_instance_private (view);
    return p->state_section;
}
void
gnc_tree_view_set_show_column_menu (GncTreeView *view, gboolean visible)
{
    GncTreeViewPrivate *p;
    g_return_if_fail (GNC_IS_TREE_VIEW (view));
    p = gnc_tree_view_get_instance_private (view);
    visible = !!visible;
    if (p->show_column_menu == visible) return;
    p->show_column_menu = visible;
    g_object_notify_by_pspec (G_OBJECT (view), properties[PROP_SHOW_COLUMN_MENU]);
}
gboolean
gnc_tree_view_get_show_column_menu (GncTreeView *view)
{
    GncTreeViewPrivate *p;
    g_return_val_if_fail (GNC_IS_TREE_VIEW (view), FALSE);
    p = gnc_tree_view_get_instance_private (view);
    return p->show_column_menu;
}
