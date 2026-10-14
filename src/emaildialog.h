#include <gtk/gtk.h>

#define ACCOUNT_TYPE_DIALOG (email_dialog_get_type ())
#define ACCOUNT_DIALOG(object) (G_TYPE_CHECK_INSTANCE_CAST (object, ACCOUNT_TYPE_DIALOG, EmailDialog))

typedef struct _EmailDialog EmailDialog;
typedef struct _EmailDialogClass EmailDialogClass;

EmailDialog * email_dialog_new (const char **attachments_text, const char **attachments_link);
//const char **attachments);

