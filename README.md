# ESP32-C6 LED Strip Controller

Complete LED control system for ESP32-C6 with WS2811/WS2812 LED strips. Features web-based configuration, real-time effects, and persistent settings storage.

## 🚀 Quick Start

### Hardware Requirements
- ESP32-C6 development board
- **Level shifter (TXS0102 recommended)** - CRITICAL!
- 5V power supply (2A+ for 30 LEDs, 3A+ for 50 LEDs)
- WS2811/WS2812 LED strip
- Jumper wires

### Software Setup
1. **Install Arduino IDE 2.x**
2. **Add ESP32 support**: `https://espressif.github.io/arduino-esp32/package_esp32_dev_index.json`
3. **Install ESP32 package v3.0.0+**
4. **Install libraries**: Adafruit NeoPixel v1.15.1+, FastLED v3.10.2+
5. **Select board**: ESP32C6 Dev Module

### Upload and Connect
1. Upload `lightbox-esp32c6-led.ino` to your ESP32-C6
2. **Option A - Hotspot Mode (Default):**
   - Connect to WiFi hotspot: `ESP32-LED-Config` (password: `ledconfig123`)
   - Open browser to `http://192.168.4.1`
3. **Option B - WiFi Client Mode:**
   - Use hotspot mode first to configure WiFi settings
   - Enable client mode and enter your network credentials
   - Device will connect to your WiFi and get a local IP address
4. Configure and control your LEDs via the web interface!

## ⚡ Critical Wiring

**⚠️ LEVEL SHIFTER REQUIRED!** ESP32-C6 outputs 3.3V but WS2811 needs 5V logic.

```
ESP32-C6          Level Shifter       WS2811 Strip
--------          -------------       ------------
GPIO5     ────────► LV (Data In)  ────► HV (Data Out) ────► Data In
3.3V      ────────► VCC_LV
GND       ────────► GND           ────► GND            ────► GND
                    VCC_HV        ◄──── 5V Power Supply ──► +5V
```

## 🎨 Features

### Web Interface
- **Real-time LED control** - Colors, brightness, effects
- **Configuration management** - LED count, pin settings, color order
- **Dual WiFi modes** - Hotspot or connect to existing network
- **Secure WiFi setup** - Password obfuscation for security
- **Persistent settings** - All configurations saved to ESP32 SPIFFS storage

### LED Effects (Auto-cycling)
- **Moving pixels** - Colored dots traveling across strip
- **Color fills** - Progressive color wipes
- **Rainbow cycles** - HSV color wheel animation
- **Row patterns** - 2D matrix effects for multi-row layouts
- **Fire simulation** - Realistic flame animation
- **Breathing** - Smooth brightness pulsing

### Manual Controls
- Set solid colors (red, green, blue, white)
- Clear all LEDs
- Real-time brightness adjustment
- Toggle effects on/off

## 📁 Project Files

- **`lightbox-esp32c6-led.ino`** - Main program (upload this)
- **`led_config.h`** - Configuration parser
- **`config.yml`** - Configuration template

## 🔧 Configuration

Default matrix layout: **2 rows × 22 LEDs = 44 total**

### YAML Configuration Structure
```yaml
led_configuration:
  pixels_per_row: 22
  number_of_rows: 2
  total_pixels: 44
  led_pin: 5
  brightness: 50
  led_type: "WS2811"
  color_order: "GRB"

wifi_configuration:
  enable_client_mode: false
  ssid: ""
  password: ""
  ap_ssid: "ESP32-LED-Config"
  ap_password: "ledconfig123"
  connection_timeout: 10000
  max_retry_attempts: 3
```

All settings configurable via web interface at runtime.

## 📡 WiFi Configuration

### Initial Setup
1. **Default mode**: Device starts in Access Point mode
2. **Connect**: Join `ESP32-LED-Config` hotspot (password: `ledconfig123`)
3. **Configure**: Open `http://192.168.4.1` to access settings

### Connecting to Your WiFi Network
1. **Enable Client Mode**: Check the "Enable WiFi Client Mode" checkbox
2. **Enter Credentials**: Provide your network SSID and password
3. **Save & Restart**: Click "Save WiFi Settings & Restart"
4. **Find New IP**: Check your router for the ESP32's new IP address

### Connection Behavior
- **Success**: Device connects to your WiFi and shows new IP in serial monitor
- **Failure**: Automatically falls back to Access Point mode after 3 retry attempts
- **Security**: WiFi passwords are never displayed after saving (obfuscated for security)

### IP Address Status Indication

When the ESP32 successfully connects to your WiFi network in client mode, it will flash the onboard status LED to indicate the last octet of its IP address. This helps you identify the device's IP without checking the router or serial monitor.

**Flashing Pattern:**
- **Digits 1-9**: LED flashes the corresponding number of times
- **Digit 0**: LED stays solid yellow for 1 second
- **Multiple digits**: 800ms pause between each digit
- **Example**: IP `192.168.1.125` → Flash 1 time, pause, flash 2 times, pause, flash 5 times

**Timing:**
- Flash duration: 300ms on, 200ms off
- Pause between digits: 800ms
- Zero representation: Solid yellow for 1000ms
- Final pause: 1000ms before returning to normal operation

### Troubleshooting WiFi
- **Can't connect**: Check SSID spelling and password accuracy
- **Intermittent connection**: Verify signal strength at ESP32 location
- **Falls back to AP**: Normal behavior when WiFi is unavailable
- **Lost IP address**: Watch for LED flashing pattern when device reconnects


## 🚨 Troubleshooting

### No LEDs Light Up
1. **Check level shifter** - Most common issue
2. **Verify 5V power supply** - Use multimeter
3. **Test connections** - Ensure solid ground connection

### Wrong Colors
- Change color order in web interface: GRB → RGB/BRG
- Check level shifter output voltage

### LEDs Flicker/Random Colors
- **Insufficient power supply** - Most likely cause
- **Poor ground connections** - Ensure common ground
- **Data signal issues** - Check level shifter operation

### Upload Issues
- **Hold BOOT button** while uploading
- Try different USB cable (data capable)
- Lower upload speed in Arduino IDE

## 📊 Power Requirements

- **Each LED**: ~60mA at full white
- **30 LEDs**: Need 5V 2A+ supply (1.8A + 20% overhead)
- **50 LEDs**: Need 5V 3A+ supply (3A + 20% overhead)
- **Formula**: (LED_COUNT × 60mA) × 1.2

## 🔌 GPIO Recommendations

**Best for LED data**: GPIO4, GPIO5 (default), GPIO6, GPIO7, GPIO8, GPIO9, GPIO10, GPIO11

**Avoid**: GPIO0-3 (boot/flash), GPIO12-13 (SPI flash), GPIO18-19 (USB)

## 🏗️ Board Configuration

### Arduino IDE Settings
```
Board: ESP32C6 Dev Module
Upload Speed: 921600
CPU Frequency: 160MHz
Flash Size: 4MB with SPIFFS
Partition Scheme: Default 4MB with spiffs
```

### Manual Upload Process
1. Hold BOOT button on board
2. Click Upload in Arduino IDE
3. Wait for "Connecting..." message
4. Keep holding BOOT until "Writing at 0x..." appears
5. Release BOOT button

## 🎯 Expected Results

**Working correctly:**
- LEDs cycle through 6 different effects automatically
- Web interface shows current configuration
- Serial monitor (115200 baud) shows status messages
- Effects can be toggled and controlled in real-time
- Configuration changes persist after restart

## 📚 Advanced Usage

### Matrix Layout
For 2D LED arrangements, the controller supports matrix addressing:
- **Linear addressing**: LED index = (row × pixels_per_row) + pixel_in_row
- **Row patterns**: Special effects utilize 2D layout
- **Configurable**: Adjust rows/columns via web interface

### Dual Library Support
- **NeoPixel**: Primary library, better ESP32-C6 compatibility
- **FastLED**: Used for advanced effects (fire animation)
- Automatic library selection based on effect

### API Endpoints
Access programmatically:
- `/` - Main web interface
- `/save` - Save LED configuration
- `/save_wifi` - Save WiFi configuration
- `/toggle_effects` - Enable/disable effects
- `/set_color?color=red` - Set solid color
- `/set_brightness?value=128` - Adjust brightness
- `/status` - Get JSON status

## 🔍 Serial Monitor Output

Watch for these messages at 115200 baud:
```
🔥 ESP32-C6 LED Controller Starting...
📡 Attempting to connect to WiFi: YourNetwork
✅ WiFi Connected! (or: ✅ WiFi Hotspot Started)
📶 Network: YourNetwork (or: ESP32-LED-Config)
🌐 Web Interface: http://192.168.1.100 (or: http://192.168.4.1)
🎨 Switching to effect 1
💾 Saving new configuration...
📡 Saving WiFi configuration...
```

## 💡 Development Tips

- **Start small**: Test with 10-20 LEDs first
- **Power first**: Verify adequate 5V supply before debugging data issues
- **Serial monitoring**: Essential for troubleshooting
- **Web interface**: Use for real-time adjustments

---

**Need help?** Check serial monitor output for error messages and status information. Most issues are hardware-related (power supply or level shifter).