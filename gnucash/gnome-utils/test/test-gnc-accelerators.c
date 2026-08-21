/********************************************************************\
 * test-gnc-accelerators.c -- GTK4 accelerator override tests       *
 * Copyright (C) 2026 GnuCash Developers                            *
 *                                                                  *
 * This program is free software: you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
\********************************************************************/

#include <config.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "gnc-gtk-utils.h"

static void
test_legacy_accelerator_map (void)
{
    const gchar *contents =
        "; GTK3 accelerator map\n"
        "(gtk_accel_path \"<Actions>/gnc-plugin-basic-commands-actions/FileOpenAction\" \"<Control><Shift>o\")\n"
        "(gtk_accel_path \"<Actions>/gnc-plugin-basic-commands-actions/FileSaveAction\" \"\")\n"
        "(gtk_accel_path \"invalid\" \"<Control>i\")\n";
    const gchar *accelerator = NULL;
    gchar *filename = NULL;
    GError *error = NULL;
    gint fd;

    fd = g_file_open_tmp ("gnc-accelerator-map-XXXXXX", &filename, &error);
    g_assert_no_error (error);
    g_assert_cmpint (fd, >=, 0);
    g_assert_true (g_close (fd, &error));
    g_assert_no_error (error);
    g_assert_true (g_file_set_contents (filename, contents, -1, &error));
    g_assert_no_error (error);

    gnc_accelerator_overrides_load_legacy_map (filename);

    g_assert_true (gnc_accelerator_overrides_lookup (
                       "gnc-plugin-basic-commands-actions.FileOpenAction",
                       &accelerator));
    g_assert_cmpstr (accelerator, ==, "<Control><Shift>o");
    g_assert_true (gnc_accelerator_overrides_lookup (
                       "gnc-plugin-basic-commands-actions.FileSaveAction",
                       &accelerator));
    g_assert_cmpstr (accelerator, ==, "");
    g_assert_false (gnc_accelerator_overrides_lookup (
                        "gnc-plugin-basic-commands-actions.FileNewAction",
                        &accelerator));

    gnc_accelerator_overrides_clear ();
    g_assert_cmpint (g_remove (filename), ==, 0);
    g_free (filename);
}

static void
test_texture_from_pixbuf (void)
{
    GdkPixbuf *pixbuf;
    GdkTexture *texture;

    pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 1, 1);
    g_assert_nonnull (pixbuf);
    gdk_pixbuf_fill (pixbuf, 0xff0000ff);

    texture = gnc_texture_new_from_pixbuf (pixbuf);
    g_assert_nonnull (texture);
    g_assert_cmpint (gdk_texture_get_width (texture), ==, 1);
    g_assert_cmpint (gdk_texture_get_height (texture), ==, 1);

    g_object_unref (texture);
    g_object_unref (pixbuf);
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/gnome-utils/accelerators/legacy-map",
                     test_legacy_accelerator_map);
    g_test_add_func ("/gnome-utils/texture/pixbuf",
                     test_texture_from_pixbuf);

    return g_test_run ();
}
