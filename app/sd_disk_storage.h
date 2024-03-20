
#include <stdbool.h>

#include <gio/gio.h>
#include <glib-object.h>

typedef GObject StorageHandler;
typedef GObject DockerdFolder;

typedef struct {
  char *folder_name;
  char *message;
} DockerdFolderData;

bool confirm_sdcard_usage(void);

// bool axstorage_setup_sdcard(void);

char *get_sdcard_path(void);

int axstorage_use(void);

int drop_axstorage(void);

void handler_create_dockerdfolder_async(StorageHandler *self,
                                        const char *folder_name,
                                        const char *message,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);

void create_dockerdfolder_cb(GObject *source_object,
                             GAsyncResult *res,
                             gpointer user_data);