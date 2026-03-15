#include "auth.h"
#include <mbedtls/sha256.h>
#include <esp_random.h>

static const int SALT_BYTES = 8;  // Generates 16-char hex salt

static String sha256Hex(const String& input) {
    unsigned char hash[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx,
        (const unsigned char*)input.c_str(), input.length());
    mbedtls_sha256_finish(&ctx, hash);
    mbedtls_sha256_free(&ctx);

    String hex;
    hex.reserve(64);
    for (int i = 0; i < 32; i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", hash[i]);
        hex += buf;
    }
    return hex;
}

static String generateSalt() {
    String salt;
    salt.reserve(SALT_BYTES * 2);
    for (int i = 0; i < SALT_BYTES; i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", (uint8_t)esp_random());
        salt += buf;
    }
    return salt;
}

String hashPassword(const String& password) {
    String salt = generateSalt();
    String hash = sha256Hex(salt + ":" + password);
    return salt + ":" + hash;
}

bool verifyPassword(const String& password, const String& storedHash) {
    int sep = storedHash.indexOf(':');
    if (sep < 0) return false;
    String salt = storedHash.substring(0, sep);
    String expectedHash = storedHash.substring(sep + 1);
    String computedHash = sha256Hex(salt + ":" + password);
    return computedHash == expectedHash;
}
