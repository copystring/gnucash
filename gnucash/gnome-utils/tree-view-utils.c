#include <config.h>
#include "tree-view-utils.h"
void
tree_view_column_set_default_width (GtkColumnViewColumn *column, const gchar *sizing_text)
{
    g_return_if_fail (GTK_IS_COLUMN_VIEW_COLUMN (column));
    /* Conservative average glyph width preserves the old sizing hint without a widget-specific Pango layout. */ gtk_column_view_column_set_fixed_width (column, MAX (48, (gint)g_utf8_strlen (sizing_text? sizing_text: "", -1) * 8 + 10));
}
