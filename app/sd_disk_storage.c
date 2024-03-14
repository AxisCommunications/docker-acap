

#include "sd_disk_storage.h"

#include <errno.h>
#include <glib.h>

#include <glib/gstdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

/* AX Storage library. */
#include <axsdk/axstorage.h>

/**
 * disk_item_t represents one storage device and its values.
 */
typedef struct {
  AXStorage *storage;         /** AXStorage reference. */
  AXStorageType storage_type; /** Storage type */
  gchar *storage_id;          /** Storage device name. */
  gchar *storage_path;        /** Storage path. */
  guint subscription_id;      /** Subscription ID for storage events. */
  gboolean setup;     /** TRUE: storage was set up async, FALSE otherwise. */
  gboolean writable;  /** Storage is writable or not. */
  gboolean available; /** Storage is available or not. */
  gboolean full;      /** Storage device is full or not. */
  gboolean exiting;   /** Storage is exiting (going to disappear) or not. */
} disk_item_t;

static GList *disks_list = NULL;
static char *_sdcard_path = NULL;

/**
 * @brief Find disk item in disks_list
 *
 * @param storage_id The storage to subscribe to its events
 *
 * @return Disk item
 */
static disk_item_t *
find_disk_item_t(gchar *storage_id)
{
  GList *node = NULL;
  disk_item_t *item = NULL;

  for (node = g_list_first(disks_list); node != NULL;
       node = g_list_next(node)) {
    item = node->data;

    if (g_strcmp0(storage_id, item->storage_id) == 0) {
      return item;
    }
  }
  return NULL;
}

/**
 * @brief Callback function registered by ax_storage_release_async(),
 *        which is triggered to release the disk
 *
 * @param user_data storage_id of a disk
 * @param error Returned errors
 */
static void
release_disk_cb(gpointer user_data, GError *error)
{
  syslog(LOG_INFO, "Release of %s", (gchar *)user_data);
  if (error != NULL) {
    syslog(LOG_WARNING,
           "Error while releasing %s: %s",
           (gchar *)user_data,
           error->message);
    g_error_free(error);
  }
}

/**
 * @brief Free disk items from disks_list
 */
static void
free_disk_item_t()
{
  GList *node = NULL;

  for (node = g_list_first(disks_list); node != NULL;
       node = g_list_next(node)) {
    GError *error = NULL;
    disk_item_t *item = node->data;

    if (item->setup) {
      /* NOTE: It is advised to finish all your reading/writing operations
         before releasing the storage device. */
      ax_storage_release_async(
          item->storage, release_disk_cb, item->storage_id, &error);
      if (error != NULL) {
        syslog(LOG_WARNING,
               "Failed to release %s. Error: %s",
               item->storage_id,
               error->message);
        g_clear_error(&error);
      } else {
        syslog(LOG_INFO, "Release of %s was successful", item->storage_id);
        item->setup = FALSE;
      }
    }

    ax_storage_unsubscribe(item->subscription_id, &error);
    if (error != NULL) {
      syslog(LOG_WARNING,
             "Failed to unsubscribe event of %s. Error: %s",
             item->storage_id,
             error->message);
      g_clear_error(&error);
    } else {
      syslog(LOG_INFO, "Unsubscribed events of %s", item->storage_id);
    }
    g_free(item->storage_id);
    g_free(item->storage_path);
  }
  g_list_free(disks_list);
}

/**
 * @brief Callback function registered by ax_storage_setup_async(),
 *        which is triggered to setup a disk
 *
 * @param storage storage_id of a disk
 * @param user_data
 * @param error Returned errors
 */
static void
setup_disk_cb(AXStorage *storage,
              __attribute__((unused)) gpointer user_data,
              GError *error)
{
  GError *ax_error = NULL;
  gchar *storage_id = NULL;
  gchar *path = NULL;
  AXStorageType storage_type;

  if (storage == NULL || error != NULL) {
    syslog(LOG_ERR, "Failed to setup disk. Error: %s", error->message);
    g_error_free(error);
    goto free_variables;
  }

  storage_id = ax_storage_get_storage_id(storage, &ax_error);
  if (ax_error != NULL) {
    syslog(
        LOG_WARNING, "Failed to get storage_id. Error: %s", ax_error->message);
    g_error_free(ax_error);
    goto free_variables;
  }

  path = ax_storage_get_path(storage, &ax_error);
  if (ax_error != NULL) {
    syslog(LOG_WARNING,
           "Failed to get storage path. Error: %s",
           ax_error->message);
    g_error_free(ax_error);
    goto free_variables;
  }

  storage_type = ax_storage_get_type(storage, &ax_error);
  if (ax_error != NULL) {
    syslog(LOG_WARNING,
           "Failed to get storage type. Error: %s",
           ax_error->message);
    g_error_free(ax_error);
    goto free_variables;
  }

  disk_item_t *disk = find_disk_item_t(storage_id);
  /* The storage pointer is created in this callback, assign it to
     disk_item_t instance. */
  disk->storage = storage;
  disk->storage_type = storage_type;
  disk->storage_path = g_strdup(path);
  disk->setup = TRUE;

  syslog(LOG_INFO, "Disk: %s has been setup in %s", storage_id, path);
free_variables:
  g_free(storage_id);
  g_free(path);
}

/**
 * @brief Subscribe to the events of the storage
 *
 * @param storage_id The storage to subscribe to its events
 * @param user_data User data to be processed
 * @param error Returned errors
 */
static void
subscribe_cb(gchar *storage_id,
             __attribute__((unused)) gpointer user_data,
             GError *error)
{
  GError *ax_error = NULL;
  gboolean available;
  gboolean writable;
  gboolean full;
  gboolean exiting;

  if (error != NULL) {
    syslog(LOG_WARNING,
           "Failed to subscribe to %s. Error: %s",
           storage_id,
           error->message);
    g_error_free(error);
    return;
  }

  syslog(LOG_INFO, "Subscribe for the events of %s", storage_id);
  disk_item_t *disk = find_disk_item_t(storage_id);

  /* Get the status of the events. */
  exiting =
      ax_storage_get_status(storage_id, AX_STORAGE_EXITING_EVENT, &ax_error);
  if (ax_error != NULL) {
    syslog(LOG_WARNING,
           "Failed to get EXITING event for %s. Error: %s",
           storage_id,
           ax_error->message);
    g_error_free(ax_error);
    return;
  }

  available =
      ax_storage_get_status(storage_id, AX_STORAGE_AVAILABLE_EVENT, &ax_error);
  if (ax_error != NULL) {
    syslog(LOG_WARNING,
           "Failed to get AVAILABLE event for %s. Error: %s",
           storage_id,
           ax_error->message);
    g_error_free(ax_error);
    return;
  }

  writable =
      ax_storage_get_status(storage_id, AX_STORAGE_WRITABLE_EVENT, &ax_error);
  if (ax_error != NULL) {
    syslog(LOG_WARNING,
           "Failed to get WRITABLE event for %s. Error: %s",
           storage_id,
           ax_error->message);
    g_error_free(ax_error);
    return;
  }

  full = ax_storage_get_status(storage_id, AX_STORAGE_FULL_EVENT, &ax_error);
  if (ax_error != NULL) {
    syslog(LOG_WARNING,
           "Failed to get FULL event for %s. Error: %s",
           storage_id,
           ax_error->message);
    g_error_free(ax_error);
    return;
  }

  disk->writable = writable;
  disk->available = available;
  disk->exiting = exiting;
  disk->full = full;

  syslog(
      LOG_INFO,
      "Status of events for %s: %s writable, %s available, %s exiting, %s full",
      storage_id,
      writable ? "" : "not ",
      available ? "" : "not ",
      exiting ? "" : "not ",
      full ? "" : "not ");

  /* If exiting, and the disk was set up before, release it. */
  if (exiting && disk->setup) {
    /* NOTE: It is advised to finish all your reading/writing operations
    before
       releasing the storage device. */
    ax_storage_release_async(
        disk->storage, release_disk_cb, storage_id, &ax_error);

    if (ax_error != NULL) {
      syslog(LOG_WARNING,
             "Failed to release %s. Error %s.",
             storage_id,
             ax_error->message);
      g_error_free(ax_error);
    } else {
      syslog(LOG_INFO, "Release of %s was successful", storage_id);
      disk->setup = FALSE;
    }

    /* Writable implies that the disk is available. */
  } else if (writable && !full && !exiting && !disk->setup) {
    syslog(LOG_INFO, "Setup %s", storage_id);
    ax_storage_setup_async(storage_id, setup_disk_cb, NULL, &ax_error);

    if (ax_error != NULL) {
      /* NOTE: It is advised to try to setup again in case of failure. */
      syslog(LOG_WARNING,
             "Failed to setup %s, reason: %s",
             storage_id,
             ax_error->message);
      g_error_free(ax_error);
    } else {
      syslog(LOG_INFO, "Setup of %s was successful", storage_id);
    }
  }
}

/**
 * @brief Subscribes to disk events and creates new disk item
 *
 * @param storage_id storage_id of a disk
 *
 * @return The item
 */
static disk_item_t *
new_disk_item_t(gchar *storage_id)
{
  GError *error = NULL;
  disk_item_t *item = NULL;
  guint subscription_id;

  /* Subscribe to disks events. */
  subscription_id =
      ax_storage_subscribe(storage_id, subscribe_cb, NULL, &error);
  if (subscription_id == 0 || error != NULL) {
    syslog(LOG_ERR,
           "Failed to subscribe to events of %s. Error: %s",
           storage_id,
           error->message);
    g_clear_error(&error);
    return NULL;
  }

  item = g_new0(disk_item_t, 1);
  item->subscription_id = subscription_id;
  item->storage_id = g_strdup(storage_id);
  item->setup = FALSE;

  return item;
}

int
axstorage_use(void)
{
  GList *disks = NULL;
  GList *node = NULL;
  GError *error = NULL;
  gint ret = EXIT_SUCCESS;

  ax_storage_list(&error);
  if (error != NULL) {
    syslog(LOG_WARNING,
           "Failed to list storage devices. Error: (%s)",
           error->message);
    g_error_free(error);
    ret = EXIT_FAILURE;
    goto out;
  }

  /* Loop through the retrieved disks and subscribe to their events. */
  for (node = g_list_first(disks); node != NULL; node = g_list_next(node)) {
    gchar *disk_name = (gchar *)node->data;
    if (strcmp(disk_name, "SD_DISK") != 0) {
      g_free(node->data);
      continue;
    }
    disk_item_t *item = new_disk_item_t(disk_name);
    if (item == NULL) {
      syslog(LOG_WARNING, "%s is skipped", disk_name);
      g_free(node->data);
      continue;
    }
    disks_list = g_list_append(disks_list, item);
    g_free(node->data);
  }
  g_list_free(disks);

out:
  return ret;
}

//////

/**
 * @brief Callback function registered by g_timeout_add_seconds(),
 *        which is triggered every 10th second and writes data to disk
 *
 * @param data The storage to subscribe to its events
 *
 * @return Result
 */
static gboolean
create_folder(const gchar *folder)
{
  GList *node = NULL;
  gboolean ret = TRUE;
  bool folder_created = _sdcard_path != NULL;

  if (!folder_created) {
    syslog(LOG_INFO, "The sdcard folder is not created");
    for (node = g_list_first(disks_list); node != NULL;
         node = g_list_next(node)) {
      disk_item_t *item = node->data;
      syslog(LOG_INFO, "about to create folder on disk %s", item->storage_id);
      syslog(LOG_INFO,
             "Status of events for %s: %s writable, %s available, %s exiting, "
             "%s full",
             item->storage_id,
             item->writable ? "" : "not ",
             item->available ? "" : "not ",
             item->exiting ? "" : "not ",
             item->full ? "" : "not ");

      /* Create folder on disk when it is available, writable and has disk space
        and the setup has been done. */
      if (item->available && item->writable && !item->full && item->setup) {
        gchar *folder_path =
            g_strdup_printf("%s/%s", item->storage_path, folder);
        syslog(LOG_INFO, "path to create: %s", folder_path);

        char *create_folder_command =
            g_strdup_printf("mkdir -p %s", folder_path);
        int res = system(create_folder_command);
        if (res != 0) {
          syslog(LOG_ERR,
                 "Failed to create data_root folder at: %s. Error code: %d",
                 folder_path,
                 res);
          ret = FALSE;
          break;
        } else {
          _sdcard_path = strdup(folder_path);
          syslog(LOG_INFO, "created %s", folder_path);
        }
        g_free(folder_path);
      }
    }
  }
  // syslog(LOG_INFO,"create folder finsished with status %i",ret);
  return ret;
}

// cake_data_free
static void
dockerdFolder_data_free(DockerdFolderData *ddf_data)
{
  syslog(LOG_INFO, "%p: %s", g_thread_self(), __func__);
  g_free(ddf_data->folder_name);
  g_free(ddf_data->message);
  g_slice_free(DockerdFolderData, ddf_data);
}

// bake_cake
static DockerdFolder *
create_dockerdfolder(__attribute__((unused)) StorageHandler *self,
                     __attribute__((unused)) char *folder_name,
                     __attribute__((unused)) GCancellable *cancellable,
                     __attribute__((unused)) GError **error)
{
  syslog(LOG_INFO, "%p: %s", g_thread_self(), __func__);

  // TODO code to create folder
  create_folder(folder_name);

  return g_object_new(G_TYPE_OBJECT, NULL);
}

// bake_cake_thread
static void
create_dockerdfolder_thread(GTask *task,
                            gpointer source_object,
                            gpointer task_data,
                            GCancellable *cancellable)
{
  StorageHandler *self = source_object;
  DockerdFolder *dockerdFolder;
  DockerdFolderData *ddf_data = task_data;
  GError *error = NULL;
  syslog(LOG_INFO, "%p: %s", g_thread_self(), __func__);

  dockerdFolder =
      create_dockerdfolder(self, ddf_data->folder_name, cancellable, &error);

  if (dockerdFolder)
    g_task_return_pointer(task, dockerdFolder, g_object_unref);
  else
    g_task_return_error(task, error);
}

void
handler_create_dockerdfolder_async(StorageHandler *self,
                                   const char *folder_name,
                                   const char *message,
                                   GCancellable *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer user_data)
{
  DockerdFolderData *ddf_data;
  GTask *task;
  syslog(LOG_INFO, "%p: %s", g_thread_self(), __func__);

  ddf_data = g_slice_new(DockerdFolderData);
  ddf_data->folder_name = strdup(folder_name);
  ddf_data->message = strdup(message);

  task = g_task_new(self, cancellable, callback, user_data);
  g_task_set_task_data(task, ddf_data, (GDestroyNotify)dockerdFolder_data_free);
  g_task_run_in_thread(task, create_dockerdfolder_thread);

  g_object_unref(task);
}

static DockerdFolder *
create_dockerdfolder_finish(StorageHandler *self,
                            GAsyncResult *res,
                            GError **error)
{
  g_return_val_if_fail(g_task_is_valid(res, self), NULL);
  syslog(LOG_INFO, "%p: %s", g_thread_self(), __func__);
  return g_task_propagate_pointer(G_TASK(res), error);
}

void
create_dockerdfolder_cb(GObject *source_object,
                        GAsyncResult *res,
                        gpointer user_data)
{
  StorageHandler *sh = (StorageHandler *)source_object;
  GMainLoop *loop = (GMainLoop *)user_data;
  DockerdFolder *ddf;
  GError *error = NULL;

  syslog(LOG_INFO, "%p: %s", g_thread_self(), __func__);
  ddf = create_dockerdfolder_finish(sh, res, &error);

  syslog(LOG_INFO, "the dockerd folder is created %p", ddf);

  g_object_unref(ddf);
  g_main_loop_quit(loop);
}

//////

bool
confirm_sdcard_usage(void)
{
  bool ret = false;
  GList *disks = NULL;
  GList *node = NULL;
  GError *error = NULL;

  syslog(LOG_INFO, "Using axstorage to setup sdcard");

  disks = ax_storage_list(&error);
  if (error != NULL) {
    syslog(LOG_WARNING,
           "Failed to list storage devices. Error: (%s)",
           error->message);
    g_error_free(error);
    goto out;
  }

  /* Loop through the retrieved disks and subscribe to their events. */
  for (node = g_list_first(disks); node != NULL; node = g_list_next(node)) {
    gchar *disk_name = (gchar *)node->data;
    syslog(LOG_INFO, "Found disk %s", disk_name);
    if (strcmp(disk_name, "SD_DISK") == 0) {
      disk_item_t *item = new_disk_item_t(disk_name);
      if (item == NULL) {
        syslog(LOG_WARNING, "%s is skipped", disk_name);
        g_free(node->data);
        continue;
      }
      syslog(LOG_INFO, "appending disk %s", item->storage_id);
      disks_list = g_list_append(disks_list, item);
    }
    g_free(node->data);
  }

  ret = true;
out:
  g_list_free(disks);
  return ret;
}

// bool
// axstorage_setup_sdcard(void)
// {
//   bool ret = false;
//   GList *disks = NULL;
//   GList *node = NULL;
//   GError *error = NULL;

//   syslog(LOG_INFO, "Using axstorage to setup sdcard");

//   disks = ax_storage_list(&error);
//   if (error != NULL) {
//     syslog(LOG_WARNING,
//            "Failed to list storage devices. Error: (%s)",
//            error->message);
//     g_error_free(error);
//     goto out;
//   }

//   /* Loop through the retrieved disks and subscribe to their events. */
//   for (node = g_list_first(disks); node != NULL; node = g_list_next(node)) {
//     gchar *disk_name = (gchar *)node->data;
//     syslog(LOG_INFO, "Found disk %s", disk_name);
//     if (strcmp(disk_name, "SD_DISK") == 0) {
//       disk_item_t *item = new_disk_item_t(disk_name);
//       if (item == NULL) {
//         syslog(LOG_WARNING, "%s is skipped", disk_name);
//         g_free(node->data);
//         continue;
//       }
//       syslog(LOG_INFO, "appending disk %s", item->storage_id);
//       disks_list = g_list_append(disks_list, item);
//     }
//     g_free(node->data);
//   }
//   syslog(LOG_INFO, "About to create folder");
//   /* Get path of dockerd folder on sdcard. create it if not existing*/
//   // create_folder("dockerd");
//   g_timeout_add_seconds(10, (GSourceFunc)create_folder, "dockerd");
//   syslog(LOG_INFO, "Folder request setup");

//   ret = true;
// out:
//   g_list_free(disks);
//   return ret;
// }

char *
get_sdcard_path(void)
{
  if (_sdcard_path != NULL)
    return strdup(_sdcard_path);
  else
    return NULL;
}

int
drop_axstorage(void)
{
  free_disk_item_t();
  return 0;
}