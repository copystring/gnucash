#ifndef GNC_DIALOG_CHOOSE_OWNER_H_
#define GNC_DIALOG_CHOOSE_OWNER_H_

#include "Split.h"

typedef void (*GncSplitAssignOwnerCallback) (Split *split, gboolean assigned,
                                             gpointer user_data);

/* Presents a non-blocking owner selection. The callback is invoked only while
 * the original split GUID still resolves in the original book. */
void gnc_split_assign_owner_async (GtkWindow *parent, Split *split,
                                   GncSplitAssignOwnerCallback callback,
                                   gpointer user_data,
                                   GDestroyNotify destroy);

#endif