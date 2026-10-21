#define _GNU_SOURCE 1

#include <string.h>

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#include <glib/gi18n.h>

#include "emailtbdialog.h"

struct _EmailTbDialog {
  GtkWindow parent;

  GtkWidget *heading;
  GtkWidget *heading2;
  GtkWidget *info_box;
  GtkWidget *info_text1;
  GtkWidget *info_text2;
  GtkWidget *info_button;
  GtkWidget *info_details_button;
  GtkWidget *yes_button;
  GtkWidget *yes_again_button;
  GtkWidget *yes_again_outer_box;
  GtkWidget *no_button;
  GtkWidget *list_files;
  const char ** attachments_links_processed; //@TODO: where to free?

  GSList *list_items; // List of GtkWidget
};

struct _EmailTbDialogClass {
  GtkWindowClass parent_class;
};

enum {
  DONE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (EmailTbDialog, email_tb_dialog, GTK_TYPE_WINDOW)

static void
email_tb_dialog_init (EmailTbDialog *dialog)
{
  gtk_widget_init_template (GTK_WIDGET (dialog));
}

static void
email_tb_dialog_finalize (GObject *object)
{
  EmailTbDialog *dialog = EMAILTB_DIALOG (object);

  G_OBJECT_CLASS (email_tb_dialog_parent_class)->finalize (object);
}

static gboolean exists_uri (const char * uri){
  if ( uri != NULL && uri[0] != '\0')
  {
    const char *filename = g_filename_from_uri (uri, NULL, NULL);
    if ( filename != NULL && g_file_test (filename, G_FILE_TEST_EXISTS))
     return TRUE;
  }
  return FALSE;
}


static gboolean has_non_existing_uri (const char ** attachments_uri)
{
  int i;
  for (i = 0; attachments_uri[i]; i++)
  {
    if(!exists_uri (attachments_uri[i]))
      return TRUE;
  }
  return FALSE;
}
static void style_file_not_found_for_linkbutton (GtkWidget *widget)
{
  gtk_link_button_set_uri (GTK_LINK_BUTTON (widget), "");
  gtk_widget_set_tooltip_text (GTK_WIDGET (widget) , _("File not found"));

  GtkWidget *child = gtk_bin_get_child(GTK_BIN(widget));
  g_return_if_fail ( GTK_IS_LABEL ( child ) );

  PangoAttrList *attrlist = pango_attr_list_new ();
  PangoAttribute *attr = pango_attr_strikethrough_new (TRUE);
  pango_attr_list_insert (attrlist, attr);
  gtk_label_set_attributes (GTK_LABEL(child), attrlist);
  pango_attr_list_unref (attrlist);
}

static gboolean check_file_item_button_exist_and_style ( GtkWidget *widget )
{
  g_return_val_if_fail ( GTK_IS_LINK_BUTTON ( widget ), FALSE );
  const char *uri = gtk_link_button_get_uri (GTK_LINK_BUTTON (widget));
  if (uri != NULL && uri[0] != '\0')
    {
      const char *filename = g_filename_from_uri (uri, NULL, NULL);
      if ( filename!=NULL && g_file_test (filename, G_FILE_TEST_EXISTS))
        return FALSE;
    }
  g_warning ("File does not exists: %s ", uri);
  style_file_not_found_for_linkbutton (widget);
  return TRUE;
}

static void
check_exists_and_set_not_found (GtkWidget *widget, gpointer user_data)
{
  check_file_item_button_exist_and_style(widget);
}

static void
main_buttons_clicked (GtkWidget *button,
                EmailTbDialog *dialog)
{
  int response;

  if (button == dialog->yes_button)
    {
      if (has_non_existing_uri (dialog->attachments_links_processed))
      {
        g_slist_foreach ( dialog->list_items, (GFunc) check_exists_and_set_not_found, NULL);
        gtk_widget_set_visible (GTK_WIDGET (dialog->yes_again_outer_box),TRUE);
        gtk_widget_set_visible (GTK_WIDGET (dialog->yes_button),FALSE);
        return;
      }
      response = GTK_RESPONSE_YES;
    }
  else if (button == dialog->yes_again_button)
    response = GTK_RESPONSE_YES;
  else if (button == dialog->no_button)
    response = GTK_RESPONSE_NO;
  else
    response = GTK_RESPONSE_CANCEL;

  gtk_widget_hide (GTK_WIDGET (dialog));
  g_signal_emit (dialog, signals[DONE], 0, response);
}

static void
info_button_clicked (GtkWidget *button,
               EmailTbDialog *dialog)
{
  if (gtk_widget_get_visible (GTK_WIDGET (dialog->info_box)) == 0)
    gtk_widget_set_visible (GTK_WIDGET (dialog->info_box), 1);
  else
    gtk_widget_set_visible (GTK_WIDGET (dialog->info_box), 0);

}

static void
info_details_button_clicked (GtkWidget *button,
               EmailTbDialog *dialog)
{
  if (gtk_widget_get_visible (GTK_WIDGET (dialog->info_text2)) == 0)
    gtk_widget_set_visible (GTK_WIDGET (dialog->info_text2), 1);
  else
    gtk_widget_set_visible (GTK_WIDGET (dialog->info_text2), 0);

}

static void
email_tb_dialog_class_init (EmailTbDialogClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

  object_class->finalize = email_tb_dialog_finalize;

  signals[DONE] = g_signal_new ("done",
                                G_TYPE_FROM_CLASS (class),
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 1,
                                G_TYPE_INT);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/freedesktop/portal/desktop/gtk/emailtbdialog.ui");
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, yes_button);
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, yes_again_button);
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, yes_again_outer_box);
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, no_button);
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, heading);
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, heading2);
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, info_box);
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, info_text1);
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, info_text2);
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, info_button);
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, info_details_button);
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, list_files);

  gtk_widget_class_bind_template_callback (widget_class, main_buttons_clicked);
  gtk_widget_class_bind_template_callback (widget_class, info_button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, info_details_button_clicked);
}

static gboolean file_item_linkbutton_press_event_clbk ( GtkWidget *widget, GdkEvent *event )
{
  return check_file_item_button_exist_and_style(widget);
}

EmailTbDialog *
email_tb_dialog_new (const char **attachments_text,
                     const char **attachments_links)
{
  EmailTbDialog *dialog;
  GString *heading = g_string_new ("");
  GString *yes_button_text = g_string_new ("");
  g_autoptr(GError) error = NULL;

  dialog = g_object_new (email_tb_dialog_get_type (), NULL);

  dialog->attachments_links_processed = attachments_links;

  dialog->list_items = NULL;
  GtkWidget *file_item = NULL;
  int i = 0;
  for (i = 0; attachments_text[i]; i++)
    {
      GString *text = g_string_new ("");
      g_string_append_printf (text, "%d.) %s", (i+1), attachments_text[i]);
      file_item = gtk_link_button_new_with_label (attachments_links[i], text->str);
      g_string_free (text, TRUE);
      if(attachments_links[i] == NULL || attachments_links[i][0] == '\0')
          style_file_not_found_for_linkbutton (file_item);
      // A full file exists check, will be done on click item or send.

      gtk_widget_set_valign (file_item, GTK_ALIGN_START);
      gtk_widget_set_halign (file_item, GTK_ALIGN_START);
      
      g_signal_connect (file_item, "button-press-event", G_CALLBACK (file_item_linkbutton_press_event_clbk), file_item);

      gtk_box_pack_start (GTK_BOX (dialog->list_files), file_item, TRUE,TRUE,2);
      dialog->list_items = g_slist_append (dialog->list_items, file_item);
    }
  int files_counter = i;
  g_string_append_printf (heading, _("Share these %d of your private files / data by E-Mail?"), files_counter);
  gtk_label_set_label (GTK_LABEL (dialog->heading), heading->str);

  g_string_append_printf (yes_button_text, _("_Yes, send %d private files"), files_counter);
  gtk_button_set_label (GTK_BUTTON (dialog->yes_button), yes_button_text->str);

  gtk_widget_show_all(GTK_WIDGET(dialog->list_files));

  g_string_free (heading, TRUE);
  g_string_free (yes_button_text, TRUE);
  return dialog;
}
