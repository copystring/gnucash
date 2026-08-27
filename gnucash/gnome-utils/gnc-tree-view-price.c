#include <config.h>
#include <stdarg.h>
#include <glib/gi18n.h>
#include "gnc-tree-view-price.h"
#include "gnc-tree-model-price.h"
#include "gnc-engine.h"
#include "gnc-string-utils.h"
struct _GncTreeViewPrice
{
    GncTreeView parent_instance;
};
typedef struct
{
    GncTreeModelPrice *model;
    GListStore *roots;
    GtkTreeListModel *rows;
    GtkMultiSelection *selection;
    GHashTable *selected;
    GHashTable *expanded;
    gnc_tree_view_price_ns_filter_func ns_filter;
    gnc_tree_view_price_cm_filter_func cm_filter;
    gnc_tree_view_price_pc_filter_func pc_filter;
    gpointer filter_data;
    GDestroyNotify filter_destroy;
    GncTreeModelPriceColumn sort_column;
    GtkSortType sort_order;
    guint restore_source;
    guint suspended;
    gboolean dirty;
    gboolean synchronizing;
} GncTreeViewPricePrivate;
typedef struct
{
    GncTreeViewPrice *view;
    GncTreeModelPriceColumn column;
    gboolean tree;
    gchar *id;
} PriceColumn;
G_DEFINE_TYPE_WITH_PRIVATE (GncTreeViewPrice, gnc_tree_view_price, GNC_TYPE_TREE_VIEW)

static GncTreeViewPricePrivate *
priv (GncTreeViewPrice *view)
{
    return gnc_tree_view_price_get_instance_private (view);
}
static GncTreeModelPriceRow *
row_from_item (gpointer item)
{
    return GTK_IS_TREE_LIST_ROW (item)? GNC_TREE_MODEL_PRICE_ROW (gtk_tree_list_row_get_item (GTK_TREE_LIST_ROW (item))): NULL;
}
static gboolean
row_visible (GncTreeViewPricePrivate *p, GncTreeModelPriceRow *row)
{
    switch (gnc_tree_model_price_row_get_kind (row))
    {
        case GNC_TREE_MODEL_PRICE_ROW_NAMESPACE: return !p->ns_filter || p->ns_filter (gnc_tree_model_price_row_get_namespace (row), p->filter_data);
        case GNC_TREE_MODEL_PRICE_ROW_COMMODITY: return !p->cm_filter || p->cm_filter (gnc_tree_model_price_row_get_commodity (row), p->filter_data);
        case GNC_TREE_MODEL_PRICE_ROW_PRICE: return !p->pc_filter || p->pc_filter (gnc_tree_model_price_row_get_price (row), p->filter_data);
        default: return FALSE;
    }
}
static gint
compare_prices (GNCPrice *a, GNCPrice *b, GncTreeModelPriceColumn column)
{
    gint result = 0;
    if (column == GNC_TREE_MODEL_PRICE_COL_DATE)
    {
        time64 ta = gnc_price_get_time64 (a), tb = gnc_price_get_time64 (b);
        result = ta < tb? 1: ta > tb? -1: 0;
    }
    else if (column == GNC_TREE_MODEL_PRICE_COL_SOURCE) result = (gint)gnc_price_get_source (a) - (gint)gnc_price_get_source (b);
    else if (column == GNC_TREE_MODEL_PRICE_COL_TYPE) result = safe_utf8_collate (gnc_price_get_typestr (a), gnc_price_get_typestr (b));
    else if (column == GNC_TREE_MODEL_PRICE_COL_VALUE) result = gnc_numeric_compare (gnc_price_get_value (a), gnc_price_get_value (b));
    if (result) return result;
    gnc_commodity *ca = gnc_price_get_currency (a), *cb = gnc_price_get_currency (b);
    result = safe_utf8_collate (ca? gnc_commodity_get_unique_name (ca): "", cb? gnc_commodity_get_unique_name (cb): "");
    if (result) return result;
    return gnc_numeric_compare (gnc_price_get_value (a), gnc_price_get_value (b));
}
static gint
row_compare (gconstpointer left, gconstpointer right, gpointer data)
{
    GncTreeViewPricePrivate *p = data;
    GncTreeModelPriceRow *a = GNC_TREE_MODEL_PRICE_ROW ((gpointer)left), *b = GNC_TREE_MODEL_PRICE_ROW ((gpointer)right);
    gint result;
    if (gnc_tree_model_price_row_get_kind (a) == GNC_TREE_MODEL_PRICE_ROW_PRICE && gnc_tree_model_price_row_get_kind (b) == GNC_TREE_MODEL_PRICE_ROW_PRICE) result = compare_prices (gnc_tree_model_price_row_get_price (a), gnc_tree_model_price_row_get_price (b), p->sort_column);
    else
    {
        gchar *sa = gnc_tree_model_price_row_get_string (a, GNC_TREE_MODEL_PRICE_COL_COMMODITY);
        gchar *sb = gnc_tree_model_price_row_get_string (b, GNC_TREE_MODEL_PRICE_COL_COMMODITY);
        result = g_utf8_collate (sa, sb);
        g_free (sa);
        g_free (sb);
    }
    return p->sort_order == GTK_SORT_DESCENDING? -result: result;
}
static void
append_sorted_visible (GncTreeViewPricePrivate *p, GListStore *store, GListModel *source)
{
    for (guint i = 0; i < g_list_model_get_n_items (source); i++)
    {
        GncTreeModelPriceRow *row = g_list_model_get_item (source, i);
        if (row_visible (p, row)) g_list_store_insert_sorted (store, row, row_compare, p);
        g_object_unref (row);
    }
}
static GListModel *
create_children (gpointer item, gpointer user_data)
{
    GncTreeViewPrice *view = GNC_TREE_VIEW_PRICE (user_data);
    GncTreeViewPricePrivate *p = priv (view);
    GListModel *source = gnc_tree_model_price_row_get_children (GNC_TREE_MODEL_PRICE_ROW (item));
    GListStore *children;
    if (!source || g_list_model_get_n_items (source) == 0) return NULL;
    children = g_list_store_new (GNC_TYPE_TREE_MODEL_PRICE_ROW);
    append_sorted_visible (p, children, source);
    if (g_list_model_get_n_items (G_LIST_MODEL (children)) == 0)
    {
        g_object_unref (children);
        return NULL;
    }
    return G_LIST_MODEL (children);
}
static void
rebuild_roots (GncTreeViewPrice *view)
{
    GncTreeViewPricePrivate *p = priv (view);
    if (!p->roots || !p->model) return;
    p->synchronizing = TRUE;
    if (p->selection)
        gtk_selection_model_unselect_all (GTK_SELECTION_MODEL (p->selection));
    g_list_store_remove_all (p->roots);
    append_sorted_visible (p, p->roots, gnc_tree_model_price_get_roots (p->model));
}
static gboolean
restore_state (gpointer data)
{
    GncTreeViewPrice *view = GNC_TREE_VIEW_PRICE (data);
    GncTreeViewPricePrivate *p = priv (view);
    gboolean changed = FALSE;
    for (guint i = 0; i < g_list_model_get_n_items (G_LIST_MODEL (p->rows)); i++)
    {
        GtkTreeListRow *tr = gtk_tree_list_model_get_row (p->rows, i);
        GncTreeModelPriceRow *row = row_from_item (tr);
        if (row && gtk_tree_list_row_is_expandable (tr) && g_hash_table_contains (p->expanded, gnc_tree_model_price_row_get_id (row)) && !gtk_tree_list_row_get_expanded (tr))
        {
            gtk_tree_list_row_set_expanded (tr, TRUE);
            changed = TRUE;
        }
        if (row && g_hash_table_contains (p->selected, gnc_tree_model_price_row_get_id (row))) gtk_selection_model_select_item (GTK_SELECTION_MODEL (p->selection), i, FALSE);
        g_object_unref (tr);
    }
    if (changed) return G_SOURCE_CONTINUE;
    p->synchronizing = FALSE;
    p->restore_source = 0;
    return G_SOURCE_REMOVE;
}
static void
schedule_restore (GncTreeViewPrice *view)
{
    GncTreeViewPricePrivate *p = priv (view);
    if (!p->restore_source) p->restore_source = g_idle_add (restore_state, view);
}
static void
model_changed (GncTreeModelPrice *model, GncTreeViewPrice *view)
{
    GncTreeViewPricePrivate *p = priv (view);
    (void)model;
    if (p->suspended)
    {
        p->dirty = TRUE;
        return;
    }
    rebuild_roots (view);
    schedule_restore (view);
}
static void
selection_changed (GtkSelectionModel *selection, guint position, guint n_items, GncTreeViewPrice *view)
{
    GncTreeViewPricePrivate *p = priv (view);
    if (p->synchronizing) return;
    g_hash_table_remove_all (p->selected);
    for (guint i = 0; i < g_list_model_get_n_items (G_LIST_MODEL (p->rows)); i++) if (gtk_selection_model_is_selected (selection, i))
    {
        GtkTreeListRow *tr = gtk_tree_list_model_get_row (p->rows, i);
        GncTreeModelPriceRow *row = row_from_item (tr);
        if (row) g_hash_table_add (p->selected, g_strdup (gnc_tree_model_price_row_get_id (row)));
        g_object_unref (tr);
    }
    (void)position;
    (void)n_items;
}
static void
row_expanded (GtkTreeListRow *tr, GParamSpec *pspec, GncTreeViewPrice *view)
{
    GncTreeViewPricePrivate *p = priv (view);
    GncTreeModelPriceRow *row = row_from_item (tr);
    if (row && !p->synchronizing)
    {
        const gchar *id = gnc_tree_model_price_row_get_id (row);
        if (gtk_tree_list_row_get_expanded (tr)) g_hash_table_add (p->expanded, g_strdup (id));
        else g_hash_table_remove (p->expanded, id);
    }
    (void)pspec;
}
static void
factory_setup (GtkSignalListItemFactory *factory, GtkListItem *item, PriceColumn *column)
{
    GtkWidget *label = gtk_label_new (NULL);
    gtk_widget_set_halign (label, GTK_ALIGN_START);
    if (column->tree)
    {
        GtkWidget *expander = gtk_tree_expander_new ();
        gtk_tree_expander_set_child (GTK_TREE_EXPANDER (expander), label);
        gtk_list_item_set_child (item, expander);
    }
    else gtk_list_item_set_child (item, label);
    (void)factory;
}
static void
factory_bind (GtkSignalListItemFactory *factory, GtkListItem *item, PriceColumn *column)
{
    GtkTreeListRow *tr = GTK_TREE_LIST_ROW (gtk_list_item_get_item (item));
    GncTreeModelPriceRow *row = row_from_item (tr);
    GtkWidget *child = gtk_list_item_get_child (item);
    GtkWidget *label = column->tree? gtk_tree_expander_get_child (GTK_TREE_EXPANDER (child)): child;
    gchar *text = gnc_tree_model_price_row_get_string (row, column->column);
    if (column->tree) gtk_tree_expander_set_list_row (GTK_TREE_EXPANDER (child), tr);
    gtk_label_set_text (GTK_LABEL (label), text);
    g_free (text);
    g_signal_connect_object (tr, "notify::expanded", G_CALLBACK (row_expanded), column->view, 0);
    (void)factory;
}
static void
factory_unbind (GtkSignalListItemFactory *factory, GtkListItem *item, PriceColumn *column)
{
    GtkTreeListRow *tr = GTK_TREE_LIST_ROW (gtk_list_item_get_item (item));
    g_signal_handlers_disconnect_by_func (tr, row_expanded, column->view);
    (void)factory;
}
static GtkOrdering
sorter_cb (gconstpointer left, gconstpointer right, gpointer user_data)
{
    PriceColumn *column = user_data;
    GncTreeModelPriceRow *a = row_from_item ((gpointer)left), *b = row_from_item ((gpointer)right);
    gint result = row_compare (a, b, priv (column->view));
    return result < 0? GTK_ORDERING_SMALLER: result > 0? GTK_ORDERING_LARGER: GTK_ORDERING_EQUAL;
}
static void
sort_changed (GtkColumnViewColumn *column_view, GParamSpec *pspec, PriceColumn *column)
{
    GtkSortType order = GTK_SORT_ASCENDING;
    GncTreeViewPricePrivate *p = priv (column->view);
    g_object_get (column_view, "sort-order", &order, NULL);
    p->sort_column = column->column;
    p->sort_order = order;
    rebuild_roots (column->view);
    schedule_restore (column->view);
    (void)pspec;
}
static GtkColumnViewColumn *
add_column (GncTreeViewPrice *view, const gchar *title, const gchar *id, GncTreeModelPriceColumn value, gboolean tree, gboolean visible)
{
    PriceColumn *data = g_new0 (PriceColumn, 1);
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new ();
    GtkCustomSorter *sorter;
    GtkColumnViewColumn *column;
    data->view = view;
    data->column = value;
    data->tree = tree;
    data->id = g_strdup (id);
    g_signal_connect (factory, "setup", G_CALLBACK (factory_setup), data);
    g_signal_connect (factory, "bind", G_CALLBACK (factory_bind), data);
    g_signal_connect (factory, "unbind", G_CALLBACK (factory_unbind), data);
    column = gtk_column_view_column_new (title, factory);
    gtk_column_view_column_set_id (column, id);
    gtk_column_view_column_set_resizable (column, TRUE);
    gtk_column_view_column_set_expand (column, tree);
    gtk_column_view_column_set_visible (column, visible);
    sorter = gtk_custom_sorter_new (sorter_cb, data, NULL);
    gtk_column_view_column_set_sorter (column, GTK_SORTER (sorter));
    g_signal_connect (column, "notify::sort-order", G_CALLBACK (sort_changed), data);
    g_object_set_data_full (G_OBJECT (column), "gnc-price-column", data, (GDestroyNotify)g_free);
    gtk_column_view_append_column (gnc_tree_view_get_column_view (GNC_TREE_VIEW (view)), column);
    g_object_unref (sorter);
    return column;
}
static void
view_dispose (GObject *object)
{
    GncTreeViewPrice *view = GNC_TREE_VIEW_PRICE (object);
    GncTreeViewPricePrivate *p = priv (view);
    if (p->restore_source) g_source_remove (p->restore_source);
    p->restore_source = 0;
    if (p->filter_destroy) p->filter_destroy (p->filter_data);
    p->filter_destroy = NULL;
    g_clear_pointer (&p->selected, g_hash_table_unref);
    g_clear_pointer (&p->expanded, g_hash_table_unref);
    g_clear_object (&p->selection);
    g_clear_object (&p->rows);
    g_clear_object (&p->roots);
    g_clear_object (&p->model);
    G_OBJECT_CLASS (gnc_tree_view_price_parent_class)->dispose (object);
}
static void
gnc_tree_view_price_class_init (GncTreeViewPriceClass *klass)
{
    G_OBJECT_CLASS (klass)->dispose = view_dispose;
}
static void
gnc_tree_view_price_init (GncTreeViewPrice *view)
{
    GncTreeViewPricePrivate *p = priv (view);
    p->selected = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    p->expanded = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    p->sort_column = GNC_TREE_MODEL_PRICE_COL_COMMODITY;
    p->sort_order = GTK_SORT_ASCENDING;
}
GtkWidget *
gnc_tree_view_price_new (QofBook *book, const gchar *first_property_name, ...)
{
    GncTreeViewPrice *view = g_object_new (GNC_TYPE_TREE_VIEW_PRICE, "name", "gnc-id-price-tree", NULL);
    GncTreeViewPricePrivate *p = priv (view);
    va_list args;
    p->model = gnc_tree_model_price_new (book, gnc_pricedb_get_db (book));
    p->roots = g_list_store_new (GNC_TYPE_TREE_MODEL_PRICE_ROW);
    rebuild_roots (view);
    p->rows = gtk_tree_list_model_new (G_LIST_MODEL (p->roots), FALSE, FALSE, create_children, view, NULL);
    p->selection = gtk_multi_selection_new (G_LIST_MODEL (p->rows));
    gtk_column_view_set_model (gnc_tree_view_get_column_view (GNC_TREE_VIEW (view)), GTK_SELECTION_MODEL (p->selection));
    add_column (view, _("Security"), "security", GNC_TREE_MODEL_PRICE_COL_COMMODITY, TRUE, TRUE);
    add_column (view, _("Currency"), "currency", GNC_TREE_MODEL_PRICE_COL_CURRENCY, FALSE, TRUE);
    add_column (view, _("Date"), "date", GNC_TREE_MODEL_PRICE_COL_DATE, FALSE, TRUE);
    add_column (view, _("Source"), "source", GNC_TREE_MODEL_PRICE_COL_SOURCE, FALSE, TRUE);
    add_column (view, _("Type"), "type", GNC_TREE_MODEL_PRICE_COL_TYPE, FALSE, TRUE);
    add_column (view, _("Price"), "price", GNC_TREE_MODEL_PRICE_COL_VALUE, FALSE, TRUE);
    va_start (args, first_property_name);
    g_object_set_valist (G_OBJECT (view), first_property_name, args);
    va_end (args);
    g_signal_connect_object (p->selection, "selection-changed", G_CALLBACK (selection_changed), view, 0);
    g_signal_connect_object (p->model, "changed", G_CALLBACK (model_changed), view, 0);
    return GTK_WIDGET (view);
}
GtkColumnView *
gnc_tree_view_price_get_column_view (GncTreeViewPrice *view)
{
    g_return_val_if_fail (GNC_IS_TREE_VIEW_PRICE (view), NULL);
    return gnc_tree_view_get_column_view (GNC_TREE_VIEW (view));
}
GtkSelectionModel *
gnc_tree_view_price_get_selection_model (GncTreeViewPrice *view)
{
    g_return_val_if_fail (GNC_IS_TREE_VIEW_PRICE (view), NULL);
    return GTK_SELECTION_MODEL (priv (view)->selection);
}
void
gnc_tree_view_price_set_filter (GncTreeViewPrice *view, gnc_tree_view_price_ns_filter_func ns, gnc_tree_view_price_cm_filter_func cm, gnc_tree_view_price_pc_filter_func pc, gpointer data, GDestroyNotify destroy)
{
    GncTreeViewPricePrivate *p = priv (view);
    if (p->filter_destroy) p->filter_destroy (p->filter_data);
    p->ns_filter = ns;
    p->cm_filter = cm;
    p->pc_filter = pc;
    p->filter_data = data;
    p->filter_destroy = destroy;
    rebuild_roots (view);
    schedule_restore (view);
}
void
gnc_tree_view_price_suspend_updates (GncTreeViewPrice *view)
{
    g_return_if_fail (GNC_IS_TREE_VIEW_PRICE (view));
    priv (view)->suspended++;
}
void
gnc_tree_view_price_resume_updates (GncTreeViewPrice *view)
{
    GncTreeViewPricePrivate *p;
    g_return_if_fail (GNC_IS_TREE_VIEW_PRICE (view));
    p = priv (view);
    if (!p->suspended) return;
    if (--p->suspended == 0 && p->dirty)
    {
        p->dirty = FALSE;
        rebuild_roots (view);
        schedule_restore (view);
    }
}
void
gnc_tree_view_price_toggle_expand (GncTreeViewPrice *view, guint position)
{
    GncTreeViewPricePrivate *p = priv (view);
    GtkTreeListRow *row;
    if (position >= g_list_model_get_n_items (G_LIST_MODEL (p->rows))) return;
    row = gtk_tree_list_model_get_row (p->rows, position);
    if (gtk_tree_list_row_is_expandable (row)) gtk_tree_list_row_set_expanded (row, !gtk_tree_list_row_get_expanded (row));
    g_object_unref (row);
}
static GncTreeModelPriceRow *
first_selected (GncTreeViewPrice *view)
{
    GncTreeViewPricePrivate *p = priv (view);
    for (guint i = 0; i < g_list_model_get_n_items (G_LIST_MODEL (p->rows)); i++) if (gtk_selection_model_is_selected (GTK_SELECTION_MODEL (p->selection), i))
    {
        GtkTreeListRow *tr = gtk_tree_list_model_get_row (p->rows, i);
        GncTreeModelPriceRow *row = tr? g_object_ref (row_from_item (tr)): NULL;
        g_clear_object (&tr);
        return row;
    }
    return NULL;
}
GNCPrice *
gnc_tree_view_price_get_cursor_price (GncTreeViewPrice *view)
{
    GncTreeModelPriceRow *row = first_selected (view);
    GNCPrice *price = row? gnc_tree_model_price_row_get_price (row): NULL;
    g_clear_object (&row);
    return price;
}
GNCPrice *
gnc_tree_view_price_get_selected_price (GncTreeViewPrice *view)
{
    return gnc_tree_view_price_get_cursor_price (view);
}
void
gnc_tree_view_price_set_selected_price (GncTreeViewPrice *view, GNCPrice *price)
{
    GncTreeViewPricePrivate *p = priv (view);
    gchar guid[GUID_ENCODING_LENGTH + 1];
    gnc_commodity *commodity;
    gnc_commodity_namespace *ns;
    if (!price)
    {
        g_hash_table_remove_all (p->selected);
        schedule_restore (view);
        return;
    }
    guid_to_string_buff (gnc_price_get_guid (price), guid);
    g_hash_table_remove_all (p->selected);
    g_hash_table_add (p->selected, g_strconcat ("price:", guid, NULL));
    commodity = gnc_price_get_commodity (price);
    if (commodity)
    {
        guid_to_string_buff (qof_instance_get_guid (QOF_INSTANCE (commodity)), guid);
        g_hash_table_add (p->expanded, g_strconcat ("commodity:", guid, NULL));
        ns = gnc_commodity_get_namespace_ds (commodity);
        g_hash_table_add (p->expanded, g_strconcat ("namespace:", gnc_commodity_namespace_get_name (ns), NULL));
    }
    schedule_restore (view);
}
GList *
gnc_tree_view_price_get_selected_prices (GncTreeViewPrice *view)
{
    GncTreeViewPricePrivate *p = priv (view);
    GList *result = NULL;
    for (guint i = 0; i < g_list_model_get_n_items (G_LIST_MODEL (p->rows)); i++) if (gtk_selection_model_is_selected (GTK_SELECTION_MODEL (p->selection), i))
    {
        GtkTreeListRow *tr = gtk_tree_list_model_get_row (p->rows, i);
        GncTreeModelPriceRow *row = row_from_item (tr);
        GNCPrice *price = row? gnc_tree_model_price_row_get_price (row): NULL;
        if (price) result = g_list_prepend (result, price);
        g_object_unref (tr);
    }
    return g_list_reverse (result);
}
GList *
gnc_tree_view_price_get_selected_commodities (GncTreeViewPrice *view)
{
    GncTreeViewPricePrivate *p = priv (view);
    GList *result = NULL;
    for (guint i = 0; i < g_list_model_get_n_items (G_LIST_MODEL (p->rows)); i++) if (gtk_selection_model_is_selected (GTK_SELECTION_MODEL (p->selection), i))
    {
        GtkTreeListRow *tr = gtk_tree_list_model_get_row (p->rows, i);
        GncTreeModelPriceRow *row = row_from_item (tr);
        gnc_commodity *commodity = row? gnc_tree_model_price_row_get_commodity (row): NULL;
        if (commodity) result = g_list_prepend (result, commodity);
        g_object_unref (tr);
    }
    return g_list_reverse (result);
}
