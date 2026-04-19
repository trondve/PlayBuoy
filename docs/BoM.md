# Bill of Materials — PlayBuoy

## Main Board

| Qty | Component | Notes |
|-----|-----------|-------|
| 1 | LilyGo T-SIM7000G v2 | ESP32-D0WD-V3 + SIM7000G modem + battery holder. Core board with integrated 4G/GPS |

## Power System

| Qty | Component | Specs | Notes |
|-----|-----------|-------|-------|
| 2 | LiitoKala Lii-35S 18650 | 3500mAh, Samsung INR18650-35E core | Parallel config (7000mAh total, same voltage). Best honest-capacity 18650 on AliExpress |
| 4 | Solar panel 0.3W 5V | ~60×60mm each | ~1.2W peak total. Wired to board's solar input |

## Sensors

| Qty | Component | Interface | Notes |
|-----|-----------|-----------|-------|
| 1 | DS18B20 waterproof probe | OneWire (GPIO 13) | 12-bit, 750ms conversion. Measures water temperature |
| 1 | GY-91 IMU board (MPU6500) | I2C (GPIO 21/22) | Accelerometer for wave FFT. Magnetometer non-functional in sealed enclosure |

## Connectivity

| Qty | Component | Notes |
|-----|-----------|-------|
| 1 | SIM card (Telenor Norway) | Standard 2FF. APN: `telenor.smart`. LTE-M preferred, NB-IoT fallback |
| 1 | Cellular/GPS antenna | Included with T-SIM7000G board. Keep clear of water |

## Passive Components

| Qty | Component | Notes |
|-----|-----------|-------|
| 2 | 100K resistor | Battery ADC voltage divider on GPIO 35 (1:2 ratio). May be on-board depending on T-SIM7000G revision |
| 1 | 4.7K resistor | OneWire pull-up for DS18B20 on GPIO 13 |

## Enclosure & Mechanical

| Qty | Component | Notes |
|-----|-----------|-------|
| 1 | Waterproof sealed enclosure | Permanently sealed after deployment. Must fit board, batteries, IMU, and solar panels |
| 1 | Anchor line + weight | 50m+ drift threshold triggers alerts |
| — | Waterproof cable glands / sealant | For DS18B20 probe pass-through |
| — | Ballast weights | Tuned for neutral buoyancy with probe submerged |

## Software Dependencies

Not physical components, but required for the build:

| Library | Version | Purpose |
|---------|---------|---------|
| TinyGSM | — | SIM7000G modem driver |
| ArduinoJson | ≥6.21.2 | JSON payload construction |
| OneWire | — | DS18B20 communication |
| DallasTemperature | — | DS18B20 high-level API |

## Deployment Sites

| Buoy ID | Location | Coordinates |
|---------|----------|-------------|
| `playbuoy_grinde` | Litla Grindevatnet | ~59.4°N, 5.3°E |
| `playbuoy_vatna` | Vatnakvamsvatnet | — |

Each site needs one complete set of the above components.
