#define _GNU_SOURCE 1

#include <string.h>

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

#include <glib/gi18n.h>

#include "emaildialog.h"

struct _EmailDialog {
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

struct _EmailDialogClass {
  GtkWindowClass parent_class;
};

enum {
  DONE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (EmailDialog, email_dialog, GTK_TYPE_WINDOW)

static void
email_dialog_init (EmailDialog *dialog)
{
  gtk_widget_init_template (GTK_WIDGET (dialog));
}

static void
email_dialog_finalize (GObject *object)
{
  EmailDialog *dialog = EMAIL_DIALOG (object);

  G_OBJECT_CLASS (email_dialog_parent_class)->finalize (object);
}


static void
button_clicked (GtkWidget *button,
                EmailDialog *dialog)
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
               EmailDialog *dialog)
{
  if (gtk_widget_get_visible (GTK_WIDGET (dialog->info_text)) == 0)
    gtk_widget_set_visible (GTK_WIDGET (dialog->info_text), 1);
  else
    gtk_widget_set_visible (GTK_WIDGET (dialog->info_text), 0);

}

static void
email_dialog_class_init (EmailDialogClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

  object_class->finalize = email_dialog_finalize;


  signals[DONE] = g_signal_new ("done",
                                G_TYPE_FROM_CLASS (class),
                                G_SIGNAL_RUN_LAST,
                                0,
                                NULL, NULL,
                                NULL,
                                G_TYPE_NONE, 1,
                                G_TYPE_INT);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/freedesktop/portal/desktop/gtk/emaildialog.ui");
  gtk_widget_class_bind_template_child (widget_class, EmailDialog, yes_button);
  gtk_widget_class_bind_template_child (widget_class, EmailDialog, no_button);
  gtk_widget_class_bind_template_child (widget_class, EmailDialog, heading);
  gtk_widget_class_bind_template_child (widget_class, EmailDialog, list_footer);
  gtk_widget_class_bind_template_child (widget_class, EmailDialog, info_text);
  gtk_widget_class_bind_template_child (widget_class, EmailDialog, info_button);
  gtk_widget_class_bind_template_child (widget_class, EmailDialog, list_files);

  gtk_widget_class_bind_template_callback (widget_class, button_clicked);
  gtk_widget_class_bind_template_callback (widget_class, info_button_clicked);
}

EmailDialog *
email_dialog_new (const char **attachments_text,
                  const char **attachments_links)
{
  EmailDialog *dialog;
  GString *heading = g_string_new ("");
  GString *yes_button_text = g_string_new ("");
  g_autoptr(GError) error = NULL;

  dialog = g_object_new (email_dialog_get_type (), NULL);

  dialog->list_items = NULL;
  GtkWidget *file_item = NULL;
  int i = 0;
  for (i = 0; attachments_text[i]; i++)
    {
      GString *text = g_string_new ("");
      g_string_append_printf (text, "%d.) %s", (i+1), attachments_text[i]);
      file_item = gtk_link_button_new_with_label (attachments_links[i], text->str);
      g_string_free (text, TRUE);
      gtk_widget_set_valign (file_item, GTK_ALIGN_START);
      gtk_widget_set_halign (file_item, GTK_ALIGN_START);

      gtk_box_pack_start (GTK_BOX (dialog->list_files), file_item, TRUE,TRUE,2);
      dialog->list_items = g_slist_append (dialog->list_items, file_item);
    }

  int files_counter = i;

  g_string_append_printf (heading, _("Share these %d of your private files / data by E-Mail?"), files_counter);
  gtk_label_set_label (GTK_LABEL (dialog->heading), heading->str);

  g_string_append_printf (yes_button_text, _("_Yes, send %d private files"), files_counter);
  gtk_button_set_label (GTK_BUTTON (dialog->yes_button), yes_button_text->str);

  gtk_widget_show_all(GTK_WIDGET(dialog->list_files));

  //@TODO?? NEEDED? g_free(attachments_list); adn the other?
  g_string_free (heading, TRUE);
  g_string_free (yes_button_text, TRUE);
  return dialog;
}
