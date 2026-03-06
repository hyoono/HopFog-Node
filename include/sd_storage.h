#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <Arduino.h>
#include <ArduinoJson.h>

/// Initialize the SD card. Returns true on success.
bool initSDCard();

/// Read a JSON file from SD into a JsonDocument.
/// Returns true if file exists and was parsed.
bool readJsonFile(const char* path, JsonDocument& doc);

/// Write a JsonDocument to a file on SD (overwrites).
bool writeJsonFile(const char* path, JsonDocument& doc);

#endif // SD_STORAGE_H
