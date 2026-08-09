/********************************************************************\
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
\********************************************************************/
/** @addtogroup Import_Export
    @{ */
/**@internal
 @file import-commodity-matcher.c
  @brief  A Generic commodity matcher/picker
  @author Copyright (C) 2002 Benoit Grégoire <bock@step.polymtl.ca>
 */
#include <config.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <stdlib.h>
#include <math.h>

#include "import-commodity-matcher.h"
#include "Account.h"
#include "Transaction.h"
#include "dialog-commodity.h"
#include "gnc-engine.h"
#include "gnc-ui-util.h"

/********************************************************************\
 *   Constants   *
\********************************************************************/


/********************************************************************\
 *   Constants, should ideally be defined a user preference dialog    *
\********************************************************************/

static QofLogModule log_module = GNC_MOD_IMPORT;



gnc_commodity *
gnc_import_find_commodity_by_cusip (const char *cusip)
{
    const gnc_commodity_table *commodity_table = gnc_get_current_commodities ();
    gnc_commodity *commodity = nullptr;

    g_return_val_if_fail (cusip, nullptr);
    g_assert (commodity_table);
    for (auto namespaces = gnc_commodity_table_get_namespaces (commodity_table);
         namespaces && !commodity; namespaces = g_list_next (namespaces))
    {
        auto name_space = static_cast<const char *> (namespaces->data);
        auto commodities = gnc_commodity_table_get_commodities (commodity_table, name_space);
        for (auto node = commodities; node && !commodity; node = g_list_next (node))
        {
            auto candidate = static_cast<gnc_commodity *> (node->data);
            if (!g_strcmp0 (gnc_commodity_get_cusip (candidate), cusip))
                commodity = candidate;
        }
        g_list_free (commodities);
    }
    return commodity;
}

static void
set_commodity_cusip (gnc_commodity *commodity, const char *cusip)
{
    if (!commodity || !cusip)
        return;
    if (g_strcmp0 (gnc_commodity_get_cusip (commodity), cusip))
        gnc_commodity_set_cusip (commodity, cusip);
}

struct CommoditySelectionRequest
{
    gchar *cusip;
    GncImportCommoditySelectedCB callback;
    gpointer user_data;
};

static void
commodity_selection_finished (gnc_commodity *commodity, gpointer user_data)
{
    auto request = static_cast<CommoditySelectionRequest *> (user_data);
    if (commodity)
        set_commodity_cusip (commodity, request->cusip);
    auto callback = request->callback;
    auto callback_data = request->user_data;
    g_free (request->cusip);
    g_free (request);
    if (callback)
        callback (commodity, commodity != nullptr, callback_data);
}


void
gnc_import_select_commodity_async (GtkWidget *parent, const char *cusip,
                                    gboolean ask_on_unknown,
                                    const char *default_fullname,
                                    const char *default_mnemonic,
                                    GCancellable *cancellable,
                                    GncImportCommoditySelectedCB callback,
                                    gpointer user_data)
{
    auto commodity = gnc_import_find_commodity_by_cusip (cusip);
    if (commodity || !ask_on_unknown)
    {
        set_commodity_cusip (commodity, cusip);
        if (callback)
            callback (commodity, commodity != nullptr, user_data);
        return;
    }

    auto request = g_new0 (CommoditySelectionRequest, 1);
    request->cusip = g_strdup (cusip);
    request->callback = callback;
    request->user_data = user_data;
    const gchar *message =
        _("Please select a commodity to match the following exchange "
          "specific code. Please note that the exchange code of the "
          "commodity you select will be overwritten.");
    gnc_ui_select_commodity_async_full (nullptr, parent, DIAG_COMM_ALL, message,
                                        cusip, default_fullname, default_mnemonic,
                                        cancellable, commodity_selection_finished,
                                        request);
}/**@}*/
