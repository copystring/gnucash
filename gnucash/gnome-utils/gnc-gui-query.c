/********************************************************************\
 * gnc-gui-query.c -- functions for creating dialogs for GnuCash    *
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
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       *
 * Boston, MA  02110-1301,  USA       gnu@gnu.org                   *
\********************************************************************/

#include <config.h>

#include <glib/gi18n.h>

#include "dialog-utils.h"
#include "gnc-gtk-utils.h"
#include "qof.h"
#include "gnc-gui-query.h"
#include "gnc-ui.h"

/* This static indicates the debugging module that this .o belongs to.  */
/* static short module = MOD_GUI; */

/* GtkAlertDialog in GTK 4.24 keeps a borrowed dialog in its GTask and
 * leaves the response handler connected after cancellation. Natural transient
 * parent teardown can therefore destroy that dialog before choose_finish, or
 * return the task twice. Own the decision window directly, as the input dialog
 * below does, instead of trying to repair GTK's private task/response state. */
typedef struct
{
    GtkWindow *dialog;
    GWeakRef parent;
    GListModel *toplevels;
    gulong toplevels_handler;
    gboolean has_parent;
    gboolean parent_destroyed;
    GPtrArray *controls;
    GncGuiChoiceCallback completed;
    gpointer user_data;
    GDestroyNotify destroy;
    gint cancel_choice;
    gint response;
    gboolean done;
} GncGuiChoiceRequest;

static gboolean
gnc_gui_choice_dispatch (gpointer data)
{
    GncGuiChoiceRequest *request = data;

    GtkWindow *parent = g_weak_ref_get (&request->parent);
    if (request->dialog)
    {
        gtk_window_destroy (request->dialog);
        g_clear_object (&request->dialog);
    }
    /* Keep observing through synchronous cleanup: external dialog handlers
     * may destroy the parent here. */
    g_signal_handler_disconnect (request->toplevels, request->toplevels_handler);
    g_clear_object (&request->toplevels);
    if (request->parent_destroyed)
        g_clear_object (&parent);
    if (request->completed)
        request->completed (parent, request->parent_destroyed ? request->cancel_choice :
                             request->response, request->user_data);
    if (request->destroy)
        request->destroy (request->user_data);
    g_clear_object (&parent);
    g_weak_ref_clear (&request->parent);
    g_free (request);
    return G_SOURCE_REMOVE;
}

static void
gnc_gui_choice_disconnect (GncGuiChoiceRequest *request)
{
    for (guint i = 0; i < request->controls->len; i++)
        g_signal_handlers_disconnect_by_data (
            g_ptr_array_index (request->controls, i), request);
    g_clear_pointer (&request->controls, g_ptr_array_unref);
}

static void
gnc_gui_choice_complete (GncGuiChoiceRequest *request, gint response)
{
    GtkWindow *dialog;

    if (request->done)
        return;
    request->done = TRUE;
    request->response = response;
    gnc_gui_choice_disconnect (request);
    dialog = request->dialog;
    g_signal_handlers_disconnect_by_data (dialog, request);
    gtk_widget_set_visible (GTK_WIDGET (dialog), FALSE);

    /* Defer the owner continuation and final window release to the main loop,
     * rather than calling it directly from this signal or initiating call. */
    g_idle_add (gnc_gui_choice_dispatch, request);
}

static void
gnc_gui_choice_destroyed (GtkWidget *dialog, GncGuiChoiceRequest *request)
{
    (void)dialog;
    /* gtk_window_destroy unrealizes immediately but destroy/dispose can be
     * delayed by references (including ours). Finish outside the native
     * destroy stack; never recursively destroy a window while it unrealizes. */
    gnc_gui_choice_complete (request, request->cancel_choice);
}

static gboolean
gnc_gui_choice_closed (GtkWindow *dialog, GncGuiChoiceRequest *request)
{
    (void)dialog;
    gnc_gui_choice_complete (request, request->cancel_choice);
    return TRUE;
}

static gboolean
gnc_gui_choice_is_toplevel (GListModel *windows, GtkWindow *window)
{
    for (guint i = 0; i < g_list_model_get_n_items (windows); i++)
    {
        GObject *candidate = g_list_model_get_item (windows, i);
        gboolean found = candidate == G_OBJECT (window);
        g_object_unref (candidate);
        if (found)
            return TRUE;
    }
    return FALSE;
}

static void
gnc_gui_choice_toplevels_changed (GListModel *windows, guint position,
                                   guint removed, guint added, gpointer data)
{
    GncGuiChoiceRequest *request = data;
    GtkWindow *parent = g_weak_ref_get (&request->parent);
    (void)position;
    (void)added;
    if (removed)
    {
        /* gtk_window_destroy removes the window immediately, independently
         * of realization and references delaying GtkWidget::destroy. */
        if (request->has_parent &&
            (!parent || !gnc_gui_choice_is_toplevel (windows, parent)))
            request->parent_destroyed = TRUE;
        if (!request->done && (request->parent_destroyed ||
            !gnc_gui_choice_is_toplevel (windows, request->dialog)))
            gnc_gui_choice_complete (request, request->cancel_choice);
    }
    g_clear_object (&parent);
}

static gboolean
gnc_gui_choice_key_pressed (GtkEventControllerKey *controller, guint keyval,
                            guint keycode, GdkModifierType state, gpointer data)
{
    (void)controller;
    (void)keycode;
    (void)state;
    if (keyval != GDK_KEY_Escape)
        return FALSE;
    gnc_gui_choice_closed (NULL, data);
    return TRUE;
}

static void
gnc_gui_choice_clicked (GtkButton *button, GncGuiChoiceRequest *request)
{
    gint choice = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button),
                                                    "gnc-choice")) - 1;
    gnc_gui_choice_complete (request, choice);
}

static void
gnc_gui_choice_show (GtkWindow *parent, const gchar *title,
                     const gchar *message, const gchar * const *buttons,
                     gint default_choice, gint cancel_choice,
                     GncGuiChoiceCallback completed,
                     gpointer user_data, GDestroyNotify destroy)
{
    GncGuiChoiceRequest *request = g_new0 (GncGuiChoiceRequest, 1);
    GtkWidget *dialog, *content, *label, *actions;
    GtkEventController *keys;
    guint n_buttons = g_strv_length ((gchar **)buttons);

    g_weak_ref_init (&request->parent, parent);
    request->has_parent = parent != NULL;
    request->completed = completed;
    request->user_data = user_data;
    request->destroy = destroy;
    request->cancel_choice = cancel_choice;
    request->controls = g_ptr_array_new_with_free_func (g_object_unref);
    dialog = gtk_window_new ();
    request->dialog = GTK_WINDOW (g_object_ref_sink (dialog));
    gtk_window_set_title (request->dialog, title);
    gtk_window_set_modal (request->dialog, TRUE);
    gtk_window_set_transient_for (request->dialog, parent);
    gtk_window_set_destroy_with_parent (request->dialog, TRUE);
    gtk_window_set_resizable (request->dialog, FALSE);
    g_signal_connect (dialog, "close-request",
                      G_CALLBACK (gnc_gui_choice_closed), request);
    g_signal_connect (dialog, "destroy",
                      G_CALLBACK (gnc_gui_choice_destroyed), request);
    g_signal_connect (dialog, "unrealize",
                      G_CALLBACK (gnc_gui_choice_destroyed), request);

    content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top (content, 12);
    gtk_widget_set_margin_bottom (content, 12);
    gtk_widget_set_margin_start (content, 12);
    gtk_widget_set_margin_end (content, 12);
    label = gtk_label_new (message);
    gtk_label_set_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_max_width_chars (GTK_LABEL (label), 72);
    gtk_box_append (GTK_BOX (content), label);
    actions = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign (actions, GTK_ALIGN_END);
    for (guint i = 0; i < n_buttons; i++)
    {
        GtkWidget *button = gtk_button_new_with_mnemonic (buttons[i]);
        g_object_set_data (G_OBJECT (button), "gnc-choice", GINT_TO_POINTER (i + 1));
        g_signal_connect (button, "clicked", G_CALLBACK (gnc_gui_choice_clicked), request);
        g_ptr_array_add (request->controls, g_object_ref (button));
        gtk_box_append (GTK_BOX (actions), button);
        if (i == CLAMP (default_choice, 0, (gint)n_buttons - 1))
            gtk_window_set_default_widget (request->dialog, button);
    }
    gtk_box_append (GTK_BOX (content), actions);
    gtk_window_set_child (request->dialog, content);
    request->toplevels = g_object_ref (gtk_window_get_toplevels ());
    request->toplevels_handler = g_signal_connect (request->toplevels, "items-changed",
        G_CALLBACK (gnc_gui_choice_toplevels_changed), request);
    keys = gtk_event_controller_key_new ();
    gtk_event_controller_set_propagation_phase (keys, GTK_PHASE_CAPTURE);
    g_signal_connect (keys, "key-pressed",
                      G_CALLBACK (gnc_gui_choice_key_pressed), request);
    /* Controller lifetime is bound to the dialog and can outlive its destroy
     * signal. Disconnect it together with the buttons before freeing request. */
    g_ptr_array_add (request->controls, g_object_ref (keys));
    gtk_widget_add_controller (dialog, keys);
    gtk_window_present (request->dialog);
}

typedef struct
{
    gint response_map[2];
    gint cancel_response;
    GncGuiQueryResponseCallback completed;
    gpointer user_data;
} GncGuiQueryRequest;

static void
gnc_gui_query_finished (GtkWindow *parent, gint choice, gpointer data)
{
    GncGuiQueryRequest *request = data;
    gint response = choice >= 0 && choice < 2 ? request->response_map[choice] :
                    request->cancel_response;
    if (request->completed)
        request->completed (parent, response, request->user_data);
}

static void
gnc_gui_query_async_va (GtkWindow *parent, const gchar *first_button,
                        const gchar *second_button, gint first_response,
                        gint second_response, gint cancel_button,
                        gint cancel_response, gint default_button,
                        GncGuiQueryResponseCallback completed, gpointer user_data,
                        const gchar *format, va_list args)
{
    GncGuiQueryRequest *request;
    const gchar *buttons[] = {first_button, second_button, NULL};
    gchar *message;

    g_return_if_fail (completed != NULL);
    request = g_new0 (GncGuiQueryRequest, 1);
    request->response_map[0] = first_response;
    request->response_map[1] = second_response;
    request->cancel_response = cancel_response;
    request->completed = completed;
    request->user_data = user_data;
    message = g_strdup_vprintf (format, args);
    gnc_gui_choice_show (parent ? parent : gnc_ui_get_main_window (NULL),
                         _("Confirm"), message, buttons, default_button,
                         cancel_button, gnc_gui_query_finished, request, g_free);
    g_free (message);
}

void
gnc_ok_cancel_dialog_async (GtkWindow *parent, gint default_result,
                            GncGuiQueryResponseCallback completed,
                            gpointer user_data, const gchar *format, ...)
{
    va_list args;

    va_start (args, format);
    gnc_gui_query_async_va (parent, _("Cancel"), _("OK"), GTK_RESPONSE_CANCEL,
                            GTK_RESPONSE_OK, 0, GTK_RESPONSE_CANCEL,
                            default_result == GTK_RESPONSE_OK ? 1 : 0,
                            completed, user_data, format, args);
    va_end (args);
}

void
gnc_verify_dialog_async (GtkWindow *parent, gboolean yes_is_default,
                         GncGuiQueryResponseCallback completed, gpointer user_data,
                         const gchar *format, ...)
{
    va_list args;

    va_start (args, format);
    gnc_gui_query_async_va (parent, _("No"), _("Yes"), GTK_RESPONSE_NO,
                            GTK_RESPONSE_YES, 0, GTK_RESPONSE_NO,
                            yes_is_default ? 1 : 0,
                            completed, user_data, format, args);
    va_end (args);
}

void
gnc_action_dialog_async (GtkWindow *parent, const gchar *action,
                         gboolean action_default,
                         GncGuiQueryResponseCallback completed, gpointer user_data,
                         const gchar *format, ...)
{
    va_list args;

    g_return_if_fail (action != NULL);
    va_start (args, format);
    gnc_gui_query_async_va (parent, action, _("Cancel"), GTK_RESPONSE_ACCEPT,
                            GTK_RESPONSE_CANCEL, 1, GTK_RESPONSE_CANCEL,
                            action_default ? 0 : 1,
                            completed, user_data, format, args);
    va_end (args);
}

void
gnc_choose_option_dialog_async (GtkWindow *parent, const gchar *title,
                                 const gchar *message, GList *choices,
                                 gint default_choice,
                                 GncGuiChoiceCallback completed,
                                 gpointer user_data)
{
    guint n_buttons = g_list_length (choices);
    const gchar **buttons;
    guint i = 0;

    g_return_if_fail (title != NULL);
    g_return_if_fail (message != NULL);
    g_return_if_fail (n_buttons > 0);
    g_return_if_fail (completed != NULL);
    for (GList *node = choices; node; node = node->next)
        g_return_if_fail (node->data != NULL);
    if (!parent)
        parent = gnc_ui_get_main_window (NULL);
    buttons = g_new0 (const gchar *, n_buttons + 1);
    for (GList *node = choices; node; node = node->next)
        buttons[i++] = node->data;
    gnc_gui_choice_show (parent, title, message,
                         buttons, default_choice, -1, completed, user_data, NULL);
    g_free (buttons);
}

static void
gnc_message_dialog_common (GtkWindow *parent, const gchar *format,
                           GtkMessageType msg_type, va_list args)
{
    GtkAlertDialog *dialog;
    gchar *buffer;

    if (!parent)
        parent = gnc_ui_get_main_window (NULL);

    buffer = g_strdup_vprintf (format, args);
    dialog = gtk_alert_dialog_new ("%s", buffer);
    gtk_alert_dialog_show (dialog, parent);
    g_object_unref (dialog);
    g_free (buffer);

    /* GtkAlertDialog deliberately has no message-type property. The caller's
     * distinction remains semantic; the native platform controls presentation. */
    (void)msg_type;
}

/********************************************************************\
 * gnc_info_dialog                                                  *
 *   displays an information dialog box                             *
 *                                                                  *
 * Args:   parent  - the parent window                              *
 *         format - the format string for the message to display    *
 *                   This is a standard 'printf' style string.      *
 *         args - a pointer to the first argument for the format    *
 *                string.                                           *
 * Return: none                                                     *
\********************************************************************/
void
gnc_info_dialog (GtkWindow *parent, const gchar *format, ...)
{
    va_list args;

    va_start (args, format);
    gnc_message_dialog_common (parent, format, GTK_MESSAGE_INFO, args);
    va_end (args);
}



/********************************************************************\
 * gnc_warning_dialog                                               *
 *   displays a warning dialog box                                  *
 *                                                                  *
 * Args:   parent  - the parent window                              *
 *         format - the format string for the message to display    *
 *                   This is a standard 'printf' style string.      *
 *         args - a pointer to the first argument for the format    *
 *                string.                                           *
 * Return: none                                                     *
\********************************************************************/

void
gnc_warning_dialog (GtkWindow *parent, const gchar *format, ...)
{
    va_list args;

    va_start (args, format);
    gnc_message_dialog_common (parent, format, GTK_MESSAGE_WARNING, args);
    va_end (args);
}


/********************************************************************\
 * gnc_error_dialog                                                 *
 *   displays an error dialog box                                   *
 *                                                                  *
 * Args:   parent  - the parent window                              *
 *         format - the format string for the message to display    *
 *                   This is a standard 'printf' style string.      *
 *         args - a pointer to the first argument for the format    *
 *                string.                                           *
 * Return: none                                                     *
\********************************************************************/
void gnc_error_dialog (GtkWindow* parent, const char* format, ...)
{
    va_list args;

    va_start (args, format);
    gnc_message_dialog_common (parent, format, GTK_MESSAGE_ERROR, args);
    va_end (args);
}

typedef struct
{
    GWeakRef parent;
    gulong parent_destroy_handler;
    GtkWindow *dialog;
    GtkTextView *view;
    GncInputDialogCallback completed;
    gpointer user_data;
    gboolean done;
} GncInputDialogRequest;

static void gnc_input_dialog_complete (GncInputDialogRequest *request,
                                       gboolean accepted);

static void
input_dialog_request_free (GncInputDialogRequest *request)
{
    GtkWindow *parent = GTK_WINDOW (g_weak_ref_get (&request->parent));

    if (parent && request->parent_destroy_handler)
        g_signal_handler_disconnect (parent, request->parent_destroy_handler);
    g_clear_object (&parent);
    g_weak_ref_clear (&request->parent);
    g_free (request);
}

static void
input_dialog_destroy_window (GncInputDialogRequest *request)
{
    GtkWindow *dialog = g_steal_pointer (&request->dialog);

    if (!dialog)
        return;

    g_signal_handlers_disconnect_by_data (dialog, request);
    gtk_window_destroy (dialog);
    g_object_unref (dialog);
}

static gchar *
input_dialog_get_text (GncInputDialogRequest *request)
{
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;

    buffer = gtk_text_view_get_buffer (request->view);
    gtk_text_buffer_get_start_iter (buffer, &start);
    gtk_text_buffer_get_end_iter (buffer, &end);
    return gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
}

static void
input_dialog_parent_destroyed_cb (GtkWidget *widget,
                                  GncInputDialogRequest *request)
{
    (void)widget;
    request->parent_destroy_handler = 0;
    gnc_input_dialog_complete (request, FALSE);
}

static gboolean
input_dialog_close_request_cb (GtkWindow *dialog,
                               GncInputDialogRequest *request)
{
    (void)dialog;
    gnc_input_dialog_complete (request, FALSE);
    return TRUE;
}

static void
input_dialog_destroy_cb (GtkWidget *widget, GncInputDialogRequest *request)
{
    (void)widget;
    if (!request->done)
        g_clear_object (&request->dialog);
    gnc_input_dialog_complete (request, FALSE);
}

static void
input_dialog_accept_clicked_cb (GtkButton *button,
                                GncInputDialogRequest *request)
{
    (void)button;
    gnc_input_dialog_complete (request, TRUE);
}

static void
input_dialog_cancel_clicked_cb (GtkButton *button,
                                GncInputDialogRequest *request)
{
    (void)button;
    gnc_input_dialog_complete (request, FALSE);
}

static void
gnc_input_dialog_complete (GncInputDialogRequest *request, gboolean accepted)
{
    gchar *input = NULL;

    if (!request || request->done)
        return;

    request->done = TRUE;
    if (accepted && request->view)
        input = input_dialog_get_text (request);
    input_dialog_destroy_window (request);
    request->completed (input, request->user_data);
    input_dialog_request_free (request);
}

void
gnc_input_dialog_async (GtkWindow *parent, const gchar *title, const gchar *msg,
                        const gchar *default_input,
                        GncInputDialogCallback completed, gpointer user_data)
{
    GncInputDialogRequest *request;
    GtkWidget *root;
    GtkWidget *content;
    GtkWidget *label;
    GtkWidget *scrolled;
    GtkWidget *actions;
    GtkWidget *cancel;
    GtkWidget *accept;

    g_return_if_fail (completed != NULL);

    request = g_new0 (GncInputDialogRequest, 1);
    request->completed = completed;
    request->user_data = user_data;
    g_weak_ref_init (&request->parent, parent);
    if (parent)
        request->parent_destroy_handler = g_signal_connect (
            parent, "destroy", G_CALLBACK (input_dialog_parent_destroyed_cb), request);

    request->dialog = GTK_WINDOW (g_object_ref_sink (gtk_window_new ()));
    gnc_window_bind_to_application (request->dialog);
    gtk_window_set_title (request->dialog, title);
    gtk_window_set_modal (request->dialog, TRUE);
    if (parent)
        gtk_window_set_transient_for (request->dialog, parent);

    root = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start (root, 12);
    gtk_widget_set_margin_end (root, 12);
    gtk_widget_set_margin_top (root, 12);
    gtk_widget_set_margin_bottom (root, 12);
    gtk_window_set_child (request->dialog, root);

    content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    label = gtk_label_new (msg);
    gtk_label_set_wrap (GTK_LABEL (label), TRUE);
    gtk_label_set_xalign (GTK_LABEL (label), 0.0);
    gtk_box_append (GTK_BOX (content), label);

    request->view = GTK_TEXT_VIEW (gtk_text_view_new ());
    gtk_text_view_set_wrap_mode (request->view, GTK_WRAP_WORD_CHAR);
    gtk_text_buffer_set_text (gtk_text_view_get_buffer (request->view),
                              default_input ? default_input : "", -1);
    scrolled = gtk_scrolled_window_new ();
    gtk_widget_set_size_request (scrolled, 480, 160);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled),
                                   GTK_WIDGET (request->view));
    gtk_box_append (GTK_BOX (content), scrolled);
    gtk_box_append (GTK_BOX (root), content);

    actions = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign (actions, GTK_ALIGN_END);
    cancel = gtk_button_new_with_mnemonic (_("_Cancel"));
    accept = gtk_button_new_with_mnemonic (_("_OK"));
    gtk_widget_set_receives_default (accept, TRUE);
    gtk_box_append (GTK_BOX (actions), cancel);
    gtk_box_append (GTK_BOX (actions), accept);
    gtk_box_append (GTK_BOX (root), actions);

    gtk_window_set_default_widget (request->dialog, accept);
    g_signal_connect (accept, "clicked",
                      G_CALLBACK (input_dialog_accept_clicked_cb), request);
    g_signal_connect (cancel, "clicked",
                      G_CALLBACK (input_dialog_cancel_clicked_cb), request);
    g_signal_connect (request->dialog, "close-request",
                      G_CALLBACK (input_dialog_close_request_cb), request);
    g_signal_connect (request->dialog, "destroy",
                      G_CALLBACK (input_dialog_destroy_cb), request);

    gtk_window_present (request->dialog);
    gtk_widget_grab_focus (GTK_WIDGET (request->view));
}
void
gnc_info2_dialog (GtkWidget *parent, const gchar *title, const gchar *msg)
{
    GtkWindow *window;
    GtkWidget *content;
    GtkWidget *view;
    GtkWidget *scrolled_window;
    GtkWidget *close_button;
    GtkTextBuffer *buffer;
    gint width;
    gint height;

    window = GTK_WINDOW (gtk_window_new ());
    gnc_window_bind_to_application (window);
    gtk_window_set_title (window, title);
    gtk_window_set_modal (window, TRUE);
    if (GTK_IS_WINDOW (parent))
    {
        gtk_window_set_transient_for (window, GTK_WINDOW (parent));
        gtk_window_get_default_size (GTK_WINDOW (parent), &width, &height);
        gtk_window_set_default_size (window, width, height);
    }
    else
    {
        gtk_window_set_default_size (window, 600, 400);
    }

    content = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start (content, 12);
    gtk_widget_set_margin_end (content, 12);
    gtk_widget_set_margin_top (content, 12);
    gtk_widget_set_margin_bottom (content, 12);
    gtk_window_set_child (window, content);

    scrolled_window = gtk_scrolled_window_new ();
    gtk_widget_set_vexpand (scrolled_window, TRUE);
    gtk_box_append (GTK_BOX (content), scrolled_window);

    view = gtk_text_view_new ();
    gtk_text_view_set_editable (GTK_TEXT_VIEW (view), FALSE);
    buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
    gtk_text_buffer_set_text (buffer, msg, -1);
    gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), view);

    close_button = gtk_button_new_with_mnemonic (_("_Close"));
    gtk_widget_set_halign (close_button, GTK_ALIGN_END);
    gtk_box_append (GTK_BOX (content), close_button);
    g_signal_connect_swapped (close_button, "clicked", G_CALLBACK (gtk_window_destroy),
                              window);
    gtk_window_present (window);
}
