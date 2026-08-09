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
#include "qof.h"
#include "gnc-gui-query.h"
#include "gnc-ui.h"

#define INDEX_LABEL "index"

/* This static indicates the debugging module that this .o belongs to.  */
/* static short module = MOD_GUI; */

typedef struct
{
    GWeakRef parent;
    gboolean has_parent;
    GncGuiQueryResponseCallback completed;
    gpointer user_data;
    gchar **buttons;
    gint responses[2];
    gint cancel_response;
} GncGuiQueryRequest;

static void
gnc_gui_query_request_free (GncGuiQueryRequest *request)
{
    g_weak_ref_clear (&request->parent);
    g_strfreev (request->buttons);
    g_free (request);
}

static void
gnc_gui_query_finished (GObject *source, GAsyncResult *result,
                        gpointer user_data)
{
    GncGuiQueryRequest *request = user_data;
    GError *error = NULL;
    GtkWindow *parent = GTK_WINDOW (g_weak_ref_get (&request->parent));
    gint choice = gtk_alert_dialog_choose_finish (GTK_ALERT_DIALOG (source), result,
                                                  &error);
    gint response = request->cancel_response;

    if (!error && choice >= 0 && choice < 2)
        response = request->responses[choice];
    else if (error && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        g_warning ("Decision dialog failed: %s", error->message);
    if (request->has_parent && !parent)
        response = request->cancel_response;

    request->completed (parent, response, request->user_data);
    g_clear_error (&error);
    g_clear_object (&parent);
    gnc_gui_query_request_free (request);
}

static void
gnc_gui_query_async_va (GtkWindow *parent, const gchar *first_button,
                        const gchar *second_button, gint first_response,
                        gint second_response, gint default_button,
                        GncGuiQueryResponseCallback completed, gpointer user_data,
                        const gchar *format, va_list args)
{
    GncGuiQueryRequest *request;
    GtkAlertDialog *dialog;
    gchar *message;

    g_return_if_fail (completed != NULL);
    if (!parent)
        parent = gnc_ui_get_main_window (NULL);

    request = g_new0 (GncGuiQueryRequest, 1);
    g_weak_ref_init (&request->parent, parent);
    request->has_parent = parent != NULL;
    request->completed = completed;
    request->user_data = user_data;
    request->buttons = g_new0 (gchar *, 3);
    request->buttons[0] = g_strdup (first_button);
    request->buttons[1] = g_strdup (second_button);
    request->responses[0] = first_response;
    request->responses[1] = second_response;
    request->cancel_response = second_response;

    message = g_strdup_vprintf (format, args);
    dialog = gtk_alert_dialog_new ("%s", message);
    gtk_alert_dialog_set_buttons (dialog, (const char * const *)request->buttons);
    gtk_alert_dialog_set_default_button (dialog, default_button);
    gtk_alert_dialog_set_cancel_button (dialog, 1);
    gtk_alert_dialog_choose (dialog, parent, NULL, gnc_gui_query_finished, request);
    g_object_unref (dialog);
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
                            GTK_RESPONSE_OK,
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
                            GTK_RESPONSE_YES, yes_is_default ? 1 : 0,
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
                            GTK_RESPONSE_CANCEL, action_default ? 0 : 1,
                            completed, user_data, format, args);
    va_end (args);
}

/********************************************************************\
 * gnc_ok_cancel_dialog                                             *
 *   display a message, and asks the user to press "Ok" or "Cancel" *
 *                                                                  *
 * NOTE: This function does not return until the dialog is closed   *
 *                                                                  *
 * Args:   parent  - the parent window                              *
 *         default - the button that will be the default            *
 *         message - the message to display                         *
 *         format - the format string for the message to display    *
 *                   This is a standard 'printf' style string.      *
 *         args - a pointer to the first argument for the format    *
 *                string.                                           *
 * Return: the result the user selected                             *
\********************************************************************/
gint
gnc_ok_cancel_dialog (GtkWindow *parent,
                      gint default_result,
                      const gchar *format, ...)
{
    GtkWidget *dialog = NULL;
    gint result;
    gchar *buffer;
    va_list args;

    if (!parent)
        parent = gnc_ui_get_main_window (NULL);

    va_start (args, format);
    buffer = g_strdup_vprintf (format, args);
    dialog = gtk_message_dialog_new (parent,
                                     GTK_DIALOG_MODAL |
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_QUESTION,
                                     GTK_BUTTONS_OK_CANCEL,
                                     "%s",
                                     buffer);
    g_free (buffer);
    va_end (args);

//FIXME gtk4    if (!parent)
//        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dialog), FALSE);

    gtk_dialog_set_default_response (GTK_DIALOG(dialog), default_result);
    result = gnc_dialog_run_non_destructive (GTK_DIALOG(dialog));

    return (result);
}

gboolean
gnc_action_dialog (GtkWindow *parent, const gchar *action,
                   gboolean action_default, const gchar *format, ...)
{
    g_return_val_if_fail (action, FALSE);

    if (!parent)
        parent = gnc_ui_get_main_window (NULL);

    va_list args;
    va_start(args, format);
    gchar *buffer = g_strdup_vprintf(format, args);
    va_end(args);

    GtkWidget *dialog = gtk_message_dialog_new (parent,
                                                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
                                                "%s", buffer);

    gtk_dialog_add_button (GTK_DIALOG(dialog), action, GTK_RESPONSE_ACCEPT);
    gtk_dialog_add_button (GTK_DIALOG(dialog), _("_Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_set_default_response (GTK_DIALOG(dialog), action_default ?
                                     GTK_RESPONSE_ACCEPT : GTK_RESPONSE_CANCEL);

    gint result = gnc_dialog_run(GTK_DIALOG(dialog));
    g_free(buffer);

    return result == GTK_RESPONSE_ACCEPT;
}

/********************************************************************\
 * gnc_verify_dialog                                                *
 *   display a message, and asks the user to press "Yes" or "No"    *
 *                                                                  *
 * NOTE: This function does not return until the dialog is closed   *
 *                                                                  *
 * Args:   parent  - the parent window                              *
 *         yes_is_default - If true, "Yes" is default,              *
 *                          "No" is the default button.             *
 *         format - the format string for the message to display    *
 *                   This is a standard 'printf' style string.      *
 *         args - a pointer to the first argument for the format    *
 *                string.                                           *
\********************************************************************/
gboolean
gnc_verify_dialog(GtkWindow *parent, gboolean yes_is_default,
                  const gchar *format, ...)
{
    GtkWidget *dialog;
    gchar *buffer;
    gint result;
    va_list args;

    if (!parent)
        parent = gnc_ui_get_main_window (NULL);

    va_start (args, format);
    buffer = g_strdup_vprintf (format, args);
    dialog = gtk_message_dialog_new (parent,
                                     GTK_DIALOG_MODAL |
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_MESSAGE_QUESTION,
                                     GTK_BUTTONS_YES_NO,
                                     "%s",
                                     buffer);
    g_free (buffer);
    va_end (args);

//FIXME gtk4    if (!parent)
//        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dialog), FALSE);

    gtk_dialog_set_default_response (GTK_DIALOG(dialog),
                                    (yes_is_default ? GTK_RESPONSE_YES : GTK_RESPONSE_NO));
    result = gnc_dialog_run (GTK_DIALOG(dialog));

    return (result == GTK_RESPONSE_YES);
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

static void
gnc_choose_check_button_cb (GtkWidget *w, gpointer data)
{
    int *result = data;

    if (gtk_check_button_get_active (GTK_CHECK_BUTTON(w)))
        *result = GPOINTER_TO_INT(g_object_get_data (G_OBJECT(w), INDEX_LABEL));
}

/********************************************************************
 gnc_choose_radio_option_dialog

 display a group of radio_buttons and return the index of
 the selected one
*/

int
gnc_choose_radio_option_dialog (GtkWidget *parent,
                                const char *title,
                                const char *msg,
                                const char *button_name,
                                int default_value,
                                GList *radio_list)
{
    int radio_result = 0; /* initial selected value is first one */
    GtkWidget *vbox;
    GtkWidget *main_vbox;
    GtkWidget *label;
    GtkWidget *check_button;
    GtkWidget *dialog;
    GtkWidget *dvbox;
    GtkWidget *first_check_button;
    GList *node;
    int i;

    main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
    gtk_box_set_homogeneous (GTK_BOX (main_vbox), FALSE);
    gnc_box_set_all_margins (GTK_BOX(main_vbox), 6);
    gtk_widget_set_visible (GTK_WIDGET(main_vbox), TRUE);

    label = gtk_label_new(msg);
    gtk_label_set_justify (GTK_LABEL(label), GTK_JUSTIFY_LEFT);
    gtk_box_append (GTK_BOX(main_vbox), GTK_WIDGET(label));
    gtk_widget_set_visible (GTK_WIDGET(label), TRUE);

    vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
    gtk_box_set_homogeneous (GTK_BOX(vbox), TRUE);
    gnc_box_set_all_margins (GTK_BOX(vbox), 6);
    gtk_box_prepend (GTK_BOX(main_vbox), GTK_WIDGET(vbox));
    gtk_widget_set_visible (GTK_WIDGET(vbox), TRUE);

    for (node = radio_list, i = 0; node; node = node->next, i++)
    {
        check_button = gtk_check_button_new_with_mnemonic (node->data);

        if (i == 0)
            first_check_button = check_button;
        else
            gtk_check_button_set_group (GTK_CHECK_BUTTON(check_button),
                                        GTK_CHECK_BUTTON(first_check_button));

        gtk_widget_set_halign (GTK_WIDGET(check_button), GTK_ALIGN_START);

        if (i == default_value) /* default is first radio button */
        {
            gtk_check_button_set_active (GTK_CHECK_BUTTON(check_button), TRUE);
            radio_result = default_value;
        }

        gtk_widget_set_visible (GTK_WIDGET(check_button), TRUE);
        gtk_box_append (GTK_BOX(vbox), GTK_WIDGET(check_button));
        g_object_set_data (G_OBJECT(check_button), INDEX_LABEL, GINT_TO_POINTER(i));
        g_signal_connect (G_OBJECT(check_button), "clicked",
                          G_CALLBACK(gnc_choose_check_button_cb),
                          &radio_result);
    }

    if (!button_name)
        button_name = _("_OK");
    dialog = gtk_dialog_new_with_buttons (title,
                                          GTK_WINDOW(parent),
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          _("_Cancel"),
                                          GTK_RESPONSE_CANCEL,
                                          button_name,
                                          GTK_RESPONSE_OK,
                                          NULL);

    /* default to ok */
    gtk_dialog_set_default_response (GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    dvbox = gtk_dialog_get_content_area (GTK_DIALOG(dialog));

    gtk_box_append (GTK_BOX(dvbox), GTK_WIDGET(main_vbox));

    if (gnc_dialog_run (GTK_DIALOG(dialog)) != GTK_RESPONSE_OK)
        radio_result = -1;

    return radio_result;
}

static gchar *
gnc_input_dialog_internal (GtkWidget *parent, const gchar *title,
                           const gchar *msg, const gchar *default_input,
                           gboolean use_entry)
{
    gint result;
    GtkWidget *view;
    GtkTextBuffer *buffer;
    gchar *user_input = NULL;
    GtkTextIter start, end;

    /* Create the widgets */
    GtkWidget* dialog = gtk_dialog_new_with_buttons (title,
                                                     GTK_WINDOW(parent),
                                                     GTK_DIALOG_MODAL |
                                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     _("_OK"), GTK_RESPONSE_ACCEPT,
                                                     _("_Cancel"), GTK_RESPONSE_REJECT,
                                                     NULL);
    GtkWidget* content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));

    // add a label
    GtkWidget* label = gtk_label_new (msg);
    gtk_box_append (GTK_BOX(content_area), GTK_WIDGET(label));

    // add a textview or an entry.
    if (use_entry)
    {
        view = gtk_entry_new ();
        gnc_entry_set_text (GTK_ENTRY (view), default_input);
    }
    else
    {
        view = gtk_text_view_new ();
        gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (view), GTK_WRAP_WORD_CHAR);
        buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
        gtk_text_buffer_set_text (buffer, default_input, -1);
    }
    gtk_box_append (GTK_BOX(content_area), GTK_WIDGET(view));

    // run the dialog
    result = gnc_dialog_run (GTK_DIALOG(dialog));

    if (result != GTK_RESPONSE_REJECT)
    {
        if (use_entry)
            user_input = g_strdup (gnc_entry_get_text ((GTK_ENTRY(view))));
        else
        {
            gtk_text_buffer_get_start_iter (buffer, &start);
            gtk_text_buffer_get_end_iter (buffer, &end);
            user_input = gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
        }
    }
    gtk_window_destroy (GTK_WINDOW(dialog));
    return user_input;
}

/********************************************************************\
 * gnc_input_dialog                                                 *
 *   simple convenience dialog to get a single value from the user  *
 *   user may choose between "Ok" and "Cancel"                      *
 *                                                                  *
 * NOTE: This function does not return until the dialog is closed   *
 *                                                                  *
 * Args:   parent  - the parent window or NULL                      *
 *         title   - the title of the dialog                        *
 *         msg     - the message to display                         *
 *         default_input - will be displayed as default input       *
 * Return: the input (text) the user entered, if pressed "Ok"       *
 *         NULL, if pressed "Cancel"                                *
 \********************************************************************/
gchar *
gnc_input_dialog (GtkWidget *parent, const gchar *title, const gchar *msg,
                  const gchar *default_input)
{
    return gnc_input_dialog_internal (parent, title, msg, default_input, FALSE);
}

/********************************************************************\
 * gnc_input_dialog_with_entry                                      *
 *   Similar to gnc_input_dialog but use a single line entry widget *
 *   user may choose between "Ok" and "Cancel"                      *
 \********************************************************************/
gchar *
gnc_input_dialog_with_entry (GtkWidget *parent, const gchar *title,
                             const gchar *msg, const gchar *default_input)
{
    return gnc_input_dialog_internal (parent, title, msg, default_input, TRUE);
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
