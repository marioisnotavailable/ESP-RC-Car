#include "rc_fs.h"
#include "rc_console.h"
#include <LittleFS.h>

bool rc_fs_begin() {
  if (!LittleFS.begin()) {
    console.println("[FS] LittleFS mount failed");
    return false;
  }

  console.println("[FS] Listing:");
  File root = LittleFS.open("/");
  if (!root) {
    console.println("  cannot open /");
    return true;
  }
  File f = root.openNextFile();
  while (f) {
    console.printf("  %s (%u bytes)\n", f.name(), (unsigned)f.size());
    f = root.openNextFile();
  }
  return true;
}
