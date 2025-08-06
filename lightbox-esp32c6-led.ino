/*
 * ESP32-C6 LED Strip Controller with Web Configuration
 * Complete LED control system with web-based configuration interface
 * Combines all LED effects with real-time web configuration management
 *
 * Hardware:
 * - ESP32-C6 board
 * - WS2811/WS2812 LED strip
 * - Level shifter (3.3V to 5V) - IMPORTANT!
 * - External 5V power supply for LEDs
 *
 * Connections:
 * ESP32-C6 GPIO -> Level Shifter Input
 * Level Shifter Output -> LED Strip Data Pin
 * 5V Power -> LED Strip VCC
 * GND -> Common ground (ESP32, Level Shifter, Power Supply, LED Strip)
 *
 * Features:
 * - WiFi hotspot for web configuration (ESP32-LED-Config)
 * - Real-time LED effects and animations
 * - YAML configuration storage in SPIFFS
 * - Multiple LED libraries support (NeoPixel + FastLED)
 * - Matrix layout support for 2D effects
 * - Serial debugging at 115200 baud
 */

#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <Adafruit_NeoPixel.h>
#include <FastLED.h>
#include <ESPmDNS.h>
#include "led_config.h"

// Try using NeoPixel library exclusively for better ESP32-C6 timing compatibility
// FastLED on ESP32-C6 can have timing issues - NeoPixel library is more stable
// Note: This will disable FastLED-specific effects but should fix the first LED issue

// Web server and configuration
WebServer server(80);
ConfigParser configParser;
LEDConfig currentConfig;
WiFiConfig currentWiFiConfig;

// LED strip objects (dual library support)
Adafruit_NeoPixel* neoPixelStrip = nullptr;
CRGB* fastLedStrip = nullptr;
bool useFastLED = false;

// WiFi mode variables
bool clientModeActive = false;
IPAddress currentIP;

// Onboard RGB LED configuration (ESP32-C6 specific)
const int ONBOARD_LED_PIN = 8; // GPIO8 for ESP32-C6 RGB LED
Adafruit_NeoPixel* onboardLED = nullptr;
bool hasRGBLED = true; // ESP32-C6 has RGB LED on GPIO8

// Status colors for RGB LED
const uint32_t COLOR_STARTING = 0xFF4500;    // Orange - System starting
const uint32_t COLOR_WIFI_SETUP = 0x0000FF;  // Blue - WiFi setting up
const uint32_t COLOR_READY = 0x00FF00;       // Green - System ready
const uint32_t COLOR_ERROR = 0xFF0000;       // Red - Error state
const uint32_t COLOR_EFFECTS = 0x200020;     // Dim purple - Running effects
const uint32_t COLOR_WEB_REQUEST = 0xFFFF00; // Yellow - Processing web request

// Animation control
unsigned long lastEffectUpdate = 0;
int currentEffect = 0;
int effectStep = 0;
bool effectsEnabled = true;
unsigned long effectDuration = 10000; // 10 seconds per effect

// Status LED control
unsigned long lastStatusUpdate = 0;
bool statusBlink = false;

// IP address indication
bool ipIndicationActive = false;
unsigned long ipIndicationStartTime = 0;

void setStatusLED(uint32_t color) {
  if (onboardLED) {
    onboardLED->setPixelColor(0, color);
    onboardLED->show();
  }
}

void blinkStatusLED(uint32_t color, int interval = 500) {
  if (millis() - lastStatusUpdate > interval) {
    statusBlink = !statusBlink;
    if (onboardLED) {
      onboardLED->setPixelColor(0, statusBlink ? color : 0x000000);
      onboardLED->show();
    }
    lastStatusUpdate = millis();
  }
}

// Parse last octet from IP address (e.g., 192.168.1.125 -> 125)
int getLastOctet(IPAddress ip) {
  return ip[3];
}

// Flash the status LED to indicate IP address last octet
// Uses flashes for digits 1-9, solid hold for 0
void flashIPAddressOctet(int octet) {
  if (!onboardLED) return;

  ipIndicationActive = true;
  ipIndicationStartTime = millis();

  Serial.printf("🔢 Flashing IP last octet: %d\n", octet);

  // Convert three-digit number to individual digits
  int hundreds = octet / 100;
  int tens = (octet / 10) % 10;
  int ones = octet % 10;

  // Flash each digit with pauses between
  if (hundreds > 0) {
    flashDigit(hundreds);
    delay(800); // Pause between digits
  }

  if (tens > 0 || hundreds > 0) { // Show tens digit if there are hundreds, or if tens > 0
    flashDigit(tens);
    delay(800); // Pause between digits
  }

  flashDigit(ones);
  delay(1000); // Final pause

  ipIndicationActive = false;
  Serial.println("✅ IP address indication completed");
}

// Flash a single digit (1-9 flashes, 0 = solid for 1 second)
void flashDigit(int digit) {
  if (!onboardLED) return;

  if (digit == 0) {
    // Solid color for zero
    setStatusLED(COLOR_WEB_REQUEST); // Yellow for visibility
    delay(1000);
    setStatusLED(0x000000); // Off
  } else {
    // Flash digit times for 1-9
    for (int i = 0; i < digit; i++) {
      setStatusLED(COLOR_WEB_REQUEST); // Yellow flash
      delay(300);
      setStatusLED(0x000000); // Off
      delay(200);
    }
  }
}

// Effect list
enum Effects {
  EFFECT_MOVING_PIXEL,
  EFFECT_COLOR_FILL,
  EFFECT_RAINBOW,
  EFFECT_ROW_PATTERNS,
  EFFECT_FIRE,
  EFFECT_BREATHING,
  EFFECT_COUNT
};

const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32 LED Controller</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }
        .container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { color: #333; text-align: center; }
        .control-section { margin: 20px 0; padding: 15px; border: 1px solid #ddd; border-radius: 5px; }
        .form-group { margin: 15px 0; }
        label { display: block; margin-bottom: 5px; font-weight: bold; color: #555; }
        input, select, button { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }
        button { background-color: #4CAF50; color: white; cursor: pointer; margin: 5px 0; }
        button:hover { background-color: #45a049; }
        .status { padding: 10px; margin: 10px 0; border-radius: 4px; }
        .success { background-color: #d4edda; border: 1px solid #c3e6cb; color: #155724; }
        .info { background-color: #e2e3e5; border: 1px solid #d6d8db; color: #383d41; }
        .effect-controls button { width: auto; margin: 5px; padding: 10px 15px; }
        .brightness-control { display: flex; align-items: center; gap: 10px; }
        .brightness-control input[type="range"] { flex: 1; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔥 ESP32-C6 LED Controller</h1>

        <div class="status info">
            <strong>Current Configuration:</strong><br>
            Matrix: %d rows × %d pixels = %d total LEDs<br>
            Pin: GPIO%d | Brightness: %d | Type: %s | Order: %s<br>
            Effects: %s | Library: %s
        </div>

        <div class="control-section">
            <h2>🎮 LED Effects Control</h2>
            <div class="effect-controls">
                <button onclick="toggleEffects()">%s Effects</button>
                <button onclick="nextEffect()">Next Effect</button>
                <button onclick="setColor('red')">Red</button>
                <button onclick="setColor('green')">Green</button>
                <button onclick="setColor('blue')">Blue</button>
                <button onclick="setColor('white')">White</button>
                <button onclick="clearLeds()">Clear All</button>
            </div>

            <div class="brightness-control">
                <label>Brightness:</label>
                <input type="range" id="brightness" min="0" max="255" value="%d" onchange="setBrightness(this.value)">
                <span id="brightness_val">%d</span>
            </div>
        </div>

        <div class="control-section">
            <h2>⚙️ Configuration</h2>
            <form action="/save" method="POST">
                <div class="form-group">
                    <label for="pixels_per_row">Pixels per Row:</label>
                    <input type="number" id="pixels_per_row" name="pixels_per_row" value="%d" min="1" max="500" required>
                </div>

                <div class="form-group">
                    <label for="number_of_rows">Number of Rows:</label>
                    <input type="number" id="number_of_rows" name="number_of_rows" value="%d" min="1" max="100" required>
                </div>

                <div class="form-group">
                    <label for="led_pin">LED Pin (GPIO):</label>
                    <select id="led_pin" name="led_pin" required>
                        <option value="4" %s>GPIO 4</option>
                        <option value="5" %s>GPIO 5 (Recommended)</option>
                        <option value="6" %s>GPIO 6</option>
                        <option value="7" %s>GPIO 7</option>
                        <option value="8" %s>GPIO 8</option>
                        <option value="9" %s>GPIO 9</option>
                        <option value="10" %s>GPIO 10</option>
                        <option value="11" %s>GPIO 11</option>
                    </select>
                </div>

                <div class="form-group">
                    <label for="led_type">LED Type:</label>
                    <select id="led_type" name="led_type" required>
                        <option value="WS2811" %s>WS2811</option>
                        <option value="WS2812" %s>WS2812</option>
                        <option value="WS2812B" %s>WS2812B</option>
                        <option value="SK6812" %s>SK6812</option>
                    </select>
                </div>

                <div class="form-group">
                    <label for="color_order">Color Order:</label>
                    <select id="color_order" name="color_order" required>
                        <option value="RGB" %s>RGB</option>
                        <option value="GRB" %s>GRB (WS2811 Standard)</option>
                        <option value="BRG" %s>BRG</option>
                        <option value="BGR" %s>BGR</option>
                        <option value="RBG" %s>RBG</option>
                        <option value="GBR" %s>GBR</option>
                    </select>
                </div>

                <input type="submit" value="💾 Save & Restart">
            </form>
        </div>

        <div class="control-section">
            <h2>📡 WiFi Configuration</h2>
            <form action="/save_wifi" method="POST">
                <div class="form-group">
                    <label>
                        <input type="checkbox" id="enable_client_mode" name="enable_client_mode" %s onchange="toggleWiFiFields()">
                        Enable WiFi Client Mode (Connect to existing network)
                    </label>
                </div>

                <div id="wifi_fields" style="%s">
                    <div class="form-group">
                        <label for="ssid">WiFi Network (SSID):</label>
                        <input type="text" id="ssid" name="ssid" value="%s" maxlength="32">
                    </div>

                    <div class="form-group">
                        <label for="password">WiFi Password:</label>
                        <input type="password" id="password" name="password" value="%s" maxlength="64">
                        <small style="color: #666;">Leave blank to keep current password</small>
                    </div>

                    <div class="form-group">
                        <label for="hostname">Hostname (.local domain):</label>
                        <input type="text" id="hostname" name="hostname" value="%s" pattern="[a-zA-Z0-9-]+" maxlength="32">
                        <small style="color: #666;">Used for http://hostname.local access</small>
                    </div>
                </div>

                <input type="submit" value="💾 Save WiFi Settings & Restart">
            </form>
        </div>

        <div class="control-section">
            <h2>📊 Network Status</h2>
            <p><strong>Mode:</strong> %s</p>
            <p><strong>Network:</strong> %s</p>
            <p><strong>IP Address:</strong> %s</p>
            <p><strong>Connected Clients:</strong> <span id="clients">%d</span></p>
        </div>
    </div>

    <script>
        function toggleEffects() {
            fetch('/toggle_effects').then(() => location.reload());
        }

        function nextEffect() {
            fetch('/next_effect');
        }

        function setColor(color) {
            fetch('/set_color?color=' + color);
        }

        function clearLeds() {
            fetch('/clear');
        }

        function setBrightness(value) {
            document.getElementById('brightness_val').textContent = value;
            fetch('/set_brightness?value=' + value);
        }

        function toggleWiFiFields() {
            const checkbox = document.getElementById('enable_client_mode');
            const fields = document.getElementById('wifi_fields');
            fields.style.display = checkbox.checked ? 'block' : 'none';
        }

        // Initialize WiFi fields visibility on page load
        window.onload = function() {
            toggleWiFiFields();
        };

        // Auto-refresh client count every 30 seconds
        setInterval(() => {
            fetch('/status').then(r => r.json()).then(data => {
                document.getElementById('clients').textContent = data.clients;
            });
        }, 30000);
    </script>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n🔥 ESP32-C6 LED Controller Starting...");
  Serial.println("==========================================");

  // Initialize onboard RGB LED on GPIO8 (ESP32-C6 specific)
  onboardLED = new Adafruit_NeoPixel(1, ONBOARD_LED_PIN, NEO_GRB + NEO_KHZ800);
  onboardLED->begin();
  onboardLED->clear();
  setStatusLED(COLOR_STARTING); // Orange - System starting
  Serial.printf("✅ Onboard RGB LED (GPIO%d) initialized\n", ONBOARD_LED_PIN);

  // Initialize SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("❌ SPIFFS initialization failed");
    setStatusLED(COLOR_ERROR); // Red - Error state
    return;
  }

  // Load configuration
  configParser.loadConfig();
  currentConfig = configParser.getConfig();
  currentWiFiConfig = configParser.getWiFiConfig();

  // Initialize LED strips
  initializeLEDs();

  // Setup WiFi (client mode or hotspot)
  setStatusLED(COLOR_WIFI_SETUP); // Blue - WiFi setting up
  setupWiFi();

  // Setup web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/save_wifi", HTTP_POST, handleSaveWiFi);
  server.on("/toggle_effects", HTTP_GET, handleToggleEffects);
  server.on("/next_effect", HTTP_GET, handleNextEffect);
  server.on("/set_color", HTTP_GET, handleSetColor);
  server.on("/clear", HTTP_GET, handleClear);
  server.on("/set_brightness", HTTP_GET, handleSetBrightness);
  server.on("/status", HTTP_GET, handleStatus);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("🔧 Web server started on port 80");
  Serial.println("🚀 Connect to hotspot and open browser to control LEDs");

  // Initial LED test
  testLEDs();

  // System ready - show green status
  setStatusLED(COLOR_READY);
  Serial.println("✅ System ready - Status LED: Green");
}

void loop() {
  // Handle web server requests with timeout protection
  unsigned long webRequestStart = millis();
  server.handleClient();

  // If web request took too long, reset status
  if (millis() - webRequestStart > 5000) {
    Serial.println("⚠️ Web request timeout, resetting status");
    setStatusLED(COLOR_READY);
  }

  // Run LED effects if enabled
  if (effectsEnabled) {
    runCurrentEffect();
    // Only update status LED if not showing IP indication
    if (!ipIndicationActive) {
      setStatusLED(COLOR_EFFECTS); // Dim solid purple when effects running
    }
  } else {
    // Only update status LED if not showing IP indication
    if (!ipIndicationActive) {
      setStatusLED(COLOR_READY); // Solid green when idle
    }
  }

  delay(10);
}

void setupWiFi() {
  Serial.println("🔧 Setting up WiFi...");

  if (currentWiFiConfig.enableClientMode && currentWiFiConfig.ssid.length() > 0) {
    // Try to connect to WiFi network
    Serial.printf("📡 Attempting to connect to WiFi: %s\n", currentWiFiConfig.ssid.c_str());

    WiFi.mode(WIFI_STA);
    WiFi.begin(currentWiFiConfig.ssid.c_str(), currentWiFiConfig.password.c_str());

    int attempts = 0;
    unsigned long startTime = millis();

    while (WiFi.status() != WL_CONNECTED && attempts < currentWiFiConfig.maxRetryAttempts) {
      if (millis() - startTime > currentWiFiConfig.connectionTimeout) {
        attempts++;
        startTime = millis();
        Serial.printf("⏱️  Connection timeout, attempt %d/%d\n", attempts, currentWiFiConfig.maxRetryAttempts);

        if (attempts < currentWiFiConfig.maxRetryAttempts) {
          WiFi.disconnect();
          delay(1000);
          WiFi.begin(currentWiFiConfig.ssid.c_str(), currentWiFiConfig.password.c_str());
        }
      }

      blinkStatusLED(COLOR_WIFI_SETUP); // Blue blink during connection
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
      clientModeActive = true;
      currentIP = WiFi.localIP();
      Serial.printf("✅ WiFi Connected!\n");
      Serial.printf("📶 Network: %s\n", currentWiFiConfig.ssid.c_str());
      Serial.printf("🌐 IP Address: %s\n", currentIP.toString().c_str());
      Serial.printf("🔧 Web Interface: http://%s\n", currentIP.toString().c_str());

      // Setup mDNS for .local domain access
      if (MDNS.begin(currentWiFiConfig.hostname.c_str())) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("✅ mDNS responder started: http://%s.local\n", currentWiFiConfig.hostname.c_str());
        Serial.printf("🔧 Web Interface: http://%s.local\n", currentWiFiConfig.hostname.c_str());
      } else {
        Serial.println("❌ Error setting up mDNS responder");
      }

      // Flash IP address last octet on status LED
      int lastOctet = getLastOctet(currentIP);
      flashIPAddressOctet(lastOctet);

      return;
    } else {
      Serial.printf("❌ Failed to connect to WiFi after %d attempts\n", currentWiFiConfig.maxRetryAttempts);
      Serial.println("📡 Falling back to Access Point mode...");
    }
  }

  // Start Access Point mode (fallback or default)
  clientModeActive = false;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(currentWiFiConfig.apSsid.c_str(), currentWiFiConfig.apPassword.c_str());

  currentIP = WiFi.softAPIP();
  Serial.printf("✅ WiFi Hotspot Started\n");
  Serial.printf("📶 Network: %s\n", currentWiFiConfig.apSsid.c_str());
  Serial.printf("🔑 Password: %s\n", currentWiFiConfig.apPassword.c_str());
  Serial.printf("🌐 Web Interface: http://%s\n", currentIP.toString().c_str());
}

void initializeLEDs() {
  Serial.printf("🔧 Initializing LED strip: %d LEDs on GPIO%d\n",
                currentConfig.totalPixels, currentConfig.ledPin);

  // Try to determine color order for NeoPixel
  uint16_t neoColorOrder = NEO_GRB + NEO_KHZ800; // Default
  if (currentConfig.colorOrder == "RGB") neoColorOrder = NEO_RGB + NEO_KHZ800;
  else if (currentConfig.colorOrder == "BRG") neoColorOrder = NEO_BRG + NEO_KHZ800;
  else if (currentConfig.colorOrder == "BGR") neoColorOrder = NEO_BGR + NEO_KHZ800;
  else if (currentConfig.colorOrder == "RBG") neoColorOrder = NEO_RBG + NEO_KHZ800;
  else if (currentConfig.colorOrder == "GBR") neoColorOrder = NEO_GBR + NEO_KHZ800;

  // Initialize NeoPixel strip
  if (neoPixelStrip) delete neoPixelStrip;
  neoPixelStrip = new Adafruit_NeoPixel(currentConfig.totalPixels, currentConfig.ledPin, neoColorOrder);
  neoPixelStrip->begin();
  neoPixelStrip->clear();
  neoPixelStrip->setBrightness(currentConfig.brightness);
  neoPixelStrip->show();

  // Initialize FastLED strip for advanced effects
  if (fastLedStrip) delete[] fastLedStrip;
  fastLedStrip = new CRGB[currentConfig.totalPixels];

  // Add FastLED configuration with ESP32-C6 optimized timing
  FastLED.clear();

  // Initialize FastLED with original settings (revert timing changes that caused issues)
  // Note: FastLED requires compile-time pin specification
  switch (currentConfig.ledPin) {
    case 4:
      if (currentConfig.ledType == "WS2812B") {
        FastLED.addLeds<WS2812B, 4, GRB>(fastLedStrip, currentConfig.totalPixels).setCorrection(TypicalLEDStrip);
      } else {
        FastLED.addLeds<WS2811, 4, GRB>(fastLedStrip, currentConfig.totalPixels).setCorrection(TypicalLEDStrip);
      }
      break;
    case 6:
      if (currentConfig.ledType == "WS2812B") {
        FastLED.addLeds<WS2812B, 6, GRB>(fastLedStrip, currentConfig.totalPixels).setCorrection(TypicalLEDStrip);
      } else {
        FastLED.addLeds<WS2811, 6, GRB>(fastLedStrip, currentConfig.totalPixels).setCorrection(TypicalLEDStrip);
      }
      break;
    case 7:
      if (currentConfig.ledType == "WS2812B") {
        FastLED.addLeds<WS2812B, 7, GRB>(fastLedStrip, currentConfig.totalPixels).setCorrection(TypicalLEDStrip);
      } else {
        FastLED.addLeds<WS2811, 7, GRB>(fastLedStrip, currentConfig.totalPixels).setCorrection(TypicalLEDStrip);
      }
      break;
    default: // GPIO5 and other pins (recommended default)
      if (currentConfig.ledType == "WS2812B") {
        FastLED.addLeds<WS2812B, 5, GRB>(fastLedStrip, currentConfig.totalPixels).setCorrection(TypicalLEDStrip);
      } else {
        FastLED.addLeds<WS2811, 5, GRB>(fastLedStrip, currentConfig.totalPixels).setCorrection(TypicalLEDStrip);
      }
      break;
  }
  FastLED.setBrightness(currentConfig.brightness);
  FastLED.clear();
  FastLED.show();

  Serial.println("✅ LED strips initialized (NeoPixel primary, FastLED secondary)");
}

void testLEDs() {
  Serial.println("🧪 Running LED connectivity test...");

  // Test first 3 LEDs with different colors
  neoPixelStrip->clear();
  if (currentConfig.totalPixels > 0) neoPixelStrip->setPixelColor(0, 255, 0, 0); // Red
  if (currentConfig.totalPixels > 1) neoPixelStrip->setPixelColor(1, 0, 255, 0); // Green
  if (currentConfig.totalPixels > 2) neoPixelStrip->setPixelColor(2, 0, 0, 255); // Blue
  neoPixelStrip->show();

  delay(1000);
  neoPixelStrip->clear();
  neoPixelStrip->show();

  Serial.println("✅ LED test completed");
}

void runCurrentEffect() {
  unsigned long currentTime = millis();

  // Switch effects every effectDuration milliseconds
  if (currentTime - lastEffectUpdate > effectDuration) {
    currentEffect = (currentEffect + 1) % EFFECT_COUNT;
    effectStep = 0;
    lastEffectUpdate = currentTime;
    Serial.printf("🎨 Switching to effect %d\n", currentEffect);
  }

  // Run current effect
  switch (currentEffect) {
    case EFFECT_MOVING_PIXEL:
      effectMovingPixel();
      break;
    case EFFECT_COLOR_FILL:
      effectColorFill();
      break;
    case EFFECT_RAINBOW:
      effectRainbow();
      break;
    case EFFECT_ROW_PATTERNS:
      effectRowPatterns();
      break;
    case EFFECT_FIRE:
      effectFire();
      break;
    case EFFECT_BREATHING:
      effectBreathing();
      break;
  }
}

void effectMovingPixel() {
  static unsigned long lastUpdate = 0;
  static uint32_t colors[] = {
    neoPixelStrip->Color(255, 0, 0),   // Red
    neoPixelStrip->Color(0, 255, 0),   // Green
    neoPixelStrip->Color(0, 0, 255),   // Blue
    neoPixelStrip->Color(255, 255, 0)  // Yellow
  };
  static int colorIndex = 0;

  if (millis() - lastUpdate > 100) {
    neoPixelStrip->clear();

    int pos = effectStep % currentConfig.totalPixels;
    neoPixelStrip->setPixelColor(pos, colors[colorIndex]);
    neoPixelStrip->show();

    effectStep++;
    if (effectStep >= currentConfig.totalPixels) {
      effectStep = 0;
      colorIndex = (colorIndex + 1) % 4;
    }

    lastUpdate = millis();
  }
}

void effectColorFill() {
  static unsigned long lastUpdate = 0;
  static uint32_t colors[] = {
    neoPixelStrip->Color(255, 0, 0),     // Red
    neoPixelStrip->Color(0, 255, 0),     // Green
    neoPixelStrip->Color(0, 0, 255),     // Blue
    neoPixelStrip->Color(255, 255, 255)  // White
  };
  static int colorIndex = 0;

  if (millis() - lastUpdate > 50) {
    if (effectStep < currentConfig.totalPixels) {
      neoPixelStrip->setPixelColor(effectStep, colors[colorIndex]);
      neoPixelStrip->show();
      effectStep++;
    } else {
      // Color fill complete, switch to next color
      effectStep = 0;
      colorIndex = (colorIndex + 1) % 4;
      neoPixelStrip->clear();
    }

    lastUpdate = millis();
  }
}

void effectRainbow() {
  static unsigned long lastUpdate = 0;

  if (millis() - lastUpdate > 20) {
    for (int i = 0; i < currentConfig.totalPixels; i++) {
      int pixelHue = (effectStep * 256 / currentConfig.totalPixels) + (i * 256 / currentConfig.totalPixels);
      neoPixelStrip->setPixelColor(i, neoPixelStrip->gamma32(neoPixelStrip->ColorHSV(pixelHue * 256)));
    }
    neoPixelStrip->show();

    effectStep = (effectStep + 1) % 256;
    lastUpdate = millis();
  }
}

void effectRowPatterns() {
  static unsigned long lastUpdate = 0;
  static int pattern = 0;

  if (millis() - lastUpdate > 1000) {
    neoPixelStrip->clear();

    switch (pattern) {
      case 0: // Alternating row colors
        for (int row = 0; row < currentConfig.numberOfRows; row++) {
          uint32_t color = (row % 2 == 0) ? neoPixelStrip->Color(255, 0, 0) : neoPixelStrip->Color(0, 0, 255);
          for (int pixel = 0; pixel < currentConfig.pixelsPerRow; pixel++) {
            int ledIndex = (row * currentConfig.pixelsPerRow) + pixel;
            neoPixelStrip->setPixelColor(ledIndex, color);
          }
        }
        break;

      case 1: // Progressive row fill
        for (int row = 0; row <= effectStep && row < currentConfig.numberOfRows; row++) {
          for (int pixel = 0; pixel < currentConfig.pixelsPerRow; pixel++) {
            int ledIndex = (row * currentConfig.pixelsPerRow) + pixel;
            neoPixelStrip->setPixelColor(ledIndex, neoPixelStrip->Color(0, 255, 0));
          }
        }
        effectStep++;
        if (effectStep >= currentConfig.numberOfRows) {
          effectStep = 0;
          pattern = (pattern + 1) % 2;
        }
        break;
    }

    neoPixelStrip->show();
    lastUpdate = millis();
  }
}

void effectFire() {
  static unsigned long lastUpdate = 0;

  if (millis() - lastUpdate > 50) {
    // Use NeoPixel instead of FastLED to avoid ESP32-C6 timing issues
    useFastLED = false;

    for (int i = 0; i < currentConfig.totalPixels; i++) {
      int heat = random(0, 255);
      if (heat > 160) {
        neoPixelStrip->setPixelColor(i, neoPixelStrip->Color(255, heat - 96, 0));  // Hot flame
      } else if (heat > 96) {
        neoPixelStrip->setPixelColor(i, neoPixelStrip->Color(heat * 2, heat, 0));  // Medium flame
      } else {
        neoPixelStrip->setPixelColor(i, neoPixelStrip->Color(heat, 0, 0));         // Cool flame
      }
    }

    neoPixelStrip->show();
    lastUpdate = millis();
  }
}

void effectBreathing() {
  static unsigned long lastUpdate = 0;

  if (millis() - lastUpdate > 30) {
    int brightness = (sin(effectStep * 0.1) + 1) * 127.5; // 0-255

    for (int i = 0; i < currentConfig.totalPixels; i++) {
      neoPixelStrip->setPixelColor(i, neoPixelStrip->Color(0, 0, brightness));
    }
    neoPixelStrip->show();

    effectStep++;
    lastUpdate = millis();
  }
}

// Web server handlers
void handleRoot() {
  setStatusLED(COLOR_WEB_REQUEST); // Yellow - Processing web request

  // Increase buffer size significantly to handle large HTML page
  static char* buffer = nullptr;
  const size_t bufferSize = 16384; // 16KB buffer

  if (!buffer) {
    buffer = (char*)malloc(bufferSize);
    if (!buffer) {
      Serial.println("❌ Failed to allocate memory for HTML buffer");
      server.send(500, "text/plain", "Server memory error");
      setStatusLED(COLOR_ERROR);
      return;
    }
  }

  int result = snprintf(buffer, bufferSize, htmlPage,
    currentConfig.numberOfRows, currentConfig.pixelsPerRow, currentConfig.totalPixels,
    currentConfig.ledPin, currentConfig.brightness, currentConfig.ledType.c_str(), currentConfig.colorOrder.c_str(),
    effectsEnabled ? "ON" : "OFF", useFastLED ? "FastLED" : "NeoPixel",
    effectsEnabled ? "Disable" : "Enable",
    currentConfig.brightness, currentConfig.brightness,
    currentConfig.pixelsPerRow, currentConfig.numberOfRows,
    (currentConfig.ledPin == 4) ? "selected" : "",
    (currentConfig.ledPin == 5) ? "selected" : "",
    (currentConfig.ledPin == 6) ? "selected" : "",
    (currentConfig.ledPin == 7) ? "selected" : "",
    (currentConfig.ledPin == 8) ? "selected" : "",
    (currentConfig.ledPin == 9) ? "selected" : "",
    (currentConfig.ledPin == 10) ? "selected" : "",
    (currentConfig.ledPin == 11) ? "selected" : "",
    (currentConfig.ledType == "WS2811") ? "selected" : "",
    (currentConfig.ledType == "WS2812") ? "selected" : "",
    (currentConfig.ledType == "WS2812B") ? "selected" : "",
    (currentConfig.ledType == "SK6812") ? "selected" : "",
    (currentConfig.colorOrder == "RGB") ? "selected" : "",
    (currentConfig.colorOrder == "GRB") ? "selected" : "",
    (currentConfig.colorOrder == "BRG") ? "selected" : "",
    (currentConfig.colorOrder == "BGR") ? "selected" : "",
    (currentConfig.colorOrder == "RBG") ? "selected" : "",
    (currentConfig.colorOrder == "GBR") ? "selected" : "",
    currentWiFiConfig.enableClientMode ? "checked" : "",
    currentWiFiConfig.enableClientMode ? "display:block" : "display:none",
    currentWiFiConfig.ssid.c_str(),
    "", // Never show the actual password in the form
    currentWiFiConfig.hostname.c_str(),
    clientModeActive ? "Client" : "Access Point",
    clientModeActive ? currentWiFiConfig.ssid.c_str() : currentWiFiConfig.apSsid.c_str(),
    currentIP.toString().c_str(),
    clientModeActive ? 0 : WiFi.softAPgetStationNum()
  );

  if (result >= bufferSize) {
    Serial.println("❌ HTML buffer overflow detected!");
    server.send(500, "text/plain", "HTML content too large");
    setStatusLED(COLOR_ERROR);
    return;
  }

  server.send(200, "text/html; charset=UTF-8", buffer);
  setStatusLED(COLOR_READY); // Immediately reset status LED
  Serial.printf("✅ Sent HTML page (%d bytes)\n", result);
}

void handleSave() {
  setStatusLED(COLOR_WEB_REQUEST); // Yellow - Processing web request
  Serial.println("💾 Saving new configuration...");

  LEDConfig newConfig;
  newConfig.pixelsPerRow = server.arg("pixels_per_row").toInt();
  newConfig.numberOfRows = server.arg("number_of_rows").toInt();
  newConfig.totalPixels = newConfig.pixelsPerRow * newConfig.numberOfRows;
  newConfig.ledPin = server.arg("led_pin").toInt();
  newConfig.brightness = currentConfig.brightness; // Keep current brightness
  newConfig.ledType = server.arg("led_type");
  newConfig.colorOrder = server.arg("color_order");
  newConfig.refreshRate = currentConfig.refreshRate;
  newConfig.maxCurrent = currentConfig.maxCurrent;

  if (saveConfigToFile(newConfig)) {
    currentConfig = newConfig;

    String successPage = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Configuration Saved</title>
    <meta charset="UTF-8">
    <meta http-equiv="refresh" content="3;url=/">
    <style>body{font-family:Arial;text-align:center;margin:50px;}</style>
</head>
<body>
    <h1>✅ Configuration Saved!</h1>
    <p>🔄 Restarting system in 3 seconds...</p>
</body>
</html>
    )";

    server.send(200, "text/html; charset=UTF-8", successPage);
    delay(1000);

    // Reinitialize LEDs with new config
    initializeLEDs();
    Serial.println("✅ Configuration updated and LEDs reinitialized");
    setStatusLED(COLOR_READY);
  } else {
    server.send(500, "text/plain", "Failed to save configuration");
    setStatusLED(COLOR_ERROR);
  }
}

void handleToggleEffects() {
  setStatusLED(COLOR_WEB_REQUEST);
  effectsEnabled = !effectsEnabled;
  if (!effectsEnabled) {
    neoPixelStrip->clear();
    neoPixelStrip->show();
    FastLED.clear();
    FastLED.show();
  }
  Serial.printf("🎨 Effects %s\n", effectsEnabled ? "ENABLED" : "DISABLED");
  server.send(200, "text/plain", effectsEnabled ? "Effects enabled" : "Effects disabled");
  setStatusLED(COLOR_READY);
}

void handleNextEffect() {
  setStatusLED(COLOR_WEB_REQUEST);
  currentEffect = (currentEffect + 1) % EFFECT_COUNT;
  effectStep = 0;
  lastEffectUpdate = millis();
  Serial.printf("🎨 Switched to effect %d\n", currentEffect);
  server.send(200, "text/plain", "Effect switched");
  setStatusLED(COLOR_READY);
}

void handleSetColor() {
  setStatusLED(COLOR_WEB_REQUEST);
  String color = server.arg("color");
  uint32_t rgb = 0;

  if (color == "red") rgb = neoPixelStrip->Color(255, 0, 0);
  else if (color == "green") rgb = neoPixelStrip->Color(0, 255, 0);
  else if (color == "blue") rgb = neoPixelStrip->Color(0, 0, 255);
  else if (color == "white") rgb = neoPixelStrip->Color(255, 255, 255);

  effectsEnabled = false; // Disable effects when setting manual color

  for (int i = 0; i < currentConfig.totalPixels; i++) {
    neoPixelStrip->setPixelColor(i, rgb);
  }
  neoPixelStrip->show();

  server.send(200, "text/plain", "Color set to " + color);
  setStatusLED(COLOR_READY);
}

void handleClear() {
  setStatusLED(COLOR_WEB_REQUEST);
  effectsEnabled = false;
  neoPixelStrip->clear();
  neoPixelStrip->show();
  FastLED.clear();
  FastLED.show();

  Serial.println("🔄 LEDs cleared");
  server.send(200, "text/plain", "LEDs cleared");
  setStatusLED(COLOR_READY);
}

void handleSetBrightness() {
  setStatusLED(COLOR_WEB_REQUEST);
  int brightness = server.arg("value").toInt();
  brightness = constrain(brightness, 0, 255);

  currentConfig.brightness = brightness;
  neoPixelStrip->setBrightness(brightness);
  FastLED.setBrightness(brightness);

  Serial.printf("🔆 Brightness set to %d\n", brightness);
  server.send(200, "text/plain", "Brightness updated");
  setStatusLED(COLOR_READY);
}

void handleStatus() {
  setStatusLED(COLOR_WEB_REQUEST);
  String json = "{\"clients\":" + String(WiFi.softAPgetStationNum()) +
                ",\"effects\":" + String(effectsEnabled ? "true" : "false") +
                ",\"brightness\":" + String(currentConfig.brightness) + "}";
  server.send(200, "application/json", json);
  setStatusLED(COLOR_READY);
}

void handleSaveWiFi() {
  setStatusLED(COLOR_WEB_REQUEST); // Yellow - Processing web request
  Serial.println("📡 Saving WiFi configuration...");

  WiFiConfig newWiFiConfig = currentWiFiConfig;
  newWiFiConfig.enableClientMode = server.hasArg("enable_client_mode");

  if (server.hasArg("ssid")) {
    newWiFiConfig.ssid = server.arg("ssid");
  }

  // Only update password if one was provided (not empty)
  if (server.hasArg("password") && server.arg("password").length() > 0) {
    newWiFiConfig.password = server.arg("password");
  }

  if (server.hasArg("hostname")) {
    newWiFiConfig.hostname = server.arg("hostname");
  }

  if (saveWiFiConfigToFile(newWiFiConfig)) {
    currentWiFiConfig = newWiFiConfig;

    String successPage = R"(
<!DOCTYPE html>
<html>
<head>
    <title>WiFi Configuration Saved</title>
    <meta charset="UTF-8">
    <meta http-equiv="refresh" content="5;url=/">
    <style>body{font-family:Arial;text-align:center;margin:50px;}</style>
</head>
<body>
    <h1>📡 WiFi Configuration Saved!</h1>
    <p>🔄 Restarting WiFi connection in 5 seconds...</p>
    <p>If connecting to a new network, you may need to find the new IP address.</p>
</body>
</html>
    )";

    server.send(200, "text/html; charset=UTF-8", successPage);
    delay(2000);

    // Restart WiFi with new settings
    Serial.println("🔄 Restarting WiFi with new configuration...");
    WiFi.disconnect();
    delay(1000);
    setupWiFi();
    Serial.println("✅ WiFi configuration updated");
  } else {
    server.send(500, "text/plain", "Failed to save WiFi configuration");
  }
}

void handleNotFound() {
  setStatusLED(COLOR_WEB_REQUEST);
  Serial.printf("❌ 404 Not Found: %s\n", server.uri().c_str());
  server.send(404, "text/plain", "Page not found");
  setStatusLED(COLOR_READY);
}

bool saveConfigToFile(LEDConfig config) {
  return saveFullConfigToFile(config, currentWiFiConfig);
}

bool saveWiFiConfigToFile(WiFiConfig wifiConfig) {
  return saveFullConfigToFile(currentConfig, wifiConfig);
}

bool saveFullConfigToFile(LEDConfig config, WiFiConfig wifiConfig) {
  File file = SPIFFS.open("/config.yml", "w");
  if (!file) {
    Serial.println("❌ Failed to open config.yml for writing");
    return false;
  }

  file.println("# LED Strip Configuration");
  file.println("led_configuration:");
  file.println("  # Physical layout");
  file.printf("  pixels_per_row: %d\n", config.pixelsPerRow);
  file.printf("  number_of_rows: %d\n", config.numberOfRows);
  file.printf("  total_pixels: %d\n", config.totalPixels);
  file.println();
  file.println("  # Hardware settings");
  file.printf("  led_pin: %d\n", config.ledPin);
  file.printf("  brightness: %d\n", config.brightness);
  file.println();
  file.println("  # LED strip specifications");
  file.printf("  led_type: \"%s\"\n", config.ledType.c_str());
  file.printf("  color_order: \"%s\"\n", config.colorOrder.c_str());
  file.println();
  file.println("  # Optional settings");
  file.printf("  refresh_rate: %d  # Hz\n", config.refreshRate);
  file.printf("  max_current: %d  # mA\n", config.maxCurrent);
  file.println();
  file.println("# WiFi Configuration");
  file.println("wifi_configuration:");
  file.println("  # Client mode settings (connect to existing network)");
  file.printf("  enable_client_mode: %s\n", wifiConfig.enableClientMode ? "true" : "false");
  file.printf("  ssid: \"%s\"\n", wifiConfig.ssid.c_str());
  file.printf("  password: \"%s\"\n", wifiConfig.password.c_str());
  file.println();
  file.println("  # Access Point settings (current hotspot mode)");
  file.printf("  ap_ssid: \"%s\"\n", wifiConfig.apSsid.c_str());
  file.printf("  ap_password: \"%s\"\n", wifiConfig.apPassword.c_str());
  file.println();
  file.println("  # Network settings");
  file.printf("  hostname: \"%s\"\n", wifiConfig.hostname.c_str());
  file.printf("  connection_timeout: %d  # ms\n", wifiConfig.connectionTimeout);
  file.printf("  max_retry_attempts: %d\n", wifiConfig.maxRetryAttempts);

  file.close();
  return true;
}