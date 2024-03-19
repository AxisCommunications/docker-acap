#pragma once
#include <stdbool.h>

enum Location { LOCATION_NOT_SET = 0, LOCATION_INTERNAL, LOCATION_SDCARD };

typedef void (*OnStorageAvailableCallback)(const char *path);
typedef void (*OnStorageRevokedCallback)(void);

// on_storage_available() will be called immediately for LOCATION_INTERNAL and
// whenever the SD card becomes available.
struct storage *storage_init(OnStorageAvailableCallback on_storage_available,
                             OnStorageRevokedCallback on_storage_revoked);

void storage_free(struct storage *storage);

bool storage_set_location(struct storage *storage, enum Location location);
