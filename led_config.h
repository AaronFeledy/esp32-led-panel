/*
 * LED Configuration Parser
 * Reads configuration from config.yml file stored in SPIFFS
 */

#ifndef LED_CONFIG_H
#define LED_CONFIG_H

#include "SPIFFS.h"

struct LEDConfig {
  int pixelsPerRow = 22;
  int numberOfRows = 2;
  int totalPixels = 44;
  int ledPin = 5;
  int brightness = 50;
  String ledType = "WS2811";
  String colorOrder = "GRB";
  int refreshRate = 60;
  int maxCurrent = 2000;
};

struct WiFiConfig {
  bool enableClientMode = false;
  String ssid = "";
  String password = "";
  String apSsid = "ESP32-LED-Config";
  String apPassword = "ledconfig123";
  String hostname = "esp32-led";
  int connectionTimeout = 10000;
  int maxRetryAttempts = 3;
};

class ConfigParser {
private:
  LEDConfig config;
  WiFiConfig wifiConfig;
  int indentLevel = 0;

  String trim(String str) {
    str.trim();
    return str;
  }

  int getIndentLevel(String line) {
    int spaces = 0;
    for (int i = 0; i < line.length(); i++) {
      if (line.charAt(i) == ' ') {
        spaces++;
      } else {
        break;
      }
    }
    return spaces / 2; // Assuming 2-space indentation
  }

  void parseYamlLine(String line) {
    line = trim(line);

    // Skip comments and empty lines
    if (line.startsWith("#") || line.length() == 0) {
      return;
    }

    int colonIndex = line.indexOf(':');
    if (colonIndex == -1) return;

    String key = trim(line.substring(0, colonIndex));
    String value = trim(line.substring(colonIndex + 1));

    // Remove quotes if present
    if (value.startsWith("\"") && value.endsWith("\"")) {
      value = value.substring(1, value.length() - 1);
    }

    // Parse values based on key
    if (key.equals("pixels_per_row")) {
      config.pixelsPerRow = value.toInt();
      Serial.printf("pixels_per_row: %d\n", config.pixelsPerRow);
    } else if (key.equals("number_of_rows")) {
      config.numberOfRows = value.toInt();
      Serial.printf("number_of_rows: %d\n", config.numberOfRows);
    } else if (key.equals("total_pixels")) {
      config.totalPixels = value.toInt();
      Serial.printf("total_pixels: %d\n", config.totalPixels);
    } else if (key.equals("led_pin")) {
      config.ledPin = value.toInt();
      Serial.printf("led_pin: %d\n", config.ledPin);
    } else if (key.equals("brightness")) {
      config.brightness = value.toInt();
      Serial.printf("brightness: %d\n", config.brightness);
    } else if (key.equals("led_type")) {
      config.ledType = value;
      Serial.printf("led_type: %s\n", value.c_str());
    } else if (key.equals("color_order")) {
      config.colorOrder = value;
      Serial.printf("color_order: %s\n", value.c_str());
    } else if (key.equals("refresh_rate")) {
      config.refreshRate = value.toInt();
      Serial.printf("refresh_rate: %d\n", config.refreshRate);
    } else if (key.equals("max_current")) {
      config.maxCurrent = value.toInt();
      Serial.printf("max_current: %d\n", config.maxCurrent);
    } else if (key.equals("enable_client_mode")) {
      wifiConfig.enableClientMode = value.equalsIgnoreCase("true");
      Serial.printf("enable_client_mode: %s\n", wifiConfig.enableClientMode ? "true" : "false");
    } else if (key.equals("ssid")) {
      wifiConfig.ssid = value;
      Serial.printf("ssid: %s\n", value.c_str());
    } else if (key.equals("password")) {
      wifiConfig.password = value;
      Serial.printf("password: %s\n", value.length() > 0 ? "[SET]" : "[EMPTY]");
    } else if (key.equals("ap_ssid")) {
      wifiConfig.apSsid = value;
      Serial.printf("ap_ssid: %s\n", value.c_str());
    } else if (key.equals("ap_password")) {
      wifiConfig.apPassword = value;
      Serial.printf("ap_password: %s\n", value.c_str());
    } else if (key.equals("hostname")) {
      wifiConfig.hostname = value;
      Serial.printf("hostname: %s\n", value.c_str());
    } else if (key.equals("connection_timeout")) {
      wifiConfig.connectionTimeout = value.toInt();
      Serial.printf("connection_timeout: %d\n", wifiConfig.connectionTimeout);
    } else if (key.equals("max_retry_attempts")) {
      wifiConfig.maxRetryAttempts = value.toInt();
      Serial.printf("max_retry_attempts: %d\n", wifiConfig.maxRetryAttempts);
    }
  }

public:
  bool loadConfig() {
    if (!SPIFFS.begin(true)) {
      Serial.println("SPIFFS initialization failed");
      return false;
    }

    File file = SPIFFS.open("/config.yml", "r");
    if (!file) {
      Serial.println("Failed to open config.yml, using defaults");
      return false;
    }

    Serial.println("Reading config.yml...");
    while (file.available()) {
      String line = file.readStringUntil('\n');
      parseYamlLine(line);
    }

    file.close();

    // Validate total pixels calculation
    if (config.totalPixels != config.pixelsPerRow * config.numberOfRows) {
      config.totalPixels = config.pixelsPerRow * config.numberOfRows;
      Serial.printf("Corrected total_pixels to: %d\n", config.totalPixels);
    }

    Serial.println("YAML configuration loaded successfully");
    return true;
  }

  LEDConfig getConfig() {
    return config;
  }

  WiFiConfig getWiFiConfig() {
    return wifiConfig;
  }
};

#endif