#ifndef TREE_VIEW_UTILS_H_
#define TREE_VIEW_UTILS_H_
#include <gtk/gtk.h>
G_BEGIN_DECLS
/* Apply a content-derived fixed width to a GTK4 ColumnView column. */
void tree_view_column_set_default_width (GtkColumnViewColumn *column, const gchar *sizing_text);
G_END_DECLS
#endif