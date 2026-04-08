#include "rc_recovery.h"
#include "rc_console.h"
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>

bool safeMode = false;

static Preferences prefsRecovery;
static uint32_t bootMs = 0;
static bool markedStable = false;

void rc_recovery_check_boot() {
  prefsRecovery.begin("recovery", false);
  uint8_t crashes = prefsRecovery.getUChar("crashes", 0);
  crashes++;
  prefsRecovery.putUChar("crashes", crashes);
  prefsRecovery.end();

  console.printf("[REC] Boot crash counter: %u / %u\n", crashes, CRASH_THRESHOLD);

  if (crashes >= CRASH_THRESHOLD) {
    safeMode = true;
    console.println("[REC] === SAFE MODE === Nur WiFi-AP + OTA + Terminal aktiv");
  }

  bootMs = millis();

  // Task-Watchdog auf Main-Loop (10s Timeout)
  esp_task_wdt_init(10, true);
  esp_task_wdt_add(NULL);
}

void rc_recovery_loop() {
  // Watchdog füttern — wenn loop() hängt, resettet der WDT nach 10s
  esp_task_wdt_reset();

  if (markedStable) return;

  // Nach 30s stabilem Betrieb: Counter zurücksetzen
  if (millis() - bootMs >= BOOT_STABLE_MS) {
    rc_recovery_mark_stable();
  }
}

void rc_recovery_mark_stable() {
  if (markedStable) return;
  markedStable = true;

  prefsRecovery.begin("recovery", false);
  prefsRecovery.putUChar("crashes", 0);
  prefsRecovery.end();

  console.println("[REC] Boot stabil — Crash-Counter zurueckgesetzt");

  // OTA-Rollback bestätigen
  rc_rollback_confirm();
}

void rc_rollback_confirm() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
      esp_ota_mark_app_valid_cancel_rollback();
      console.println("[REC] OTA-Partition als gueltig markiert — Rollback deaktiviert");
    }
  }
}
