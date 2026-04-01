#include "rc_fs.h"
#include <LittleFS.h>

bool rc_fs_begin() {
  if (!LittleFS.begin()) {
    Serial.println("[FS] LittleFS mount failed");
    return false;
  }

  Serial.println("[FS] Listing:");
  File root = LittleFS.open("/");
  if (!root) {
    Serial.println("  cannot open /");
    return true;
  }
  File f = root.openNextFile();
  while (f) {
    Serial.printf("  %s (%u bytes)\n", f.name(), (unsigned)f.size());
    f = root.openNextFile();
  }
  return true;
}
