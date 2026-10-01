#include "epd_wrapper.h"
#include "global.h"
#include "sdcard_bsp.h" 
#include "utils.h"
#include <WiFi.h>

// SPI Configuration
static custom_lcd_spi_t spi_settings = {
    .cs = EPD_CS_PIN,
    .dc = EPD_DC_PIN,
    .rst = EPD_RST_PIN,
    .busy = EPD_BUSY_PIN,
    .mosi = EPD_MOSI_PIN,
    .scl = EPD_SCK_PIN,
    .spi_host = EPD_SPI_HOST,
    .buffer_len = (EPD_WIDTH * EPD_HEIGHT) / 8
};

EPD_Wrapper::EPD_Wrapper() 
    : Adafruit_GFX(EPD_WIDTH, EPD_HEIGHT), 
      driver(EPD_WIDTH, EPD_HEIGHT, spi_settings) {
}

void EPD_Wrapper::begin(i2c_equipment_shtc3 *sensor_ptr, i2c_equipment *rtc_ptr, UserData *userdata_ptr) {
    // 1. Power up Screen
    pinMode(EPD_PWR_PIN, OUTPUT);
    digitalWrite(EPD_PWR_PIN, LOW);
    delay(50); 
    
    // 2. Init Subsystems
    driver.EPD_Init();
    driver.EPD_Clear();

    // 3. Init SHTC3 Sensor
    // NOTE: Ensure your I2C bus is initialized before this line runs!
    // e.g., i2c_bsp_init(); 
    // if (shtc3 == NULL) {
    //     shtc3 = new i2c_equipment_shtc3();
    // }
    this->shtc3 = sensor_ptr;
    this->rtc = rtc_ptr;
    this->userdata = userdata_ptr;
}

void EPD_Wrapper::drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) return;
    uint8_t driver_color = (color > 0) ? DRIVER_COLOR_BLACK : DRIVER_COLOR_WHITE;
    driver.EPD_DrawColorPixel(x, y, driver_color);
}

// --- HELPER: FETCH SD DATA ---
void EPD_Wrapper::fetchSDData(uint64_t &freeBytes, uint64_t &usedBytes, uint64_t &totalBytes) {
    sdcard_get_stats(&totalBytes, &usedBytes, &freeBytes);
}

// --- HELPER: FETCH SENSOR DATA ---
void EPD_Wrapper::fetchSensorData(float &temp, float &humidity) {
    if (shtc3 != NULL) {
        // Read actual data from the sensor
        shtc3_data_t data = shtc3->readTempHumi();
        temp = data.Temp;
        humidity = data.RH;
        
        Serial.printf("Sensor -> Temp: %.2fC, Humi: %.2f%%\n", temp, humidity);
    } else {
        // Fallback if init failed
        temp = 0.0;
        humidity = 0.0;
    }
}

// --- MAIN DASHBOARD FUNCTION ---
void EPD_Wrapper::updateDashboard() {
    uint64_t freeBytes = 0, usedBytes = 0, totalBytes = 0;
    float temp, hum;

    // 1. Fetch Real Data
    fetchSDData(freeBytes, usedBytes, totalBytes);
    fetchSensorData(temp, hum);

    // 2. Format Storage Units
    float freeVal, usedVal, totalVal;
    char freeUnit[3], usedUnit[3], totalUnit[3];

    Utils::formatStorage(freeBytes, freeVal, freeUnit);
    Utils::formatStorage(usedBytes, usedVal, usedUnit);
    Utils::formatStorage(totalBytes, totalVal, totalUnit);

    // 3. Draw Interface
    fillScreen(0); 

    // **********************
    
    // Header
    this->displayHeader();

    // SD Card
    setTextColor(1); 
    drawRoundRect(5, 40, 24, 32, 2, 1); 
    drawLine(19, 40, 29, 50, 1);         
    drawLine(29, 50, 29, 72, 1);         
    fillRect(9, 44, 3, 8, 1);           
    fillRect(14, 44, 3, 8, 1);
    fillRect(19, 44, 3, 8, 1);

    setTextSize(2); setCursor(40, 48); print("STORAGE");

    // Bar
    drawRect(5, 80, 190, 14, 1);
    float percentage = (totalBytes > 0) ? ((float)usedBytes / (float)totalBytes) : 0;
    int fillWidth = (int)(190 * percentage);
    if(fillWidth < 2 && usedBytes > 0) fillWidth = 2; 
    if(fillWidth > 190) fillWidth = 190;
    fillRect(5, 80, fillWidth, 14, 1); 
    
    // Storage Text
    setTextSize(2); setCursor(5, 100); print("Used: "); 
    setTextSize(2); print(usedVal, 2); print(" "); print(usedUnit);
    
    setTextSize(2); setCursor(5, 120); print("Free: "); 
    setTextSize(2); print(freeVal, 2); print(" "); print(freeUnit);

    // Sensors
    drawLine(0, 140, 199, 140, 1);
    drawLine(100, 140, 100, 199, 1); 

    // Temperature
    setTextSize(2); setCursor(20, 148); print("TEMP");
    setCursor(15, 170); setTextSize(3); print((int)temp);
    setTextSize(1); print("o"); setTextSize(3); print("C");

    // Humidity
    setTextSize(2); setCursor(115, 148); print("HUMID");
    setCursor(115, 170); setTextSize(3); print((int)hum); print("%");

    // 4. Eco indicator
    if (globalConfig.isEcoMode) {
        setTextColor(1);
        setTextSize(1);
        setCursor(5, 190);
        print("ZZZ");
    }

    // 5. Refresh
    driver.EPD_Display();
}

void EPD_Wrapper::displayHeader() {
    // 1. Fetch Time
    RtcDateTime_t now = this->rtc->get_rtcTime();

    // 2. Draw Header Background
    fillRect(0, 0, 200, 28, 1); 

    // 3. Setup Text
    setTextColor(0); 
    setTextSize(2); // Increased font size
    setCursor(5, 6); // y=6 centers Size 2 text (approx 14px high) in 28px header

    // 4. Format Date/Time: "Mar 12 12:30"
    const char* monthNames[] = { "", "Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    
    // Safety check for month index
    const char* monStr = (now.month >= 1 && now.month <= 12) ? monthNames[now.month] : "???";

    char timeBuffer[20];
    sprintf(timeBuffer, "%s %d %02d:%02d", 
            monStr, now.day, now.hour, now.minute);
    
    print(timeBuffer);

    // 5. Battery Logic
    int batPct = userdata->getBatteryPercentage();
    int numBars = 0;
    
    if (batPct > 80)      numBars = 5;
    else if (batPct > 60) numBars = 4;
    else if (batPct > 40) numBars = 3;
    else if (batPct > 20) numBars = 2;
    else if (batPct > 5)  numBars = 1;

    // 6. Draw Battery Icon (Top Right: x=170)
    int batX = 170;
    int batY = 6;
    
    drawRect(batX, batY, 22, 14, 0); 
    fillRect(batX + 22, batY + 4, 2, 6, 0); 

    for (int i = 0; i < numBars; i++) {
        fillRect(batX + 2 + (i * 4), batY + 2, 3, 10, 0);
    }

    // 7. Draw Charging Icon
    if (userdata->getChargingStatus() == 1) {
        int flashX = batX - 12;
        drawLine(flashX + 5, batY, flashX + 2, batY + 7, 0);
        drawLine(flashX + 2, batY + 7, flashX + 6, batY + 7, 0); 
        drawLine(flashX + 6, batY + 7, flashX + 3, batY + 14, 0); 
    }
}

void EPD_Wrapper::startSetup() {
    fillScreen(0);
    // 2. Draw a Header Bar
    fillRect(0, 0, 200, 35, 1);
    setTextColor(0);
    setTextSize(2);
    setCursor(45, 10);
    print("SETTINGS");

    // 3. Main Status Text
    setTextColor(1);
    setTextSize(2);
    
    setCursor(10, 70);
    print("Connect to WiFi");

    setCursor(10, 95);
    print("AP EPY_Setup");

    setCursor(10, 120);
    print("through IP addr");

    setCursor(10, 145);
    print("http://192.168.");

    setCursor(10, 170);
    print("4.1");

    driver.EPD_Display();
}

void EPD_Wrapper::completeSetup() {
    fillScreen(0);
    // 2. Draw a Header Bar
    fillRect(0, 0, 200, 35, 1);
    setTextColor(0);
    setTextSize(2);
    setCursor(45, 10);
    print("CONNECTED");

    // 3. Main Status Text
    setTextColor(1);
    setTextSize(2);
    
    setCursor(10, 70);
    print("Connect to: ");

    setCursor(10, 95);
    print("http://");

    setCursor(10, 120);
    print(globalConfig.ipAddress);

    setCursor(10, 145);
    print("in your browser");

    driver.EPD_Display();
}

void EPD_Wrapper::constantMode() {
    // 1. Clear buffer
    fillScreen(0);

    // 2. Draw Header
    fillRect(0, 0, 200, 35, 1);
    setTextColor(0);
    setTextSize(2);
    // Center alignment roughly for "CURRENT MODE"
    setCursor(30, 10); 
    print("CURRENT MODE");

    // 3. Main Mode Display
    setTextColor(1);
    setTextSize(3); 
    setCursor(30, 60); 
    print("CONSTANT");

    // 4. Divider
    drawFastHLine(10, 100, 180, 0);

    // 5. Instructions (Minimum Size 2)
    setTextSize(2);
    
    // Line 1: Action
    setCursor(35, 115);
    print("Short Press");

    // Line 2: The Button Visual
    // Draw box slightly larger to accommodate Size 2 text
    drawRect(70, 138, 60, 24, 0); 
    setCursor(82, 143);
    print("PWR");

    // Line 3: Result
    setCursor(35, 170);
    print("-> ECO");

    // 6. Push to display
    driver.EPD_Display();
}

void EPD_Wrapper::ecoMode() {
    // 1. Clear buffer
    fillScreen(0);

    // 2. Draw Header
    fillRect(0, 0, 200, 35, 1);
    setTextColor(0);
    setTextSize(2);
    // Center alignment roughly for "CURRENT MODE"
    setCursor(30, 10); 
    print("CURRENT MODE");

    // 3. Main Mode Display - "ECO"
    setTextColor(1);
    setTextSize(4); 
    setCursor(65, 55); 
    print("ECO");

    // 4. Divider
    drawFastHLine(10, 100, 180, 0);

    // 5. Instructions (Minimum Size 2)
    setTextSize(2);
    
    // Line 1: Action
    setCursor(35, 115);
    print("Short Press");

    // Line 2: The Button Visual
    // Draw box slightly larger to accommodate Size 2 text
    drawRect(70, 138, 60, 24, 0); 
    setCursor(82, 143);
    print("PWR");

    // Line 3: Result
    setCursor(35, 170);
    print("-> CONSTANT");

    // 6. Push to display
    driver.EPD_Display();
}