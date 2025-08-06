# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-C6 LED Strip Controller - Complete solution for controlling WS2811/WS2812 LED strips on ESP32-C6 hardware. This project provides a single consolidated program with web-based configuration interface, real-time LED effects, and persistent YAML configuration storage.

## Development Commands

### Arduino CLI (WSL2/Linux)
```bash
# Compile main program
arduino-cli compile --fqbn esp32:esp32:esp32c6 lightbox-esp32c6-led.ino

# Upload to connected board (requires USB passthrough in WSL2)
arduino-cli upload --fqbn esp32:esp32:esp32c6 --port /dev/ttyUSB0 lightbox-esp32c6-led.ino
```

### Arduino IDE (Windows - Recommended)
```
Board: ESP32C6 Dev Module
Port: Select appropriate COM port
Upload Speed: 921600 (or lower if issues)
Required Libraries: Adafruit NeoPixel v1.15.1+, FastLED v3.10.2+
```

## Architecture

### Program Architecture
1. **lightbox-esp32c6-led.ino** - Main program combining all functionality:
   - Web server for configuration and control
   - Real-time LED effects and animations
   - YAML configuration management
   - WiFi hotspot for browser-based interface
   - Dual library support (NeoPixel + FastLED)
2. **led_config.h** - Configuration parser for YAML settings

### Hardware Architecture Requirements
- **ESP32-C6** (RISC-V, 3.3V logic) + **Level Shifter** + **WS2811 Strip** (5V logic)
- Critical hardware dependency: Level shifter required for 3.3V→5V logic conversion
- External 5V power supply mandatory for LED strips (ESP32 cannot power strips directly)

### Configuration System
- **YAML-based configuration**: Uses `config.yml` stored in SPIFFS filesystem
- **Matrix layout support**: Configurable rows and columns for 2D LED arrangements
- **Default configuration**: 2 rows × 22 pixels = 44 total LEDs
- **Dynamic loading**: Configuration read at runtime, no hardcoded values in main programs

### Core Configuration Structure
```yaml
led_configuration:
  pixels_per_row: 22    # LEDs per row in matrix layout
  number_of_rows: 2     # Number of rows in matrix
  total_pixels: 44      # Calculated total (rows × pixels_per_row)
  led_pin: 5           # GPIO5 recommended
  brightness: 50       # Start low (0-255) for testing
  color_order: "GRB"   # WS2811 standard
```

### LED Effects Architecture
The main program includes 6 automatic cycling effects plus manual controls:
- `effectMovingPixel()` - Colored pixels moving across strip
- `effectColorFill()` - Progressive color fills with different colors
- `effectRainbow()` - HSV rainbow color wheel rotation
- `effectRowPatterns()` - Matrix-aware 2D patterns using row/column layout
- `effectFire()` - Simulated fire animation using FastLED
- `effectBreathing()` - Smooth brightness breathing effect
- Manual color controls (red, green, blue, white, clear)
- Real-time brightness adjustment via web interface

## Critical Hardware Dependencies

### Level Shifter Requirement
ESP32-C6 outputs 3.3V logic but WS2811 requires 5V logic (minimum 3.5V). Without level shifter:
- LEDs may not respond
- Intermittent flickering
- Wrong colors displayed
- Data corruption

### Power Supply Calculation
- Each LED: ~60mA at full white
- 44 LEDs (default): 2.64A + 20% overhead = 3.2A minimum
- Formula: (LED_COUNT × 60mA) × 1.2 for overhead calculation

## Usage Workflow

### Setup and Operation
1. **Hardware verification**: Check voltages with multimeter before powering on
2. **Program upload**: Upload `lightbox-esp32c6-led.ino` to ESP32-C6
3. **WiFi connection**: Connect to `ESP32-LED-Config` hotspot (password: `ledconfig123`)
4. **Web interface**: Open browser to `http://192.168.4.1` for full control
5. **Configuration**: Adjust LED settings, effects, and brightness via web interface
6. **Real-time control**: Toggle effects, set manual colors, or adjust brightness instantly

### Serial Monitor Debugging
The main program outputs status information at 115200 baud:
- Startup sequence and configuration loading
- WiFi hotspot status and IP address
- LED effect switching and web requests
- Configuration changes and saves
- No output = likely compilation or upload issue
- Garbled output = incorrect baud rate

## Library Dependencies

### Primary: Adafruit NeoPixel
- Better ESP32-C6 compatibility
- Stable timing for WS2811 protocols
- Simpler API for basic effects

### Alternative: FastLED  
- More advanced effects and HSV support
- May have timing issues on ESP32-C6
- Use if NeoPixel has compatibility problems

## Common Failure Modes

### Hardware Issues (90% of problems)
1. Missing level shifter - most common
2. Insufficient power supply capacity
3. Poor ground connections
4. Wrong GPIO pin selection

### Software Issues
1. Incorrect color order (NEO_GRB vs NEO_RGB)
2. ESP32 board package version < 3.0.0
3. Missing library dependencies
4. Upload failure (hold BOOT button)

## ESP32-C6 Specific Considerations

### Differences from Standard ESP32
- RISC-V architecture (not Xtensa)
- Single core vs dual core
- Some libraries may have compatibility issues
- Newer chip with evolving Arduino support

### GPIO Pin Selection
- Recommended: GPIO4, GPIO5, GPIO6, GPIO7, GPIO8, GPIO9, GPIO10, GPIO11
- Avoid: GPIO0-3 (boot/flash), GPIO12-13 (SPI flash), GPIO18-19 (USB)

## File Organization

- **Main program**:
  - `lightbox-esp32c6-led.ino` - Complete LED controller with web interface
  - `led_config.h` - YAML parser and configuration structure definitions

- **Configuration files**:
  - `config.yml` - YAML configuration template for reference


- **Documentation files** (.md): Hardware setup, troubleshooting, board configuration
- **README_ESP32C6_SETUP.md**: Primary entry point for new users

## Configuration Management

### SPIFFS Filesystem Usage
- ESP32 uses SPIFFS to store `config.yml` in flash memory
- Configuration persists between power cycles and firmware updates
- Main program automatically initializes SPIFFS and loads configuration on startup
- YAML parser handles runtime configuration loading with fallback to defaults

### Web Configuration Interface
- Main program creates WiFi hotspot `ESP32-LED-Config` for remote configuration
- Access via browser at `http://192.168.4.1` after connecting to hotspot (password: `ledconfig123`)
- Provides complete web interface to:
  - Control LED effects and animations in real-time
  - Adjust brightness and colors manually
  - Modify all YAML configuration parameters
  - Save configuration changes permanently to SPIFFS
- No router required - ESP32 acts as access point for direct device connection

### Matrix Layout Concepts
- LED strips arranged in rows (e.g., 2 rows × 22 LEDs = 44 total)
- Linear addressing: LED index = (row × pixels_per_row) + pixel_in_row
- Row-specific patterns available for 2D effects and testing
- Configuration validates total pixel count matches row/column calculations