/********************************************************************
 * test-gnc-html-url-policy.cpp -- Report navigation policy tests   *
 *                                                                  *
 * Copyright 2026 GnuCash Contributors                              *
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
 ********************************************************************/

#include <gtk/gtk.h>

#include "gnc-html.h"

static void
test_internal_url_types (void)
{
    const URLType types[] =
    {
        URL_TYPE_REGISTER,
        URL_TYPE_ACCTTREE,
        URL_TYPE_REPORT,
        URL_TYPE_OPTIONS,
        URL_TYPE_SCHEME,
        URL_TYPE_HELP,
        URL_TYPE_XMLDATA,
        URL_TYPE_PRICE,
        URL_TYPE_BUDGET,
    };

    for (guint index = 0; index < G_N_ELEMENTS (types); index++)
        g_assert_true (gnc_html_urltype_is_internal (types[index]));
}

static void
test_renderer_url_types_are_not_internal (void)
{
    const URLType types[] =
    {
        URL_TYPE_FILE,
        URL_TYPE_JUMP,
        URL_TYPE_HTTP,
        URL_TYPE_FTP,
        URL_TYPE_SECURE,
        URL_TYPE_OTHER,
        "mailto",
        "javascript",
    };

    for (guint index = 0; index < G_N_ELEMENTS (types); index++)
        g_assert_false (gnc_html_urltype_is_internal (types[index]));
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/html/url-policy/internal", test_internal_url_types);
    g_test_add_func ("/html/url-policy/renderer",
                     test_renderer_url_types_are_not_internal);

    return g_test_run ();
}
