#include "board_power_bsp.h"
#include "event_manager.h"
#include "global.h"
#include "sdcard_bsp.h"
#include "smart_button.h"
#include "user_data.h"
#include "wifi_manager.h"
#include <ESPAsyncWebServer.h>
#include <driver/rtc_io.h>

board_power_bsp_t board_div(EPD_PWR_PIN, Audio_PWR_PIN, VBAT_PWR_PIN);
EPD_Wrapper display;
Preferences prefs;
fs::LittleFSFS SysFS;
fs::LittleFSFS UserFS;
AsyncWebServer server(80);
Portal portal;
SmartButton bootBtn(BOOT_BUTTON_PIN);
SmartButton pwrBtn(PWR_BUTTON_PIN);

i2c_equipment_shtc3 *sensor = NULL;
i2c_equipment *rtc = NULL;
Logger logger;
UserData userdata;

const String appNamespace = "epy";
const String KEY_STATUS_SCREEN = "status_screen";

String ipAddr = "";

const EventType screenSequence[] = {EVENT_DASHBOARD, EVENT_CONNECTED};

// Calculate the number of screens automatically
const int SCREEN_COUNT = sizeof(screenSequence) / sizeof(screenSequence[0]);

// Tracks the current position in the array
int currentScreenIndex = 0;

void syncScreenIndex() {
  EventType startupEvent = getEventFromString(globalConfig.screen);
  for (int i = 0; i < SCREEN_COUNT; i++) {
    if (screenSequence[i] == startupEvent) {
      currentScreenIndex = i;
      Serial.printf("Navigation synced to index %d (%s)\n", i,
                    globalConfig.screen);
      return;
    }
  }
  // If not found, default to 0
  currentScreenIndex = 0;
}

void navigateScreens(int direction) {
  // Update Index with wrapping
  // The complex modulo math handles negative numbers correctly in C++
  currentScreenIndex =
      (currentScreenIndex + direction + SCREEN_COUNT) % SCREEN_COUNT;

  EventType nextEvent = screenSequence[currentScreenIndex];

  Serial.printf("Navigation: Switched to Screen Index %d\n",
                currentScreenIndex);

  // 1. Trigger the Event
  enqueueEvent(nextEvent);

  // 2. Optional: Save this new screen as the default for next boot?
  // Uncomment if you want the device to remember navigation changes immediately
  /*
  const char* eventName = "EVENT_DASHBOARD"; // You'd need a reverse-lookup
  helper here
  // strlcpy(globalConfig.screen, eventName, sizeof(globalConfig.screen));
  // saveConfig();
  */
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Starting WiFi setup.");
  ipAddr = Wifi_Init();

  Wire.begin(47, 48);

  Serial.println("Initialising SHTC3 sensor for temperature and humidity.");
  sensor = new i2c_equipment_shtc3();

  Serial.println("Initialising RTC.");
  rtc = new i2c_equipment();

  initEventManager(rtc); // Pass the RTC pointer to the Event Manager

  // Initialize EPD display
  display.begin(sensor, rtc, &userdata);

  if (ipAddr.length() == 0) {
    Serial.println("In AP mode, skip other initialisation...");
    enqueueEvent(EVENT_SETUP);
    return;
  }

  Serial.println("Initialising SD Card.");
  sdcard_init();

  if (!SysFS.begin(true, "/sys", 10, "app_fs")) {
    logger.error("SysFS Mount Failed");
  }

  if (!UserFS.begin(true, "/user", 10, "user_fs")) {
    logger.error("UserFS Mount Failed");
  }

  logger.begin(&UserFS, rtc);
  logger.info("System Booted");

  board_div.VBAT_POWER_ON();

  Serial.println("Initialising admin web server.");
  portal.begin(&server, sensor, &userdata);

  rtc_gpio_deinit(PWR_BUTTON_PIN);
  resetSleepTimer();

  // Initialize buttons (sets up pin modes and Bounce2)
  loadConfig();
  syncScreenIndex();
  bootBtn.begin();
  pwrBtn.begin();

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Wakeup: RTC Timer");
    enqueueEvent(getEventFromString(globalConfig.screen));
  } else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("Wakeup Cause: PWR Button Pressed, set to CONSTANT mode");
    enqueueEvent(EVENT_CONSTANT_MODE);
    for (int i = 0; i < SCREEN_COUNT; i++) {
      if (screenSequence[i] == EVENT_DASHBOARD)
        currentScreenIndex = i;
    }
  } else {
    Serial.println("Wakeup: Power On");
  }

  // Only take effect after device reboot
  // if(globalConfig.isBatteryAttached) {
  //   logger.info("Attach battery to the device");
  //   board_div.VBAT_POWER_ON();
  // } else {
  //   logger.info("Detach battery to the device");
  //   board_div.VBAT_POWER_OFF();
  // }

  // =========================================================
  // BOOT BUTTON (GPIO 0) CALLBACKS
  // Using modern C++ Lambdas for super clean, inline code
  // =========================================================
  bootBtn.attachSingleClick([]() {
    Serial.println("BOOT Button: Single Click Detected!");
    Serial.println("Rotate to next view.");
    resetSleepTimer();
    navigateScreens(1); // Next
  });

  bootBtn.attachDoubleClick([]() {
    Serial.println("BOOT Button: Double Click Detected!");
    Serial.println("Return to previous view.");
    resetSleepTimer();
    navigateScreens(-1); // Previous
  });

  bootBtn.attachLongPressStart([]() {
    Serial.println("BOOT Button: Long Press Started!");
    Serial.println("Clearing all saved WiFi credentials and rebooting...");
    prefs.begin(appNamespace.c_str(), false);
    prefs.clear();
    prefs.end();
    ESP.restart();
  });

  // =========================================================
  // PWR BUTTON (GPIO 18) CALLBACKS
  // =========================================================
  pwrBtn.attachSingleClick([]() {
    Serial.println("[PWR] Action: Single Click");
    resetSleepTimer();

    // Toggle the Eco Mode in the global struct
    globalConfig.isEcoMode = !globalConfig.isEcoMode;

    Serial.print("--> Power Mode changed to: ");
    Serial.println(globalConfig.isEcoMode ? "Eco mode." : "Constant mode.");

    if (globalConfig.isEcoMode) {
      enqueueEvent(EVENT_ECO_MODE);
    } else {
      enqueueEvent(EVENT_CONSTANT_MODE);
    }
  });

  pwrBtn.attachDoubleClick([]() {
    Serial.println("PWR Button: Double Click Detected!");
    Serial.println("Syncing time with NTP server.");
    enqueueEvent(EVENT_RTC_UPDATE);
  });

  pwrBtn.attachLongPressStart([]() {
    Serial.println("PWR Button: Long Press Started!");
    // clear non volatile storage on long press
    Serial.println("Device rebooting...");
    ESP.restart();
  });
}

void loop() {
  if (ipAddr.length() == 0) {
    processDNSrequests();
  } else {
    portal.loop();
    delay(10);

    bootBtn.update();
    pwrBtn.update();
    delay(10);

    loopConfig(&portal, rtc, &userdata);
  }
}