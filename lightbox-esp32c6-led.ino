/*
 * ESP32-C6 LED Strip Controller with Web Configuration
 * Complete LED control system with web-based configuration interface
 * Combines all LED effects with real-time web configuration management
 *
 * Hardware:
 * - ESP32-C6 board
 * - WS2811 LED strip
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
 * - Hardware RMT timing optimized for WS2811
 * - Matrix layout support for 2D effects
 * - Serial debugging at 115200 baud
 */

#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ESPmDNS.h>
#include <math.h>
#include "led_config.h"

// Include RMT driver for hardware-level LED control
#include "driver/rmt.h"
#include "esp_log.h"

// RMT configuration constants
#define RMT_LED_STRIP_CHANNEL    RMT_CHANNEL_0
#define RMT_ONBOARD_LED_CHANNEL  RMT_CHANNEL_1

// RMT-based WS2811 control for ESP32-C6 timing reliability
// Hardware-accurate timing using ESP32 RMT peripheral

// Web server and configuration
WebServer server(80);
ConfigParser configParser;
LEDConfig currentConfig;
WiFiConfig currentWiFiConfig;

// LED strip control using RMT
static uint32_t* ledBuffer = nullptr;
static bool ledStripRMTInitialized = false;
static bool onboardLedRMTInitialized = false;

// WiFi mode variables
bool clientModeActive = false;
IPAddress currentIP;

// Onboard RGB LED configuration (ESP32-C6 specific)
const int ONBOARD_LED_PIN = 8; // GPIO8 for ESP32-C6 RGB LED
bool hasRGBLED = true; // ESP32-C6 has RGB LED on GPIO8

// WS2811 timing constants for RMT (WS2811 datasheet specifications)
// WS2811 Protocol: T0H=0.5µs±150ns, T0L=2.0µs±150ns, T1H=1.2µs±150ns, T1L=1.3µs±150ns
#define WS2811_T0H_NS  (500)  // 0.5µs high for '0' bit
#define WS2811_T0L_NS  (2000) // 2.0µs low for '0' bit
#define WS2811_T1H_NS  (1200) // 1.2µs high for '1' bit
#define WS2811_T1L_NS  (1300) // 1.3µs low for '1' bit
#define WS2811_RESET_NS (50000) // >50µs reset time (50µs minimum)

// Convert nanoseconds to RMT ticks (10MHz = 100ns per tick for ESP32-C6 stability)
// Use integer math: divide by 100 to avoid floating point
// Formula: ns / 100 (10MHz clock = 100ns per tick)
#define NS_TO_RMT_TICKS(ns) ((ns) / 100)

// Calculated WS2811 RMT timing values (10MHz clock):
// T0H: 500ns  = 5 ticks    T0L: 2000ns = 20 ticks   ('0' bit = 2.5µs total)
// T1H: 1200ns = 12 ticks   T1L: 1300ns = 13 ticks   ('1' bit = 2.5µs total)
// RESET: 50000ns = 500 ticks (50µs reset/latch pulse)

// RMT items for WS2811 timing (corrected field order)
static rmt_item32_t ws2811_bit0 = {
    .duration0 = NS_TO_RMT_TICKS(WS2811_T0H_NS), .level0 = 1,
    .duration1 = NS_TO_RMT_TICKS(WS2811_T0L_NS), .level1 = 0
};

static rmt_item32_t ws2811_bit1 = {
    .duration0 = NS_TO_RMT_TICKS(WS2811_T1H_NS), .level0 = 1,
    .duration1 = NS_TO_RMT_TICKS(WS2811_T1L_NS), .level1 = 0
};

// Reset/latch signal for WS2811 (>50µs low signal)
static rmt_item32_t ws2811_reset = {
    .duration0 = NS_TO_RMT_TICKS(WS2811_RESET_NS), .level0 = 0,
    .duration1 = 0, .level1 = 0
};

// RMT-based LED functions
bool initRMT(uint8_t pin, rmt_channel_t channel, bool isOnboard = false);
void rmtWritePixels(uint32_t* pixels, uint16_t numPixels, rmt_channel_t channel);
uint32_t makeColor(uint8_t r, uint8_t g, uint8_t b);
void applyBrightness(uint32_t* color, uint8_t brightness);

// Dummy LED helper functions
int getPhysicalPixelIndex(int logicalPixel);
int getLogicalPixelCount();

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
unsigned long pulseDuration = 3000; // 3 seconds per pulse cycle
uint32_t currentPulseColor = 0;

// Status LED control
unsigned long lastStatusUpdate = 0;
bool statusBlink = false;

// IP address indication
bool ipIndicationActive = false;
unsigned long ipIndicationStartTime = 0;

void setStatusLED(uint32_t color) {
  if (hasRGBLED && onboardLedRMTInitialized) {
    rmtWritePixels(&color, 1, RMT_ONBOARD_LED_CHANNEL);
  }
}

void blinkStatusLED(uint32_t color, int interval = 500) {
  if (millis() - lastStatusUpdate > interval) {
    statusBlink = !statusBlink;
    setStatusLED(statusBlink ? color : 0x000000);
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
  if (!hasRGBLED) return;

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
  if (!hasRGBLED) return;

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

// ===============================
// RMT-based WS2811 Implementation
// ===============================

bool initRMT(uint8_t pin, rmt_channel_t channel, bool isOnboard) {
  rmt_config_t config = {
    .rmt_mode = RMT_MODE_TX,
    .channel = channel,
    .gpio_num = static_cast<gpio_num_t>(pin),
    .clk_div = 8, // 80MHz / 8 = 10MHz for ESP32-C6 stability
    .mem_block_num = 2, // Increased for better buffering
    .flags = 0,
  };

  config.tx_config.loop_en = false;
  config.tx_config.carrier_en = false;
  config.tx_config.idle_output_en = true;
  config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;

  esp_err_t err = rmt_config(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ RMT config failed on GPIO%d: %s\n", pin, esp_err_to_name(err));
    return false;
  }

  err = rmt_driver_install(config.channel, 0, 0);
  if (err != ESP_OK) {
    Serial.printf("❌ RMT driver install failed on GPIO%d: %s\n", pin, esp_err_to_name(err));
    return false;
  }

  if (isOnboard) {
    onboardLedRMTInitialized = true;
  } else {
    ledStripRMTInitialized = true;
  }

  Serial.printf("✅ RMT initialized on GPIO%d (channel %d)\n", pin, channel);
  return true;
}

void rmtWritePixels(uint32_t* pixels, uint16_t numPixels, rmt_channel_t channel) {
  size_t dataSize = numPixels * 24 + 1; // 24 bits per pixel + 1 reset item
  rmt_item32_t* rmtData = (rmt_item32_t*)malloc(dataSize * sizeof(rmt_item32_t));

  if (!rmtData) {
    Serial.println("❌ Failed to allocate RMT buffer");
    return;
  }

  size_t idx = 0;
  for (int pixel = 0; pixel < numPixels; pixel++) {
    uint32_t color = pixels[pixel];

    // Extract color components for WS2811 (GRB color order)
    uint8_t red = (color >> 16) & 0xFF;
    uint8_t green = (color >> 8) & 0xFF;
    uint8_t blue = color & 0xFF;

    // Pack as GRB for WS2811 (Green-Red-Blue order)
    uint8_t colorBytes[3] = {green, red, blue};

    for (int byteIdx = 0; byteIdx < 3; byteIdx++) {
      uint8_t byte = colorBytes[byteIdx];
      for (int bit = 7; bit >= 0; bit--) {
        rmtData[idx++] = (byte & (1 << bit)) ? ws2811_bit1 : ws2811_bit0;
      }
    }
  }

  // Add reset/latch signal (>50µs low) to properly latch the data
  rmtData[idx++] = ws2811_reset;

  esp_err_t err = rmt_write_items(channel, rmtData, dataSize, true);
  if (err != ESP_OK) {
    Serial.printf("❌ RMT write failed: %s\n", esp_err_to_name(err));

    // Enhanced error recovery for ESP32-C6
    if (err == ESP_ERR_TIMEOUT) {
      Serial.println("⚠️  RMT timeout - retrying transmission");
      delay(1);
      err = rmt_write_items(channel, rmtData, dataSize, true);
    }

    if (err != ESP_OK) {
      Serial.printf("❌ RMT retry failed: %s\n", esp_err_to_name(err));
    }
  }

  free(rmtData);
}

uint32_t makeColor(uint8_t r, uint8_t g, uint8_t b) {
  return (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

void applyBrightness(uint32_t* color, uint8_t brightness) {
  if (brightness == 255) return;

  uint8_t r = (*color >> 16) & 0xFF;
  uint8_t g = (*color >> 8) & 0xFF;
  uint8_t b = *color & 0xFF;

  r = (r * brightness) >> 8;
  g = (g * brightness) >> 8;
  b = (b * brightness) >> 8;

  *color = makeColor(r, g, b);
}

// Convert logical pixel index to physical pixel index
// When dummy LED mode is enabled, there is one dummy LED at the very beginning of the strip
int getPhysicalPixelIndex(int logicalPixel) {
  if (!currentConfig.dummyLed) {
    return logicalPixel; // No dummy LEDs, direct mapping
  }

  // Physical index = logical pixel + 1 (skip the single dummy LED at position 0)
  return logicalPixel + 1;
}

// Get the logical pixel count (user-visible pixels, excluding dummies)
int getLogicalPixelCount() {
  return currentConfig.pixelsPerRow * currentConfig.numberOfRows;
}

// HSV to RGB conversion for rainbow effect
uint32_t hsv2rgb(uint16_t hue, uint8_t sat, uint8_t val) {
  uint8_t r, g, b;
  uint8_t region = hue / 43;
  uint8_t remainder = (hue - (region * 43)) * 6;

  uint8_t p = (val * (255 - sat)) >> 8;
  uint8_t q = (val * (255 - ((sat * remainder) >> 8))) >> 8;
  uint8_t t = (val * (255 - ((sat * (255 - remainder)) >> 8))) >> 8;

  switch (region) {
    case 0: r = val; g = t; b = p; break;
    case 1: r = q; g = val; b = p; break;
    case 2: r = p; g = val; b = t; break;
    case 3: r = p; g = q; b = val; break;
    case 4: r = t; g = p; b = val; break;
    default: r = val; g = p; b = q; break;
  }

  return makeColor(r, g, b);
}

void clearLEDs() {
  if (!ledBuffer || !ledStripRMTInitialized) return;

  // Clear all physical pixels
  for (int i = 0; i < currentConfig.totalPixels; i++) {
    ledBuffer[i] = 0;
  }

  // Ensure dummy LED (if enabled) stays off permanently
  if (currentConfig.dummyLed && currentConfig.totalPixels > 0) {
    ledBuffer[0] = 0; // Dummy LED at position 0 must always be off
  }

  rmtWritePixels(ledBuffer, currentConfig.totalPixels, RMT_LED_STRIP_CHANNEL);
}

void setPixelColor(int logicalPixel, uint32_t color) {
  if (!ledBuffer) return;

  // Convert logical pixel to physical pixel index
  int physicalPixel = getPhysicalPixelIndex(logicalPixel);

  // Check bounds for physical pixel array
  if (physicalPixel >= currentConfig.totalPixels) return;

  // Prevent setting the dummy LED (position 0) if dummy mode is enabled
  if (currentConfig.dummyLed && physicalPixel == 0) {
    Serial.println("⚠️  Attempted to set dummy LED - ignoring");
    return;
  }

  applyBrightness(&color, currentConfig.brightness);
  ledBuffer[physicalPixel] = color;
}

void showLEDs() {
  if (!ledBuffer || !ledStripRMTInitialized) return;

  // Ensure dummy LED (if enabled) stays off before transmission
  if (currentConfig.dummyLed && currentConfig.totalPixels > 0) {
    ledBuffer[0] = 0; // Dummy LED at position 0 must always be off
  }

  rmtWritePixels(ledBuffer, currentConfig.totalPixels, RMT_LED_STRIP_CHANNEL);
}

// Effect list
enum Effects {
  EFFECT_RANDOM_PULSE,
  EFFECT_MOVING_PIXEL,
  EFFECT_COLOR_FILL,
  EFFECT_RAINBOW,
  EFFECT_ROW_PATTERNS,
  EFFECT_FIRE,
  EFFECT_BREATHING,
  EFFECT_COUNT
};

// Effect names for UI
const char* effectNames[] = {
  "Random Pulse",
  "Moving Pixel",
  "Color Fill",
  "Rainbow",
  "Row Patterns",
  "Fire",
  "Breathing"
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
            Matrix: %d rows × %d pixels = %d logical LEDs (%d physical with %s)<br>
            Pin: GPIO%d | Brightness: %d | Type: %s | Order: %s<br>
            Current Effect: %s (%s) | Library: %s
        </div>

        <div class="control-section">
            <h2>🎮 LED Effects Control</h2>
            <div class="effect-controls">
                <button onclick="toggleEffects()">%s Effects</button>
                <div style="margin: 10px 0;">
                    <label for="effect_select">Select Effect:</label>
                    <select id="effect_select" onchange="selectEffect(this.value)" style="margin-left: 10px;">
                        %s
                    </select>
                </div>
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
                        <option value="WS2811" selected>WS2811 (Hardware Optimized)</option>
                    </select>
                </div>

                <div class="form-group">
                    <label for="color_order">Color Order:</label>
                    <select id="color_order" name="color_order" required>
                        <option value="GRB" selected>GRB (WS2811 Standard)</option>
                    </select>
                </div>

                <div class="form-group">
                    <label>
                        <input type="checkbox" id="dummy_led" name="dummy_led" %s>
                        Enable Dummy LED Buffer (adds one dummy LED at the beginning to fix first LED signal issues)
                    </label>
                    <small style="color: #666;">When enabled, adds one LED at the very start of the strip that remains off. Useful for solving first LED timing problems.</small>
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

        function selectEffect(effectId) {
            fetch('/select_effect?id=' + effectId);
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

  // Initialize RMT for onboard RGB LED on GPIO8 (ESP32-C6 specific)
  if (initRMT(ONBOARD_LED_PIN, RMT_ONBOARD_LED_CHANNEL, true)) {
    setStatusLED(COLOR_STARTING); // Orange - System starting
    Serial.printf("✅ Onboard RGB LED (GPIO%d) initialized with RMT\n", ONBOARD_LED_PIN);
  } else {
    Serial.printf("❌ Failed to initialize onboard RGB LED on GPIO%d\n", ONBOARD_LED_PIN);
    hasRGBLED = false;
  }

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
  server.on("/select_effect", HTTP_GET, handleSelectEffect);
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
  Serial.printf("🔧 Initializing LED strip: %d LEDs on GPIO%d with RMT\n",
                currentConfig.totalPixels, currentConfig.ledPin);

  // Allocate LED buffer
  if (ledBuffer) {
    free(ledBuffer);
  }
  ledBuffer = (uint32_t*)malloc(currentConfig.totalPixels * sizeof(uint32_t));

  if (!ledBuffer) {
    Serial.println("❌ Failed to allocate LED buffer");
    return;
  }

  // Initialize RMT for LED strip (different channel than onboard LED)
  if (!initRMT(currentConfig.ledPin, RMT_LED_STRIP_CHANNEL, false)) {
    Serial.printf("❌ Failed to initialize RMT for LED strip on GPIO%d\n", currentConfig.ledPin);
    free(ledBuffer);
    ledBuffer = nullptr;
    return;
  }

  // Clear LED buffer
  for (int i = 0; i < currentConfig.totalPixels; i++) {
    ledBuffer[i] = 0;
  }

  // Explicitly ensure dummy LED (position 0) is off if dummy mode is enabled
  if (currentConfig.dummyLed && currentConfig.totalPixels > 0) {
    ledBuffer[0] = 0; // Dummy LED must always be off
    Serial.printf("🔧 Dummy LED mode enabled: position 0 reserved as dummy (always off)\n");
  }

  // Send initial clear command to LEDs
  clearLEDs();

  Serial.printf("✅ LED strip initialized: %d LEDs on GPIO%d using RMT\n",
                currentConfig.totalPixels, currentConfig.ledPin);
  Serial.printf("📋 LED Configuration: Type=%s, Order=%s, Brightness=%d\n",
                currentConfig.ledType.c_str(), currentConfig.colorOrder.c_str(), currentConfig.brightness);
}

void testLEDs() {
  Serial.println("🧪 Running LED connectivity test...");

  if (!ledBuffer || !ledStripRMTInitialized) {
    Serial.println("❌ LED buffer or RMT not initialized");
    return;
  }

  // Test first 3 logical LEDs with different colors
  clearLEDs();
  int logicalPixels = getLogicalPixelCount();
  if (logicalPixels > 0) setPixelColor(0, makeColor(255, 0, 0)); // Red
  if (logicalPixels > 1) setPixelColor(1, makeColor(0, 255, 0)); // Green
  if (logicalPixels > 2) setPixelColor(2, makeColor(0, 0, 255)); // Blue
  showLEDs();

  delay(1000);
  clearLEDs();

  Serial.println("✅ LED test completed");
}

void runCurrentEffect() {
  // Run the currently selected effect
  switch (currentEffect) {
    case EFFECT_RANDOM_PULSE:
      effectRandomPulse();
      break;
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

uint32_t generateRandomColor() {
  // Generate a random color with good visibility
  // Avoid very dark colors by ensuring at least one component is bright
  uint8_t r = random(50, 256);
  uint8_t g = random(50, 256);
  uint8_t b = random(50, 256);

  // Ensure at least one color component is bright (>150)
  if (r < 150 && g < 150 && b < 150) {
    switch (random(0, 3)) {
      case 0: r = random(150, 256); break;
      case 1: g = random(150, 256); break;
      case 2: b = random(150, 256); break;
    }
  }

  return makeColor(r, g, b);
}

void effectRandomPulse() {
  static unsigned long lastUpdate = 0;
  static unsigned long cycleStartTime = 0;
  static bool newCycle = true;

  if (millis() - lastUpdate > 30) { // Update every 30ms for smooth animation
    unsigned long currentTime = millis();

    // Start new pulse cycle
    if (newCycle) {
      currentPulseColor = generateRandomColor();
      cycleStartTime = currentTime;
      newCycle = false;
      Serial.printf("🎨 New pulse color: #%06X\n", currentPulseColor & 0xFFFFFF);
    }

    // Calculate pulse progress (0 to 1 and back to 0)
    unsigned long elapsed = currentTime - cycleStartTime;
    float progress = (float)elapsed / pulseDuration;

    // Check if cycle is complete
    if (progress >= 1.0) {
      newCycle = true;
      progress = 0;
    }

    // Create breathing effect: fade in and out using sine wave
    float brightness = (sin(progress * 2 * PI) + 1) / 2; // 0 to 1
    brightness = max(0.0f, brightness); // Ensure non-negative

    // Apply brightness to the color
    uint8_t r = ((currentPulseColor >> 16) & 0xFF) * brightness;
    uint8_t g = ((currentPulseColor >> 8) & 0xFF) * brightness;
    uint8_t b = (currentPulseColor & 0xFF) * brightness;

    uint32_t pulseColor = makeColor(r, g, b);

    // Set all logical LEDs to the same pulsing color
    int logicalPixels = getLogicalPixelCount();
    for (int i = 0; i < logicalPixels; i++) {
      setPixelColor(i, pulseColor);
    }
    showLEDs();

    lastUpdate = millis();
  }
}

void effectMovingPixel() {
  static unsigned long lastUpdate = 0;
  static uint32_t colors[] = {
    makeColor(255, 0, 0),   // Red
    makeColor(0, 255, 0),   // Green
    makeColor(0, 0, 255),   // Blue
    makeColor(255, 255, 0)  // Yellow
  };
  static int colorIndex = 0;

  if (millis() - lastUpdate > 100) {
    clearLEDs();

    int logicalPixels = getLogicalPixelCount();
    int pos = effectStep % logicalPixels;
    setPixelColor(pos, colors[colorIndex]);
    showLEDs();

    effectStep++;
    if (effectStep >= logicalPixels) {
      effectStep = 0;
      colorIndex = (colorIndex + 1) % 4;
    }

    lastUpdate = millis();
  }
}

void effectColorFill() {
  static unsigned long lastUpdate = 0;
  static uint32_t colors[] = {
    makeColor(255, 0, 0),     // Red
    makeColor(0, 255, 0),     // Green
    makeColor(0, 0, 255),     // Blue
    makeColor(255, 255, 255)  // White
  };
  static int colorIndex = 0;

  if (millis() - lastUpdate > 50) {
    int logicalPixels = getLogicalPixelCount();
    if (effectStep < logicalPixels) {
      setPixelColor(effectStep, colors[colorIndex]);
      showLEDs();
      effectStep++;
    } else {
      // Color fill complete, switch to next color
      effectStep = 0;
      colorIndex = (colorIndex + 1) % 4;
      clearLEDs();
    }

    lastUpdate = millis();
  }
}

void effectRainbow() {
  static unsigned long lastUpdate = 0;

  if (millis() - lastUpdate > 20) {
    int logicalPixels = getLogicalPixelCount();
    for (int i = 0; i < logicalPixels; i++) {
      uint16_t pixelHue = (effectStep + (i * 255 / logicalPixels)) % 256;
      uint32_t color = hsv2rgb(pixelHue, 255, 255);
      setPixelColor(i, color);
    }
    showLEDs();

    effectStep = (effectStep + 1) % 256;
    lastUpdate = millis();
  }
}

void effectRowPatterns() {
  static unsigned long lastUpdate = 0;
  static int pattern = 0;

  if (millis() - lastUpdate > 1000) {
    clearLEDs();

    switch (pattern) {
      case 0: // Alternating row colors
        for (int row = 0; row < currentConfig.numberOfRows; row++) {
          uint32_t color = (row % 2 == 0) ? makeColor(255, 0, 0) : makeColor(0, 0, 255);
          for (int pixel = 0; pixel < currentConfig.pixelsPerRow; pixel++) {
            int ledIndex = (row * currentConfig.pixelsPerRow) + pixel;
            setPixelColor(ledIndex, color);
          }
        }
        break;

      case 1: // Progressive row fill
        for (int row = 0; row <= effectStep && row < currentConfig.numberOfRows; row++) {
          for (int pixel = 0; pixel < currentConfig.pixelsPerRow; pixel++) {
            int ledIndex = (row * currentConfig.pixelsPerRow) + pixel;
            setPixelColor(ledIndex, makeColor(0, 255, 0));
          }
        }
        effectStep++;
        if (effectStep >= currentConfig.numberOfRows) {
          effectStep = 0;
          pattern = (pattern + 1) % 2;
        }
        break;
    }

    showLEDs();
    lastUpdate = millis();
  }
}

void effectFire() {
  static unsigned long lastUpdate = 0;

  if (millis() - lastUpdate > 50) {
    int logicalPixels = getLogicalPixelCount();
    for (int i = 0; i < logicalPixels; i++) {
      int heat = random(0, 255);
      uint32_t color;
      if (heat > 160) {
        color = makeColor(255, heat - 96, 0);  // Hot flame
      } else if (heat > 96) {
        color = makeColor(heat * 2, heat, 0);  // Medium flame
      } else {
        color = makeColor(heat, 0, 0);         // Cool flame
      }
      setPixelColor(i, color);
    }

    showLEDs();
    lastUpdate = millis();
  }
}

void effectBreathing() {
  static unsigned long lastUpdate = 0;

  if (millis() - lastUpdate > 30) {
    int brightness = (sin(effectStep * 0.1) + 1) * 127.5; // 0-255

    int logicalPixels = getLogicalPixelCount();
    for (int i = 0; i < logicalPixels; i++) {
      setPixelColor(i, makeColor(0, 0, brightness));
    }
    showLEDs();

    effectStep++;
    lastUpdate = millis();
  }
}

// Helper function to generate effect dropdown options
String generateEffectOptions() {
  String options = "";
  for (int i = 0; i < EFFECT_COUNT; i++) {
    options += "<option value=\"" + String(i) + "\"";
    if (i == currentEffect) {
      options += " selected";
    }
    options += ">" + String(effectNames[i]) + "</option>";
  }
  return options;
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

  // Generate effect dropdown options
  String effectOptions = generateEffectOptions();

  int logicalPixels = getLogicalPixelCount();

  int result = snprintf(buffer, bufferSize, htmlPage,
    currentConfig.numberOfRows, currentConfig.pixelsPerRow, logicalPixels, currentConfig.totalPixels,
    currentConfig.dummyLed ? "dummy LEDs" : "no dummy LEDs",
    currentConfig.ledPin, currentConfig.brightness, currentConfig.ledType.c_str(), currentConfig.colorOrder.c_str(),
    effectNames[currentEffect], effectsEnabled ? "ON" : "OFF", "WS2811 RMT Hardware",
    effectsEnabled ? "Disable" : "Enable",
    effectOptions.c_str(),
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
    currentConfig.dummyLed ? "checked" : "",
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
  newConfig.ledPin = server.arg("led_pin").toInt();
  newConfig.brightness = currentConfig.brightness; // Keep current brightness
  newConfig.ledType = "WS2811"; // Force WS2811 for hardware optimization
  newConfig.colorOrder = "GRB"; // Force GRB for WS2811
  newConfig.refreshRate = currentConfig.refreshRate;
  newConfig.maxCurrent = currentConfig.maxCurrent;
  newConfig.dummyLed = server.hasArg("dummy_led") && (server.arg("dummy_led") == "on" || server.arg("dummy_led") == "true");

  // Calculate total pixels based on dummy LED setting
  int logicalPixels = newConfig.pixelsPerRow * newConfig.numberOfRows;
  newConfig.totalPixels = logicalPixels;
  if (newConfig.dummyLed) {
    newConfig.totalPixels += 1; // Add one dummy LED at the beginning of the strip
  }

  if (saveConfigToFile(newConfig)) {
    // Preserve animation state before config update
    bool wasEffectsEnabled = effectsEnabled;
    int previousEffect = currentEffect;

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

    // Restore animation state
    effectsEnabled = wasEffectsEnabled;
    currentEffect = previousEffect;
    effectStep = 0; // Reset effect step for clean restart with new configuration

    Serial.printf("✅ Configuration updated and LEDs reinitialized (Effects: %s, Current: %s)\n",
                  effectsEnabled ? "ENABLED" : "DISABLED",
                  effectsEnabled ? effectNames[currentEffect] : "None");
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
    clearLEDs();
  }
  Serial.printf("🎨 Effects %s\n", effectsEnabled ? "ENABLED" : "DISABLED");
  server.send(200, "text/plain", effectsEnabled ? "Effects enabled" : "Effects disabled");
  setStatusLED(COLOR_READY);
}

void handleSelectEffect() {
  setStatusLED(COLOR_WEB_REQUEST);
  int effectId = server.arg("id").toInt();

  if (effectId >= 0 && effectId < EFFECT_COUNT) {
    currentEffect = effectId;
    effectStep = 0; // Reset effect state
    Serial.printf("🎨 Selected effect: %s (%d)\n", effectNames[currentEffect], currentEffect);
    server.send(200, "text/plain", "Effect selected: " + String(effectNames[currentEffect]));
  } else {
    server.send(400, "text/plain", "Invalid effect ID");
  }
  setStatusLED(COLOR_READY);
}


void handleSetColor() {
  setStatusLED(COLOR_WEB_REQUEST);
  String color = server.arg("color");
  uint32_t rgb = 0;

  if (color == "red") rgb = makeColor(255, 0, 0);
  else if (color == "green") rgb = makeColor(0, 255, 0);
  else if (color == "blue") rgb = makeColor(0, 0, 255);
  else if (color == "white") rgb = makeColor(255, 255, 255);

  effectsEnabled = false; // Disable effects when setting manual color

  int logicalPixels = getLogicalPixelCount();
  for (int i = 0; i < logicalPixels; i++) {
    setPixelColor(i, rgb);
  }
  showLEDs();

  server.send(200, "text/plain", "Color set to " + color);
  setStatusLED(COLOR_READY);
}

void handleClear() {
  setStatusLED(COLOR_WEB_REQUEST);
  effectsEnabled = false;
  clearLEDs();

  Serial.println("🔄 LEDs cleared");
  server.send(200, "text/plain", "LEDs cleared");
  setStatusLED(COLOR_READY);
}

void handleSetBrightness() {
  setStatusLED(COLOR_WEB_REQUEST);
  int brightness = server.arg("value").toInt();
  brightness = constrain(brightness, 0, 255);

  currentConfig.brightness = brightness;

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
  file.println("  # LED strip specifications (WS2811 hardware optimized)");
  file.printf("  led_type: \"WS2811\"\n");
  file.printf("  color_order: \"GRB\"\n");
  file.println();
  file.println("  # Signal optimization");
  file.printf("  dummy_led: %s  # Add dummy LED at start of strip for signal stability\n", config.dummyLed ? "true" : "false");
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