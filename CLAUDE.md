# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-C6 LED Strip Controller - Complete solution for controlling WS2811 LED strips on ESP32-C6 hardware. This project provides a single consolidated program with web-based configuration interface, real-time LED effects, and persistent YAML configuration storage. **Hardware-optimized exclusively for WS2811 timing using ESP32-C6 RMT peripheral.**

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
Required Libraries: None (uses ESP32 RMT peripheral directly)
```

## Architecture

### Program Architecture
1. **lightbox-esp32c6-led.ino** - Main program combining all functionality:
   - Web server for configuration and control
   - Real-time LED effects and animations
   - YAML configuration management
   - WiFi hotspot for browser-based interface
   - **ESP32 RMT peripheral control** for hardware-accurate timing
2. **led_config.h** - Configuration parser for YAML settings

### LED Control Architecture
**RMT (Remote Control Transceiver) Implementation:**
- **Hardware timing control**: Uses ESP32-C6 RMT peripheral for precise WS2811 timing
- **Dual RMT channels**: Channel 0 for LED strip, Channel 1 for onboard RGB LED
- **Interrupt immunity**: Hardware-level timing unaffected by WiFi or system interrupts
- **ESP32-C6 optimized**: Eliminates timing issues common with software bit-banging libraries

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
  color_order: "GRB"   # WS2811 standard (Green-Red-Blue)
  dummy_led: false     # Add dummy LED at start for signal stability
```

### LED Effects Architecture
The main program includes 7 selectable LED effects with manual controls:
- `effectRandomPulse()` - **Default effect** - Whole panel pulses with random colors each cycle
- `effectMovingPixel()` - Colored pixels moving across strip
- `effectColorFill()` - Progressive color fills with different colors
- `effectRainbow()` - HSV rainbow color wheel rotation with custom HSV-to-RGB conversion
- `effectRowPatterns()` - Matrix-aware 2D patterns using row/column layout
- `effectFire()` - Simulated fire animation with RMT-based control
- `effectBreathing()` - Smooth brightness breathing effect

**Effect Selection:**
- **Manual selection via web interface** - dropdown menu allows choosing any effect
- **No automatic cycling** - effects only change when manually selected
- **Random Pulse default** - starts with random color pulsing effect on boot
- Manual color controls (red, green, blue, white, clear) override effects
- Real-time brightness adjustment via web interface
- Effect state resets when switching to prevent visual artifacts

**All effects optimized for RMT peripheral control with hardware-accurate timing.**

## Critical Hardware Dependencies

### Level Shifter Requirement
ESP32-C6 outputs 3.3V logic but WS2811 requires 5V logic (minimum 3.5V). Without level shifter:
- LEDs may not respond
- Intermittent flickering
- Wrong colors displayed
- Data corruption

**Note**: RMT peripheral provides perfect timing but level shifter is still required for voltage compatibility.

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
6. **Effect selection**: Choose from 7 available effects using the dropdown menu (Random Pulse is default)
7. **Real-time control**: Toggle effects on/off, select effects, set manual colors, or adjust brightness instantly

### Serial Monitor Debugging
The main program outputs status information at 115200 baud:
- **RMT initialization**: Displays RMT channel setup for both LED strip and onboard LED
- Startup sequence and configuration loading
- WiFi hotspot status and IP address
- LED effect switching and web requests
- Configuration changes and saves
- No output = likely compilation or upload issue
- Garbled output = incorrect baud rate

**Expected RMT output:**
```
✅ RMT initialized on GPIO8 (channel 1)
✅ Onboard RGB LED (GPIO8) initialized with RMT
✅ RMT initialized on GPIO5 (channel 0)
✅ LED strip initialized: 44 LEDs on GPIO5 using RMT
```

## Library Dependencies

### RMT Peripheral (Current Implementation)
- **Hardware-level timing control**: ESP32-C6 RMT peripheral provides microsecond-accurate WS2811 timing
- **No external libraries required**: Uses built-in ESP32 RMT drivers (`driver/rmt.h`)
- **Immune to interference**: Hardware timing unaffected by WiFi, interrupts, or system load
- **ESP32-C6 optimized**: Designed specifically for RISC-V architecture and WS2811 timing requirements

### Previously Used Libraries (Replaced by RMT)
~~**Adafruit NeoPixel**: Timing issues on ESP32-C6 due to RISC-V architecture differences~~
~~**FastLED**: Compatibility problems with ESP32-C6 interrupt timing~~

**Migration completed**: All effects now use RMT peripheral for reliable operation.

## Common Failure Modes

### Hardware Issues (90% of problems)
1. Missing level shifter - most common
2. Insufficient power supply capacity
3. Poor ground connections
4. Wrong GPIO pin selection
5. First LED signal timing issues - enable dummy LED feature if needed

### Software Issues
1. Incorrect color order (GRB is WS2811 standard - now fixed in hardware)
2. ESP32 board package version < 3.0.0
3. ~~Missing library dependencies~~ (No longer applicable - uses built-in RMT)
4. Upload failure (hold BOOT button)
5. **RMT initialization failure**: Check serial output for RMT channel conflicts

## ESP32-C6 Specific Considerations

### Differences from Standard ESP32
- RISC-V architecture (not Xtensa)
- Single core vs dual core
- ~~Some libraries may have compatibility issues~~ (Solved with RMT implementation)
- Newer chip with evolving Arduino support
- **RMT peripheral**: Hardware timing solution eliminates architecture-specific timing issues

### GPIO Pin Selection
- Recommended: GPIO4, GPIO5, GPIO6, GPIO7, GPIO8, GPIO9, GPIO10, GPIO11
- Avoid: GPIO0-3 (boot/flash), GPIO12-13 (SPI flash), GPIO18-19 (USB)

### WS2811 RMT Timing Specifications
**WS2811 Protocol Implementation (Hardware-Accurate Datasheet Values):**
- **'0' bit**: T0H=0.5µs HIGH, T0L=2.0µs LOW (5 RMT ticks, 20 RMT ticks) = 2.5µs total
- **'1' bit**: T1H=1.2µs HIGH, T1L=1.3µs LOW (12 RMT ticks, 13 RMT ticks) = 2.5µs total
- **Reset/Latch**: >50µs LOW (500 RMT ticks = exactly 50µs)
- **Color order**: GRB (Green-Red-Blue), 8 bits per color, 24 bits per LED
- **RMT clock**: 10MHz (100ns per tick precision for ESP32-C6 stability)
- **Channels used**: CH0 (LED strip), CH1 (onboard LED)
- **Tolerance**: ±150ns on all timing values per WS2811 datasheet

**WS2811 Characteristics:**
- External driver chip controlling groups of LEDs (typically 3 LEDs per IC)
- More tolerant of timing variations than integrated chips
- Requires proper reset/latch pulse for data commitment

**Hardware advantages over software timing:**
- Immune to WiFi/interrupt interference
- Microsecond-accurate timing with reduced jitter on ESP32-C6
- No CPU overhead during transmission
- 10MHz clock provides stability optimizations for RISC-V architecture

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
  - Enable/disable LED effects and select from 7 available animations
  - Choose specific effects via dropdown menu (Random Pulse, Moving Pixel, Color Fill, Rainbow, Row Patterns, Fire, Breathing)
  - Adjust brightness and set manual colors (red, green, blue, white, clear)
  - Configure dummy LED option for first LED signal stability
  - Modify all YAML configuration parameters
  - Save configuration changes permanently to SPIFFS
- No router required - ESP32 acts as access point for direct device connection

### Matrix Layout Concepts
- LED strips arranged in rows (e.g., 2 rows × 22 LEDs = 44 total)
- Linear addressing: LED index = (row × pixels_per_row) + pixel_in_row
- Row-specific patterns available for 2D effects and testing
- Configuration validates total pixel count matches row/column calculations

### Dummy LED Feature
- **Purpose**: Solves common first LED signal timing issues
- **Implementation**: Adds one dummy LED at the very beginning of the strip (position 0)
- **Behavior**: Dummy LED remains off permanently, serves as signal buffer
- **Mapping**: All logical pixels shifted by 1 position (logical 0 → physical 1)
- **Configuration**: Enable via web interface checkbox or `dummy_led: true` in YAML
- **Physical requirements**: When enabled, strip needs one extra LED (e.g., 45 total for 44 logical)
- **Transparent operation**: All effects and controls work with logical pixel counts