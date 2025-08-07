# Dummy LED Implementation Example

## Configuration: 2 rows × 22 pixels = 44 logical LEDs

### Without Dummy LED (`dummy_led: false`)
- **Physical LEDs**: 44 total
- **Mapping**: Direct 1:1 mapping
  - Logical pixel 0 → Physical pixel 0
  - Logical pixel 1 → Physical pixel 1
  - ...
  - Logical pixel 43 → Physical pixel 43

### With Dummy LED (`dummy_led: true`)
- **Physical LEDs**: 45 total (44 logical + 1 dummy)
- **Mapping**: Offset by 1 due to dummy LED at beginning
  - Physical pixel 0 → **DUMMY LED (always off)**
  - Logical pixel 0 → Physical pixel 1
  - Logical pixel 1 → Physical pixel 2
  - ...
  - Logical pixel 43 → Physical pixel 44

## Physical Strip Layout with Dummy LED Enabled

```
Physical Strip: [DUMMY][LED0][LED1][LED2]...[LED21][LED22][LED23]...[LED43]
Strip Position:   0      1     2     3          22    23    24        44
Logical Mapping:  --     0     1     2          21    22    23        43
Matrix Position:  --   (0,0) (0,1) (0,2)     (0,21) (1,0) (1,1)    (1,21)
```

## Benefits
- **Signal stability**: First logical LED (physical position 1) receives clean signal
- **Timing buffer**: Dummy LED absorbs any timing irregularities from ESP32
- **Transparent**: All effects work normally with logical pixels 0-43
- **Matrix preserved**: Row/column calculations remain unchanged

## Usage
1. Enable via web interface checkbox: "Enable Dummy LED Buffer"
2. Or set in config.yml: `dummy_led: true`
3. Connect 45 physical LEDs to the strip (instead of 44)
4. First LED will remain off, next 44 LEDs will display the pattern