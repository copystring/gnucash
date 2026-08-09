/********************************************************************\
 * FileDialog.c -- file-handling utility dialogs for gnucash.       *
 *                                                                  *
 * Copyright (C) 1997 Robin D. Clark                                *
 * Copyright (C) 1998, 1999, 2000 Linas Vepstas                     *
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
 * along with this program; if not, write to the Free Software      *
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.        *
\********************************************************************/

#include <config.h>

#include <stdbool.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <errno.h>
#include <string.h>

#include "dialog-utils.h"
#include "assistant-xml-encoding.h"
#include "gnc-backend-xml.h"
#include "gnc-commodity.h"
#include "gnc-component-manager.h"
#include "gnc-engine.h"
#include "Account.h"
#include "gnc-file.h"
#include "gnc-features.h"
#include "gnc-filepath-utils.h"
#include "gnc-string-utils.h"
#include "gnc-gui-query.h"
#include "gnc-hooks.h"
#include "gnc-keyring.h"
#include "gnc-splash.h"
#include "gnc-ui.h"
#include "gnc-ui-balances.h"
#include "gnc-ui-util.h"
#include "gnc-uri-utils.h"
#include "gnc-window.h"
#include "gnc-plugin-file-history.h"
#include "qof.h"
#include "Scrub.h"
#include "ScrubBudget.h"
#include "TransLog.h"
#include "gnc-session.h"
#include "gnc-state.h"
#include "gnc-autosave.h"
#include <gnc-sx-instance-model.h>
#include <SX-book.h>

/** GLOBALS *********************************************************/
/* This static indicates the debugging module that this .o belongs to.  */
static QofLogModule log_module = GNC_MOD_GUI;

static GNCShutdownCB shutdown_cb = NULL;
static gint save_in_progress = 0;


struct _GncFileDialogRequest
{
    GObject parent_instance;
    GWeakRef parent;
    gchar *title;
    GFile *initial_folder;
    GFile *initial_file;
    GListStore *filters;
    GtkFileFilter *default_filter;
    GNCFileDialogType type;
};

G_DEFINE_FINAL_TYPE (GncFileDialogRequest, gnc_file_dialog_request, G_TYPE_OBJECT)

static gboolean
file_dialog_type_is_valid (GNCFileDialogType type)
{
    return type >= GNC_FILE_DIALOG_OPEN && type <= GNC_FILE_DIALOG_EXPORT;
}

static gboolean
file_dialog_type_is_open (GNCFileDialogType type)
{
    return type == GNC_FILE_DIALOG_OPEN || type == GNC_FILE_DIALOG_IMPORT;
}

static const gchar *
file_dialog_default_title (GNCFileDialogType type)
{
    switch (type)
    {
    case GNC_FILE_DIALOG_OPEN:
        return _("Open");
    case GNC_FILE_DIALOG_IMPORT:
        return _("Import");
    case GNC_FILE_DIALOG_SAVE:
        return _("Save");
    case GNC_FILE_DIALOG_EXPORT:
        return _("Export");
    }

    g_assert_not_reached ();
    return NULL;
}

static const gchar *
file_dialog_accept_label (GNCFileDialogType type)
{
    switch (type)
    {
    case GNC_FILE_DIALOG_OPEN:
        return _("Open");
    case GNC_FILE_DIALOG_IMPORT:
        return _("Import");
    case GNC_FILE_DIALOG_SAVE:
        return _("Save");
    case GNC_FILE_DIALOG_EXPORT:
        return _("Export");
    }

    g_assert_not_reached ();
    return NULL;
}

static void
file_dialog_request_finalize (GObject *object)
{
    GncFileDialogRequest *request = GNC_FILE_DIALOG_REQUEST (object);

    g_weak_ref_clear (&request->parent);
    g_clear_pointer (&request->title, g_free);
    g_clear_object (&request->initial_folder);
    g_clear_object (&request->initial_file);
    g_clear_object (&request->default_filter);
    g_clear_object (&request->filters);

    G_OBJECT_CLASS (gnc_file_dialog_request_parent_class)->finalize (object);
}

static void
file_dialog_request_class_init (GncFileDialogRequestClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = file_dialog_request_finalize;
}

static void
file_dialog_request_init (GncFileDialogRequest *request)
{
    g_weak_ref_init (&request->parent, NULL);
}

static void
file_dialog_request_take_filters (GncFileDialogRequest *request, GList *filters)
{
    GtkFileFilter *all_filter;

    if (!filters)
        return;

    request->filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
    for (GList *node = filters; node; node = node->next)
    {
        GtkFileFilter *filter = GTK_FILE_FILTER (node->data);

        if (!GTK_IS_FILE_FILTER (filter))
        {
            g_warning ("Ignoring invalid file filter in dialog request");
            continue;
        }

        g_list_store_append (request->filters, filter);
        if (!request->default_filter)
            request->default_filter = g_object_ref (filter);
        g_object_unref (filter);
    }
    g_list_free (filters);

    all_filter = gtk_file_filter_new ();
    gtk_file_filter_set_name (all_filter, _("All files"));
    gtk_file_filter_add_pattern (all_filter, "*");
    g_list_store_append (request->filters, all_filter);
    g_object_unref (all_filter);
}

static GncFileDialogRequest *
file_dialog_request_new_internal (GtkWindow *parent, const gchar *title,
                                  GList *filters, GFile *initial_folder,
                                  GFile *initial_file, GNCFileDialogType type)
{
    GncFileDialogRequest *request;

    g_return_val_if_fail (!parent || GTK_IS_WINDOW (parent), NULL);
    g_return_val_if_fail (!initial_folder || G_IS_FILE (initial_folder), NULL);
    g_return_val_if_fail (!initial_file || G_IS_FILE (initial_file), NULL);
    g_return_val_if_fail (!initial_folder || !initial_file, NULL);
    g_return_val_if_fail (file_dialog_type_is_valid (type), NULL);

    request = g_object_new (GNC_TYPE_FILE_DIALOG_REQUEST, NULL);
    g_weak_ref_set (&request->parent, parent);
    request->title = g_strdup (title ? title : file_dialog_default_title (type));
    if (initial_folder)
        request->initial_folder = g_object_ref (initial_folder);
    if (initial_file)
        request->initial_file = g_object_ref (initial_file);
    request->type = type;
    file_dialog_request_take_filters (request, filters);

    return request;
}

GncFileDialogRequest *
gnc_file_dialog_request_new (GtkWindow *parent, const gchar *title,
                             GList *filters, const gchar *starting_dir,
                             GNCFileDialogType type)
{
    GFile *initial_folder = NULL;
    GncFileDialogRequest *request;

    if (starting_dir && *starting_dir)
        initial_folder = g_file_new_for_path (starting_dir);
    request = file_dialog_request_new_internal (parent, title, filters,
                                                initial_folder, NULL, type);
    g_clear_object (&initial_folder);
    return request;
}

GncFileDialogRequest *
gnc_file_dialog_request_new_for_folder (GtkWindow *parent, const gchar *title,
                                        GList *filters, GFile *initial_folder,
                                        GNCFileDialogType type)
{
    return file_dialog_request_new_internal (parent, title, filters,
                                             initial_folder, NULL, type);
}

GncFileDialogRequest *
gnc_file_dialog_request_new_for_file (GtkWindow *parent, const gchar *title,
                                      GList *filters, GFile *initial_file,
                                      GNCFileDialogType type)
{
    return file_dialog_request_new_internal (parent, title, filters, NULL,
                                             initial_file, type);
}

static GtkFileDialog *
file_dialog_request_create_dialog (GncFileDialogRequest *request)
{
    GtkFileDialog *dialog = gtk_file_dialog_new ();

    gtk_file_dialog_set_title (dialog, request->title);
    gtk_file_dialog_set_accept_label (dialog,
                                      file_dialog_accept_label (request->type));
    if (request->initial_file)
        gtk_file_dialog_set_initial_file (dialog, request->initial_file);
    else if (request->initial_folder)
        gtk_file_dialog_set_initial_folder (dialog, request->initial_folder);
    if (request->filters)
    {
        gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (request->filters));
        gtk_file_dialog_set_default_filter (dialog, request->default_filter);
    }

    return dialog;
}

static void
file_dialog_request_return_invalid_operation (GncFileDialogRequest *request,
                                              GCancellable *cancellable,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data,
                                              const gchar *operation)
{
    GTask *task = g_task_new (request, cancellable, callback, user_data);

    g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                             "%s is incompatible with this file dialog type",
                             operation);
    g_object_unref (task);
}

static void
file_dialog_request_open_finished (GObject *source, GAsyncResult *result,
                                   gpointer user_data)
{
    GTask *task = G_TASK (user_data);
    GError *error = NULL;
    GFile *file = gtk_file_dialog_open_finish (GTK_FILE_DIALOG (source), result,
                                               &error);

    if (file)
        g_task_return_pointer (task, file, g_object_unref);
    else if (error)
        g_task_return_error (task, error);
    else
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                                 "The file dialog returned no selected file");
    g_object_unref (task);
}

static void
file_dialog_request_save_finished (GObject *source, GAsyncResult *result,
                                   gpointer user_data)
{
    GTask *task = G_TASK (user_data);
    GError *error = NULL;
    GFile *file = gtk_file_dialog_save_finish (GTK_FILE_DIALOG (source), result,
                                               &error);

    if (file)
        g_task_return_pointer (task, file, g_object_unref);
    else if (error)
        g_task_return_error (task, error);
    else
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                                 "The file dialog returned no selected file");
    g_object_unref (task);
}

static void
file_dialog_request_open_multiple_finished (GObject *source,
                                            GAsyncResult *result,
                                            gpointer user_data)
{
    GTask *task = G_TASK (user_data);
    GError *error = NULL;
    GListModel *files = gtk_file_dialog_open_multiple_finish (
        GTK_FILE_DIALOG (source), result, &error);

    if (files)
        g_task_return_pointer (task, files, g_object_unref);
    else if (error)
        g_task_return_error (task, error);
    else
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_FAILED,
                                 "The file dialog returned no selected files");
    g_object_unref (task);
}

void
gnc_file_dialog_request_open_async (GncFileDialogRequest *request,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    GtkFileDialog *dialog;
    GtkWindow *parent;
    GTask *task;

    g_return_if_fail (GNC_IS_FILE_DIALOG_REQUEST (request));
    if (!file_dialog_type_is_open (request->type))
    {
        file_dialog_request_return_invalid_operation (request, cancellable,
                                                      callback, user_data,
                                                      "Opening files");
        return;
    }

    dialog = file_dialog_request_create_dialog (request);
    parent = g_weak_ref_get (&request->parent);
    task = g_task_new (request, cancellable, callback, user_data);
    gtk_file_dialog_open (dialog, parent, cancellable,
                          file_dialog_request_open_finished, task);
    g_clear_object (&parent);
    g_object_unref (dialog);
}

void
gnc_file_dialog_request_save_async (GncFileDialogRequest *request,
                                    GCancellable *cancellable,
                                    GAsyncReadyCallback callback,
                                    gpointer user_data)
{
    GtkFileDialog *dialog;
    GtkWindow *parent;
    GTask *task;

    g_return_if_fail (GNC_IS_FILE_DIALOG_REQUEST (request));
    if (file_dialog_type_is_open (request->type))
    {
        file_dialog_request_return_invalid_operation (request, cancellable,
                                                      callback, user_data,
                                                      "Saving files");
        return;
    }

    dialog = file_dialog_request_create_dialog (request);
    parent = g_weak_ref_get (&request->parent);
    task = g_task_new (request, cancellable, callback, user_data);
    gtk_file_dialog_save (dialog, parent, cancellable,
                          file_dialog_request_save_finished, task);
    g_clear_object (&parent);
    g_object_unref (dialog);
}

void
gnc_file_dialog_request_open_multiple_async (GncFileDialogRequest *request,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data)
{
    GtkFileDialog *dialog;
    GtkWindow *parent;
    GTask *task;

    g_return_if_fail (GNC_IS_FILE_DIALOG_REQUEST (request));
    if (!file_dialog_type_is_open (request->type))
    {
        file_dialog_request_return_invalid_operation (request, cancellable,
                                                      callback, user_data,
                                                      "Opening multiple files");
        return;
    }

    dialog = file_dialog_request_create_dialog (request);
    parent = g_weak_ref_get (&request->parent);
    task = g_task_new (request, cancellable, callback, user_data);
    gtk_file_dialog_open_multiple (dialog, parent, cancellable,
                                   file_dialog_request_open_multiple_finished,
                                   task);
    g_clear_object (&parent);
    g_object_unref (dialog);
}

GFile *
gnc_file_dialog_request_finish (GncFileDialogRequest *request,
                                GAsyncResult *result, GError **error)
{
    g_return_val_if_fail (GNC_IS_FILE_DIALOG_REQUEST (request), NULL);
    g_return_val_if_fail (g_task_is_valid (result, request), NULL);

    return g_task_propagate_pointer (G_TASK (result), error);
}

GListModel *
gnc_file_dialog_request_finish_multiple (GncFileDialogRequest *request,
                                         GAsyncResult *result, GError **error)
{
    g_return_val_if_fail (GNC_IS_FILE_DIALOG_REQUEST (request), NULL);
    g_return_val_if_fail (g_task_is_valid (result, request), NULL);

    return g_task_propagate_pointer (G_TASK (result), error);
}

GList*
gnc_file_dialog_get_datafile_filters (void)
{
    /* GtkFileDialog accepts GtkFileFilter patterns only. The explicit backup
     * filter retains direct access to timestamped copies; the datafile filter
     * is intentionally an extension filter because the native portal cannot
     * express the previous negative backup predicate. "All files" has always
     * remained available as an explicit final filter. */
    const char *datafiles = N_("GnuCash files (*.gnucash, *.xac)");
    const char *backups = N_("Backups only (*.gnucash.*.gnucash, *.xac.*.xac)");
    GtkFileFilter *filter;
    GList *filters = NULL;

    filter = gtk_file_filter_new ();
    gtk_file_filter_set_name (filter, _(datafiles));
    gtk_file_filter_add_pattern (filter, "*.gnucash");
    gtk_file_filter_add_pattern (filter, "*.xac");
    filters = g_list_append (filters, filter);

    filter = gtk_file_filter_new ();
    gtk_file_filter_set_name (filter, _(backups));
    gtk_file_filter_add_pattern (filter, "*.gnucash.??????????????.gnucash");
    gtk_file_filter_add_pattern (filter, "*.xac.??????????????.xac");
    filters = g_list_append (filters, filter);

    return filters;
}

typedef void (*GncFileSelectionFunc) (GtkWindow *parent,
                                      const gchar *filename,
                                      gpointer user_data);
typedef void (*GncFileSelectionCancelledFunc) (GtkWindow *parent,
                                               const GError *error,
                                               gpointer user_data);

typedef struct
{
    GWeakRef parent;
    gboolean has_parent;
    GncFileSelectionFunc selected;
    GncFileSelectionCancelledFunc cancelled;
    gpointer user_data;
    GDestroyNotify user_data_destroy;
} GncFileSelectionData;

static void
gnc_file_selection_data_free (GncFileSelectionData *data)
{
    if (data->user_data_destroy)
        data->user_data_destroy (data->user_data);
    g_weak_ref_clear (&data->parent);
    g_free (data);
}

static void
gnc_file_selection_finished (GObject *source, GAsyncResult *result,
                             gpointer user_data)
{
    GncFileSelectionData *data = user_data;
    GncFileDialogRequest *request = GNC_FILE_DIALOG_REQUEST (source);
    GError *error = NULL;
    GFile *file;
    GtkWindow *parent;

    file = gnc_file_dialog_request_finish (request, result, &error);
    parent = GTK_WINDOW (g_weak_ref_get (&data->parent));
    if (data->has_parent && !parent)
    {
        if (data->cancelled)
            data->cancelled (NULL, NULL, data->user_data);
    }
    else if (file)
    {
        gchar *filename = g_file_get_path (file);

        if (filename)
        {
            data->selected (parent, filename, data->user_data);
            g_free (filename);
        }
        else if (data->cancelled)
            data->cancelled (parent, NULL, data->user_data);
        else if (parent)
            gnc_error_dialog (parent, "%s", _("Please select a local file."));
    }
    else if (data->cancelled)
        data->cancelled (parent, error, data->user_data);
    else if (error && parent &&
             !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        gnc_error_dialog (parent, "%s", error->message);

    g_clear_object (&file);
    g_clear_error (&error);
    g_clear_object (&parent);
    gnc_file_selection_data_free (data);
}

static void
gnc_file_select_async_full (GtkWindow *parent, const gchar *title, GList *filters,
                            const gchar *starting_dir, GNCFileDialogType type,
                            GncFileSelectionFunc selected,
                            GncFileSelectionCancelledFunc cancelled,
                            gpointer user_data,
                            GDestroyNotify user_data_destroy)
{
    GncFileSelectionData *data;
    GncFileDialogRequest *request;

    g_return_if_fail (selected != NULL);

    data = g_new0 (GncFileSelectionData, 1);
    g_weak_ref_init (&data->parent, parent);
    data->has_parent = parent != NULL;
    data->selected = selected;
    data->cancelled = cancelled;
    data->user_data = user_data;
    data->user_data_destroy = user_data_destroy;
    request = gnc_file_dialog_request_new (parent, title, filters, starting_dir,
                                           type);
    if (type == GNC_FILE_DIALOG_OPEN || type == GNC_FILE_DIALOG_IMPORT)
        gnc_file_dialog_request_open_async (request, NULL,
                                            gnc_file_selection_finished, data);
    else
        gnc_file_dialog_request_save_async (request, NULL,
                                            gnc_file_selection_finished, data);
    g_object_unref (request);
}

static void
gnc_file_select_async (GtkWindow *parent, const gchar *title, GList *filters,
                       const gchar *starting_dir, GNCFileDialogType type,
                       GncFileSelectionFunc selected)
{
    gnc_file_select_async_full (parent, title, filters, starting_dir, type,
                                selected, NULL, NULL, NULL);
}
static void gnc_file_open_request_dialog (GtkWindow *parent,
                                          const gchar *starting_dir);
gboolean
show_session_error (GtkWindow *parent,
                    QofBackendError io_error,
                    const char *newfile,
                    GNCFileDialogType type)
{
    GtkWidget *dialog;
    gboolean uh_oh = TRUE;
    const char *fmt, *label;
    gchar *displayname;
    gint response;

    if (NULL == newfile)
    {
        displayname = g_strdup(_("(null)"));
    }
    else if (!gnc_uri_targets_local_fs (newfile)) /* Hide the db password in error messages */
        displayname = gnc_uri_normalize_uri ( newfile, FALSE);
    else
    {
        /* Strip the protocol from the file name and ensure absolute filename. */
        char *uri = gnc_uri_normalize_uri(newfile, FALSE);
        displayname = gnc_uri_get_path(uri);
        g_free(uri);
    }

    switch (io_error)
    {
    case ERR_BACKEND_NO_ERR:
        uh_oh = FALSE;
        break;

    case ERR_BACKEND_NO_HANDLER:
        fmt = _("No suitable backend was found for %s.");
        gnc_error_dialog(parent, fmt, displayname);
        break;

    case ERR_BACKEND_NO_BACKEND:
        fmt = _("The URL %s is not supported by this version of GnuCash.");
        gnc_error_dialog (parent, fmt, displayname);
        break;

    case ERR_BACKEND_BAD_URL:
        fmt = _("Can't parse the URL %s.");
        gnc_error_dialog (parent, fmt, displayname);
        break;

    case ERR_BACKEND_CANT_CONNECT:
        fmt = _("Can't connect to %s. "
                "The host, username or password were incorrect.");
        gnc_error_dialog (parent, fmt, displayname);
        break;

    case ERR_BACKEND_CONN_LOST:
        fmt = _("Can't connect to %s. "
                "Connection was lost, unable to send data.");
        gnc_error_dialog (parent, fmt, displayname);
        break;

    case ERR_BACKEND_TOO_NEW:
        fmt = _("This file/URL appears to be from a newer version "
                "of GnuCash. You must upgrade your version of GnuCash "
                "to work with this data.");
        gnc_error_dialog (parent, "%s", fmt);
        break;

    case ERR_BACKEND_NO_SUCH_DB:
        fmt = _("The database %s doesn't seem to exist. "
                "Do you want to create it?");
        if (gnc_verify_dialog (parent, TRUE, fmt, displayname))
        {
            uh_oh = FALSE;
        }
        break;

    case ERR_BACKEND_LOCKED:
        switch (type)
        {
        case GNC_FILE_DIALOG_OPEN:
        default:
            label = _("Open");
            fmt = _("GnuCash could not obtain the lock for %s. "
                    "That database may be in use by another user, "
                    "in which case you should not open the database. "
                    "Do you want to proceed with opening the database?");
            break;

        case GNC_FILE_DIALOG_IMPORT:
            label = _("Import");
            fmt = _("GnuCash could not obtain the lock for %s. "
                    "That database may be in use by another user, "
                    "in which case you should not import the database. "
                    "Do you want to proceed with importing the database?");
            break;

        case GNC_FILE_DIALOG_SAVE:
            label = _("Save");
            fmt = _("GnuCash could not obtain the lock for %s. "
                    "That database may be in use by another user, "
                    "in which case you should not save the database. "
                    "Do you want to proceed with saving the database?");
            break;

        case GNC_FILE_DIALOG_EXPORT:
            label = _("Export");
            fmt = _("GnuCash could not obtain the lock for %s. "
                    "That database may be in use by another user, "
                    "in which case you should not export the database. "
                    "Do you want to proceed with exporting the database?");
            break;
        }

        dialog = gtk_message_dialog_new(parent,
                                        GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_MESSAGE_QUESTION,
                                        GTK_BUTTONS_NONE,
                                        fmt,
                                        displayname);
        gtk_dialog_add_buttons(GTK_DIALOG(dialog),
                               _("_Cancel"), GTK_RESPONSE_CANCEL,
                               label, GTK_RESPONSE_YES,
                               NULL);
//FIXME gtk4        if (!parent)
//            gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dialog), FALSE);
        response = gnc_dialog_run (GTK_DIALOG(dialog));

        uh_oh = (response != GTK_RESPONSE_YES);
        break;

    case ERR_BACKEND_READONLY:
        fmt = _("GnuCash could not write to %s. "
                "That database may be on a read-only file system, "
                "you may not have write permission for the directory "
                "or your anti-virus software is preventing this action.");
        gnc_error_dialog (parent, fmt, displayname);
        break;

    case ERR_BACKEND_DATA_CORRUPT:
        fmt = _("The file/URL %s "
                "does not contain GnuCash data or the data is corrupt.");
        gnc_error_dialog (parent, fmt, displayname);
        break;

    case ERR_BACKEND_SERVER_ERR:
        fmt = _("The server at URL %s "
                "experienced an error or encountered bad or corrupt data.");
        gnc_error_dialog (parent, fmt, displayname);
        break;

    case ERR_BACKEND_PERM:
        fmt = _("You do not have permission to access %s.");
        gnc_error_dialog (parent, fmt, displayname);
        break;

    case ERR_BACKEND_MISC:
        fmt = _("An error occurred while processing %s.");
        gnc_error_dialog (parent, fmt, displayname);
        break;

    case ERR_FILEIO_FILE_BAD_READ:
        fmt = _("There was an error reading the file. "
                "Do you want to continue?");
        if (gnc_verify_dialog (parent, TRUE, "%s", fmt))
        {
            uh_oh = FALSE;
        }
        break;

    case ERR_FILEIO_PARSE_ERROR:
        fmt = _("There was an error parsing the file %s.");
        gnc_error_dialog (parent, fmt, displayname);
        break;

    case ERR_FILEIO_FILE_EMPTY:
        fmt = _("The file %s is empty.");
        gnc_error_dialog (parent, fmt, displayname);
        break;

    case ERR_FILEIO_FILE_NOT_FOUND:
        if (type == GNC_FILE_DIALOG_SAVE)
        {
            uh_oh = FALSE;
        }
        else
        {
            if (gnc_history_test_for_file (displayname))
            {
                fmt = _("The file/URI %s could not be found.\n\nThe file is in the history list, do you want to remove it?");
                if (gnc_verify_dialog (parent, FALSE, fmt, displayname))
                    gnc_history_remove_file (displayname);
            }
            else
            {
                fmt = _("The file/URI %s could not be found.");
                gnc_error_dialog (parent, fmt, displayname);
            }
        }
        break;

    case ERR_FILEIO_FILE_TOO_OLD:
        fmt = _("This file is from an older version of GnuCash. "
                "Do you want to continue?");
        if (gnc_verify_dialog (parent, TRUE, "%s", fmt))
        {
            uh_oh = FALSE;
        }
        break;

    case ERR_FILEIO_UNKNOWN_FILE_TYPE:
        fmt = _("The file type of file %s is unknown.");
        gnc_error_dialog(parent, fmt, displayname);
        break;

    case ERR_FILEIO_BACKUP_ERROR:
        fmt = _("Could not make a backup of the file %s");
        gnc_error_dialog(parent, fmt, displayname);
        break;

    case ERR_FILEIO_WRITE_ERROR:
        fmt = _("Could not write to file %s. Check that you have "
                "permission to write to this file and that "
                "there is sufficient space to create it.");
        gnc_error_dialog(parent, fmt, displayname);
        break;

    case ERR_FILEIO_FILE_EACCES:
        fmt = _("No read permission to read from file %s.");
        gnc_error_dialog (parent, fmt, displayname);
        break;

    case ERR_FILEIO_RESERVED_WRITE:
        /* Translators: the first %s is a path in the filesystem,
           the second %s is PACKAGE_NAME, which by default is "GnuCash" */
        fmt = _("You attempted to save in\n%s\nor a subdirectory thereof. "
                "This is not allowed as %s reserves that directory for internal use.\n\n"
                "Please try again in a different directory.");
        gnc_error_dialog (parent, fmt, gnc_userdata_dir(), PACKAGE_NAME);
        break;

    case ERR_SQL_DB_TOO_OLD:
        fmt = _("This database is from an older version of GnuCash. "
                "Select OK to upgrade it to the current version, Cancel "
                "to mark it read-only.");

        response = gnc_ok_cancel_dialog(parent, GTK_RESPONSE_CANCEL, "%s", fmt);
        uh_oh = (response == GTK_RESPONSE_CANCEL);
        break;

    case ERR_SQL_DB_TOO_NEW:
        fmt = _("This database is from a newer version of GnuCash. "
                "This version can read it, but cannot safely save to it. "
                "It will be marked read-only until you do File->Save As, "
                "but data may be lost in writing to the old version.");
        gnc_warning_dialog (parent, "%s", fmt);
        uh_oh = TRUE;
        break;

    case ERR_SQL_DB_BUSY:
        fmt = _("The SQL database is in use by other users, "
                "and the upgrade cannot be performed until they logoff. "
                "If there are currently no other users, consult the "
                "documentation to learn how to clear out dangling login "
                "sessions.");
        gnc_error_dialog (parent, "%s", fmt);
        break;

    case ERR_SQL_BAD_DBI:

        fmt = _("The library \"libdbi\" installed on your system doesn't correctly "
                "store large numbers. This means GnuCash cannot use SQL databases "
                "correctly. Gnucash will not open or save to SQL databases until this is "
                "fixed by installing a different version of \"libdbi\". Please see "
                "https://bugs.gnucash.org/show_bug.cgi?id=611936 for more "
                "information.");

        gnc_error_dialog (parent, "%s", fmt);
        break;

    case ERR_SQL_DBI_UNTESTABLE:

        fmt = _("GnuCash could not complete a critical test for the presence of "
                "a bug in the \"libdbi\" library. This may be caused by a "
                "permissions misconfiguration of your SQL database. Please see "
                "https://bugs.gnucash.org/show_bug.cgi?id=645216 for more "
                "information.");

        gnc_error_dialog (parent, "%s", fmt);
        break;

    case ERR_FILEIO_FILE_UPGRADE:
        fmt = _("This file is from an older version of GnuCash and will be "
                "upgraded when saved by this version. You will not be able "
                "to read the saved file from the older version of Gnucash "
                "(it will report an \"error parsing the file\"). If you wish "
                "to preserve the old version, exit without saving.");
        gnc_warning_dialog (parent, "%s", fmt);
        uh_oh = FALSE;
        break;

    default:
        PERR("FIXME: Unhandled error %d", io_error);
        fmt = _("An unknown I/O error (%d) occurred.");
        gnc_error_dialog (parent, fmt, io_error);
        break;
    }

    g_free (displayname);
    return uh_oh;
}

static void
gnc_add_history (QofSession * session)
{
    const gchar *url;
    char *file;

    if (!session) return;

    url = qof_session_get_url ( session );
    if ( !strlen (url) )
        return;

    if (gnc_uri_targets_local_fs (url))
        file = gnc_uri_get_path ( url );
    else
        file = gnc_uri_normalize_uri ( url, FALSE ); /* Note that the password is not saved in history ! */

    gnc_history_add_file (file);
    g_free (file);
}

static void
gnc_book_opened (void)
{
    gnc_hook_run(HOOK_BOOK_OPENED, gnc_get_current_session());
}

typedef struct
{
    GWeakRef parent;
    gboolean has_parent;
    gboolean can_cancel;
    GncFileQuerySaveCallback completed;
    gpointer user_data;
} GncFileQuerySaveRequest;

static void gnc_file_save_with_completion (GtkWindow *parent,
                                           GncFileQuerySaveCallback completed,
                                           gpointer user_data);

static void
gnc_file_query_save_request_free (GncFileQuerySaveRequest *request)
{
    g_weak_ref_clear (&request->parent);
    g_free (request);
}

static void
gnc_file_query_save_complete (GncFileQuerySaveRequest *request,
                              gboolean can_continue)
{
    GtkWindow *parent = GTK_WINDOW (g_weak_ref_get (&request->parent));

    if (request->has_parent && !parent)
        can_continue = FALSE;
    request->completed (parent, can_continue, request->user_data);
    g_clear_object (&parent);
    gnc_file_query_save_request_free (request);
}

static void gnc_file_query_save_continue (GncFileQuerySaveRequest *request);
static void gnc_file_query_save_after_save (GtkWindow *parent, gboolean saved,
                                             gpointer user_data);

static void
gnc_file_query_save_finished (GObject *source, GAsyncResult *result,
                              gpointer user_data)
{
    GncFileQuerySaveRequest *request = user_data;
    GError *error = NULL;
    gint response;
    GtkWindow *parent;

    response = gtk_alert_dialog_choose_finish (GTK_ALERT_DIALOG (source), result,
                                               &error);
    parent = GTK_WINDOW (g_weak_ref_get (&request->parent));
    if (request->has_parent && !parent)
    {
        g_clear_error (&error);
        gnc_file_query_save_complete (request, FALSE);
        return;
    }

    if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Save-before-close confirmation failed: %s", error->message);
    g_clear_error (&error);

    if (response == 0)
        gnc_file_query_save_complete (request, TRUE);
    else if (response == 1 && request->can_cancel)
        gnc_file_query_save_complete (request, FALSE);
    else if (response == (request->can_cancel ? 2 : 1))
        gnc_file_save_with_completion (parent, gnc_file_query_save_after_save,
                                       request);
    else
        gnc_file_query_save_complete (request, request->can_cancel ? FALSE : TRUE);

    g_clear_object (&parent);
}

static void
gnc_file_query_save_after_save (GtkWindow *parent, gboolean saved,
                                gpointer user_data)
{
    GncFileQuerySaveRequest *request = user_data;

    (void)parent;
    if (saved)
        gnc_file_query_save_complete (request, TRUE);
    else
        gnc_file_query_save_continue (request);
}

static void
gnc_file_query_save_continue (GncFileQuerySaveRequest *request)
{
    QofBook *book;
    GtkWindow *parent;
    GtkAlertDialog *dialog;
    const char *buttons_with_cancel[] =
    {
        _("Continue Without Saving"),
        _("Cancel"),
        _("Save"),
        NULL
    };
    const char *buttons_without_cancel[] =
    {
        _("Continue Without Saving"),
        _("Save"),
        NULL
    };
    time64 oldest_change;
    gint minutes;
    gchar *detail;

    if (!gnc_current_session_exist ())
    {
        gnc_file_query_save_complete (request, TRUE);
        return;
    }

    book = qof_session_get_book (gnc_get_current_session ());
    gnc_autosave_remove_timer (book);
    if (!qof_book_session_not_saved (book))
    {
        gnc_file_query_save_complete (request, TRUE);
        return;
    }

    parent = GTK_WINDOW (g_weak_ref_get (&request->parent));
    if (request->has_parent && !parent)
    {
        g_clear_object (&parent);
        gnc_file_query_save_complete (request, FALSE);
        return;
    }

    oldest_change = qof_book_get_session_dirty_time (book);
    minutes = (gnc_time (NULL) - oldest_change) / 60 + 1;
    detail = g_strdup_printf (ngettext (
        "If you don't save, changes from the past %d minute will be discarded.",
        "If you don't save, changes from the past %d minutes will be discarded.",
        minutes), minutes);
    dialog = gtk_alert_dialog_new ("%s", _("Save changes to the file?"));
    gtk_alert_dialog_set_detail (dialog, detail);
    gtk_alert_dialog_set_buttons (dialog, request->can_cancel ?
                                  buttons_with_cancel : buttons_without_cancel);
    gtk_alert_dialog_set_default_button (dialog, request->can_cancel ? 2 : 1);
    gtk_alert_dialog_set_cancel_button (dialog, request->can_cancel ? 1 : 0);
    gtk_alert_dialog_choose (dialog, parent, NULL, gnc_file_query_save_finished,
                             request);
    g_object_unref (dialog);
    g_free (detail);
    g_clear_object (&parent);
}

void
gnc_file_query_save_async (GtkWindow *parent, gboolean can_cancel,
                           GncFileQuerySaveCallback completed,
                           gpointer user_data)
{
    GncFileQuerySaveRequest *request;

    g_return_if_fail (completed != NULL);
    request = g_new0 (GncFileQuerySaveRequest, 1);
    g_weak_ref_init (&request->parent, parent);
    request->has_parent = parent != NULL;
    request->can_cancel = can_cancel;
    request->completed = completed;
    request->user_data = user_data;
    gnc_file_query_save_continue (request);
}

static void
gnc_file_new_after_query (GtkWindow *parent, gboolean can_continue,
                          gpointer user_data)
{
    QofSession *session;

    (void)user_data;
    if (!can_continue)
        return;

    if (gnc_current_session_exist())
    {
        session = gnc_get_current_session ();
        qof_event_suspend ();
        gnc_hook_run(HOOK_BOOK_CLOSED, session);
        gnc_close_gui_component_by_session (session);
        gnc_state_save (session);
        gnc_clear_current_session();
        qof_event_resume ();
    }

    gnc_get_current_session ();
    gnc_hook_run(HOOK_NEW_BOOK, NULL);
    gnc_gui_refresh_all ();
    gnc_book_opened ();
    (void)parent;
}

void
gnc_file_new (GtkWindow *parent)
{
    gnc_file_query_save_async (parent, TRUE, gnc_file_new_after_query, NULL);
}


static char*
get_account_sep_warning (QofBook *book)
{
    const char *sep = gnc_get_account_separator_string ();
    GList *violation_accts = gnc_account_list_name_violations (book, sep);
    if (!violation_accts)
        return NULL;

    gchar *rv = gnc_account_name_violations_errmsg (sep, violation_accts);
    g_list_free_full (violation_accts, g_free);
    return rv;
}

/* private utilities for file open; done in two stages */

#define RESPONSE_NEW 1
#define RESPONSE_OPEN 2
#define RESPONSE_QUIT 3
#define RESPONSE_READONLY 4
#define RESPONSE_FILE 5

/* This function is called after loading datafile. It's meant to
   collect all scrubbing routines. */
static void
run_post_load_scrubs (GtkWindow *parent, QofBook *book)
{
    const char *budget_warning =
        _("This book has budgets. The internal representation of "
          "budget amounts no longer depends on the Reverse Balanced "
          "Accounts preference. Please review the budgets and amend "
          "signs if necessary.");

    GList *infos = NULL;

    qof_event_suspend();

    /* If feature GNC_FEATURE_BUDGET_UNREVERSED is not set, and there
       are budgets, fix signs */
    if (gnc_maybe_scrub_all_budget_signs (book))
        infos = g_list_prepend (infos, g_strdup (budget_warning));

    // Fix account color slots being set to 'Not Set', should run once on a book
    xaccAccountScrubColorNotSet (book);

    /* Check for account names that may contain the current separator character
     * and inform the user if there are any */
    char *sep_warning = get_account_sep_warning (book);
    if (sep_warning)
        infos = g_list_prepend (infos, sep_warning);

    qof_event_resume();

    if (!infos)
        return;

    const char *header = N_("The following are noted in this file:");
    infos = g_list_reverse (infos);
    infos = g_list_prepend (infos, g_strdup (_(header)));
    char *final = gnc_g_list_stringjoin (infos, "\n\n• ");
    gnc_info_dialog (parent, "%s", final);

    g_free (final);
    g_list_free_full (infos, g_free);
}

typedef struct
{
    GWeakRef parent;
    gchar *filename;
    gboolean is_readonly;
    gboolean reset_bayes_conversion;
    gboolean break_lock;
} GncFileOpenRequest;

typedef struct
{
    GWeakRef parent;
    gboolean has_parent;
    gchar *filename;
    gboolean reset_bayes_conversion;
    gboolean offer_quit;
} GncFileLockedOpenRequest;

static gboolean gnc_file_open_request_with_mode (GtkWindow *parent,
                                                  const char *filename,
                                                  gboolean is_readonly,
                                                  gboolean reset_bayes_conversion,
                                                  gboolean break_lock);
static gboolean gnc_post_file_open (GtkWindow *parent, const char *filename,
                                    gboolean is_readonly,
                                    gboolean reset_bayes_conversion,
                                    gboolean break_lock);

static void
gnc_file_open_request_free (GncFileOpenRequest *request)
{
    g_weak_ref_clear (&request->parent);
    g_free (request->filename);
    g_free (request);
}

static gboolean
gnc_file_needs_xml_encoding_conversion (const gchar *filename)
{
    gchar *normalized_uri;
    gchar *scheme = NULL;
    gchar *hostname = NULL;
    gchar *username = NULL;
    gchar *password = NULL;
    gchar *path = NULL;
    gint32 port = 0;
    gboolean needs_conversion = FALSE;

    normalized_uri = gnc_uri_normalize_uri (filename, FALSE);
    if (!normalized_uri)
        return FALSE;

    gnc_uri_get_components (normalized_uri, &scheme, &hostname, &port,
                            &username, &password, &path);
    if (gnc_uri_is_file_scheme (scheme) && path)
        needs_conversion = gnc_xml_file_needs_encoding_conversion (path);

    g_free (scheme);
    g_free (hostname);
    g_free (username);
    g_free (password);
    g_free (path);
    g_free (normalized_uri);

    return needs_conversion;
}

static void
gnc_file_locked_open_request_free (GncFileLockedOpenRequest *request)
{
    g_weak_ref_clear (&request->parent);
    g_free (request->filename);
    g_free (request);
}

static void
gnc_file_locked_open_finished (GObject *source, GAsyncResult *result,
                               gpointer user_data)
{
    GncFileLockedOpenRequest *request = user_data;
    GError *error = NULL;
    GtkWindow *parent = GTK_WINDOW (g_weak_ref_get (&request->parent));
    gint response = gtk_alert_dialog_choose_finish (GTK_ALERT_DIALOG (source), result,
                                                    &error);

    if (request->has_parent && !parent)
    {
        g_clear_error (&error);
        gnc_file_locked_open_request_free (request);
        return;
    }
    if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Locked-file decision failed: %s", error->message);
    if (error)
        response = request->offer_quit ? 4 : 3;
    g_clear_error (&error);

    switch (response)
    {
    case 0: /* Open Read-Only */
        (void)gnc_file_open_request_with_mode (parent, request->filename, TRUE,
                                               request->reset_bayes_conversion,
                                               FALSE);
        break;
    case 1: /* Create New File */
        gnc_file_new (parent);
        break;
    case 2: /* Open Anyway */
        (void)gnc_file_open_request_with_mode (parent, request->filename, FALSE,
                                               request->reset_bayes_conversion,
                                               TRUE);
        break;
    case 4: /* Quit */
        if (request->offer_quit && shutdown_cb)
            shutdown_cb (0);
        break;
    default: /* Open Folder and cancellation */
        gnc_file_open (parent);
        break;
    }

    g_clear_object (&parent);
    gnc_file_locked_open_request_free (request);
}

static void
gnc_file_locked_open_async (GtkWindow *parent, const char *filename,
                            gboolean reset_bayes_conversion,
                            QofBackendError io_error, const char *newfile)
{
    const char *buttons_without_quit[] =
    {
        _("Open Read-Only"),
        _("Create New File"),
        _("Open Anyway"),
        _("Open Folder"),
        NULL
    };
    const char *buttons_with_quit[] =
    {
        _("Open Read-Only"),
        _("Create New File"),
        _("Open Anyway"),
        _("Open Folder"),
        _("Quit"),
        NULL
    };
    const char *detail = io_error == ERR_BACKEND_LOCKED ?
        _("That database may be in use by another user, in which case you "
          "should not open the database. What would you like to do?") :
        _("That database may be on a read-only file system, you may not have "
          "write permission for the directory, or your anti-virus software is "
          "preventing this action. If you proceed you may not be able to save "
          "any changes. What would you like to do?");
    GncFileLockedOpenRequest *request;
    GtkAlertDialog *dialog;
    gchar *displayname;
    gchar *message;

    if (!gnc_uri_is_file_uri (newfile))
        displayname = gnc_uri_normalize_uri (newfile, FALSE);
    else
        displayname = gnc_uri_get_path (newfile);
    message = g_strdup_printf (_("GnuCash could not obtain the lock for %s."),
                               displayname);

    request = g_new0 (GncFileLockedOpenRequest, 1);
    g_weak_ref_init (&request->parent, parent);
    request->has_parent = parent != NULL;
    request->filename = g_strdup (filename);
    request->reset_bayes_conversion = reset_bayes_conversion;
    request->offer_quit = shutdown_cb != NULL;

    dialog = gtk_alert_dialog_new ("%s", message);
    gtk_alert_dialog_set_detail (dialog, detail);
    gtk_alert_dialog_set_buttons (dialog, request->offer_quit ?
                                  buttons_with_quit : buttons_without_quit);
    gtk_alert_dialog_set_default_button (dialog, request->offer_quit ? 4 : 3);
    gtk_alert_dialog_set_cancel_button (dialog, request->offer_quit ? 4 : 3);
    gtk_alert_dialog_choose (dialog, parent, NULL, gnc_file_locked_open_finished,
                             request);
    g_object_unref (dialog);
    g_free (message);
    g_free (displayname);
}
static void
gnc_file_open_after_xml_conversion (GObject *source, GAsyncResult *result,
                                    gpointer user_data)
{
    GncFileOpenRequest *request = user_data;
    GtkWindow *parent = g_weak_ref_get (&request->parent);
    GError *error = NULL;

    if (gnc_xml_convert_single_file_finish (result, &error))
    {
        if (request->reset_bayes_conversion)
            gnc_account_reset_convert_bayes_to_flat ();
        gnc_post_file_open (parent, request->filename, request->is_readonly,
                            request->reset_bayes_conversion, request->break_lock);
    }
    else if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        gnc_error_dialog (parent, "%s", error->message);

    g_clear_error (&error);
    g_clear_object (&parent);
    gnc_file_open_request_free (request);
    (void)source;
}

static gboolean
gnc_file_open_request (GtkWindow *parent, const char *filename,
                       gboolean is_readonly,
                       gboolean reset_bayes_conversion)
{
    return gnc_file_open_request_with_mode (parent, filename, is_readonly,
                                            reset_bayes_conversion, FALSE);
}

static gboolean
gnc_file_open_request_with_mode (GtkWindow *parent, const char *filename,
                                  gboolean is_readonly,
                                  gboolean reset_bayes_conversion,
                                  gboolean break_lock)
{
    GncFileOpenRequest *request;

    if (!filename || *filename == '\0')
        return FALSE;

    if (!gnc_file_needs_xml_encoding_conversion (filename))
    {
        if (reset_bayes_conversion)
            gnc_account_reset_convert_bayes_to_flat ();
        return gnc_post_file_open (parent, filename, is_readonly,
                                   reset_bayes_conversion, break_lock);
    }

    request = g_new0 (GncFileOpenRequest, 1);
    g_weak_ref_init (&request->parent, parent);
    request->filename = g_strdup (filename);
    request->is_readonly = is_readonly;
    request->reset_bayes_conversion = reset_bayes_conversion;
    request->break_lock = break_lock;
    gnc_xml_convert_single_file_async (filename, parent, NULL,
                                       gnc_file_open_after_xml_conversion,
                                       request);
    return TRUE;
}
static gboolean
gnc_post_file_open (GtkWindow *parent, const char *filename, gboolean is_readonly,
                    gboolean reset_bayes_conversion, gboolean break_lock)
{
    QofSession *new_session;
    gboolean uh_oh = FALSE;
    char * newfile;
    QofBackendError io_err = ERR_BACKEND_NO_ERR;

    gchar *scheme   = NULL;
    gchar *hostname = NULL;
    gchar *username = NULL;
    gchar *password = NULL;
    gchar *path = NULL;
    gint32 port = 0;


    ENTER("filename %s", filename);
    if (!filename || (*filename == '\0')) return FALSE;

    /* Convert user input into a normalized uri
     * Note that the normalized uri for internal use can have a password */
    newfile = gnc_uri_normalize_uri ( filename, TRUE );
    if (!newfile)
    {
        show_session_error (parent,
                            ERR_FILEIO_FILE_NOT_FOUND, filename,
                            GNC_FILE_DIALOG_OPEN);
        return FALSE;
    }

    gnc_uri_get_components (newfile, &scheme, &hostname,
                            &port, &username, &password, &path);

    /* If the file to open is a database, and no password was given,
     * attempt to look it up in a keyring. If that fails the keyring
     * function will ask the user to enter a password. The user can
     * cancel this dialog, in which case the open file action will be
     * abandoned.
     * Note newfile is normalized uri so we can safely call
     * gnc_uri_is_file_scheme on it.
     */
    if (!gnc_uri_is_file_scheme (scheme) && !password)
    {
        gboolean have_valid_pw = FALSE;
        have_valid_pw = gnc_keyring_get_password ( NULL, scheme, hostname, port,
                        path, &username, &password );
        if (!have_valid_pw)
            return FALSE;

        /* Got password. Recreate the uri to use internally. */
        g_free ( newfile );
        newfile = gnc_uri_create_uri ( scheme, hostname, port,
                                       username, password, path);
    }

    /* For file based uri's, remember the directory as the default. */
    if (gnc_uri_is_file_scheme(scheme))
    {
        gchar *default_dir = g_path_get_dirname(path);
        gnc_set_default_directory (GNC_PREFS_GROUP_OPEN_SAVE, default_dir);
        g_free(default_dir);
    }

    /* disable events while moving over to the new set of accounts;
     * the mass deletion of accounts and transactions during
     * switchover would otherwise cause excessive redraws. */
    qof_event_suspend ();

    /* Change the mouse to a busy cursor */
    gnc_set_busy_cursor (NULL, TRUE);

    /* -------------- BEGIN CORE SESSION CODE ------------- */
    /* -- this code is almost identical in FileOpen and FileSaveAs -- */
    if (gnc_current_session_exist())
    {
        QofSession *current_session = gnc_get_current_session();
        gnc_hook_run(HOOK_BOOK_CLOSED, current_session);
        gnc_close_gui_component_by_session (current_session);
        gnc_state_save (current_session);
        gnc_clear_current_session();
    }

    /* load the accounts from the users datafile */
    /* but first, check to make sure we've got a session going. */
    new_session = qof_session_new (qof_book_new());

    // Begin the new session. If we are in read-only mode, ignore the locks.
    qof_session_begin (new_session, newfile,
                       break_lock ? SESSION_BREAK_LOCK :
                       (is_readonly ? SESSION_READ_ONLY : SESSION_NORMAL_OPEN));
    io_err = qof_session_get_error (new_session);

    if (ERR_BACKEND_BAD_URL == io_err)
    {
        gchar *directory;

        show_session_error (parent, io_err, newfile, GNC_FILE_DIALOG_OPEN);
        if (g_file_test (filename, G_FILE_TEST_IS_DIR))
            directory = g_strdup (filename);
        else
            directory = gnc_get_default_directory (GNC_PREFS_GROUP_OPEN_SAVE);

        /* The failed session cannot continue. Restore the event state before
         * scheduling a new native request for a valid local file. */
        qof_book_mark_session_saved (qof_session_get_book (new_session));
        qof_session_destroy (new_session);
        g_free (scheme);
        g_free (hostname);
        g_free (username);
        g_free (password);
        g_free (path);
        g_free (newfile);
        gnc_unset_busy_cursor (NULL);
        qof_event_resume ();
        gnc_gui_refresh_all ();
        gnc_get_current_session ();
        gnc_file_open_request_dialog (parent, directory);
        g_free (directory);
        return FALSE;
    }
    /* A lock decision must not keep the partially opened session alive while
     * a native GTK4 alert is visible. The callback restarts the request with
     * an explicit open mode after this attempt has cleaned up. */
    else if (ERR_BACKEND_LOCKED == io_err || ERR_BACKEND_READONLY == io_err)
    {
        gnc_file_locked_open_async (parent, filename, reset_bayes_conversion,
                                    io_err, newfile);
    }
    /* if the database doesn't exist, ask the user ... */
    else if ((ERR_BACKEND_NO_SUCH_DB == io_err))
    {
        if (!show_session_error (parent, io_err, newfile, GNC_FILE_DIALOG_OPEN))
        {
            /* user told us to create a new database. Do it. We
                     * shouldn't have to worry about locking or clobbering,
                     * it's supposed to be new. */
            qof_session_begin (new_session, newfile, SESSION_NEW_STORE);
        }
    }

    /* Check for errors again, since above may have cleared the lock.
     * If its still locked, still, doesn't exist, still too old, then
     * don't bother with the message, just die. */
    io_err = qof_session_get_error (new_session);
    if ((ERR_BACKEND_LOCKED == io_err) ||
            (ERR_BACKEND_READONLY == io_err) ||
            (ERR_BACKEND_NO_SUCH_DB == io_err))
    {
        uh_oh = TRUE;
    }

    else
    {
        uh_oh = show_session_error (parent, io_err, newfile, GNC_FILE_DIALOG_OPEN);
    }

    if (!uh_oh)
    {
        Account *new_root;

        /* If the new "file" is a database, attempt to store the password
         * in a keyring. GnuCash itself will not save it.
         */
        if ( !gnc_uri_is_file_scheme (scheme))
            gnc_keyring_set_password ( scheme, hostname, port,
                                       path, username, password );

        xaccLogDisable();
        gnc_window_show_progress(_("Loading user data…"), 0.0);
        qof_session_load (new_session, gnc_window_show_progress);
        gnc_window_show_progress(NULL, -1.0);
        xaccLogEnable();

        if (is_readonly)
        {
            // If the user chose "open read-only" above, make sure to have this
            // read-only here.
            qof_book_mark_readonly(qof_session_get_book(new_session));
        }

        /* check for i/o error, put up appropriate error dialog */
        io_err = qof_session_pop_error (new_session);


        uh_oh = show_session_error (parent, io_err, newfile, GNC_FILE_DIALOG_OPEN);
        /* Attempt to update the database if it's too old */
        if ( !uh_oh && io_err == ERR_SQL_DB_TOO_OLD )
        {
            gnc_window_show_progress(_("Re-saving user data…"), 0.0);
            qof_session_safe_save(new_session, gnc_window_show_progress);
            io_err = qof_session_get_error(new_session);
            uh_oh = show_session_error(parent, io_err, newfile, GNC_FILE_DIALOG_SAVE);
        }
        /* Database is either too old and couldn't (or user didn't
         * want it to) be updated or it's too new. Mark it as
         * read-only
         */
        if (uh_oh && (io_err == ERR_SQL_DB_TOO_OLD ||
                      io_err == ERR_SQL_DB_TOO_NEW))
        {
            qof_book_mark_readonly(qof_session_get_book(new_session));
            uh_oh = FALSE;
        }
        new_root = gnc_book_get_root_account (qof_session_get_book (new_session));
        if (uh_oh) new_root = NULL;

        /* Umm, came up empty-handed, but no error:
         * The backend forgot to set an error. So make one up. */
        if (!uh_oh && !new_root)
        {
            uh_oh = show_session_error (parent, ERR_BACKEND_MISC, newfile,
                                        GNC_FILE_DIALOG_OPEN);
        }

        /* test for unknown features. */
        if (!uh_oh)
        {
            QofBook *book = qof_session_get_book (new_session);
            gchar *msg = gnc_features_test_unknown (book);
            Account *template_root = gnc_book_get_template_root (book);

            if (msg)
            {
                uh_oh = TRUE;

                // XXX: should pull out the file name here */
                gnc_error_dialog (parent, msg, "");
                g_free (msg);
            }
            if (template_root != NULL)
            {
                GList *child = NULL;
                GList *children = gnc_account_get_descendants (template_root);

                for (child = children; child; child = g_list_next (child))
                {
                    Account *acc = GNC_ACCOUNT (child->data);
                    GList *splits = xaccAccountGetSplitList (acc);
                    g_list_foreach (splits,
                                    (GFunc)gnc_sx_scrub_split_numerics, NULL);
                    g_list_free (splits);
                }
                g_list_free (children);
            }
        }
    }

    g_free (scheme);
    g_free (hostname);
    g_free (username);
    g_free (password);
    g_free (path);

    gnc_unset_busy_cursor (NULL);

    /* going down -- abandon ship */
    if (uh_oh)
    {
        xaccLogDisable();
        qof_session_destroy (new_session);
        xaccLogEnable();

        /* well, no matter what, I think it's a good idea to have a root
         * account around.  For example, early in the gnucash startup
         * sequence, the user opens a file; if this open fails for any
         * reason, we don't want to leave them high & dry without a root
         * account, because if the user continues, then bad things will
         * happen. */
        gnc_get_current_session ();

        g_free (newfile);

        qof_event_resume ();
        gnc_gui_refresh_all ();

        return FALSE;
    }

    /* if we got to here, then we've successfully gotten a new session */
    /* close up the old file session (if any) */
    gnc_set_current_session(new_session);

    /* --------------- END CORE SESSION CODE -------------- */

    /* clean up old stuff, and then we're outta here. */
    gnc_add_history (new_session);

    g_free (newfile);

    qof_event_resume ();
    gnc_gui_refresh_all ();

    /* Call this after re-enabling events. */
    gnc_book_opened ();

    run_post_load_scrubs (parent, gnc_get_current_book ());

    return TRUE;
}

/* Routine that pops up a file chooser dialog
 *
 * Note: this dialog is used when dbi is not enabled
 *       so the paths used in here are always file
 *       paths, never db uris.
 */
static void
gnc_file_open_selected (GtkWindow *parent, const gchar *filename,
                        gpointer user_data)
{
    (void)user_data;
    (void)gnc_file_open_request (parent, filename, FALSE, FALSE);
}

static void
gnc_file_open_request_dialog (GtkWindow *parent, const gchar *starting_dir)
{
    gnc_file_select_async (parent, _("Open"),
                           gnc_file_dialog_get_datafile_filters (), starting_dir,
                           GNC_FILE_DIALOG_OPEN, gnc_file_open_selected);
}

/* Starts the native file request only after the current session has either
 * been saved or explicitly discarded. */
static void
gnc_file_open_after_query (GtkWindow *parent, gboolean can_continue,
                           gpointer user_data)
{
    gchar *default_dir;
    gchar *last;

    (void)user_data;
    if (!can_continue)
        return;

    last = gnc_history_get_last ();
    if (last && gnc_uri_targets_local_fs (last))
    {
        gchar *filepath = gnc_uri_get_path (last);

        default_dir = g_path_get_dirname (filepath);
        g_free (filepath);
    }
    else
        default_dir = gnc_get_default_directory (GNC_PREFS_GROUP_OPEN_SAVE);

    gnc_file_open_request_dialog (parent, default_dir);
    g_free (last);
    g_free (default_dir);

    /* Keep a valid empty session if the native chooser is cancelled. */
    gnc_get_current_session ();
}

gboolean
gnc_file_open (GtkWindow *parent)
{
    gnc_file_query_save_async (parent, TRUE, gnc_file_open_after_query, NULL);
    return TRUE;
}

typedef struct
{
    gchar *filename;
    gboolean open_readonly;
} GncFileOpenAfterQuery;

static void
gnc_file_open_after_query_file (GtkWindow *parent, gboolean can_continue,
                                gpointer user_data)
{
    GncFileOpenAfterQuery *request = user_data;

    if (can_continue)
        (void)gnc_file_open_request (parent, request->filename,
                                     request->open_readonly,
                                     /*reset_bayes_conversion*/ TRUE);
    g_free (request->filename);
    g_free (request);
}

gboolean
gnc_file_open_file (GtkWindow *parent, const char *newfile, gboolean open_readonly)
{
    GncFileOpenAfterQuery *request;

    if (!newfile)
        return FALSE;

    request = g_new0 (GncFileOpenAfterQuery, 1);
    request->filename = g_strdup (newfile);
    request->open_readonly = open_readonly;
    gnc_file_query_save_async (parent, TRUE, gnc_file_open_after_query_file,
                               request);
    return TRUE;
}
/* Note: this dialog will only be used when dbi is not enabled
 *       paths used in it always refer to files and are
 *       never db uris
 */
static void
gnc_file_export_selected (GtkWindow *parent, const gchar *filename,
                          gpointer user_data)
{
    (void)user_data;
    gnc_file_do_export (parent, filename);
}

void
gnc_file_export (GtkWindow *parent)
{
    gchar *default_dir;
    gchar *last;

    ENTER (" ");

    last = gnc_history_get_last ();
    if (last && gnc_uri_targets_local_fs (last))
    {
        gchar *filepath = gnc_uri_get_path (last);

        default_dir = g_path_get_dirname (filepath);
        g_free (filepath);
    }
    else
        default_dir = gnc_get_default_directory (GNC_PREFS_GROUP_EXPORT);

    gnc_file_select_async (parent, _("Save"),
                           gnc_file_dialog_get_datafile_filters (), default_dir,
                           GNC_FILE_DIALOG_EXPORT, gnc_file_export_selected);
    g_free (last);
    g_free (default_dir);

    LEAVE (" ");
}
/* Prevent the user from storing or exporting data files into the settings
 * directory.
 */
static gboolean
check_file_path (const char *path)
{
    /* Remember the directory as the default. */
     gchar *dir = g_path_get_dirname(path);
     const gchar *dotgnucash = gnc_userdata_dir();
     char *dirpath = dir;

     /* Prevent user from storing file in GnuCash' private configuration
      * directory (~/.gnucash by default in linux, but can be overridden)
      */
     while (strcmp(dir = g_path_get_dirname(dirpath), dirpath) != 0)
     {
         if (strcmp(dirpath, dotgnucash) == 0)
         {
             g_free (dir);
             g_free (dirpath);
             return TRUE;
         }
         g_free (dirpath);
         dirpath = dir;
     }
     g_free (dirpath);
     g_free(dir);
     return FALSE;
}


void
gnc_file_do_export(GtkWindow *parent, const char * filename)
{
    QofSession *current_session, *new_session;
    gboolean ok;
    QofBackendError io_err = ERR_BACKEND_NO_ERR;
    gchar *norm_file;
    gchar *newfile;
    const gchar *oldfile;

    gchar *scheme   = NULL;
    gchar *hostname = NULL;
    gchar *username = NULL;
    gchar *password = NULL;
    gchar *path = NULL;
    gint32 port = 0;

    ENTER(" ");

    /* Convert user input into a normalized uri
     * Note that the normalized uri for internal use can have a password */
    norm_file = gnc_uri_normalize_uri ( filename, TRUE );
    if (!norm_file)
    {
        show_session_error (parent, ERR_FILEIO_FILE_NOT_FOUND, filename,
                            GNC_FILE_DIALOG_EXPORT);
        return;
    }

    newfile = gnc_uri_add_extension (norm_file, GNC_DATAFILE_EXT);
    g_free (norm_file);
    gnc_uri_get_components (newfile, &scheme, &hostname,
                            &port, &username, &password, &path);

    /* Save As can't use the generic 'file' protocol. If the user didn't set
     * a specific protocol, assume the default 'xml'.
     */
    if (g_strcmp0 (scheme, "file") == 0)
    {
        g_free (scheme);
        scheme = g_strdup ("xml");
        norm_file = gnc_uri_create_uri (scheme, hostname, port,
                                        username, password, path);
        g_free (newfile);
        newfile = norm_file;
    }

    /* Some extra steps for file based uri's only
     * Note newfile is normalized uri so we can safely call
     * gnc_uri_is_file_scheme on it. */
    if (gnc_uri_is_file_scheme (scheme))
    {
        if (check_file_path (path))
        {
            show_session_error (parent, ERR_FILEIO_RESERVED_WRITE, newfile,
                    GNC_FILE_DIALOG_SAVE);
            return;
        }
        gnc_set_default_directory (GNC_PREFS_GROUP_OPEN_SAVE,
                       g_path_get_dirname(path));
    }
    /* Check to see if the user specified the same file as the current
     * file. If so, prevent the export from happening to avoid killing this file */
    current_session = gnc_get_current_session ();
    oldfile = qof_session_get_url(current_session);
    if (strlen (oldfile) && (strcmp(oldfile, newfile) == 0))
    {
        g_free (newfile);
        show_session_error (parent, ERR_FILEIO_WRITE_ERROR, filename,
                            GNC_FILE_DIALOG_EXPORT);
        return;
    }

    qof_event_suspend();

    /* -- this session code is NOT identical in FileOpen and FileSaveAs -- */

    new_session = qof_session_new (NULL);
    qof_session_begin (new_session, newfile, SESSION_NEW_STORE);

    io_err = qof_session_get_error (new_session);
    /* If the file exists and would be clobbered, ask the user */
    if (ERR_BACKEND_STORE_EXISTS == io_err)
    {
        const char *format = _("The file %s already exists. "
                               "Are you sure you want to overwrite it?");

        const char *name;
        if ( gnc_uri_is_file_uri ( newfile ) )
            name = gnc_uri_get_path ( newfile );
        else
            name = gnc_uri_normalize_uri ( newfile, FALSE );
        /* if user says cancel, we should break out */
        if (!gnc_verify_dialog (parent, FALSE, format, name))
        {
            return;
        }
        qof_session_begin (new_session, newfile, SESSION_NEW_OVERWRITE);
    }
    /* if file appears to be locked, ask the user ... */
    if (ERR_BACKEND_LOCKED == io_err || ERR_BACKEND_READONLY == io_err)
    {
        if (!show_session_error (parent, io_err, newfile, GNC_FILE_DIALOG_EXPORT))
        {
            /* user told us to ignore locks. So ignore them. */
            qof_session_begin (new_session, newfile, SESSION_BREAK_LOCK);
        }
    }

    /* --------------- END CORE SESSION CODE -------------- */

    /* use the current session to save to file */
    gnc_set_busy_cursor (NULL, TRUE);
    gnc_window_show_progress(_("Exporting file…"), 0.0);
    ok = qof_session_export (new_session, current_session,
                             gnc_window_show_progress);
    gnc_window_show_progress(NULL, -1.0);
    gnc_unset_busy_cursor (NULL);
    xaccLogDisable();
    qof_session_destroy (new_session);
    xaccLogEnable();
    qof_event_resume();

    if (!ok)
    {
        /* %s is the strerror(3) error string of the error that occurred. */
        const char *format = _("There was an error saving the file.\n\n%s");

        gnc_error_dialog (parent, format, strerror(errno));
        return;
    }
}

static gboolean been_here_before = FALSE;

typedef struct
{
    GncFileQuerySaveCallback completed;
    gpointer user_data;
} GncFileSaveRetry;

static void gnc_file_save_as_with_completion (GtkWindow *parent,
                                               GncFileQuerySaveCallback completed,
                                               gpointer user_data);

static void
gnc_file_save_complete (GtkWindow *parent, GncFileQuerySaveCallback completed,
                        gpointer user_data, gboolean saved)
{
    if (completed)
        completed (parent, saved, user_data);
}

typedef struct
{
    GncFileQuerySaveCallback completed;
    gpointer user_data;
} GncFileReadOnlySaveRequest;

static void
gnc_file_read_only_save_finished (GtkWindow *parent, gint response,
                                  gpointer user_data)
{
    GncFileReadOnlySaveRequest *request = user_data;

    if (response == GTK_RESPONSE_OK)
        gnc_file_save_as_with_completion (parent, request->completed, request->user_data);
    else
        gnc_file_save_complete (parent, request->completed, request->user_data, FALSE);
    g_free (request);
}

static void
gnc_file_read_only_save_as_async (GtkWindow *parent,
                                  GncFileQuerySaveCallback completed,
                                  gpointer user_data)
{
    GncFileReadOnlySaveRequest *request = g_new0 (GncFileReadOnlySaveRequest, 1);

    request->completed = completed;
    request->user_data = user_data;
    gnc_ok_cancel_dialog_async (parent, GTK_RESPONSE_CANCEL,
                                gnc_file_read_only_save_finished, request,
                                "%s", _("The database was opened read-only. "
                                        "Do you want to save it to a different location?"));
}

static void
gnc_file_save_after_retry (GtkWindow *parent, gboolean saved, gpointer user_data)
{
    GncFileSaveRetry *retry = user_data;

    been_here_before = FALSE;
    gnc_file_save_complete (parent, retry->completed, retry->user_data, saved);
    g_free (retry);
}

static void
gnc_file_save_with_completion (GtkWindow *parent,
                               GncFileQuerySaveCallback completed,
                               gpointer user_data)
{
    QofBackendError io_err;
    const char *newfile;
    QofSession *session;

    ENTER (" ");

    if (!gnc_current_session_exist ())
    {
        gnc_file_save_complete (parent, completed, user_data, TRUE);
        return;
    }

    session = gnc_get_current_session ();
    if (!strlen (qof_session_get_url (session)))
    {
        gnc_file_save_as_with_completion (parent, completed, user_data);
        return;
    }

    if (qof_book_is_readonly (qof_session_get_book (session)))
    {
        gnc_file_read_only_save_as_async (parent, completed, user_data);
        return;
    }

    save_in_progress++;
    gnc_set_busy_cursor (NULL, TRUE);
    gnc_window_show_progress (_("Writing file…"), 0.0);
    qof_session_save (session, gnc_window_show_progress);
    gnc_window_show_progress (NULL, -1.0);
    gnc_unset_busy_cursor (NULL);
    save_in_progress--;

    io_err = qof_session_get_error (session);
    if (ERR_BACKEND_NO_ERR != io_err)
    {
        GncFileSaveRetry *retry;

        newfile = qof_session_get_url (session);
        show_session_error (parent, io_err, newfile, GNC_FILE_DIALOG_SAVE);
        if (been_here_before)
        {
            gnc_file_save_complete (parent, completed, user_data, FALSE);
            return;
        }

        been_here_before = TRUE;
        retry = g_new0 (GncFileSaveRetry, 1);
        retry->completed = completed;
        retry->user_data = user_data;
        gnc_file_save_as_with_completion (parent, gnc_file_save_after_retry, retry);
        return;
    }

    xaccReopenLog ();
    gnc_add_history (session);
    gnc_hook_run (HOOK_BOOK_SAVED, session);
    gnc_file_save_complete (parent, completed, user_data, TRUE);
    LEAVE (" ");
}

void
gnc_file_save (GtkWindow *parent)
{
    gnc_file_save_with_completion (parent, NULL, NULL);
}
/* Note: this dialog will only be used when dbi is not enabled
 *       paths used in it always refer to files and are
 *       never db uris. See gnc_file_do_save_as for that.
 */
typedef struct
{
    GWeakRef parent;
    gboolean has_parent;
    GncFileQuerySaveCallback completed;
    gpointer user_data;
} GncFileSaveAsRequest;

static void
gnc_file_save_as_request_free (GncFileSaveAsRequest *request)
{
    g_weak_ref_clear (&request->parent);
    g_free (request);
}

static void
gnc_file_save_as_complete (GncFileSaveAsRequest *request, GtkWindow *parent,
                           gboolean saved)
{
    if (request->has_parent && !parent)
        saved = FALSE;
    gnc_file_save_complete (parent, request->completed, request->user_data, saved);
}

static void
gnc_file_save_as_selected (GtkWindow *parent, const gchar *filename,
                           gpointer user_data)
{
    GncFileSaveAsRequest *request = user_data;
    gboolean saved = FALSE;

    if (filename)
    {
        gnc_file_do_save_as (parent, filename);
        saved = gnc_current_session_exist () &&
                !qof_book_session_not_saved (
                    qof_session_get_book (gnc_get_current_session ()));
    }
    gnc_file_save_as_complete (request, parent, saved);
}

static void
gnc_file_save_as_cancelled (GtkWindow *parent, const GError *error,
                             gpointer user_data)
{
    GncFileSaveAsRequest *request = user_data;

    (void)error;
    gnc_file_save_as_complete (request, parent, FALSE);
}

static void
gnc_file_save_as_with_completion (GtkWindow *parent,
                                  GncFileQuerySaveCallback completed,
                                  gpointer user_data)
{
    gchar *default_dir;
    gchar *last;
    GncFileSaveAsRequest *request;

    ENTER (" ");

    if (!gnc_current_session_exist ())
    {
        gnc_file_save_complete (parent, completed, user_data, FALSE);
        LEAVE ("No Session.");
        return;
    }

    last = gnc_history_get_last ();
    if (last && gnc_uri_targets_local_fs (last))
    {
        gchar *filepath = gnc_uri_get_path (last);

        default_dir = g_path_get_dirname (filepath);
        g_free (filepath);
    }
    else
        default_dir = gnc_get_default_directory (GNC_PREFS_GROUP_OPEN_SAVE);

    request = g_new0 (GncFileSaveAsRequest, 1);
    g_weak_ref_init (&request->parent, parent);
    request->has_parent = parent != NULL;
    request->completed = completed;
    request->user_data = user_data;
    gnc_file_select_async_full (parent, _("Save"),
                                gnc_file_dialog_get_datafile_filters (), default_dir,
                                GNC_FILE_DIALOG_SAVE, gnc_file_save_as_selected,
                                gnc_file_save_as_cancelled, request,
                                (GDestroyNotify)gnc_file_save_as_request_free);
    g_free (last);
    g_free (default_dir);

    LEAVE (" ");
}

void
gnc_file_save_as (GtkWindow *parent)
{
    gnc_file_save_as_with_completion (parent, NULL, NULL);
}

void
gnc_file_do_save_as (GtkWindow *parent, const char* filename)
{
    QofSession *new_session;
    QofSession *session;
    gchar *norm_file;
    gchar *newfile;
    const gchar *oldfile;

    gchar *scheme   = NULL;
    gchar *hostname = NULL;
    gchar *username = NULL;
    gchar *password = NULL;
    gchar *path = NULL;
    gint32 port = 0;


    QofBackendError io_err = ERR_BACKEND_NO_ERR;

    ENTER(" ");

    /* Convert user input into a normalized uri
     * Note that the normalized uri for internal use can have a password */
    norm_file = gnc_uri_normalize_uri ( filename, TRUE );
    if (!norm_file)
    {
        show_session_error (parent, ERR_FILEIO_FILE_NOT_FOUND, filename,
                            GNC_FILE_DIALOG_SAVE);
        return;
    }

    newfile = gnc_uri_add_extension (norm_file, GNC_DATAFILE_EXT);
    g_free (norm_file);
    gnc_uri_get_components (newfile, &scheme, &hostname,
                            &port, &username, &password, &path);

    /* Save As can't use the generic 'file' protocol. If the user didn't set
     * a specific protocol, assume the default 'xml'.
     */
    if (g_strcmp0 (scheme, "file") == 0)
    {
        g_free (scheme);
        scheme = g_strdup ("xml");
        norm_file = gnc_uri_create_uri (scheme, hostname, port,
                                        username, password, path);
        g_free (newfile);
        newfile = norm_file;
    }

    /* Some extra steps for file based uri's only
     * Note newfile is normalized uri so we can safely call
     * gnc_uri_is_file_scheme on it. */
    if (gnc_uri_is_file_scheme (scheme))
    {
        if (check_file_path (path))
        {
            show_session_error (parent, ERR_FILEIO_RESERVED_WRITE, newfile,
                    GNC_FILE_DIALOG_SAVE);
            return;
        }
        gnc_set_default_directory (GNC_PREFS_GROUP_OPEN_SAVE,
                       g_path_get_dirname (path));
    }

    /* Check to see if the user specified the same file as the current
     * file. If so, then just do a simple save, instead of a full save as */
    session = gnc_get_current_session ();
    oldfile = qof_session_get_url(session);
    if (strlen (oldfile) && (strcmp(oldfile, newfile) == 0))
    {
        g_free (newfile);
        gnc_file_save (parent);
        return;
    }

    /* Make sure all of the data from the old file is loaded */
    qof_event_suspend ();
    gnc_suspend_gui_refresh ();
    qof_session_ensure_all_data_loaded(session);
    gnc_resume_gui_refresh ();
    qof_event_resume ();

    /* -- this session code is NOT identical in FileOpen and FileSaveAs -- */

    save_in_progress++;

    new_session = qof_session_new (NULL);
    qof_session_begin (new_session, newfile, SESSION_NEW_STORE);

    io_err = qof_session_get_error (new_session);

    /* If the file exists and would be clobbered, ask the user */
    if (ERR_BACKEND_STORE_EXISTS == io_err)
    {
        const char *format = _("The file %s already exists. "
                               "Are you sure you want to overwrite it?");

        const char *name;
        if ( gnc_uri_is_file_uri ( newfile ) )
            name = gnc_uri_get_path ( newfile );
        else
            name = gnc_uri_normalize_uri ( newfile, FALSE );

        /* if user says cancel, we should break out */
        if (!gnc_verify_dialog (parent, FALSE, format, name ))
        {
            xaccLogDisable();
            qof_session_destroy (new_session);
            xaccLogEnable();
            g_free (newfile);
            save_in_progress--;
            return;
        }
        qof_session_begin (new_session, newfile, SESSION_NEW_OVERWRITE);
    }
    /* if file appears to be locked, ask the user ... */
    else if (ERR_BACKEND_LOCKED == io_err || ERR_BACKEND_READONLY == io_err)
    {
        if (!show_session_error (parent, io_err, newfile, GNC_FILE_DIALOG_SAVE))
        {
            // User wants to replace the file.
            qof_session_begin (new_session, newfile, SESSION_BREAK_LOCK);
        }
    }

    /* if the database doesn't exist, ask the user ... */
    else if ((ERR_FILEIO_FILE_NOT_FOUND == io_err) ||
             (ERR_BACKEND_NO_SUCH_DB == io_err) ||
             (ERR_SQL_DB_TOO_OLD == io_err))
    {
        if (!show_session_error (parent, io_err, newfile, GNC_FILE_DIALOG_SAVE))
        {
            /* user told us to create a new database. Do it. */
            qof_session_begin (new_session, newfile, SESSION_NEW_STORE);
        }
    }

    /* check again for session errors (since above dialog may have
     * cleared a file lock & moved things forward some more)
     * This time, errors will be fatal.
     */
    io_err = qof_session_get_error (new_session);
    if (ERR_BACKEND_NO_ERR != io_err)
    {
        show_session_error (parent, io_err, newfile, GNC_FILE_DIALOG_SAVE);
        xaccLogDisable();
        qof_session_destroy (new_session);
        xaccLogEnable();
        g_free (newfile);
        save_in_progress--;
        return;
    }

    /* If the new "file" is a database, attempt to store the password
     * in a keyring. GnuCash itself will not save it.
     */
    if ( !gnc_uri_is_file_scheme (scheme))
        gnc_keyring_set_password ( scheme, hostname, port,
                                   path, username, password );

    /* Prevent race condition between swapping the contents of the two
     * sessions, and actually installing the new session as the current
     * one. Any event callbacks that occur in this interval will have
     * problems if they check for the current book. */
    qof_event_suspend();

    /* if we got to here, then we've successfully gotten a new session */
    /* close up the old file session (if any) */
    qof_session_swap_data (session, new_session);
    qof_book_mark_session_dirty (qof_session_get_book (new_session));

    qof_event_resume();


    gnc_set_busy_cursor (NULL, TRUE);
    gnc_window_show_progress(_("Writing file…"), 0.0);
    qof_session_save (new_session, gnc_window_show_progress);
    gnc_window_show_progress(NULL, -1.0);
    gnc_unset_busy_cursor (NULL);

    io_err = qof_session_get_error( new_session );
    if ( ERR_BACKEND_NO_ERR != io_err )
    {
        /* Well, poop. The save failed, so the new session is invalid and we
         * need to restore the old one.
         */
        show_session_error (parent, io_err, newfile, GNC_FILE_DIALOG_SAVE);
        qof_event_suspend();
        qof_session_swap_data( new_session, session );
        qof_session_destroy( new_session );
        new_session = NULL;
        qof_event_resume();
    }
    else
    {
        /* Yay! Save was successful, we can dump the old session */
        qof_event_suspend();
        gnc_gui_component_reset_session (session, new_session);
        gnc_clear_current_session();
        gnc_set_current_session( new_session );
        qof_event_resume();
        session = NULL;

        xaccReopenLog();
        gnc_add_history (new_session);
        gnc_hook_run(HOOK_BOOK_SAVED, new_session);
    }
    /* --------------- END CORE SESSION CODE -------------- */

    save_in_progress--;

    g_free (newfile);
    LEAVE (" ");
}

void
gnc_file_revert (GtkWindow *parent)
{
    QofSession *session;
    const gchar *fileurl, *filename, *tmp;
    const gchar *title = _("Reverting will discard all unsaved changes to %s. Are you sure you want to proceed?");

    if (!gnc_main_window_all_finish_pending())
        return;

    session = gnc_get_current_session();
    fileurl = qof_session_get_url(session);
    if (!strlen (fileurl))
        fileurl = _("<unknown>");
    if ((tmp = strrchr(fileurl, '/')) != NULL)
        filename = tmp + 1;
    else
        filename = fileurl;

    if (!gnc_verify_dialog (parent, FALSE, title, filename))
        return;

    qof_book_mark_session_saved (qof_session_get_book (session));
    gnc_file_open_file (parent, fileurl, qof_book_is_readonly(gnc_get_current_book()));}

void
gnc_file_quit (void)
{
    QofSession *session;

    if (!gnc_current_session_exist ())
        return;
    gnc_set_busy_cursor (NULL, TRUE);
    session = gnc_get_current_session ();

    /* disable events; otherwise the mass deletion of accounts and
     * transactions during shutdown would cause massive redraws */
    qof_event_suspend ();

    gnc_hook_run(HOOK_BOOK_CLOSED, session);
    gnc_close_gui_component_by_session (session);
    gnc_state_save (session);
    gnc_clear_current_session();

    qof_event_resume ();
    gnc_unset_busy_cursor (NULL);
}

void
gnc_file_set_shutdown_callback (GNCShutdownCB cb)
{
    shutdown_cb = cb;
}

gboolean
gnc_file_save_in_progress (void)
{
    if (gnc_current_session_exist())
    {
        QofSession *session = gnc_get_current_session();
        return (qof_session_save_in_progress(session) || save_in_progress > 0);
    }
    return FALSE;
}
