#include "event_manager.h"
#include "device_config.h"
#include "global.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

// The FreeRTOS Queue Handle
static QueueHandle_t eventQueue = NULL;
i2c_equipment *rtcE = NULL;

// Helper to dynamically fetch timezone offset based on IP
long fetchTimezoneOffsetSecs() {
  HTTPClient http;
  // We only request the 'status' and 'offset' fields to save bandwidth and
  // memory
  http.begin("http://ip-api.com/json/?fields=status,offset");
  int httpCode = http.GET();

  long offsetSecs = 0; // Default fallback to UTC

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();

    JsonDocument doc; // Utilizing your existing ArduinoJson dependency
    DeserializationError error = deserializeJson(doc, payload);

    if (!error && doc["status"] == "success") {
      offsetSecs = doc["offset"].as<long>(); // Example: 3600 for +1 hr offset
      logger.info("Fetched timezone offset via IP: " + String(offsetSecs) +
                  " seconds");
    } else {
      logger.error("Failed to parse Timezone JSON format or status was not "
                   "full success");
    }
  } else {
    logger.error("Failed to connect to ip-api.com, defaulting to UTC");
  }

  http.end();
  return offsetSecs;
}

// ---------------------------------------------------------
// Handler Functions (Called by the Dispatcher on Core 0)
// ---------------------------------------------------------
static void handleSetup(const SystemEvent &evt) {
  Serial.println("[Core 0] Task: Entering SETUP Mode");
  logger.info("Entering SETUP Mode");
  display.startSetup();
}

static void handleSetupCompleted(const SystemEvent &evt) {
  Serial.println("[Core 0] Task: Setup Completed");
  logger.info("Setup Completed, Entering Connected Mode");
  display.completeSetup();
}

static void handleDashboard(const SystemEvent &evt) {
  Serial.println("[Core 0] Task: Loading DASHBOARD");
  logger.info("Entering DASHBOARD mode");
  display.updateDashboard();
}

static void handleEcoMode(const SystemEvent &evt) {
  Serial.println("[Core 0] Task: Switching to ECO_MODE");
  logger.info("Switching to ECO mode");
  globalConfig.isEcoMode = true;
  saveConfig();
  // Show the dashboard (with the eco ZZZ indicator) so the last screen before
  // deep sleep is useful info, not the mode splash.
  display.updateDashboard();
}

static void handleConstantMode(const SystemEvent &evt) {
  Serial.println("[Core 0] Task: Switching to CONSTANT_MODE");
  logger.info("Switching to CONSTANT mode");
  globalConfig.isEcoMode = false;
  saveConfig();
  display.constantMode();
}

static void handleRtcUpdate(const SystemEvent &evt) {
  Serial.println("[Core 0] Task: Updating RTC & Timezone");
  logger.info("Updating RTC with new time from SensorPCF85063");

  // 1. Fetch local timezone offset dynamically
  long localOffset = fetchTimezoneOffsetSecs();

  // 2. Sync with NTP using the local offset (replaces hardcoded 0)
  rtcE->syncRTCwithNTP(localOffset);

  // 3. Get the new validated date from RTC
  RtcDateTime_t now = rtcE->get_rtcTime();
  globalConfig.lastSyncDay = now.day;

  // 4. Save to LittleFS so we don't do it again today
  saveConfig();
  logger.info(
      "RTC Updated successfully with local timezone. Next sync tomorrow.");
}

static void handleLogRotation(const SystemEvent &evt) {
  Serial.println("[Core 0] Task: Rotating Logs");
  logger.info("Rotating logs as per schedule");
  logger.rotate();
}

// ---------------------------------------------------------
// The Core 0 Event Loop Task
// ---------------------------------------------------------
static void eventTask(void *pvParameters) {
  SystemEvent incomingEvent;

  Serial.printf("Event Task Started on Core: %d\n", xPortGetCoreID());

  // Infinite FreeRTOS loop
  for (;;) {
    // Wait indefinitely (portMAX_DELAY) for an event to arrive in the queue
    if (xQueueReceive(eventQueue, &incomingEvent, portMAX_DELAY) == pdPASS) {

      // Dispatch the event to the specific handler based on the enum
      switch (incomingEvent.type) {
      case EVENT_SETUP:
        handleSetup(incomingEvent);
        break;
      case EVENT_CONNECTED:
        handleSetupCompleted(incomingEvent);
        break;
      case EVENT_DASHBOARD:
        handleDashboard(incomingEvent);
        break;
      case EVENT_ECO_MODE:
        handleEcoMode(incomingEvent);
        break;
      case EVENT_CONSTANT_MODE:
        handleConstantMode(incomingEvent);
        break;
      case EVENT_RTC_UPDATE:
        handleRtcUpdate(incomingEvent);
        break;
      case EVENT_LOG_ROTATION:
        handleLogRotation(incomingEvent);
        break;
      default:
        Serial.println("Unknown Event!");
        break;
      }
    }
  }
}

// ---------------------------------------------------------
// Public API
// ---------------------------------------------------------
void initEventManager(i2c_equipment *rtc_ptr) {
  rtcE = rtc_ptr;
  // Create a queue capable of holding 10 events
  eventQueue = xQueueCreate(10, sizeof(SystemEvent));

  if (eventQueue == NULL) {
    Serial.println("Failed to create event queue!");
    return;
  }

  // Pin the Event Task to Core 0
  xTaskCreatePinnedToCore(
      eventTask,   // Function to implement the task
      "EventTask", // Name of the task
      8192, // Stack size in bytes (8192 is safe for heavy UI/SD operations)
      NULL, // Task input parameter
      1,    // Priority of the task (1 is standard)
      NULL, // Task handle (not needed here)
      0     // Core where the task should run (Core 0)
  );
}

bool enqueueEvent(EventType type, const char *strings[], uint8_t count) {
  if (eventQueue == NULL)
    return false;

  SystemEvent newEvent;
  newEvent.type = type;

  // Cap the count to prevent buffer overflows
  newEvent.payloadCount =
      (count > MAX_EVENT_STRINGS) ? MAX_EVENT_STRINGS : count;

  // Safely copy the string arrays into the struct
  for (uint8_t i = 0; i < newEvent.payloadCount; i++) {
    strlcpy(newEvent.payloads[i], strings[i], MAX_STRING_LENGTH);
  }

  logger.info("Enqueuing Event: " + String(type) + " with " +
              String(newEvent.payloadCount) + " payloads.");
  // Send the struct to the back of the queue.
  // Wait max 10 ticks if the queue is full.
  if (xQueueSend(eventQueue, &newEvent, (TickType_t)10) != pdPASS) {
    logger.error("Event Queue is Full! Dropping event.");
    return false;
  }
  return true;
}

EventType getEventFromString(const char *eventName) {
  if (strcmp(eventName, "EVENT_SETUP") == 0)
    return EVENT_SETUP;
  if (strcmp(eventName, "EVENT_DASHBOARD") == 0)
    return EVENT_DASHBOARD;
  if (strcmp(eventName, "EVENT_ECO_MODE") == 0)
    return EVENT_ECO_MODE;
  if (strcmp(eventName, "EVENT_CONSTANT_MODE") == 0)
    return EVENT_CONSTANT_MODE;
  if (strcmp(eventName, "EVENT_RTC_UPDATE") == 0)
    return EVENT_RTC_UPDATE;
  if (strcmp(eventName, "EVENT_CONNECTED") == 0)
    return EVENT_CONNECTED;

  // Default fallback if the string is invalid or empty
  // Serial.printf("Warning: Unknown event string '%s'. Defaulting to
  // DASHBOARD.\n", eventName);
  logger.warn("Unknown event string '" + String(eventName) +
              "'. Defaulting to DASHBOARD.");
  return EVENT_DASHBOARD;
}