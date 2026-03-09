#include "sd_storage.h"
#include "config.h"
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>

#define SD_FS SD

bool initSDCard() {
    // ESP32-CAM: use SPI mode to access the built-in SD card slot.
    // This avoids the SD_MMC peripheral which permanently claims
    // GPIO 12/13 via IOMUX.
    static SPIClass spiSD(HSPI);
    spiSD.begin(SD_SPI_CLK, SD_SPI_MISO, SD_SPI_MOSI, SD_CS_PIN);
    if (!SD.begin(SD_CS_PIN, spiSD)) {
        return false;
    }

    // Create /db directory if needed
    if (!SD_FS.exists(SD_DB_DIR)) {
        SD_FS.mkdir(SD_DB_DIR);
    }

    // Seed empty JSON array files
    const char* files[] = {
        SD_USERS_FILE, SD_ANNOUNCE_FILE, SD_CONVOS_FILE,
        SD_DMS_FILE, SD_FOG_FILE, SD_MSGS_FILE
    };
    for (const char* f : files) {
        if (!SD_FS.exists(f)) {
            File file = SD_FS.open(f, FILE_WRITE);
            if (file) {
                file.print("[]");
                file.close();
            }
        }
    }

    return true;
}

bool readJsonFile(const char* path, JsonDocument& doc) {
    File file = SD_FS.open(path, FILE_READ);
    if (!file) {
        dbgprintf("[SD] File not found: %s\n", path);
        return false;
    }
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        dbgprintf("[SD] JSON parse error in %s: %s\n", path, err.c_str());
        return false;
    }
    return true;
}

bool writeJsonFile(const char* path, JsonDocument& doc) {
    File file = SD_FS.open(path, FILE_WRITE);
    if (!file) {
        dbgprintf("[SD] Cannot write: %s\n", path);
        return false;
    }
    serializeJson(doc, file);
    file.close();
    return true;
}
