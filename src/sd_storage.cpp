#include "sd_storage.h"
#include "config.h"
#include <SD_MMC.h>

bool initSDCard() {
    // ESP32-CAM flash LED (GPIO 4) can interfere with SD bus — keep it OFF
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);

    if (!SD_MMC.begin("/sdcard", true)) {  // true = 1-bit mode
        Serial.println("[SD] SD_MMC init FAILED");
        return false;
    }

    Serial.printf("[SD] Card mounted — size: %lluMB\n",
                  SD_MMC.totalBytes() / (1024 * 1024));

    // Create /db directory if it doesn't exist
    if (!SD_MMC.exists(SD_DB_DIR)) {
        SD_MMC.mkdir(SD_DB_DIR);
    }

    // Create default empty JSON files if they don't exist
    const char* files[] = {
        SD_USERS_FILE, SD_ANNOUNCE_FILE, SD_CONVOS_FILE,
        SD_DMS_FILE, SD_FOG_FILE, SD_MSGS_FILE
    };
    for (const char* f : files) {
        if (!SD_MMC.exists(f)) {
            File file = SD_MMC.open(f, FILE_WRITE);
            if (file) {
                file.print("[]");
                file.close();
            }
        }
    }

    return true;
}

bool readJsonFile(const char* path, JsonDocument& doc) {
    File file = SD_MMC.open(path, FILE_READ);
    if (!file) {
        Serial.printf("[SD] File not found: %s\n", path);
        return false;
    }
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        Serial.printf("[SD] JSON parse error in %s: %s\n", path, err.c_str());
        return false;
    }
    return true;
}

bool writeJsonFile(const char* path, JsonDocument& doc) {
    File file = SD_MMC.open(path, FILE_WRITE);
    if (!file) {
        Serial.printf("[SD] Cannot write: %s\n", path);
        return false;
    }
    serializeJson(doc, file);
    file.close();
    return true;
}
