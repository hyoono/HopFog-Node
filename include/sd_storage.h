#ifndef SD_STORAGE_H
#define SD_STORAGE_H

#include <ArduinoJson.h>

bool initSDCard();
bool readJsonFile(const char* path, JsonDocument& doc);
bool writeJsonFile(const char* path, JsonDocument& doc);

#endif // SD_STORAGE_H
