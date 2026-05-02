/*
  Structured debug log helper

  The remote debugging workflow depends on students sending back plain serial
  text.  Every log line therefore uses one predictable format:

    [milliseconds][LEVEL][MODULE][EVENT] key=value key2=value2

  Example:

    [12345][INFO][CAM][IMAGE_LEN] bytes=18342

  Keep these log strings ASCII.  ASCII survives most serial monitor encodings,
  so logs copied from a remote computer are easier to read and search.
*/

#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <Arduino.h>

void logEvent(const char* level, const char* module, const char* event);
void logEvent(const char* level, const char* module, const char* event, const char* detail);
void logEvent(const char* level, const char* module, const char* event, const String& detail);

void logInfo(const char* module, const char* event);
void logInfo(const char* module, const char* event, const char* detail);
void logInfo(const char* module, const char* event, const String& detail);

void logWarn(const char* module, const char* event);
void logWarn(const char* module, const char* event, const char* detail);
void logWarn(const char* module, const char* event, const String& detail);

void logError(const char* module, const char* event);
void logError(const char* module, const char* event, const char* detail);
void logError(const char* module, const char* event, const String& detail);

#endif
