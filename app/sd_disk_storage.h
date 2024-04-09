#pragma once

typedef void (*SdDiskCallback)(const char* area_path, void* user_data);

// Call sd_disk_callback with a path to the SD card when it has become
// available. Call sd_disk_callback with NULL when it is about to be unmounted.
// Unmounting will fail if the SD card area contains open files when the
// callback returns.
struct sd_disk_storage* sd_disk_storage_init(SdDiskCallback sd_disk_callback, void* user_data);

void sd_disk_storage_free(struct sd_disk_storage* storage);
