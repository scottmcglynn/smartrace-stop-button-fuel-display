#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WebSocketsClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>

// Display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Button pin
const int buttonPin = D4;

// Configuration structure
struct Config {
  char ssid[32];
  char password[64];
  char ws_host[64];
  int ws_port;
  int controller_id;
  bool configured;
  char magic[4];  // "CFG" to validate EEPROM
  char light_host[64];
  int light_port;
};

Config config;
ESP8266WebServer server(80);
WebSocketsClient webSocket;

// State variables
bool apMode = false;
unsigned long buttonPressTime = 0;
unsigned long lastButtonReleaseTime = 0;  // For debounce after medium/long press
bool showingIP = false;
bool ipDisplayLocked = false;
int currentFuel = 100;
String currentDriverName = "";  // Only store current controller's driver name
String currentCarName = "";     // Store current controller's car name
bool connected = false;
bool wasConnected = false;  // Track previous state to prevent repeated "Disconnected" messages
bool raceEnded = false;
int finalPosition = 0;
bool positionLocked = false;  // Simplified: track if we've locked final position
bool showingStopMessage = false;
unsigned long stopMessageStartTime = 0;
bool showingDriverInfo = false;  // Track if displaying driver/car info

// AP credentials
const char* ap_ssid = "SmartRace-Setup";
const char* ap_password = "smartrace2025";  // AP password - change this!

// HTML page stored in PROGMEM to save RAM
const char CONFIG_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>SmartRace Fuel Display Config</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body{font-family:Arial;margin:20px;background:#f0f0f0}
.container{max-width:400px;margin:0 auto;background:white;padding:20px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}
h1{color:#333;font-size:24px;margin-bottom:20px}
label{display:block;margin:15px 0 5px;color:#666;font-weight:bold}
input{width:100%;padding:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}
button{width:100%;padding:12px;margin-top:20px;background:#007bff;color:white;border:none;border-radius:4px;font-size:16px;cursor:pointer}
button:hover{background:#0056b3}
.info{background:#e7f3ff;padding:10px;border-radius:4px;margin-bottom:20px;font-size:14px}
</style>
</head>
<body>
<div class="container">
<h1>🏁 SmartRace Config</h1>
<div class="info">Configure your fuel display to connect to WiFi and SmartRace server.</div>
<form method="POST" action="/save">
<label>WiFi Network Name (SSID)</label>
<input type="text" name="ssid" value="%SSID%" required>
<label>WiFi Password</label>
<input type="password" name="password" value="%PASSWORD%">
<label>SmartRace Server IP</label>
<input type="text" name="ws_host" value="%WS_HOST%" required placeholder="192.168.1.100">
<label>SmartRace Server Port</label>
<input type="number" name="ws_port" value="%WS_PORT%" required placeholder="53919">
<label>Controller ID</label>
<input type="number" name="controller_id" value="%CONTROLLER_ID%" min="1" max="6" required>
<label>SR Light Controller IP (optional)</label>
<input type="text" name="light_host" value="%LIGHT_HOST%" placeholder="192.168.1.200">
<label>SR Light Controller Port</label>
<input type="number" name="light_port" value="%LIGHT_PORT%" placeholder="8080">
<button type="submit">Save & Restart</button>
</form>
</div>
</body>
</html>
)rawliteral";

const char SAVE_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<title>Configuration Saved</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body{font-family:Arial;margin:20px;background:#f0f0f0;text-align:center}
.container{max-width:400px;margin:50px auto;background:white;padding:30px;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}
h1{color:#28a745;font-size:28px}
p{color:#666;font-size:16px;line-height:1.6}
</style>
</head>
<body>
<div class="container">
<h1>✓ Configuration Saved!</h1>
<p>Device is restarting and will connect to your WiFi network.</p>
<p>If successful, access it at:<br><strong>sr-fuel-%CONTROLLER_ID%.local</strong></p>
</div>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  pinMode(buttonPin, INPUT_PULLUP);
  
  // Initialize EEPROM
  EEPROM.begin(sizeof(Config));
  
  // Initialize display
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Display failed"));
    for(;;);
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Load configuration
  loadConfig();
  
  // Check if configured
  if (!config.configured) {
    startAPMode();
  } else {
    connectToWiFi();
  }
}

void loop() {
  // Handle button presses
  handleButton();
  
  // Check if stop message should be cleared
  if (showingStopMessage && (millis() - stopMessageStartTime >= 5000)) {
    showingStopMessage = false;
    drawFuelDisplay();
  }
  
  if (apMode) {
    // In AP mode, just handle web server
    server.handleClient();
    if (MDNS.isRunning()) {
      MDNS.update();
    }
  } else {
    // Normal operation
    webSocket.loop();
    server.handleClient();
    if (MDNS.isRunning()) {
      MDNS.update();
    }
  }
}

void handleButton() {
  static bool wasPressed = false;
  static bool ipShownDuringHold = false;
  bool isPressed = (digitalRead(buttonPin) == LOW);
  
  // Button just pressed
  if (isPressed && !wasPressed) {
    // Debounce: ignore if pressed too soon after last release (500ms)
    if (millis() - lastButtonReleaseTime < 500) {
      return;
    }
    
    buttonPressTime = millis();
    wasPressed = true;
    ipShownDuringHold = false;
  }
  
  // Button is being held - check for long press actions
  if (isPressed && wasPressed) {
    unsigned long pressDuration = millis() - buttonPressTime;
    
    // At 2 seconds, show IP address immediately
    if (pressDuration >= 2000 && pressDuration < 8000 && !apMode && !ipShownDuringHold) {
      ipShownDuringHold = true;
      showingStopMessage = false;  // Clear stop message if showing
      showingIP = true;  // Mark that we're showing IP
      showIPAddress();
    }
    
    // At 8 seconds, reset configuration
    if (pressDuration >= 8000) {
      lastButtonReleaseTime = millis();  // Set debounce time
      resetConfiguration();
      wasPressed = false;
      return;
    }
  }
  
  // Button released
  if (!isPressed && wasPressed) {
    unsigned long pressDuration = millis() - buttonPressTime;
    
    // If IP is locked on screen from a previous hold, any short press dismisses it
    if (ipDisplayLocked && pressDuration < 2000) {
      ipDisplayLocked = false;
      showingIP = false;
      drawFuelDisplay();
    }
    // If driver info is showing, short press dismisses it
    else if (showingDriverInfo && pressDuration < 2000) {
      showingDriverInfo = false;
      drawFuelDisplay();
    }
    // If final position is showing, short press dismisses it
    else if (raceEnded && finalPosition > 0 && pressDuration < 2000) {
      raceEnded = false;
      finalPosition = 0;
      positionLocked = false;
      drawFuelDisplay();
    }
    // Short press (< 2 sec) - emergency stop (only if stop message not already showing)
    else if (pressDuration < 2000 && !apMode && !ipDisplayLocked && !raceEnded && !showingDriverInfo && !showingStopMessage) {
      sendStopCommand();
      sendLightCommand();

      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(10, 8);
      display.println("STOPPED!");
      display.display();
      
      showingStopMessage = true;
      stopMessageStartTime = millis();
      lastButtonReleaseTime = millis();  // Set debounce timer to prevent rapid presses
    }
    // Released between 2-8 sec - lock IP display on screen
    else if (pressDuration >= 2000 && pressDuration < 8000 && !apMode) {
      ipDisplayLocked = true;
      showingIP = true;
      showingStopMessage = false;  // Clear stop message if showing
      lastButtonReleaseTime = millis();  // Set debounce time for medium press
      // IP already showing from the hold, just keep it locked
    }
    
    wasPressed = false;
  }
}

void showIPAddress() {
  showingIP = true;
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("IP: ");
  display.println(WiFi.localIP().toString());
  display.setCursor(0, 12);
  display.println("or");
  display.setCursor(0, 24);
  display.print("sr-fuel-");
  display.print(config.controller_id);
  display.println(".local");
  display.display();
}

void loadConfig() {
  EEPROM.get(0, config);
  
  // Check if EEPROM has valid config
  if (strncmp(config.magic, "CFG", 3) != 0) {
    // No valid config, set defaults
    config.configured = false;
    strcpy(config.magic, "CFG");
    config.ws_port = 53919;
    config.controller_id = 1;
    config.ssid[0] = '\0';
    config.password[0] = '\0';
    config.ws_host[0] = '\0';
    config.light_host[0] = '\0';
    config.light_port = 8080;
  }
}

void saveConfig() {
  EEPROM.put(0, config);
  EEPROM.commit();
}

void resetConfiguration() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 8);
  display.println("Resetting...");
  display.display();
  
  config.configured = false;
  saveConfig();
  
  delay(2000);
  ESP.restart();
}

void startAPMode() {
  apMode = true;
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("Setup Mode");
  display.print("AP: ");
  display.println(ap_ssid);
  display.println("192.168.4.1");
  display.print("PW: ");
  display.println(ap_password);
  display.display();
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);  // Password-protected AP
  
  // Setup web server
  setupWebServer();
  server.begin();
}

void connectToWiFi() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.println(config.ssid);
  display.display();
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid, config.password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi Failed!");
    display.println("Starting AP...");
    display.display();
    delay(2000);
    startAPMode();
    return;
  }
  
  // Setup mDNS with unique name based on controller ID
  String mdnsName = "sr-fuel-" + String(config.controller_id);
  if (MDNS.begin(mdnsName.c_str())) {
    MDNS.addService("http", "tcp", 80);
  }
  
  // Setup web server for configuration access
  setupWebServer();
  server.begin();
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi Connected!");
  display.println("Connecting to");
  display.println("SmartRace...");
  display.display();
  
  // Connect to WebSocket
  webSocket.begin(config.ws_host, config.ws_port, "/");
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
}

void handleRoot() {
  String page = FPSTR(CONFIG_PAGE);
  page.replace("%SSID%", config.ssid);
  page.replace("%PASSWORD%", config.password);
  page.replace("%WS_HOST%", config.ws_host);
  page.replace("%WS_PORT%", String(config.ws_port));
  page.replace("%CONTROLLER_ID%", String(config.controller_id));
  page.replace("%LIGHT_HOST%", config.light_host);
  page.replace("%LIGHT_PORT%", String(config.light_port));

  server.send(200, "text/html; charset=UTF-8", page);
}

void handleSave() {
  // Get form data
  if (server.hasArg("ssid")) {
    strncpy(config.ssid, server.arg("ssid").c_str(), sizeof(config.ssid) - 1);
  }
  if (server.hasArg("password")) {
    strncpy(config.password, server.arg("password").c_str(), sizeof(config.password) - 1);
  }
  if (server.hasArg("ws_host")) {
    strncpy(config.ws_host, server.arg("ws_host").c_str(), sizeof(config.ws_host) - 1);
  }
  if (server.hasArg("ws_port")) {
    config.ws_port = server.arg("ws_port").toInt();
  }
  if (server.hasArg("controller_id")) {
    config.controller_id = server.arg("controller_id").toInt();
  }
  if (server.hasArg("light_host")) {
    strncpy(config.light_host, server.arg("light_host").c_str(), sizeof(config.light_host) - 1);
  }
  if (server.hasArg("light_port")) {
    config.light_port = server.arg("light_port").toInt();
  }

  config.configured = true;
  saveConfig();
  
  String page = FPSTR(SAVE_PAGE);
  page.replace("%CONTROLLER_ID%", String(config.controller_id));
  server.send(200, "text/html; charset=UTF-8", page);
  
  delay(2000);
  ESP.restart();
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_CONNECTED:
      if (!connected) {  // Only if state actually changed
        connected = true;
        wasConnected = true;
        
        if (!showingIP && !ipDisplayLocked && !showingStopMessage) {
          display.clearDisplay();
          display.setCursor(0, 0);
          display.println("Connected!");
          display.println("Subscribing...");
          display.display();
        }
        
        delay(500);
        subscribeToController(config.controller_id);
      }
      break;
      
    case WStype_DISCONNECTED:
      if (connected || !wasConnected) {  // Only if state actually changed OR first disconnect
        connected = false;
        wasConnected = false;
        
        // Only show "Disconnected" if not showing IP or other messages
        if (!showingIP && !ipDisplayLocked && !showingStopMessage && !raceEnded) {
          display.clearDisplay();
          display.setCursor(0, 0);
          display.println("Disconnected");
          display.display();
        }
      }
      break;
      
    case WStype_TEXT:
      parseRaceData((char*)payload);
      break;
      
    case WStype_ERROR:
      break;
  }
}

void subscribeToController(int controllerId) {
  StaticJsonDocument<128> doc;
  doc["type"] = "controller_set";
  JsonObject data = doc.createNestedObject("data");
  data["controller_id"] = String(controllerId);
  
  String jsonString;
  serializeJson(doc, jsonString);
  webSocket.sendTXT(jsonString);
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Monitoring");
  display.print("Controller ");
  display.println(controllerId);
  display.display();
  delay(1500);
  
  drawFuelDisplay();
}

void sendStopCommand() {
  StaticJsonDocument<128> doc;
  doc["type"] = "stop";
  JsonObject data = doc.createNestedObject("data");
  data["controller_id"] = config.controller_id;
  
  String jsonString;
  serializeJson(doc, jsonString);
  webSocket.sendTXT(jsonString);
}

void sendLightCommand() {
  if (config.light_host[0] == '\0') return;

  WiFiClient client;
  HTTPClient http;
  String url = "http://" + String(config.light_host) + ":" + String(config.light_port) + "/";
  http.begin(client, url);
  http.setConnectTimeout(500);
  http.setTimeout(500);
  http.addHeader("Content-Type", "application/json");
  http.POST("{\"event_type\":\"event.change_status\",\"event_data\":{\"new\":\"suspended\"}}");
  http.end();
}

void parseRaceData(char* json) {
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, json);
  
  if (error) {
    return;
  }
  
  const char* msgType = doc["type"];
  if (msgType == nullptr) return;
  
  // Handle update_controller_data - store and display driver/car info
  if (strcmp(msgType, "update_controller_data") == 0) {
    JsonObject data = doc["data"];
    String controllerId = String(config.controller_id);
    
    if (data.containsKey(controllerId)) {
      const char* driver = data[controllerId]["driver"];
      const char* car = data[controllerId]["car"];
      
      if (driver != nullptr && strcmp(driver, "Unassigned") != 0) {
        currentDriverName = String(driver);
        currentCarName = (car != nullptr && strcmp(car, "Unassigned") != 0) ? String(car) : "";
        
        // Display driver and car info
        showingDriverInfo = true;
        displayDriverInfo();
      } else {
        currentDriverName = "";
        currentCarName = "";
        showingDriverInfo = false;
      }
    }
  }
  // Handle update_event_status to detect race end or start
  else if (strcmp(msgType, "update_event_status") == 0) {
    const char* status = doc["data"];
    if (status != nullptr) {
      if (strcmp(status, "ended") == 0) {
        raceEnded = true;
        positionLocked = false;  // Reset for new race end
      } 
      else if (strcmp(status, "running") == 0) {
        // Race started - clear driver info display
        if (showingDriverInfo) {
          showingDriverInfo = false;
          drawFuelDisplay();
        }
      }
      else {
        // Race is not ended - reset flags
        if (raceEnded) {
          raceEnded = false;
          finalPosition = 0;
          positionLocked = false;
          drawFuelDisplay();
        }
      }
    }
  }
  // Handle update_position - if race ended, use second message (with penalties)
  else if (strcmp(msgType, "update_position") == 0) {
    if (raceEnded && !positionLocked) {
      int position = doc["data"]["position"].as<int>();
      
      // First position message is without penalties, skip it
      if (finalPosition == 0) {
        finalPosition = position;  // Store temporarily
      } else {
        // Second message has penalties applied - use this one
        finalPosition = position;
        positionLocked = true;  // Lock it in
        displayFinalPosition();
      }
    }
  }
  // Handle update_fuel messages
  else if (strcmp(msgType, "update_fuel") == 0) {
    String controllerId = doc["data"]["controller_id"].as<String>();
    if (controllerId.toInt() == config.controller_id) {
      currentFuel = doc["data"]["fuel"].as<int>();
      drawFuelDisplay();
    }
  }
}

void displayDriverInfo() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  
  // Line 1: Ctrl x: DriverName
  display.print("Ctrl ");
  display.print(config.controller_id);
  display.print(": ");
  display.println(currentDriverName);
  
  // Line 2: Car name (may wrap to multiple lines if long)
  display.setCursor(0, 10);
  display.println(currentCarName);
  
  display.display();
}

void drawFuelDisplay() {
  if (!connected || showingIP || ipDisplayLocked || showingStopMessage || showingDriverInfo || (raceEnded && finalPosition > 0)) return;
  
  display.clearDisplay();
  
  // Display controller ID and fuel percentage on same line
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Ctrl ");
  display.print(config.controller_id);
  display.print(" Fuel: ");
  display.print(currentFuel);
  display.println("%");
  
  // Draw larger fuel bar (20 pixels tall instead of 16)
  int barWidth = 118;
  int barHeight = 20;  // Increased from 16
  int barX = 5;
  int barY = 10;  // Adjusted for better spacing
  
  // Draw border
  display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
  
  // Calculate filled portion
  int filledWidth = (barWidth - 2) * currentFuel / 100;
  
  // Fill the fuel bar
  if (filledWidth > 0) {
    display.fillRect(barX + 1, barY + 1, filledWidth, barHeight - 2, SSD1306_WHITE);
  }
  
  display.display();
}

void displayFinalPosition() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 12);  // Center vertically
  
  // Use stored driver name or fallback
  String driverName = currentDriverName.length() > 0 ? currentDriverName : "Driver";
  
  display.print(driverName);
  display.print(": ");
  
  // Convert to abbreviated ordinal (1st, 2nd, 3rd, etc)
  display.print(finalPosition);
  switch(finalPosition % 10) {
    case 1: if (finalPosition != 11) display.print("st"); else display.print("th"); break;
    case 2: if (finalPosition != 12) display.print("nd"); else display.print("th"); break;
    case 3: if (finalPosition != 13) display.print("rd"); else display.print("th"); break;
    default: display.print("th"); break;
  }
  display.print("!");
  
  display.display();
}
