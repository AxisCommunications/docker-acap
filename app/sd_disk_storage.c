#include "sd_disk_storage.h"
#include <axsdk/axstorage.h>
#include <stdbool.h>
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>

struct sd_disk_storage {
  SdDiskCallback callback;
  void *user_data;
  uint subscription_id;
  AXStorage *handle;
};

static bool
event_status_or_log(gchar *storage_id, AXStorageStatusEventId event)
{
  GError *error = NULL;
  bool value = ax_storage_get_status(storage_id, event, &error);
  if (error) {
    syslog(LOG_WARNING, "Could not read ax_storage status: %s", error->message);
    g_clear_error(&error);
  }
  return value;
}

static void
setup_cb(AXStorage *handle, gpointer storage_void_ptr, GError *error)
{
  struct sd_disk_storage *storage = storage_void_ptr;
  if (handle)
    storage->handle = handle;

  if (error) {
    syslog(LOG_WARNING, "setup_cb error: %s", error->message);
    g_clear_error(&error);
    storage->callback(NULL, storage->user_data);
    return;
  }

  g_autofree char *path = ax_storage_get_path(handle, &error);
  if (!path) {
    syslog(LOG_WARNING, "Failed to get storage path: %s", error->message);
    g_clear_error(&error);
    storage->callback(NULL, storage->user_data);
    return;
  }

  storage->callback(path, storage->user_data);
}

static void
release_cb(gpointer, GError *error)
{
  if (error)
    syslog(LOG_WARNING, "Error while releasing storage: %s", error->message);
}

static void
release(struct sd_disk_storage *storage)
{
  GError *error = NULL;
  if (storage->handle) {
    if (!ax_storage_release_async(storage->handle, release_cb, NULL, &error)) {
      syslog(LOG_WARNING, "Failed to release storage: %s", error->message);
      g_clear_error(&error);
    }
    storage->handle = NULL;
  }
}

static void
release_and_unsubscribe(struct sd_disk_storage *storage)
{
  GError *error = NULL;

  release(storage);

  if (storage->subscription_id) {
    if (!ax_storage_unsubscribe(storage->subscription_id, &error)) {
      syslog(LOG_WARNING,
             "Failed to unsubscribe to storage events: %s",
             error->message);
      g_clear_error(&error);
    }
    storage->subscription_id = 0;
  }
}

void
sd_disk_storage_free(struct sd_disk_storage *storage)
{
  if (storage)
    release_and_unsubscribe(storage);
  free(storage);
}

static void
subscribe_cb(gchar *storage_id, gpointer storage_void_ptr, GError *error)
{
  struct sd_disk_storage *storage = storage_void_ptr;

  if (error) {
    syslog(LOG_WARNING, "subscribe_cb error: %s", error->message);
    g_clear_error(&error);
    storage->callback(NULL, storage->user_data);
  }

  if (event_status_or_log(storage_id, AX_STORAGE_EXITING_EVENT)) {
    storage->callback(NULL, storage->user_data);
    release(storage);
  }

  if (event_status_or_log(storage_id, AX_STORAGE_WRITABLE_EVENT)) {
    if (!ax_storage_setup_async(storage_id, setup_cb, storage, &error)) {
      syslog(LOG_WARNING, "ax_storage_setup_async error: %s", error->message);
      g_clear_error(&error);
      storage->callback(NULL, storage->user_data);
    }
  }
}

static bool
subscribe(struct sd_disk_storage *storage, const char *storage_id)
{
  GError *error = NULL;
  bool found = false;
  GList *devices = ax_storage_list(&error);
  for (GList *node = g_list_first(devices); node; node = g_list_next(node)) {
    if (strcmp(node->data, storage_id) == 0) {
      found = true;
      if (!(storage->subscription_id = ax_storage_subscribe(
                node->data, subscribe_cb, storage, &error))) {
        syslog(LOG_ERR,
               "Failed to subscribe to events of %s: %s",
               (char *)node->data,
               error->message);
        g_clear_error(&error);
        return false;
      }
    }
    g_free(node->data);
  }
  g_list_free(devices);
  if (!found)
    syslog(LOG_INFO,
           "No storage with id %s found",
           storage_id); // Not an error if products doesn't have SD card slot
  return true;
}

struct sd_disk_storage *
sd_disk_storage_init(SdDiskCallback sd_disk_callback, void *user_data)
{
  struct sd_disk_storage *storage = g_malloc0(sizeof(struct sd_disk_storage));
  storage->callback = sd_disk_callback;
  storage->user_data = user_data;
  if (!subscribe(storage, "SD_DISK")) {
    sd_disk_storage_free(storage);
    return NULL;
  }
  return storage;
}
