// ===============================================
// AirESP - Professional Air Quality Monitor
// With Web-Based Fresh Air Calibration
// ===============================================
#include <DHT.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ESP.h>

// ==================== PIN DEFINITIONS ====================
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  -1
#define TFT_BL   32
#define DHTPIN   33
#define DHTTYPE  DHT22
#define MQ135_PIN 34
#define RED_LED  25
#define YELLOW_LED 26
#define GREEN_LED 27

// ==================== CONFIG ====================
bool enableWiFi = true;
bool enableMQTT = true;

const char* ssid = "NetworkNameHere";
const char* password = "WiFiPassHere";
const char* mqtt_server = "192.168.x.x";
const int mqtt_port = 1883;
const char* mqtt_user = "mqtt-user";
const char* mqtt_pass = "MQTTPass";
const char* device_id = "airesp";

// Objects
TFT_eSPI tft = TFT_eSPI();
DHT dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient client(espClient);
WebServer server(80);
Preferences preferences;

// Calibration
const float BASE_R0 = 20000.0;
float calibrationMultiplier = 1.0;

// Calibration Mode
bool inCalibrationMode = false;
unsigned long calibrationStartTime = 0;
float rsSum = 0.0;
int rsCount = 0;
const unsigned long CALIBRATION_DURATION = 300000; // 5 minutes

// Display
#define GRAPH_POINTS 40
float co2History[GRAPH_POINTS];
int historyIndex = 0;

unsigned long lastPageSwitch = 0;
bool pageMode = 0;
unsigned long lastTempSwitch = 0;
bool showFahrenheit = false;
unsigned long lastFullRedraw = 0;
bool needsFullRedraw = true;
bool mqttEverConnected = false;

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== AirESP Starting ===");

  dht.begin();
  preferences.begin("airesp", false);
  calibrationMultiplier = preferences.getFloat("multiplier", 1.0);
  Serial.printf("Loaded Multiplier = %.3f (Base R0 = %.0f)\n", calibrationMultiplier, BASE_R0);

  // Hardware
  pinMode(RED_LED, OUTPUT); pinMode(YELLOW_LED, OUTPUT); pinMode(GREEN_LED, OUTPUT);
  turnOffAllLEDs();
  pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);

  tft.init(); tft.setRotation(3); tft.fillScreen(TFT_BLACK);

  if (enableWiFi) {
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
      delay(500); Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected! IP: " + WiFi.localIP().toString());
      if (MDNS.begin("airesp")) Serial.println("→ http://airesp.local");
      setupWebServer();
    }
  }

  if (enableMQTT) {
    client.setServer(mqtt_server, mqtt_port);
    sendHA_Discovery();
  }

  drawHeader();
  tft.setTextColor(TFT_GREEN);
  tft.setTextSize(3);
  tft.setCursor(40, 60);
  tft.println("READY!");
  delay(2000);
  needsFullRedraw = true;
}

// ==================== CO2 READING WITH CALIBRATION ====================
float readCO2() {
  float mqRaw = 0;
  for (int i = 0; i < 15; i++) {
    mqRaw += analogRead(MQ135_PIN);
    delay(15);
  }
  mqRaw /= 15.0;

  float voltage = mqRaw * (3.3 / 4095.0);
  float Rs = ((3.3 / voltage) - 1.0) * 10000.0;

  float effective_R0 = BASE_R0 * calibrationMultiplier;
  float ratio = Rs / effective_R0;
  float co2_ppm = 116.6020682 * pow(ratio, -2.769034857);

  // ==================== CALIBRATION MODE ====================
  if (inCalibrationMode) {
    rsSum += Rs;
    rsCount++;
    float avgRs = rsSum / rsCount;
    float targetRatio = 1.15;  // Good target for ~400ppm with this formula
    float suggestedMult = (avgRs / BASE_R0) / targetRatio;

    Serial.printf("[CAL] Rs:%5.0f  Avg:%5.0f  Suggested Mult:%.3f  Count:%d\n", 
                  Rs, avgRs, suggestedMult, rsCount);

    if ((millis() - calibrationStartTime > CALIBRATION_DURATION) && rsCount > 60) {
      calibrationMultiplier = constrain(suggestedMult, 0.5, 6.0);
      preferences.putFloat("multiplier", calibrationMultiplier);
      Serial.printf("=== CALIBRATION COMPLETE ===\nNew Multiplier = %.3f\n", calibrationMultiplier);
      inCalibrationMode = false;
      needsFullRedraw = true;
    }
  }

  Serial.printf("Raw:%4.0f V:%.3f Rs:%5.0f Ratio:%.3f Mult:%.3f CO2:%.1f\n",
                mqRaw, voltage, Rs, ratio, calibrationMultiplier, co2_ppm);

  return max(co2_ppm, 350.0f);
}

// ==================== WEB DASHBOARD ====================
void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    float co2 = readCO2();

    String html = R"rawliteral(
      <!DOCTYPE html><html><head><meta charset="UTF-8">
      <meta http-equiv="refresh" content="30">
      <meta name="viewport" content="width=device-width,initial-scale=1">
      <title>AirESP</title>
      <style>
        body{font-family:Segoe UI,Arial,sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:20px}
        .container{max-width:640px;margin:auto;background:#1e2937;padding:25px;border-radius:16px;box-shadow:0 10px 30px rgba(0,0,0,0.6)}
        h1{color:#60a5fa;text-align:center;margin:0}
        .card{background:#334155;padding:18px;border-radius:12px;margin:15px 0}
        .value{font-size:2.1em;color:#60a5fa;margin:8px 0}
        .label{color:#94a3b8;font-size:0.95em}
        button{width:100%;padding:14px;color:white;border:none;border-radius:8px;font-size:1.1em;margin:8px 0}
        .cal-btn{background:#f59e0b !important}
      </style></head><body>
      <div class="container">
        <h1>🌬️ AirESP Live Monitor</h1>
        
        <div class="card"><div class="label">TEMPERATURE</div><div class="value">)rawliteral";
    html += String(t,1) + "°C / " + String(t*9/5+32,1) + "°F";
    html += R"rawliteral(</div></div>
        <div class="card"><div class="label">HUMIDITY</div><div class="value">)rawliteral";
    html += String(h,0) + "%";
    html += R"rawliteral(</div></div>
        <div class="card"><div class="label">CO₂ LEVEL</div><div class="value">)rawliteral";
    html += String(co2,0) + " ppm";
    html += R"rawliteral(</div></div>

        <div class="card">
          <h3>Calibration</h3>
          <p>Current Multiplier: <strong>)rawliteral";
    html += String(calibrationMultiplier, 3);
    html += R"rawliteral(</strong> (Base R0 = 10000)</p>
          
          <form action="/setMultiplier" method="POST" style="display:inline-block;width:48%">
            <input type="number" name="mult" value=")rawliteral";
    html += String(calibrationMultiplier, 3);
    html += R"rawliteral(" step="0.05" min="0.5" max="6.0" style="width:100%;padding:8px">
            <button type="submit">💾 Save & Reboot</button>
          </form>
          
          <form action="/startCalibration" method="POST" style="display:inline-block;width:48%">
            <button type="submit" class="cal-btn">🌬️ Start Fresh Air Calibration</button>
          </form>
        </div>
      </div>
      </body></html>
    )rawliteral";

    server.send(200, "text/html", html);
  });

  server.on("/setMultiplier", HTTP_POST, []() {
    if (server.hasArg("mult")) {
      float newMult = server.arg("mult").toFloat();
      if (newMult >= 0.5 && newMult <= 6.0) {
        calibrationMultiplier = newMult;
        preferences.putFloat("multiplier", calibrationMultiplier);
        Serial.printf("Multiplier manually set to %.3f\n", calibrationMultiplier);
        server.send(200, "text/html", "<h2>Saved!</h2><p>Rebooting...</p>");
        delay(1500);
        ESP.restart();
      }
    }
    server.send(400, "text/html", "<h2>Invalid</h2><a href='/'>Back</a>");
  });

  server.on("/startCalibration", HTTP_POST, []() {
    inCalibrationMode = true;
    calibrationStartTime = millis();
    rsSum = 0;
    rsCount = 0;
    Serial.println("=== FRESH AIR CALIBRATION STARTED ===");
    server.send(200, "text/html", 
      "<h2>🌬️ Calibration Started</h2>"
      "<p>Place the sensor in fresh outdoor air (~400 ppm CO₂).</p>"
      "<p>It will run for 5 minutes then auto-save.</p>"
      "<a href='/'>← Back to Dashboard</a>");
  });

  server.begin();
  Serial.println("Web Dashboard running at http://airesp.local");
}

// ==================== DISPLAY & OTHER FUNCTIONS (unchanged except calibration handling) ====================
void drawHeader() { /* ... same as before ... */ 
  // (copy your original drawHeader() here)
  tft.fillRect(0, 0, 240, 28, TFT_NAVY);
  tft.setTextColor(TFT_WHITE); tft.setTextSize(2); tft.setCursor(8, 6); tft.print("AirESP");
  bool wifiOK = !enableWiFi || (WiFi.status() == WL_CONNECTED);
  tft.setTextSize(1); tft.setCursor(152, 9); tft.setTextColor(wifiOK ? TFT_GREEN : TFT_RED); tft.print("WIFI");
  bool mqttOK = !enableMQTT || client.connected() || mqttEverConnected;
  tft.setCursor(200, 9); tft.setTextColor(mqttOK ? TFT_GREEN : TFT_RED); tft.print("MQTT");
}

void drawReadingsPage(float t, float h, float co2, String status, uint16_t color) {
  tft.fillScreen(TFT_BLACK); drawHeader();
  tft.setTextColor(TFT_CYAN); tft.setTextSize(2); tft.setCursor(10, 45);
  if (showFahrenheit) {
    float tF = t * 9.0 / 5.0 + 32.0;
    tft.printf("Temp: %.1f F", tF);
  } else {
    tft.printf("Temp: %.1f C", t);
  }
  tft.setCursor(10, 75); tft.printf("Hum: %.0f %%", h);
  tft.setCursor(10, 105); tft.setTextSize(2); tft.printf("CO2: %.0f ppm", co2);
  
  tft.setTextSize(4); tft.setTextColor(color); tft.setCursor(55, 155); tft.print(status);
}

void updateReadingsDynamic(float t, float co2, String status, uint16_t color) {
  tft.fillRect(10, 45, 220, 20, TFT_BLACK);
  tft.fillRect(10, 105, 220, 20, TFT_BLACK);
  tft.fillRect(50, 155, 140, 35, TFT_BLACK);
  // ... same as original
  tft.setTextColor(TFT_CYAN); tft.setTextSize(2); tft.setCursor(10, 45);
  if (showFahrenheit) {
    float tF = t * 9.0 / 5.0 + 32.0;
    tft.printf("Temp: %.1f F", tF);
  } else {
    tft.printf("Temp: %.1f C", t);
  }
  tft.setCursor(10, 105); tft.setTextSize(2); tft.printf("CO2: %.0f ppm", co2);
  tft.setTextSize(4); tft.setTextColor(color); tft.setCursor(55, 155); tft.print(status);
}

void drawGraphPage(float co2) {
  tft.fillScreen(TFT_BLACK); drawHeader();
  tft.setTextColor(TFT_WHITE); tft.setTextSize(2); tft.setCursor(10, 38);
  tft.printf("CO2: %.0f", co2); tft.setTextSize(1); tft.setCursor(165, 38); tft.println("ppm");

  int graphY = 70, graphH = 65;
  tft.drawRect(5, graphY, 230, graphH, TFT_DARKGREY);

  float dataMin = 99999.0f, dataMax = 0.0f; bool hasData = false;
  for (int i = 0; i < GRAPH_POINTS; i++) {
    if (co2History[i] > 0) {
      hasData = true;
      if (co2History[i] < dataMin) dataMin = co2History[i];
      if (co2History[i] > dataMax) dataMax = co2History[i];
    }
  }

  float minVal = hasData ? dataMin : 400;
  float maxVal = hasData ? dataMax : 2000;
  if (hasData && maxVal - minVal < 10) { minVal -= 100; maxVal += 100; }
  else if (hasData) {
    float range = maxVal - minVal;
    minVal -= range * 0.05f; maxVal += range * 0.05f;
  }
  if (minVal < 300) minVal = 300;

  for (int i = 0; i < GRAPH_POINTS - 1; i++) {
    int x1 = 10 + i * (220 / (GRAPH_POINTS - 1));
    int x2 = 10 + (i + 1) * (220 / (GRAPH_POINTS - 1));
    float y1 = graphY + graphH - ((co2History[i] - minVal) / (maxVal - minVal) * graphH);
    float y2 = graphY + graphH - ((co2History[i + 1] - minVal) / (maxVal - minVal) * graphH);
    if (co2History[i] > 0 && co2History[i + 1] > 0) tft.drawLine(x1, y1, x2, y2, TFT_YELLOW);
  }

  tft.setTextColor(TFT_LIGHTGREY); tft.setTextSize(1);
  tft.setCursor(10, graphY + graphH + 5); tft.print((int)minVal);
  tft.setCursor(200, graphY + graphH + 5); tft.print((int)maxVal);
}

void updateGraphDynamic(float co2) {
  tft.fillRect(10, 38, 120, 20, TFT_BLACK);
  tft.setTextColor(TFT_WHITE); tft.setTextSize(2); tft.setCursor(10, 38);
  tft.printf("CO2: %.0f", co2);
}

// ==================== MQTT FUNCTIONS ====================
void sendHA_Discovery() { if(!enableMQTT) return; if(!client.connected()) reconnect();
  sendSensorDiscovery("temperature","Temperature","°C","temperature");
  sendSensorDiscovery("humidity","Humidity","%","humidity");
  sendSensorDiscovery("co2","CO2","ppm","carbon_dioxide");
}

void sendSensorDiscovery(const char* sensor, const char* name, const char* unit, const char* deviceClass) {
  StaticJsonDocument<512> doc;
  doc["name"] = name;
  doc["state_topic"] = String("homeassistant/sensor/") + device_id + "/" + sensor + "/state";
  doc["unit_of_measurement"] = unit;
  doc["value_template"] = "{{ value_json." + String(sensor) + " }}";
  doc["unique_id"] = String(device_id) + "_" + sensor;
  doc["device"]["identifiers"][0] = device_id;
  doc["device"]["name"] = "AirESP Air Quality Monitor";
  doc["device"]["model"] = "ESP32 + ST7789";
  doc["device_class"] = deviceClass;

  char buffer[512];
  serializeJson(doc, buffer);
  client.publish(String("homeassistant/sensor/" + String(device_id) + "/" + sensor + "/config").c_str(), buffer, true);
}

void publishToHA(float t, float h, float co2) {
  if (!enableMQTT || !client.connected()) { reconnect(); return; }
  StaticJsonDocument<256> doc;
  doc["temperature"] = t; doc["humidity"] = h; doc["co2"] = co2;
  char buffer[256]; serializeJson(doc, buffer);
  client.publish(String("homeassistant/sensor/" + String(device_id) + "/temperature/state").c_str(), buffer, false);
  client.publish(String("homeassistant/sensor/" + String(device_id) + "/humidity/state").c_str(), buffer, false);
  client.publish(String("homeassistant/sensor/" + String(device_id) + "/co2/state").c_str(), buffer, false);
  mqttEverConnected = true;
}

void reconnect() {
  if (!enableMQTT) return;
  String clientId = "AirESP-" + String(random(0xffff), HEX);
  if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) mqttEverConnected = true;
}

// ==================== HELPERS ====================
void turnOffAllLEDs() { digitalWrite(RED_LED,LOW); digitalWrite(YELLOW_LED,LOW); digitalWrite(GREEN_LED,LOW); }
void setLEDs(int pin) { turnOffAllLEDs(); digitalWrite(pin, HIGH); }
void showError() { tft.fillScreen(TFT_BLACK); drawHeader(); tft.setTextColor(TFT_RED); tft.setTextSize(3); tft.setCursor(30, 80); tft.println("ERROR"); }

// ==================== MAIN LOOP ====================
void loop() {
  if (enableWiFi) server.handleClient();
  if (enableMQTT) client.loop();

  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();
  float co2_ppm = readCO2();

  co2History[historyIndex] = co2_ppm;
  historyIndex = (historyIndex + 1) % GRAPH_POINTS;

  if (isnan(humidity) || isnan(temperature)) {
    showError();
    delay(2000);
    return;
  }

  String statusText = "GOOD";
  uint16_t color = TFT_GREEN;
  int activeLED = GREEN_LED;

  if (inCalibrationMode) {
    statusText = "CAL";
    color = TFT_ORANGE;
    turnOffAllLEDs();
  } else if (co2_ppm < 800) {
    statusText = "GOOD"; color = TFT_GREEN; activeLED = GREEN_LED;
  } else if (co2_ppm < 1200) {
    statusText = "MOD"; color = TFT_YELLOW; activeLED = YELLOW_LED;
  } else {
    statusText = "POOR"; color = TFT_RED; activeLED = RED_LED;
  }

  if (!inCalibrationMode) setLEDs(activeLED);

  // Page switching
  if (millis() - lastPageSwitch > 7000) { 
    pageMode = !pageMode; 
    lastPageSwitch = millis(); 
    needsFullRedraw = true; 
  }
  if (millis() - lastTempSwitch > 10000){ 
    showFahrenheit = !showFahrenheit; 
    lastTempSwitch = millis(); 
    needsFullRedraw = true; 
  }

  if (enableMQTT && !client.connected()) {
    static unsigned long lastAttempt = 0;
    if (millis() - lastAttempt > 10000) { reconnect(); lastAttempt = millis(); }
  }

  if (needsFullRedraw || (millis() - lastFullRedraw > 4000)) {
    if (pageMode == 0) drawReadingsPage(temperature, humidity, co2_ppm, statusText, color);
    else drawGraphPage(co2_ppm);
    lastFullRedraw = millis();
    needsFullRedraw = false;
  } else {
    if (pageMode == 0) updateReadingsDynamic(temperature, co2_ppm, statusText, color);
    else updateGraphDynamic(co2_ppm);
  }

  static unsigned long lastPublish = 0;
  if (enableMQTT && millis() - lastPublish > 30000) {
    publishToHA(temperature, humidity, co2_ppm);
    lastPublish = millis();
  }

  delay(800);
}
