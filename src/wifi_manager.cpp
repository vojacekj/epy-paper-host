#include "global.h"
#include "ap.h"
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <wifi_manager.h>
#include "event_manager.h"

#define AP_SSID      "EPY_Setup"
#define MDNS_HOSTNAME "memo" // http://memo.local + DHCP client name

const char* KEY_WIFI_SSID = "WSSID";
const char* KEY_WIFI_PASSWORD = "WPassword";
const char* KEY_WIFI_IP = "WIP";

static const char MIME_JSON[] = "application/json";
static const char MIME_HTML[] = "text/html";
static const char JSON_404[] = "{\"success\":false,\"message\":\"Not found\"}";
static const String NOT_FOUND_RESPONSE = JSON_404;

// Global instances
DNSServer dnsServer;
AsyncWebServer apServer(80);
bool isConfigured = false;
bool apMode = false;
unsigned long restartTime = 0;
bool restartScheduled = false;

void setupAPMode() {
    Serial.println("Starting AP mode. Connect to '" + String(AP_SSID) + "'");
    
    // Stop any existing WiFi
    WiFi.disconnect(true);
    delay(100);
    
    // Configure AP mode
    WiFi.mode(WIFI_AP);
    delay(500);
    
    // Start soft AP
    if (!WiFi.softAP(AP_SSID)) {
        Serial.println("❌ Failed to start AP!");
        return;
    }
    
    delay(500);
    
    // Wait for AP IP
    Serial.print("⏳ Waiting for AP IP...");
    unsigned long start = millis();
    while (WiFi.softAPIP().toString() == "0.0.0.0" && millis() - start < 10000) {
        delay(100);
    }
    Serial.println(" " + WiFi.softAPIP().toString());

    apServer.onNotFound([](AsyncWebServerRequest *request) {
      String urlStr = request->url();
      const char* url = urlStr.c_str();
      Serial.printf("❌ Route not found: %s\n", url);
      
      // Do NOT redirect - serve directly or return 204
      if (strncmp(url, "/gen_", 5) == 0 || strncmp(url, "/connectivity", 13) == 0 || strcmp(url, "/204") == 0 || strcmp(url, "/generate_204") == 0 || strcmp(url, "/success.txt") == 0) {
        Serial.println("204 response.");
        auto response = request->beginResponse(204);  // No Content
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
      } else if (strncmp(url, "/api/", 5) == 0 || strncmp(url, "/scan", 5) == 0 || strncmp(url, "/configure", 10) == 0) {
        Serial.println("404 response.");
        auto response = request->beginResponse(404, MIME_JSON, NOT_FOUND_RESPONSE.c_str());
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
      } else {
        Serial.println("200 response.");
        auto response = request->beginResponse(200, MIME_HTML, setupHTML);
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
      }
    });
    Serial.println("✅ Registered 404 handler");

    // ===== Web Server Routes =====
    apServer.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
      Serial.println("✅ /scan handler executing...");
      yield();
      
      // Temporarily enable STA for scanning
      wifi_mode_t savedMode = WiFi.getMode();
      WiFi.mode(WIFI_AP_STA);
      delay(100);
      
      WiFi.scanDelete();
      int numNetworks = WiFi.scanNetworks(false, true);
      
      String json;
      if (numNetworks <= 0) {
          json = "{\"success\":false,\"message\":\"No networks found\",\"networks\":[]}";
      } else {
          json = "{\"success\":true,\"networks\":[";
          for (int i = 0; i < numNetworks; i++) {
              if (i > 0) json += ",";
              json += "{\"ssid\":\"" + escapeJsonString(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"encryption\":" + String((WiFi.encryptionType(i) != WIFI_AUTH_OPEN) ? "true" : "false") + "}";
          }
          json += "]}";
      }
      
      request->send(200, "application/json", json);
        
      // Restore original mode
      WiFi.mode(savedMode);
    });
    Serial.println("✅ Registered GET /scan");

    apServer.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
      Serial.println("✅ /status handler executing...");
      String json = "{\"connected\":true,\"ssid\":\"" + WiFi.SSID() + "\",\"ip\":\"" + WiFi.localIP().toString() + "\",\"rssi\":" + String(WiFi.RSSI()) + "}";
      request->send(200, "application/json", json);
    });
    Serial.println("✅ Registered GET /status");

    apServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      Serial.println("📄 Serving setup page");
      auto response = request->beginResponse(200, MIME_HTML, setupHTML);
      response->addHeader("Access-Control-Allow-Origin", "*");
      request->send(response);
    });
    Serial.println("✅ Registered GET /");

    apServer.on("/configure", HTTP_POST, [](AsyncWebServerRequest *request) {
      Serial.println("✅ /configure handler executing...");
      
      String ssid;
      String password;
      
      if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
          ssid = request->getParam("ssid", true)->value();
          password = request->getParam("password", true)->value();
      } else {
          request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing parameters\"}");
          return;
      }
      
      if (ssid.length() == 0) {
          request->send(400, "application/json", "{\"success\":false,\"message\":\"SSID cannot be empty\"}");
          return;
      }
      
      Serial.println("🧪 Testing WiFi credentials for: " + ssid);
      String ip = testWiFiConnection(ssid, password);
      
      if (ip == "") {
          request->send(400, "application/json", "{\"success\":false,\"message\":\"Failed to connect. Please check credentials.\"}");
          return;
      }
      
      prefs.begin(appNamespace.c_str(), false);
      
      // Save credentials
      prefs.putString(KEY_WIFI_SSID, ssid);
      prefs.putString(KEY_WIFI_PASSWORD, password);
      prefs.putBool(KEY_STATUS_SCREEN.c_str(), true);
      prefs.end();

      // Schedule restart
      restartTime = millis() + 5000;
      restartScheduled = true;
      Serial.println("⏳ Restart scheduled in 5 seconds...");
      
      // CRITICAL: Use heap-allocated String to avoid concurrency race conditions
      // and prevent AsyncWebServer request stack from overflowing
      String successStr = String(successHTMLTemplate);
      
      successStr.replace("%SSID%", ssid);
      successStr.replace("%IP%", ip);

      strncpy(globalConfig.ipAddress, ip.c_str(), sizeof(globalConfig.ipAddress) - 1);
      globalConfig.ipAddress[sizeof(globalConfig.ipAddress) - 1] = '\0'; // Ensure null termination

      // 2. Pass the named array to the queue
      enqueueEvent(EVENT_CONNECTED);
      
      request->send(200, "text/html", successStr);
    });

    Serial.println("✅ Registered POST /configure");

    // Start web server
    apServer.begin();
    Serial.println("✅ Web server started");

    apMode = true;
    
    // Start DNS server - CRITICAL: must be AFTER web server
    if (dnsServer.start(53, "*", WiFi.softAPIP())) {
        Serial.println("✅ DNS server started (captive portal active)");
    } else {
        Serial.println("❌ Failed to start DNS server!");
    }
}

String testWiFiConnection(String ssid, String password) {
    Serial.println("🔍 Testing connection to: " + ssid);
    
    wifi_mode_t currentMode = WiFi.getMode();
    WiFi.mode(WIFI_AP_STA);
    
    WiFi.disconnect(true);
    delay(100);
    
    WiFi.begin(ssid.c_str(), password.c_str());
    
    Serial.print("⏳ Testing");
    for (int i = 0; i < 10; i++) {
        delay(400);
        yield(); // Feed watchdog
        
        wl_status_t status = WiFi.status();
        
        if (status == WL_CONNECTED) {
            String ip = WiFi.localIP().toString();
            Serial.println("\n✅ Test successful! IP: " + ip);
            WiFi.disconnect(false);
            WiFi.mode(currentMode);
            delay(100);
            return ip;
        }
        else if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
            Serial.println("\n❌ Failed early: " + String(status));
            break;
        }
        else if (i > 5 && status == WL_DISCONNECTED) {
            Serial.println("\n❌ Timeout");
            break;
        }
        
        Serial.print(".");
    }
    
    Serial.println("\n❌ Test failed");
    WiFi.disconnect(false);
    WiFi.mode(currentMode);
    delay(100);
    return "";
}

static void startMDNS() {
    if (!MDNS.begin(MDNS_HOSTNAME)) {
        Serial.println("❌ mDNS start failed (continuing without memo.local)");
        return;
    }
    MDNS.addService("http", "tcp", 80); // Portal web server (src.ino: server(80))
    Serial.println("✅ mDNS ready: http://" MDNS_HOSTNAME ".local");
}

String connectToWiFi(String ssid, String password) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(MDNS_HOSTNAME); // Advertise to router/DHCP as "memo"
    WiFi.begin(ssid.c_str(), password.c_str());

    Serial.print("Connecting");
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > 20000) {
            Serial.println("\n❌ Timeout! Restarting...");
            delay(1000);
            ESP.restart();
        }
        delay(500);
    }
    startMDNS();
    String ip = WiFi.localIP().toString();
    Serial.println("\n✅ WiFi Connected! IP: " + ip);
    return ip;
}

String Wifi_Init() {
    prefs.begin(appNamespace.c_str(), false);
    String saved_ssid = prefs.getString(KEY_WIFI_SSID, "");
    String saved_pass = prefs.getString(KEY_WIFI_PASSWORD, "");
    
    if (saved_ssid.length() == 0 || saved_pass.length() == 0) {
      prefs.end();
      Serial.println("No WiFi credentials found.");
      setupAPMode();
      return "";
    } else {
      Serial.print("Connecting to: ");
      Serial.println(saved_ssid);
      String ip = connectToWiFi(saved_ssid, saved_pass);
      prefs.putString(KEY_WIFI_IP, ip);
      prefs.end();
      return ip;
    }
}

void processDNSrequests() {
    // Safety: only process if AP mode is active
    if (!apMode) {
        return;
    }
    
    // Rate limit DNS processing to prevent overload
    static unsigned long lastDNS = 0;
    if (millis() - lastDNS < 10) return; // Max 100 queries/sec
    lastDNS = millis();
    
    dnsServer.processNextRequest();
    
    // Handle scheduled restart
    if (restartScheduled && restartTime > 0 && millis() >= restartTime) {
        Serial.println("🔄 Restarting now...");
        ESP.restart();
    }
}

String escapeJsonString(String input) {
    if (input.length() == 0) return "";
    
    String output;
    output.reserve(input.length() * 2);
    
    for (size_t i = 0; i < input.length(); i++) {
        char c = input.charAt(i);
        switch (c) {
            case '\\': output += "\\\\"; break;
            case '\"': output += "\\\""; break;
            case '/':  output += "\\/"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (c >= 32 && c <= 126) {
                    output += c;
                } else {
                    output += "\\u00";
                    if (c < 16) output += '0';
                    output += String(c, HEX);
                }
                break;
        }
    }
    return output;
}