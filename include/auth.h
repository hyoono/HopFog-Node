#ifndef AUTH_H
#define AUTH_H

#include <Arduino.h>

String hashPassword(const String& password);
bool verifyPassword(const String& password, const String& storedHash);

#endif
