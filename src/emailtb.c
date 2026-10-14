#define _GNU_SOURCE 1

#include "config.h"

#include <errno.h>
#include <locale.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <gtk/gtk.h>

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <gio/gunixfdlist.h>

#include "xdg-desktop-portal-dbus.h"
#include "shell-dbus.h"

#include "email.h"
#include "emaildialog.h"
#include "request.h"
#include "utils.h"
#include "externalwindow.h"

typedef struct {
  XdpImplEmail *impl;
  GDBusMethodInvocation *invocation;
  Request *request;

  GtkWidget *dialog;
  ExternalWindow *external_parent;

  int response;
  GVariant *arg_options;
} EmailDialogHandle;

static void
email_dialog_handle_free (gpointer data)
{
  EmailDialogHandle *handle = data;

  g_clear_object (&handle->external_parent);
  g_object_unref (handle->request);
  g_object_unref (handle->dialog);
  g_variant_unref (handle->arg_options);

  g_free (handle);
}

static void
email_dialog_handle_close (EmailDialogHandle *handle)
{
  gtk_widget_destroy (handle->dialog);
  email_dialog_handle_free (handle);
}

static void
send_response (EmailDialogHandle *handle)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);

  if (handle->request->exported)
    request_unexport (handle->request);

  xdp_impl_email_complete_compose_email (handle->impl,
                                         handle->invocation,
                                         handle->response,
                                         g_variant_builder_end (&opt_builder));

  email_dialog_handle_close (handle);
}


static gboolean
handle_close (XdpImplRequest *object,
              GDBusMethodInvocation *invocation,
              EmailDialogHandle *handle)
{
  GVariantBuilder opt_builder;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  xdp_impl_email_complete_compose_email (handle->impl,
                                         handle->invocation,
                                         2,
                                         g_variant_builder_end (&opt_builder));
  email_dialog_handle_close (handle);

  return FALSE;
}

char*
clean_up_parent_path_from_file_path (const char* input)
{
  /* Replace path-element /../ to make the path bedder readable:
   * This function only works with absolute pathes, so it must start with "/".
   * Not supported are: relative pathes or URI-pathes (e.g. file:// or percent-encoded)
   * This should also prevent one kind of illusion / attack. Example:
   * Input-Path:  "/home/user/file-ok.txt                          /../user/secret.txt"
   * Output-Path: "/home/user/secret.txt"
   * Due to the length of the filenames (which include lot of spaces),
   * the user might overlook the actual path (e.g., also because of scroll bars in GUI).
  */

  if (input[0] == '\0') return input;
  if (input[0] != '/' )
    {
      g_warning ("Error in %s with %s (must start with /)", __func__, input);
      return "";
    }

  GString *out = g_string_new ("");
  char ** splited = g_strsplit (input, "/", -1);
  int i = 0;
  for (i = 0;splited[i];i++)
    {
    // search for path ..
    if (splited[i][0] == '.' && splited[i][1] == '.' && splited[i][2] == '\0')
      {
        splited[i] = "";
        int j = 0;
        gboolean remove_done = FALSE;
        // search & remove the path part before:
        for (j = i; j>0; j--)
          {
          if (splited[j][0] != '\0')
            {
              splited[j] = "";
              remove_done = TRUE;
              break;
            }
          }
        if (!remove_done)
          {
            g_warning ("Error in %s with %s (seems invalid)", __func__, input);
            return "";
          }
      }
    }
  // pack new string:
  for (i = 0;splited[i];i++)
    {
      if (splited[i][0]!='\0')
        {
          g_string_append (out, "/");
          g_string_append (out, splited[i]);
        }
    }
  return g_string_free (out, FALSE);
}

const char *
get_uri_for_tb_and_check_exists (const char *input)
{
    if (!g_file_test(input, G_FILE_TEST_EXISTS))
      {
        g_warning ("attachment: skipped this file (does not exists): %s ", input);
        return NULL;
      }
    gchar *file_uri = g_filename_to_uri (input, NULL, NULL);
    if(file_uri==NULL)
      {
        g_warning ("failed to convert to URI: %s", input);
        return NULL;
      }
    GString *file_esc = g_string_new ((char *) file_uri);
    g_string_replace (file_esc, "\'", "%27", 0); // escape ' (this is only for a very rare problem: e.g. attach this two filenames, in this exact order: a' and abc )
    g_string_replace (file_esc, ",", "%2C", 0); // escape , (needed to handle multiple files)
    if (!g_file_test(g_filename_from_uri (file_esc->str,NULL,NULL), G_FILE_TEST_EXISTS))
      {
        // this check is a dublicate (to check before), and can be removed
        g_warning ("Skipped this file (does not exists): %s ", file_esc->str);
        g_string_free (file_esc, TRUE);
        return NULL;
      }

    return g_string_free (file_esc, FALSE);
}

const char *
escape_maybe_single_quote_comma (const char* input)
{
  /* This is just for future use:
   * In Thunderbird the string ', in E-Mail-adress is a problematic combination.
   * But actually character , in E-Mail-adress is not allowed, in xdg-desktop.
   * So, this is not a real problem, at the moment.
   */
  if (strchr (input, ',') != NULL && strchr (input, '\'') != NULL)
    {
      GString *s = g_string_new (input);
      g_string_replace (s, "\'", "\\'", 0);
      g_string_replace (s, ",", "\\,", 0);
      return g_string_free (s, FALSE);
    }
  else
    return input;
}

static gboolean
compose_mail_thunderbird (GAppInfo       *info,
                          const char * const *addrs,
                          const char * const *cc,
                          const char * const *bcc,
                          const char     *subject,
                          const char     *body,
                          const char    **attachments,
                          GError        **error)
{
  g_autofree char *enc_subject = NULL;
  g_autofree GString *enc1_body = g_string_new ("");
  g_autofree char *enc2_body = NULL;
  g_autofree char *arg = NULL;
  g_autoptr(GString) tb_arg = NULL;
  int i;
  gboolean success;
  const char *is_tb_snap;
  const char *old_cmdline;

  tb_arg = g_string_new ("");

  enc_subject = g_uri_escape_string (subject ? subject : "", NULL, FALSE);

  // Thunderbird interpret in body HTML-Code (only in this start-way ; not by mailto:).
  // @TODO: maybe only characters < > are needed to replace?
  if (body)
    for (i = 0; body[i]; i++)
      {
      switch (body[i])
        {
        case '<':
          g_string_append (enc1_body, "&lt;");
          break;
        case '>':
          g_string_append (enc1_body, "&gt;");
          break;
        case '&':
          g_string_append (enc1_body, "&amp;");
          break;
        case '"':
          g_string_append (enc1_body, "&quot;");
          break;
        case '\'':
          g_string_append (enc1_body, "&apos;");
          break;
        default:
          g_string_append_c (enc1_body, body[i]);
        }
      }
  enc2_body = g_uri_escape_string (enc1_body->str ? enc1_body->str : "", NULL, FALSE);

  if (addrs)
    {
      g_string_append_printf (tb_arg, "to='");
      for (i = 0; addrs[i]; i++)
          g_string_append_printf (tb_arg, "%s,", escape_maybe_single_quote_comma (addrs[i]));
      g_string_append_printf (tb_arg, "',");
    }

  if (cc)
    {
      g_string_append_printf (tb_arg, "cc='");
      for (i = 0; cc[i]; i++)
          g_string_append_printf (tb_arg, "%s,", escape_maybe_single_quote_comma (cc[i]));
      g_string_append_printf (tb_arg, "',");
    }

  if (bcc)
    {
      g_string_append_printf (tb_arg, "bcc='");
      for (i = 0; bcc[i]; i++)
          g_string_append_printf (tb_arg, "%s,", escape_maybe_single_quote_comma (bcc[i]));
      g_string_append_printf (tb_arg, "',");
    }

  g_string_append_printf (tb_arg, "subject='%s',", enc_subject);
  g_string_append_printf (tb_arg, "body=%s,", enc2_body);

  if (attachments)
    {
      g_string_append_printf (tb_arg, "attachment='");
      for (i = 0; attachments[i]; i++)
        {
          if (i != 0)
            g_string_append_printf (tb_arg, ",");
          const char *uri = get_uri_for_tb_and_check_exists (attachments[i]);
          g_string_append_printf (tb_arg, "%s", uri);
        }
        g_string_append_printf(tb_arg, "',");
    }

  /* The commandline can be complicated, e.g.
   * env BAMF_DESKTOP_FILE_HINT=/var/lib/snapd/.../thunderbird_thunderbird.desktop /snap/bin/thunderbird %u
   * so just match for thunderbird.
   */
  old_cmdline = g_app_info_get_commandline (info);
  is_tb_snap = strstr (old_cmdline, "/snap/bin/thunderbird");
  gchar *cmdline[] = {
      is_tb_snap ? "/snap/bin/thunderbird" : "thunderbird",
      "-compose",
      tb_arg->str,
      NULL
  };

  g_debug ("Launching: %s %s %s", cmdline[0], cmdline[1], cmdline[2]);
  success = g_spawn_async (NULL, cmdline, NULL, G_SPAWN_SEARCH_PATH, NULL,
                           NULL, NULL, error);
  return success;
}


static gboolean
compose_mail_mailto (GAppInfo       *info,
                     const char * const *addrs,
                     const char * const *cc,
                     const char * const *bcc,
                     const char     *subject,
                     const char     *body,
                     const char    **attachments,
                     GError        **error)
{
  g_autofree char *enc_subject = NULL;
  g_autofree char *enc_body = NULL;
  g_autoptr(GString) url = NULL;
  g_autoptr(GList) uris = NULL;
  int i;
  gboolean success;
  const char *sep;

  enc_subject = g_uri_escape_string (subject ? subject : "", NULL, FALSE);
  enc_body = g_uri_escape_string (body ? body : "", NULL, FALSE);

  url = g_string_new ("mailto:");

  sep = "?";

  if (addrs)
    {
    for (i = 0; addrs[i]; i++)
      {
        if (i > 0)
          g_string_append (url, ",");
        g_string_append_printf (url, "%s", addrs[i]);
      }
    }

  if (cc)
    {
      g_string_append_printf (url, "%scc=", sep);
      sep = "&";

      for (i = 0; cc[i]; i++)
        {
          if (i > 0)
            g_string_append (url, ",");
          g_string_append_printf (url, "%s", cc[i]);
        }
    }

  if (bcc)
    {
      g_string_append_printf (url, "%sbcc=", sep);
      sep = "&";

      for (i = 0; bcc[i]; i++)
        {
          if (i > 0)
            g_string_append (url, ",");
          g_string_append_printf (url, "%s", bcc[i]);
        }
    }

  g_string_append_printf (url, "%ssubject=%s", sep, enc_subject);
  g_string_append_printf (url, "&body=%s", enc_body);

  for (i = 0; attachments[i]; i++)
    g_string_append_printf (url, "&attachment=%s", attachments[i]);

  uris = g_list_append (uris, url->str);

  g_debug ("Launching: %s\n", url->str);

  success = g_app_info_launch_uris (info, uris, NULL, error);

  return success;
}

static gboolean
compose_mail_pre ( GAppInfo *info,
                   GVariant *arg_options,
                   gboolean attached_files_allowed)
{
  /*@TODO: Differents between attachments_list_text / attachments_list_link to variable "attachments"! :
   *         In the Dialog-GUI we used list of file-items.
   *         In this List was /../ in the file-path removed (=clean_up_parent_path_from_file_path())
   *         We should also do this either here with variable "attachments".
   *         Because they should be no differents between the shown path in GUI before, and the finally used one.
   *         ( Normally this is no important, becuase both should result in the same, in total ;
   *           But maybe relevant this in case of Bugs, or attacks etc.)
   */

  gboolean is_thunderbird = FALSE;
  const char *commandline;
  commandline = g_app_info_get_commandline (info);
  if (strstr (commandline, "thunderbird") )
    is_thunderbird = TRUE;

  const char *address = NULL;
  const char *subject = NULL;
  const char *body = NULL;
  const char *no_att[1] = { NULL };
  const char **attachments = no_att;
  const char * const *addresses = NULL;
  const char * const *cc = NULL;
  const char * const *bcc = NULL;
  g_auto(GStrv) addrs = NULL;

  g_variant_lookup (arg_options, "address", "&s", &address);
  g_variant_lookup (arg_options, "addresses", "^a&s", &addresses);
  g_variant_lookup (arg_options, "cc", "^a&s", &cc);
  g_variant_lookup (arg_options, "bcc", "^a&s", &bcc);
  g_variant_lookup (arg_options, "subject", "&s", &subject);
  g_variant_lookup (arg_options, "body", "&s", &body);
  if (attached_files_allowed)
    g_variant_lookup (arg_options, "attachments", "^a&s", &attachments);

  if (address)
    {
      if (addresses)
        {
          gsize len = g_strv_length ((char **)addresses);
          addrs = g_new (char *, len + 2);
          addrs[0] = g_strdup (address);
          memcpy (addrs + 1, addresses, sizeof (char *) * (len + 1));
        }
      else
        {
          addrs = g_new (char *, 2);
          addrs[0] = g_strdup (address);
          addrs[1] = NULL;
        }
    }
  else
    addrs = g_strdupv ((char **)addresses);

  if (is_thunderbird && attached_files_allowed)
    if (compose_mail_thunderbird (info, (const char * const *)addrs, cc, bcc, subject, body, attachments, NULL))
      return TRUE;
  else
    if (compose_mail_mailto (info, (const char * const *)addrs, cc, bcc, subject, body, attachments, NULL))
      return TRUE;

  return FALSE;
}

static void
email_dialog_done (GtkWidget *widget,
                   int response,
                   gpointer user_data)
{
  EmailDialogHandle *handle = user_data;
  gboolean attached_files_allowed = FALSE;

  switch (response)
    {
      default:
        g_warning ("Unexpected response: %d", response);
       /* Fall through */
      case GTK_RESPONSE_DELETE_EVENT:
        handle->response = 1;
        break;
      case GTK_RESPONSE_CANCEL:
        handle->response = 1;
        break;

      case GTK_RESPONSE_NO:
        handle->response = 1; // or should this bedder 0
        break;

      case GTK_RESPONSE_YES:
        handle->response = 0;
        attached_files_allowed = TRUE;
        break;
    }

  if (response == GTK_RESPONSE_NO || response == GTK_RESPONSE_YES)
    {
      g_autoptr(GAppInfo) info = NULL;
      info = g_app_info_get_default_for_uri_scheme ("mailto");
      if (compose_mail_pre ( info, handle->arg_options, attached_files_allowed))
        handle->response = 0;
      else
        handle->response = 2;
    }
  send_response (handle);
}

static const char **
list_the_attachments (const char ** attachments, gboolean get_uri)
{
  g_autoptr(GStrvBuilder) builder = g_strv_builder_new ();

  int i = 0;
  for (i = 0; attachments[i]; i++)
    {
      char *file =  clean_up_parent_path_from_file_path (attachments[i]);
      if (!get_uri)
        g_strv_builder_add (builder, file);
      else
        {
          const char *uri = get_uri_for_tb_and_check_exists (file);
          g_strv_builder_add (builder, uri);
        }
      g_free (file);
    }
  //g_auto(GStrv) array = g_strv_builder_end (builder);;
  return (const char **) g_strv_builder_end (builder);;

}
static gboolean
handle_compose_email (XdpImplEmail *object,
                      GDBusMethodInvocation *invocation,
                      const char *arg_handle,
                      const char *arg_app_id,
                      const char *arg_parent_window,
                      GVariant *arg_options)
{
  g_autoptr(Request) request = NULL;
  const char *sender;
  GVariantBuilder opt_builder;
  guint response = 0;
  EmailDialogHandle *handle;
  g_autoptr(GError) error = NULL;

  GtkWidget *dialog;
  GdkDisplay *display;
  GdkScreen *screen;
  ExternalWindow *external_parent = NULL;
  GtkWidget *fake_parent;

  const char *no_att[1] = { NULL };
  const char **attachments = no_att;
  g_autoptr(GAppInfo) info = NULL;
  gboolean show_confirm_attachments_dialog = FALSE; // This is only for Thunderbird needed
  gboolean is_thunderbird = FALSE;

  sender = g_dbus_method_invocation_get_sender (invocation);

  request = request_new (sender, arg_app_id, arg_handle);

  g_variant_lookup (arg_options, "attachments", "^a&s", &attachments);

  info = g_app_info_get_default_for_uri_scheme ("mailto");

  if (info == NULL)
    {
      g_warning ("ERROR: No mailto handler");
      return FALSE;
    }

  const char *commandline;
  commandline = g_app_info_get_commandline (info);
  if (strstr (commandline, "thunderbird") )
    is_thunderbird = TRUE;

  if (attachments != no_att)
    if (is_thunderbird )
      show_confirm_attachments_dialog = TRUE;  // handler for thunderbird, only when a file is attached

  if (!show_confirm_attachments_dialog)
    {
      if (!compose_mail_pre (info, arg_options, TRUE))
        response = 2;

      g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
      xdp_impl_email_complete_compose_email (object,
                                             invocation,
                                             response,
                                             g_variant_builder_end (&opt_builder));

      return TRUE;
    }
  else
    {
      g_debug("show attachment confirm Dialog");
      // This Dialog is only for Thunderbird needed
      if (arg_parent_window)
        {
          external_parent = create_external_window_from_handle (arg_parent_window);
          if (!external_parent)
            g_warning ("Failed to associate portal window with parent window %s",
                       arg_parent_window);
        }

      if (external_parent)
        display = external_window_get_display (external_parent);
      else
        display = gdk_display_get_default ();
      screen = gdk_display_get_default_screen (display);

      fake_parent = g_object_new (GTK_TYPE_WINDOW,
                                  "type", GTK_WINDOW_TOPLEVEL,
                                  "screen", screen,
                                  NULL);
      g_object_ref_sink (fake_parent);

      const char **attachments_list_text =  list_the_attachments (attachments, FALSE); //@TODO Where to free this?
      const char **attachments_list_link =  list_the_attachments (attachments, TRUE); //@TODO Where to free this?
      dialog = GTK_WIDGET (email_dialog_new (attachments_list_text , attachments_list_link ));
      gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (fake_parent));
      gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);


      handle = g_new0 (EmailDialogHandle, 1);
      handle->impl = object;
      handle->invocation = invocation;
      handle->request = g_object_ref (request);
      handle->dialog = g_object_ref (dialog);
      handle->external_parent = external_parent;
      handle->arg_options = g_variant_ref (arg_options);

      g_signal_connect (request, "handle-close", G_CALLBACK (handle_close), handle);

      g_signal_connect (dialog, "done", G_CALLBACK (email_dialog_done), handle);

      gtk_widget_realize (dialog);

      if (external_parent)
        external_window_set_parent_of (external_parent, gtk_widget_get_window (dialog));

      gtk_widget_show (dialog);

      request_export (request, g_dbus_method_invocation_get_connection (invocation));
    }
  return TRUE;
}


static GDBusConnection *system_bus;

gboolean
email_init (GDBusConnection *bus,
            GError **error)
{
  GDBusInterfaceSkeleton *helper;

  system_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
  if (system_bus == NULL)
    return FALSE;

  helper = G_DBUS_INTERFACE_SKELETON (xdp_impl_email_skeleton_new ());

  g_signal_connect (helper, "handle-compose-email", G_CALLBACK (handle_compose_email), NULL);

  if (!g_dbus_interface_skeleton_export (helper,
                                         bus,
                                         DESKTOP_PORTAL_OBJECT_PATH,
                                         error))
    return FALSE;

  g_debug ("providing %s", g_dbus_interface_skeleton_get_info (helper)->name);

  return TRUE;
}
