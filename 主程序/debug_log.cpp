#include "debug_log.h"

static void printLogPrefix(const char* level, const char* module, const char* event) {
  Serial.print('[');
  Serial.print(millis());
  Serial.print("][");
  Serial.print(level);
  Serial.print("][");
  Serial.print(module);
  Serial.print("][");
  Serial.print(event);
  Serial.print(']');
}

void logEvent(const char* level, const char* module, const char* event) {
  printLogPrefix(level, module, event);
  Serial.println();
}

void logEvent(const char* level, const char* module, const char* event, const char* detail) {
  printLogPrefix(level, module, event);
  if (detail != nullptr && detail[0] != '\0') {
    Serial.print(' ');
    Serial.print(detail);
  }
  Serial.println();
}

void logEvent(const char* level, const char* module, const char* event, const String& detail) {
  printLogPrefix(level, module, event);
  if (detail.length() > 0) {
    Serial.print(' ');
    Serial.print(detail);
  }
  Serial.println();
}

void logInfo(const char* module, const char* event) {
  logEvent("INFO", module, event);
}

void logInfo(const char* module, const char* event, const char* detail) {
  logEvent("INFO", module, event, detail);
}

void logInfo(const char* module, const char* event, const String& detail) {
  logEvent("INFO", module, event, detail);
}

void logWarn(const char* module, const char* event) {
  logEvent("WARN", module, event);
}

void logWarn(const char* module, const char* event, const char* detail) {
  logEvent("WARN", module, event, detail);
}

void logWarn(const char* module, const char* event, const String& detail) {
  logEvent("WARN", module, event, detail);
}

void logError(const char* module, const char* event) {
  logEvent("ERROR", module, event);
}

void logError(const char* module, const char* event, const char* detail) {
  logEvent("ERROR", module, event, detail);
}

void logError(const char* module, const char* event, const String& detail) {
  logEvent("ERROR", module, event, detail);
}
