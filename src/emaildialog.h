#include <gtk/gtk.h>

#define EMAIL_TYPE_DIALOG (email_dialog_get_type ())
#define EMAIL_DIALOG(object) (G_TYPE_CHECK_INSTANCE_CAST (object, EMAIL_TYPE_DIALOG, EmailDialog))

typedef struct _EmailDialog EmailDialog;
typedef struct _EmailDialogClass EmailDialogClass;

EmailDialog * email_dialog_new (const char **attachments_text,
                                const char **attachments_link);

