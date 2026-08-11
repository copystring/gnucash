/* GTK4 owner list model. */
#ifndef __GNC_TREE_MODEL_OWNER_H
#define __GNC_TREE_MODEL_OWNER_H

#include <gtk/gtk.h>
#include "gncOwner.h"

G_BEGIN_DECLS

#define GNC_TYPE_TREE_MODEL_OWNER (gnc_tree_model_owner_get_type ())
G_DECLARE_FINAL_TYPE (GncTreeModelOwner, gnc_tree_model_owner, GNC, TREE_MODEL_OWNER, GObject)

GncTreeModelOwner *gnc_tree_model_owner_new (GncOwnerType owner_type);
GListModel *gnc_tree_model_owner_get_model (GncTreeModelOwner *model);
GncOwner *gnc_tree_model_owner_get_row_owner (GObject *row);
guint gnc_tree_model_owner_find_owner (GncTreeModelOwner *model,
                                       const GncOwner *owner);

G_END_DECLS
#endif