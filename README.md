# 🌬️ AirESP - ESP32 Air Quality Monitor

**Professional CO₂, Temperature & Humidity Monitor** with TFT display, web dashboard, and automatic fresh-air calibration.

![AirESP](images/screenshot.jpg)  
*(Add a nice photo or screenshot here)*

## Features

- **Accurate CO₂ monitoring** using MQ135 sensor with automatic calibration
- **Temperature & Humidity** via DHT22
- **Color-coded status** (Good / Moderate / Poor) with RGB LEDs
- **Beautiful 240x240 TFT Display** with live readings + history graph
- **Web Interface** – live monitoring + calibration controls
- **One-click Fresh Air Calibration** (auto-adjusts multiplier in 5 minutes)
- **MQTT + Home Assistant** auto-discovery support
- **WiFi + mDNS** (`http://airesp.local`)
- **Persistent settings** using Preferences (survives reboots)

## Hardware

- **ESP32 DevKit**
- **MQ135** Gas Sensor (CO₂)
- **DHT22** Temperature & Humidity
- **ST7789 / ILI9341** 1.54" or 2.4" TFT Display (via TFT_eSPI)
- 3x Status LEDs (Red/Yellow/Green)

## Quick Start

1. Clone the repository:
   ```bash
   git clone https://github.com/YOURUSERNAME/AirESP.git

2. Open AirESP.ino in Arduino IDE (or PlatformIO)
3. Update WiFi & MQTT credentials in the code
4. pload and open Serial Monitor (115200 baud)
5. Go to http://airesp.local for the web dashboard

## Calibration

- **Recommended method:**
1. Click "🌬️ Start Fresh Air Calibration" on the web interface
2. Place the device outdoors or in a well-ventilated room with clean air (~400 ppm)
3. Wait 5 minutes — the device will automatically calculate and save the best multiplier

## Home Assistant Integration

The device automatically publishes discovery messages. It will appear in HA as three sensors:

- Temperature
- Humidity
- CO₂

## Configuration Options

BASE_R0  10000  Base resistance for MQ135
calibrationMultiplier  1.0  Auto-adjusted during calibration
CALIBRATION_DURATION  300000 ms  5 minutes
