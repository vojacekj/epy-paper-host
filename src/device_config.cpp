#include "device_config.h"
#include "esp32-hal-gpio.h"
#include "event_manager.h"
#include "global.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <driver/rtc_io.h>

unsigned long lastInteractionTime = 0;
unsigned long lastRefreshTime = 0;
const unsigned long SLEEP_TIMEOUT_MS = 30000; // 30 seconds
unsigned long lastTimeCheck = 0;
bool hasCheckedTimeThisWake = false;

// Instantiate the global object with the default fallbacks
DeviceConfig globalConfig = {
    false,             // isEcoMode
    true,              // battery is attached
    10,                // interval
    0,                 // ecoThresholdPct (0 = disabled)
    "EVENT_DASHBOARD", // screen
    "",                // ipAddress will be set on load
    0                  // lastSyncDay
};

void loadConfig() {
  File file = UserFS.open("/dynamic/setting.json", "r");
  if (!file) {
    Serial.println(
        "No setting.json found. Creating file with default values...");
    saveConfig();
    return;
  }

  JsonDocument doc;

  // Deserialize directly from the LittleFS file stream
  DeserializationError error = deserializeJson(doc, file);
  if (!error) {
    // Map JSON to the global struct using the '|' operator for safe fallbacks
    globalConfig.isEcoMode = doc["isEcoMode"] | globalConfig.isEcoMode;
    globalConfig.isBatteryAttached =
        doc["isBatteryAttached"] | globalConfig.isBatteryAttached;
    globalConfig.interval = doc["interval"] | globalConfig.interval;
    globalConfig.ecoThresholdPct =
        doc["ecoThresholdPct"] | globalConfig.ecoThresholdPct;
    globalConfig.lastSyncDay = doc["lastSyncDay"] | 0;

    // Safely copy the string into our fixed char array
    strlcpy(globalConfig.screen, doc["screen"] | globalConfig.screen,
            sizeof(globalConfig.screen));

    strlcpy(globalConfig.ipAddress, WiFi.localIP().toString().c_str(),
            sizeof(globalConfig.ipAddress)); // Update IP on load

    Serial.println("Settings loaded successfully.");
  } else {
    Serial.print("Failed to parse setting.json: ");
    Serial.println(error.c_str());
  }

  file.close();
}

void saveConfig() {
  File file = UserFS.open("/dynamic/setting.json", "w");
  if (!file) {
    Serial.println("Failed to open setting.json for writing!");
    return;
  }

  JsonDocument doc;

  // Map the global struct back to the JSON document
  doc["isEcoMode"] = globalConfig.isEcoMode;
  doc["isBatteryAttached"] = globalConfig.isBatteryAttached;
  doc["interval"] = globalConfig.interval;
  doc["ecoThresholdPct"] = globalConfig.ecoThresholdPct;
  doc["screen"] = globalConfig.screen;
  doc["ipAddress"] = globalConfig.ipAddress;
  doc["lastSyncDay"] = globalConfig.lastSyncDay;

  // Serialize directly to the file stream (highly memory efficient)
  serializeJson(doc, file);
  file.close();

  Serial.println("Settings saved to /dynamic/setting.json");
}

void resetSleepTimer() {
  lastInteractionTime = millis();
  lastRefreshTime = millis();
}

void loopConfig(Portal *portal, i2c_equipment *rtc, UserData *userdata) {
  if ((!hasCheckedTimeThisWake && millis() > 2000) ||
      (hasCheckedTimeThisWake && millis() - lastTimeCheck > 60000)) {

    hasCheckedTimeThisWake = true;
    lastTimeCheck = millis();

    if (!globalConfig.isEcoMode) {
      // 1. Keep the sleep timer reset so we don't sleep if mode changes later
      lastInteractionTime = millis(); // (Optional, depends if you want
                                      // interaction to delay updates)

      // 2. Auto-Eco: switch to Eco mode once the battery drops to/below the
      // configured threshold. One-shot — never auto-returns to Constant mode.
      if (globalConfig.ecoThresholdPct > 0 && globalConfig.isBatteryAttached &&
          userdata->getBatteryPercentage() <= globalConfig.ecoThresholdPct) {
        Serial.printf(
            "[Auto-Eco] Battery at/below %d%%. Switching to Eco mode.\n",
            globalConfig.ecoThresholdPct);
        enqueueEvent(EVENT_ECO_MODE);
        return;
      }

      // 3. Check if the Interval has passed
      unsigned long intervalMs =
          (unsigned long)globalConfig.interval * 60 * 1000;

      if (millis() - lastRefreshTime > intervalMs) {
        Serial.printf(
            "[Constant Mode] Interval %d min passed. Refreshing Screen...\n",
            globalConfig.interval);

        portal->persistStats(); // Save stats before refresh

        // Trigger the configured event (e.g., DASHBOARD)
        EventType targetEvent = getEventFromString(globalConfig.screen);
        enqueueEvent(targetEvent);

        // Reset the refresh timer
        lastRefreshTime = millis();

        logger.save(); // Flush logs to disk after each refresh
      }
      return; // Exit function here (Skip Sleep Logic)
    }

    if (millis() - lastInteractionTime > SLEEP_TIMEOUT_MS) {
      Serial.println(
          "[EcoMode] Inactivity detected. Preparing for Deep Sleep...");

      // --- 1. SET INTERNAL TIMER WAKEUP ---
      uint64_t sleepDuration =
          (uint64_t)globalConfig.interval * 60 * 1000000ULL;
      esp_sleep_enable_timer_wakeup(sleepDuration);

      // --- 2. SET BUTTON WAKEUP (GPIO 18) ---
      // Keep the internal pull-up active during sleep so the pin doesn't float
      rtc_gpio_pullup_en(PWR_BUTTON_PIN);
      rtc_gpio_pulldown_dis(PWR_BUTTON_PIN);

      // Wake up when GPIO 18 goes LOW (Button pressed)
      esp_sleep_enable_ext0_wakeup(PWR_BUTTON_PIN, 0);

      Serial.printf(
          "Going to sleep for %d minutes or until PWR is pressed...\n",
          globalConfig.interval);
      Serial.flush();

      logger.info("Device entering deep sleep mode."); // Log before sleeping

      logger.save(); // Flush logs to disk after each refresh
      esp_deep_sleep_start();
    }
  }
}