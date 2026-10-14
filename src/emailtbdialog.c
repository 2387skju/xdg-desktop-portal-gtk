#define _GNU_SOURCE 1

#include <string.h>

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#include <glib/gi18n.h>

#include "emailtbdialog.h"

struct _EmailTbDialog {
  GtkWindow parent;

  GtkWidget *heading;
  GtkWidget *list_footer;
  GtkWidget *info_text;
  GtkWidget *info_button;
  GtkWidget *yes_button;
  GtkWidget *no_button;
  GtkWidget *list_files;

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
  EmailTbDialog *dialog = ACCOUNT_DIALOG (object);

  G_OBJECT_CLASS (email_tb_dialog_parent_class)->finalize (object);
}


static void
button_clicked (GtkWidget *button,
                EmailTbDialog *dialog)
{
  int response;

  gtk_widget_hide (GTK_WIDGET (dialog));

  if (button == dialog->yes_button)
    response = GTK_RESPONSE_YES;
  else if (button == dialog->no_button)
    response = GTK_RESPONSE_NO;
  else
    response = GTK_RESPONSE_CANCEL;

  g_signal_emit (dialog, signals[DONE], 0, response);
}

static void
info_button_clicked (GtkWidget *button,
               EmailTbDialog *dialog)
{
  if (gtk_widget_get_visible (GTK_WIDGET (dialog->info_text)) == 0)
    gtk_widget_set_visible (GTK_WIDGET (dialog->info_text), 1);
  else
    gtk_widget_set_visible (GTK_WIDGET (dialog->info_text), 0);

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
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, no_button);
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, heading);
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, list_footer);
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, info_text);
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, info_button);
  gtk_widget_class_bind_template_child (widget_class, EmailTbDialog, list_files);

  gtk_widget_class_bind_template_callback (widget_class, button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, info_button_clicked);
}

EmailTbDialog *
email_tb_dialog_new (const char **attachments_text, const char **attachments_links)
{
  EmailTbDialog *dialog;
  GString *heading = g_string_new ("");
  GString *info_text = g_string_new ("");
  g_autoptr(GError) error = NULL;

  dialog = g_object_new (email_tb_dialog_get_type (), NULL);

  dialog->list_items = NULL;
  GtkWidget *file_item = NULL;
  int i = 0;
  for (i = 0; attachments_text[i]; i++)
    {
      GString *text = g_string_new ("");
      g_string_append_printf (text, "%d.) %s", (i+1), attachments_text[i]);
      file_item = gtk_link_button_new_with_label (attachments_links[i], text->str);//attachments_text[i]);
      g_string_free (text, TRUE);
      gtk_widget_set_valign (file_item, GTK_ALIGN_START);
      gtk_widget_set_halign (file_item, GTK_ALIGN_START);

      gtk_box_pack_start (GTK_BOX (dialog->list_files), file_item, TRUE,TRUE,2);
      dialog->list_items = g_slist_append (dialog->list_items, file_item);
    }

  int files_counter = i;

  g_string_append_printf (heading, _("Share these %d of your private files / data by E-Mail?"), files_counter);
  gtk_label_set_label (GTK_LABEL (dialog->heading), heading->str);

  g_string_append_printf (info_text, _("Info: \nThis dialog box appears because an application or website is attempting to compose a new email \nand wants to attach your private files in the message. These files may contain private or sensitive information:\nSome examples: \"your-secret-file.txt\" , \"~/.ssh/id_rsa\" , login-data, passwords or sensible system informationen. \nYou should therefore check what is being sent. You can click on the file to open it. \nTo be absolutely sure, you should also double-check the file list in %s. \n\nMore technical details: \nThis dialog appears only when using email program %s. \nThe technical reason for this is, that the request is processed via the mailto: protocol. \nIt is impossible to determine which application or website is making the request. \nTherefore, it is unclear whether the request is trustworthy. \n%s was manually integrated to be compatible with this technical implementation\n (it does not normally support file attachments via mailto:). That is why this dialog box was added.\n Other email programs already have their own conspicuous warnings built in."), "Thunderbird", "Thunderbird", "Thunderbird");
  gtk_label_set_label (GTK_LABEL (dialog->info_text), info_text->str);

  gtk_widget_show_all(GTK_WIDGET(dialog->list_files));


  //@TODO?? NEEDED? g_free(attachments_list); adn the other?
  g_string_free (heading, TRUE);
  g_string_free (info_text, TRUE);
  return dialog;
}
