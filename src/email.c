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
#include "request.h"
#include "utils.h"

// check version of xdg-email
// returns: -1 = error | 0 = minimal version does NOT fit | 1 = minimal version does fit
int checkversion_tool_xdg_email (int min_major, int min_minor)
{
  int output = -1;
  char* standard_output;
  gchar *cmdline = "xdg-email --version";
  if (!g_spawn_command_line_sync (cmdline, &standard_output, NULL, NULL, NULL))
    return -1;
  const char *version_string = strchr (standard_output, ' ');
  if (version_string != NULL)
  {
    char **version = g_strsplit (version_string, ".", 3);
    if ( *(version[0]) != '\0' && *(version[1]) != '\0')
    {
      output = 0;
      if( (atoi (version[0]) == min_major && atoi (version[1]) >= min_minor) ||
          (atoi (version[0]) > min_major) )
        output = 1;
    }
    g_strfreev (version);
  }
  return output;
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
compose_mail_xdg_email (GAppInfo       *info,
                     const char * const *addrs,
                     const char * const *cc,
                     const char * const *bcc,
                     const char     *subject,
                     const char     *body,
                     const char    **attachments,
                     GError        **error)
{
  g_autoptr(GString) params = NULL;
  g_autoptr(GList) uris = NULL;
  int i;
  gboolean success;
  const char *sep;

  params = g_string_new ("xdg-email");
  if (cc)
    {
      for (i = 0; cc[i]; i++)
       {
         char* quoted = g_shell_quote (cc[i]);
         g_string_append_printf (params, " --cc %s", quoted);
         g_free (quoted);
       }
    }

  if (bcc)
    {
      for (i = 0; bcc[i]; i++)
       {
         char* quoted = g_shell_quote (bcc[i]);
         g_string_append_printf (params, " --bcc %s", quoted);
         g_free (quoted);
       }
    }

  if (subject)
    {
      char* quoted = g_shell_quote (subject);
      g_string_append_printf (params, " --subject %s", quoted);
      g_free (quoted);
    }
  if (body)
    {
      char *body_proccessed = body;
      if (info != NULL)
        {
          const char *commandline_mailto_handler;
          commandline_mailto_handler = g_app_info_get_commandline (info); //or maybe g_strdup(g_app_info_get_executable (app_info));
          if (strstr (commandline, "thunderbird") || strstr (commandline, "betterbird") )
            {
               // Thunderbird special style would interpret the HTML code in BODY.
               // That's different from how mailto: behaves.
               // So, we convert it to HTML-Entities.
               body_proccessed = g_markup_escape_text (body, -1);
            }
      }
      char* quoted = g_shell_quote (body_proccessed);
      g_string_append_printf (params, " --body %s", quoted);
      g_free (quoted);
      g_string_free (body_processed, TRUE);
    }

  for (i = 0; attachments[i]; i++)
    {
      char* quoted = g_shell_quote (attachments[i]);
      g_string_append_printf (params, " --attach %s", quoted);
      g_free (quoted);
    }

  g_string_append (params, " \"mailto:");
  //@TODO:escape of " needed?
  if (addrs)
    {
      GString * addrs_str = g_string_new ("");
      for (i = 0; addrs[i]; i++)
        {
          if (i > 0)
            g_string_append (addrs_str, ",");
          g_string_append_printf (addrs_str, "%s", addrs[i]);
        }
      //does not work here: g_shell_quote
      g_string_append (params, addrs_str->str);
      g_string_free (addrs_str, TRUE);
    }

  //@TODO:escape of " needed?
  // Simplification (not sure): This Version check is only needed for Thunderbird.
  //    So, we can alternitve use the Thunderbird detection from before,
  //    and maybe skip some code (at least at the current state of xdg-email)
  if (checkversion_tool_xdg_email (1,3) == 1)
  {
      for (i = 0; attachments[i]; i++)
      {
        //needed for Thunderbird only:
        // The files can parallel (dublicate) with --attach attached
        // (only since xdg-email v. 1.3)
        if (i == 0)
          g_string_append (params, "?");
        else
          g_string_append (params, "&");
        //does not work here: g_shell_quote
        g_string_append_printf (params, "attach=%s", attachments[i]);
      }
  }
  g_string_append (params, "\"");

  char* standard_output; //@TODO: need to freed
  g_warning ("Launching: %s\n", params->str );
  gchar *cmdline= params->str;
  if (g_spawn_command_line_sync (cmdline, &standard_output, NULL, NULL, NULL))
    return TRUE;
  return FALSE;
}

static gboolean
compose_mail (const char * const *addrs,
              const char * const *cc,
              const char * const *bcc,
              const char  *subject,
              const char  *body,
              const char **attachments,
              GError     **error)
{
  g_autoptr(GAppInfo) info = NULL;
  info = g_app_info_get_default_for_uri_scheme ("mailto");

  if (compose_mail_xdg_email (info, addrs, cc, bcc, subject, body, attachments, error))
    return TRUE;

  g_warning ("Try fallback to legacy method\n");
  if (info != NULL)
    return compose_mail_mailto (info, addrs, cc, bcc, subject, body, attachments, error);
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "No mailto handler");

  return FALSE;
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
  g_autoptr(GError) error = NULL;
  const char *address = NULL;
  const char *subject = NULL;
  const char *body = NULL;
  const char *no_att[1] = { NULL };
  const char **attachments = no_att;
  guint response = 0;
  GVariantBuilder opt_builder;
  const char * const *addresses = NULL;
  const char * const *cc = NULL;
  const char * const *bcc = NULL;
  g_auto(GStrv) addrs = NULL;

  sender = g_dbus_method_invocation_get_sender (invocation);

  request = request_new (sender, arg_app_id, arg_handle);

  g_variant_lookup (arg_options, "address", "&s", &address);
  g_variant_lookup (arg_options, "addresses", "^a&s", &addresses);
  g_variant_lookup (arg_options, "cc", "^a&s", &cc);
  g_variant_lookup (arg_options, "bcc", "^a&s", &bcc);
  g_variant_lookup (arg_options, "subject", "&s", &subject);
  g_variant_lookup (arg_options, "body", "&s", &body);
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

  if (!compose_mail ((const char * const *)addrs, cc, bcc, subject, body, attachments, NULL))
    response = 2;

  g_variant_builder_init (&opt_builder, G_VARIANT_TYPE_VARDICT);
  xdp_impl_email_complete_compose_email (object,
                                         invocation,
                                         response,
                                         g_variant_builder_end (&opt_builder));

  return TRUE;
}

gboolean
email_init (GDBusConnection *bus,
            GError **error)
{
  GDBusInterfaceSkeleton *helper;

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
