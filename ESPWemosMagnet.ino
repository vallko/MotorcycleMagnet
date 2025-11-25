/*
===============================================================================
📘 Project Documentation – ESP8266 D1 Mini Magnet Controller
===============================================================================

Author: <Valeri Dimitrov>
Board:  Wemos D1 Mini (ESP8266)
Date:   2025

-------------------------------------------------------------------------------
🔹 Overview
-------------------------------------------------------------------------------
This project controls a magnet (via a MOSFET) using an ESP8266 D1 Mini.  
It provides three operating modes:

1. Automatic Mode:
   - Magnet turns ON when voltage ≥ 13.5V.
   - Magnet turns OFF when voltage < 13.5V.

2. Manual Mode:
   - Activated via web interface.
   - User can force ON/OFF regardless of voltage.

3. Hardware Override:
   - Override pin (D5 → GND) forces magnet OFF regardless of mode.

The ESP8266 creates a WiFi Access Point and serves a responsive
webpage that updates live using AJAX.

-------------------------------------------------------------------------------
🔌 Hardware Pin Mapping
-------------------------------------------------------------------------------

        ┌───────────────────────────────┐
        │        Wemos D1 Mini          │
        │                               │
        │ [ ] RST                 TX [ ]│
        │ [ ] A0                  RX [ ]│
        │ [ ] D0                 D1 [●] │ → LED indicator (GPIO5, active LOW)
        │ [ ] D5                 D2 [●] │ → MOSFET control (GPIO4)
        │ [ ] D6                 D3 [ ] │
        │ [ ] D7                 D4 [ ] │ → Onboard LED (optional)
        │ [ ] D8                 G  [ ] │
        │ [ ] G                   5V[ ] │
        └───────────────────────────────┘

Pin assignments:
- D1 (GPIO5)  → LED indicator (active LOW)
- D2 (GPIO4)  → MOSFET gate → Magnet coil
- D5 (GPIO14)SCK → Override input (connect to GND to force OFF)
- A0          → Voltage divider input (max 1.0V to pin)

-------------------------------------------------------------------------------
🔋 Voltage Sensing
-------------------------------------------------------------------------------
- A0 can only handle up to 1.0V.
- Use a voltage divider to scale your system voltage.
  Example for measuring up to 15V:
    R1 = 360kΩ, R2 = 100kΩ → Vout ≈ Vin / 15
- Adjust scaling factor in readVoltage() accordingly.

-------------------------------------------------------------------------------
🌐 WiFi Access Point
-------------------------------------------------------------------------------
- SSID:     ESP_AP
- Password: 12345678
- IP:       192.168.4.1

Steps:
1. Connect your PC/Phone to ESP_AP WiFi.
2. Open browser at http://192.168.4.1

-------------------------------------------------------------------------------
💻 Web Interface
-------------------------------------------------------------------------------
- Live status updates (AJAX, every 1s):
   • Magnet state (ON/OFF)
   • Voltage
   • Override state
   • Manual mode state
- Controls:
   • Toggle Manual/Auto mode
   • Turn Magnet ON/OFF (enabled only in manual mode)
- Background color:
   • Green = Magnet ON
   • Red   = Magnet OFF

-------------------------------------------------------------------------------
📡 REST Endpoints
-------------------------------------------------------------------------------
- GET /               → Main HTML page
- GET /status         → JSON system state
- GET /manual/toggle  → Toggle manual/auto mode
- GET /led/on         → Force magnet ON (manual mode only)
- GET /led/off        → Force magnet OFF (manual mode only)

-------------------------------------------------------------------------------
📊 Example JSON (/status)
-------------------------------------------------------------------------------
{
  "magnet": "ON",
  "voltage": 13.72,
  "override": "false",
  "manual": true,
  "led": "ON"
}

-------------------------------------------------------------------------------
⚡ Safety Notes
-------------------------------------------------------------------------------
- Use a proper MOSFET (logic-level, rated for coil current).
- Always include a flyback diode across the magnet coil.
- Ensure voltage divider resistors keep A0 ≤ 1.0V.
- Consider adding hysteresis around the 13.5V threshold
  to avoid rapid toggling.

-------------------------------------------------------------------------------
🚀 Future Ideas
-------------------------------------------------------------------------------
- Add STA mode (connect to home WiFi instead of AP).
- Store mode in EEPROM to persist across resets.
- Add graphing of voltage over time (Chart.js).
- Add OTA updates for remote firmware upgrades.

===============================================================================
*/


#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>

const char* ssid = "ArchangelsMCC";
const char* password = "12345687";

ESP8266WebServer server(80);

const int ledBuiltin = D4;   // onboard LED, active LOW
const int ledExternal = D1;  // external LED, active LOW
const int mosfetPin = D2;
const int overridePin = D5;

bool manualMode = false;
bool ledState = false;
// Voltage threshold
const float voltageThreshold = 13.5;

// Simulated voltage reader
float readVoltage() {
  int raw = analogRead(A0);
  float voltage = (raw / 1023.0) * 18.0;  // adjust to your divider; this measures 0.2V higher at 14V -> 14.2
  if (voltage < 1.0) voltage = 0.0;       // fix for <1V
  return voltage;
}

void updateState() {
  float voltage = readVoltage();
  bool overrideActive = (digitalRead(overridePin) == LOW);

  if (overrideActive) {
    // Override pin forces OFF
    ledState = false;
    digitalWrite(ledBuiltin, ledState ? LOW : HIGH);
    digitalWrite(ledExternal, ledState ? LOW : HIGH);
    digitalWrite(mosfetPin, LOW);  // MOSFET off
  } else if (manualMode) {
    // Manual mode → ledState set by buttons
    if (ledState) {
      digitalWrite(ledBuiltin, LOW);
      digitalWrite(ledExternal, LOW);
      digitalWrite(mosfetPin, HIGH);
    } else {
      digitalWrite(ledBuiltin, HIGH);
      digitalWrite(ledExternal, HIGH);
      digitalWrite(mosfetPin, LOW);
    }
  } else {
    // Automatic mode → voltage threshold
    if (voltage >= voltageThreshold) {
      ledState = true;
      digitalWrite(ledBuiltin, LOW);
      digitalWrite(ledExternal, LOW);
      digitalWrite(mosfetPin, HIGH);
    } else {
      ledState = false;
      digitalWrite(ledBuiltin, HIGH);
      digitalWrite(ledExternal, HIGH);
      digitalWrite(mosfetPin, LOW);
    }
  }
}

// ---------------- HTML PAGE ----------------
String htmlPage() {
  String page = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  page += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  page += "<title>D1 Mini Status</title>";
  page += "<style>";
  page += "body { font-family: Arial; text-align: center; background: #f0a5a5; color: black; }";
  page += "h1 { font-size: 2em; }";
  page += "button { padding: 15px 30px; font-size: 1.2em; margin: 10px; border-radius: 10px; border: none; cursor:pointer; }";
  page += ".on { background: #4CAF50; color: white; }";
  page += ".off { background: #f44336; color: white; }";
  page += ".toggle { background: #2196F3; color: white; }";
  page += ".disabled { background: #aaa; color: #666; cursor: not-allowed !important; }";
  page += "</style></head><body>";

  page += "<h1 id='status'>Magnet: OFF</h1>";
  page += "<p>Voltage: <span id='voltage'>0.00</span> V</p>";
  page += "<p>Override Pin: <span id='override'>INACTIVE</span></p>";
  page += "<p>Manual Mode: <span id='manualMode'>OFF</span></p>";

  page += "<button class='toggle' id='manualBtn'>Toggle Manual Mode</button>";
  page += "<button class='on disabled' id='btnOn' disabled>Magnet ON</button>";
  page += "<button class='off disabled' id='btnOff' disabled>Magnet OFF</button>";

  // --- JavaScript ---
  page += R"rawliteral(
<script>
function updateStatus(){
  fetch('/status')
    .then(resp => resp.json())
    .then(data => {
      document.getElementById('status').innerText = "Magnet: " + data.magnet;
      document.getElementById('voltage').innerText = data.voltage.toFixed(2);
      document.getElementById('override').innerText = data.override === "true" ? "ACTIVE (Forcing OFF)" : "INACTIVE";
      document.getElementById('manualMode').innerText = data.manual ? "ON" : "OFF";

      let btnOn = document.getElementById('btnOn');
      let btnOff = document.getElementById('btnOff');
      if(data.manual){
        btnOn.disabled = false;
        btnOff.disabled = false;
        btnOn.classList.remove('disabled');
        btnOff.classList.remove('disabled');
      } else {
        btnOn.disabled = true;
        btnOff.disabled = true;
        btnOn.classList.add('disabled');
        btnOff.classList.add('disabled');
      }

      document.body.style.backgroundColor = (data.led==="ON" && data.override==="false") ? "#a8f0a5" : "#f0a5a5";
    })
    .catch(err => console.error("Status fetch failed:", err));
}

document.addEventListener("DOMContentLoaded", function(){
  document.getElementById('manualBtn').addEventListener('click', () => { fetch('/manual/toggle').then(updateStatus); });
  document.getElementById('btnOn').addEventListener('click', () => { fetch('/led/on').then(updateStatus); });
  document.getElementById('btnOff').addEventListener('click', () => { fetch('/led/off').then(updateStatus); });

  updateStatus();
  setInterval(updateStatus, 1000);
});
</script>
)rawliteral";

  page += "</body></html>";
  return page;
}

// ---------------- STATUS ENDPOINT ----------------
void handleStatus() {
  StaticJsonDocument<200> doc;
  doc["magnet"] = ledState ? "ON" : "OFF";
  doc["voltage"] = readVoltage();
  doc["override"] = (digitalRead(overridePin) == LOW) ? "true" : "false";
  doc["manual"] = manualMode;
  doc["led"] = ledState ? "ON" : "OFF";
  String output;
  serializeJson(doc, output);
  server.send(200, "application/json", output);
}

// ---------------- BUTTON HANDLERS ----------------
void handleManualToggle() {
  manualMode = !manualMode;
  server.send(200, "text/plain", "OK");
}

void handleLEDOn() {
  if (manualMode) {
    ledState = true;
    digitalWrite(ledBuiltin, LOW);
    digitalWrite(ledExternal, LOW);
    digitalWrite(mosfetPin, HIGH);
  }
  server.send(200, "text/plain", "OK");
}

void handleLEDOff() {
  if (manualMode) {
    ledState = false;
    digitalWrite(ledBuiltin, HIGH);
    digitalWrite(ledExternal, HIGH);
    digitalWrite(mosfetPin, LOW);
  }
  server.send(200, "text/plain", "OK");
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  pinMode(ledBuiltin, OUTPUT);
  pinMode(ledExternal, OUTPUT);

  pinMode(mosfetPin, OUTPUT);
  pinMode(overridePin, INPUT_PULLUP);

  digitalWrite(ledBuiltin, HIGH);
  digitalWrite(ledExternal, HIGH);
  digitalWrite(ledExternal, HIGH);

  digitalWrite(mosfetPin, LOW);

  Serial.println("Starting AP...");
  WiFi.softAP(ssid, password);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", []() {
    server.send(200, "text/html", htmlPage());
  });
  server.on("/status", handleStatus);
  server.on("/manual/toggle", handleManualToggle);
  server.on("/led/on", handleLEDOn);
  server.on("/led/off", handleLEDOff);

  server.begin();
  Serial.println("Webserver started.");
}

void loop() {
  server.handleClient();

  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 200) {  // every 200ms
    updateState();
    lastCheck = millis();
  }

  yield();  // keep WiFi alive
}
