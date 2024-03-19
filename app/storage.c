#include "storage.h"
#include <axsdk/axstorage.h>
#include <stdio.h>
#include <syslog.h>
#include <unistd.h>

struct storage {
  enum Location location;
  OnStorageAvailableCallback on_available;
  OnStorageRevokedCallback on_revoked;
  uint subscription_id;
  AXStorage *handle;
};

struct storage *
storage_init(OnStorageAvailableCallback on_storage_available,
             OnStorageRevokedCallback on_storage_revoked)
{
  fprintf(stderr, "%s\n", __FUNCTION__);
  struct storage *storage = g_malloc0(sizeof(struct storage));
  storage->on_available = on_storage_available;
  storage->on_revoked = on_storage_revoked;
  return storage;
}

static gboolean
event_status_or_log(gchar *storage_id, AXStorageStatusEventId event)
{
  fprintf(stderr, "%s(%s, %d)\n", __FUNCTION__, storage_id, event);
  GError *error = NULL;
  gboolean value = ax_storage_get_status(storage_id, event, &error);
  if (error) {
    syslog(LOG_WARNING, "Could not read ax_storage status: %s", error->message);
    g_clear_error(&error);
  }
  fprintf(stderr, " -> %d\n", value);
  return value;
}

static void
setup_cb(AXStorage *handle, gpointer storage_void_ptr, GError *error)
{
  fprintf(stderr, "%s\n", __FUNCTION__);
  struct storage *storage = storage_void_ptr;
  if (handle)
    storage->handle = handle;

  if (!handle) {
    fprintf(stderr, "NO HANDLE!\n");
  }
  if (error) {
    syslog(LOG_WARNING, "setup_cb error: %s", error->message);
    g_clear_error(&error);
    storage->on_revoked();
    return;
  }

  fprintf(stderr, "ax_storage_get_path()...\n");
  g_autofree char *path = ax_storage_get_path(handle, &error);
  if (!path) {
    fprintf(stderr, "NO PATH!\n");
    syslog(LOG_WARNING, "Failed to get storage path: %s", error->message);
    g_clear_error(&error);
    storage->on_revoked();
    return;
  }
  fprintf(stderr, " OK\n");

  fprintf(stderr, "storage->on_available(%s)\n", path);
  storage->on_available(path);
}

static void
release_cb(gpointer, GError *error)
{
  fprintf(stderr, "%s\n", __FUNCTION__);
  if (error)
    syslog(LOG_WARNING, "Error while releasing storage: %s", error->message);
}

static void
release(struct storage *storage)
{
  fprintf(stderr, "%s\n", __FUNCTION__);
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
release_and_unsubscribe(struct storage *storage)
{
  fprintf(stderr, "%s\n", __FUNCTION__);
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

  storage->location = LOCATION_NOT_SET;
}

void
storage_free(struct storage *storage)
{
  fprintf(stderr, "%s\n", __FUNCTION__);
  if (storage)
    release_and_unsubscribe(storage);
  free(storage);
}

static void
subscribe_cb(gchar *storage_id, gpointer storage_void_ptr, GError *error)
{
  fprintf(stderr, "%s(%s)\n", __FUNCTION__, storage_id);
  struct storage *storage = storage_void_ptr;

  syslog(LOG_INFO, "subscribe_cb: %s", storage_id);
  if (error) {
    syslog(LOG_WARNING, "subscribe_cb error: %s", error->message);
    g_clear_error(&error);
    storage->on_revoked();
  }

  if (event_status_or_log(storage_id, AX_STORAGE_EXITING_EVENT)) {
    storage->on_revoked();
    release(storage);
  }

  if (event_status_or_log(storage_id, AX_STORAGE_WRITABLE_EVENT)) {
    fprintf(stderr, "ax_storage_setup_async(%s)...\n", storage_id);
    if (!ax_storage_setup_async(storage_id, setup_cb, storage, &error)) {
      syslog(LOG_WARNING, "ax_storage_setup_async error: %s", error->message);
      g_clear_error(&error);
      storage->on_revoked();
    }
    fprintf(stderr, " OK\n");
  }
}

static bool
subscribe(struct storage *storage, const char *storage_id)
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
  if (!found) {
    syslog(LOG_ERR, "No storage %s found", storage_id);
    return false;
  }
  return true;
}

bool
storage_set_location(struct storage *storage, enum Location new_location)
{
  fprintf(stderr, "%s\n", __FUNCTION__);

  release_and_unsubscribe(storage);

  storage->location = new_location;
  switch (storage->location) {
    case LOCATION_INTERNAL:
      storage->on_available("/var/lib/docker/containers");
      return true;

    case LOCATION_SDCARD:
      return subscribe(storage, "SD_DISK");

    default:
      return false;
  }
}
