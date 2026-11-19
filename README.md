# EX30 GPS Speedometer

Firmware for the **Waveshare ESP32-S3-LCD-3.16** with an AT6668 GPS receiver. The board has a 320 x 820 ST7701 RGB display, 16 MB flash, 8 MB PSRAM, and no touch panel. The UI runs in landscape at 820 x 320.

By default it runs as a **head-up display (HUD)**: the screen lies on the dashboard and the speed and a speed gauge are read as a reflection in the windscreen. The image is mirrored and pre-distorted (keystone, curvature, skew, rotation, size and position) for the windscreen, adjusted live with sliders on the device's web page. See [docs/hud.md](docs/hud.md).

![What the panel shows in HUD mode](docs/images/hud-speed-panel-corrected.png)

Features include GPS speed and trip statistics, day/night brightness, configurable colours, Wi-Fi settings, OTA updates, and optional location telemetry to a user-provided API. GPS time sets the system clock; this build has no RTC or battery-status hardware.

## Build and Upload

1. Open this folder in VS Code with the PlatformIO IDE extension installed.
2. Select the `waveshare-esp32-s3-lcd-316` environment and build or upload.
3. Use the `ota` environment for network updates when the device and PC share a Wi-Fi network.
4. The serial monitor runs at 115200 baud.

The OTA upload file is `.pio/build/waveshare-esp32-s3-lcd-316/firmware.bin`. Platform and library versions are specified in `platformio.ini`.

The build rebuilds the Arduino core with two ESP-IDF options that keep the display stable while Wi-Fi is on. The first build takes about 7 minutes. Build from the IDE or PowerShell (not Git Bash), and run only one build at a time. **Before changing anything display-related, read [docs/display.md](docs/display.md).**

## Hardware

Connect the GPS receiver to the board's UART connector: GPS TX to ESP32 RX GPIO44, GPS RX to ESP32 TX GPIO43, and connect power and ground as marked on the board and receiver. Firmware probes both UART directions and common receiver baud rates, then configures the AT6668 for GGA/RMC output and the selected 1, 5, or 10 Hz update rate.

The LCD uses the ESP-IDF RGB panel driver with bounce buffers and two frame buffers. The settings that keep it stable are in [docs/display.md](docs/display.md). Backlight brightness is controlled on GPIO6. The BOOT key on GPIO0 is configured after LCD initialization because that pin is also used during panel startup.

## Controls

- Short press BOOT on the driving screen: step through the designs Clean, Night and the HUD gauge design. All three are drawn for the windscreen reflection (mirrored and shaped as set for the HUD), with the speed centred:
  - Clean: speed with a thin bar, in the day colour theme.
  - Night: speed with a row of dots, in the low-blue night colour theme. With *Auto night* on, Night (and the HUD gauge design's night colours) take over when the sun is low.
  - HUD gauge: speed with a bar, segments or no gauge, an optional limit marker (see [docs/hud.md](docs/hud.md)).
  - The top line shows messages, and the clock and NO GPS warning if they are turned on in the HUD settings.
- Hold BOOT for 1 second: open settings.
- In settings, short press changes the current value; hold for 1 second advances to the next item.
- Hold BOOT for 3 seconds in settings to save and exit.
- In Wi-Fi setup, hold BOOT for 3 seconds to close the setup portal.

The first press while the display is dimmed or off only wakes it. The screen dims after the configured stopped interval and can turn off later; driving or pressing BOOT wakes it. GPS, Wi-Fi, and telemetry continue while the backlight is off.

Settings include the HUD gauge design, mirroring, gauge style, digit size, the HUD test grid, rotation, units, display selection, day/night colours, automatic or fixed brightness, screen timeout, GPS rate, smoothing, speed offset, visual speed alerts, Wi-Fi, telemetry, and whether the trip resets when the screen turns off. Speed alerts are displayed on screen; this board build has no vibration motor or speaker.

## Wi-Fi and Settings

Once connected, open `http://ex30-speedo.local` or the device's IP address. The settings page configures the HUD (with live sliders for the windscreen shape), Wi-Fi, colours, rotation, units, GPS rate, the standstill cut-off, speed offset, speed alerts, automatic trip reset, API telemetry, time zone, and OTA password. Wi-Fi setup can also be started from the device settings menu.

Settings can also be changed from a laptop over USB serial; see [Serial Console](#serial-console).

## Serial Console

Connect over USB at 115200 baud with any serial monitor (for example the PlatformIO monitor) and type a command followed by Enter. Type `help` for the list.

### Wi-Fi and device commands

```text
wifi1 "Phone hotspot" "password"   save Network 1 (phone hotspot)
wifi2 "Home Wi-Fi" "password"      save Network 2 (home)
clear1 / clear2                    forget Network 1 / 2
ota "new-password"                 OTA password, 6+ characters (applies after reboot)
status                             networks, Wi-Fi, hostname and GPS state
reboot                             restart the device
```

### Settings

Every setting on the web page can be read and changed over serial. The keys are the same as in the `/api/settings` JSON; nested keys use a dot (`hud.gauge`, `day.speed`). Changes apply at once and are saved.

```text
get                          all settings as JSON
get hud.gauge                one value
set <key> <value>            change a value
set simSpeed on              booleans take on/off, 1/0, true/false, yes/no
set alertLimit 70
set theme 2                  screen design 1 Clean, 2 Night (turns the HUD gauge design off)
set dayPreset Ice            colour theme by name or number
set day.speed #FF8000        or a single colour
set tz "GMT0BST,M3.5.0/1,M10.5.0"   quote values that contain spaces
reset config                 defaults for everything except Wi-Fi networks, OTA password and trip
```

After each `set` the stored value is printed. Values out of range are clamped, and values of the wrong type are ignored, so the printed value shows what was actually saved. The web page's *Reset settings to defaults* button does the same as `reset config` (`POST /api/settings/reset`).

| Key | Values |
| --- | --- |
| `units` | 0 mph, 1 km/h |
| `rotation` | 0 normal (USB right), 2 turned 180° |
| `design` | 0 Clean, 1 Night |
| `dayPreset` | day colour theme (Clean and the HUD gauge design by day): 0 Teal, 1 Ice, 2 Lime, 3 Orange, 4 Volvo, 5 Mono, or the name |
| `nightPreset` | night colour theme (Night and the HUD gauge design after dark): 0 Amber, 1 Red, 2 Green, 3 "Dim white", or the name |
| `autoNight` | on/off: switch to Night after dark |
| `nightBelowDeg` | -18 to 10: sun angle for night |
| `brightMode` | 0 auto by sun, 1 fixed |
| `fixedLevel` | 0 to 4: 100, 75, 50, 30, 15 % |
| `gpsHz` | 1, 5 or 10 |
| `smoothing` | 0 low, 1 medium, 2 high |
| `fixSettleSec` | 0 to 30: speed ignored after a new fix |
| `startSpeedMs` | 0.6 to 3.0 m/s: speed needed to leave standstill |
| `startHoldMs` | 0 to 5000: ...for this long |
| `speedOffset` | -10 to 10 display units |
| `alertMode` | 0 off, 1 visual, 2 visual reminder |
| `alertLimit` | 0 (off) to 200 display units |
| `idleDimMin` | 0 (never) to 60 minutes stopped before dimming |
| `idleOffMin` | 0 (stay dim) to 60 minutes after dimming before screen off |
| `resetTripOnSleep` | on/off |
| `simSpeed` | on/off: see [Speed simulation](#speed-simulation) |
| `wifiEnabled`, `preferHome` | on/off |
| `telemetryEnabled` | on/off |
| `apiUrl` | text, up to 159 characters |
| `apiToken` | text (write only, never printed) |
| `otaPass` | text, 6+ characters (write only) |
| `sampleSec` | 1 to 600 |
| `uploadSec` | 2 to 3600 |
| `deviceName` | text: hostname, `<name>.local` |
| `tz` | POSIX time zone |
| `day.<colour>`, `night.<colour>` | `#RRGGBB`; colours: `background`, `speed`, `unit`, `ringTrack`, `ringFill`, `status`, `statusIcon`, `info`, `alert`, `tickMajor`, `tickMinor`, `tickLabel`, `limitMark` |
| `hud.enabled`, `hud.mirrorV`, `hud.mirrorH` | on/off |
| `hud.showUnits`, `hud.showLimit`, `hud.showClock`, `hud.showGps` | on/off |
| `hud.testGrid` | on/off (not saved) |
| `hud.gauge` | 0 off, 1 bar, 2 segments |
| `hud.digitSize` | 0 medium, 1 large |
| `hud.gaugeMax` | 0 (auto) to 300 |
| `hud.scaleX`, `hud.scaleY` | 0.5 to 1.5 |
| `hud.keystone`, `hud.taper`, `hud.bow`, `hud.skew` | -0.5 to 0.5 |
| `hud.rotateDeg` | -15 to 15 |
| `hud.offsetX` | -300 to 300 px |
| `hud.offsetY` | -150 to 150 px |

## Speed simulation

*Simulated speed* on the web page (or `set simSpeed on`) replaces the GPS speed with a sweep from 0 to about 80 mph and back every 40 seconds, so the displays, gauge and speed alert can be tested indoors without a fix. It is off by default but is saved, so it stays on after a restart until switched off. While it is on the screen shows **SIMULATED SPEED**, and the trip and API telemetry are not updated.

## OTA and Secrets

Open `http://ex30-speedo.local/update` (user `admin`) to upload firmware in a browser, or select the `ota` PlatformIO environment. The default OTA password is `speedo1234`; change it in the settings page. The upload password and host can be overridden in `secrets.ini`.

`include/secrets.h` and `secrets.ini` are ignored by git. Example templates are provided for preloading Wi-Fi credentials and OTA settings.

## Speed display

- The shown speed rounds up, so it never under-reads, and a *Speed offset* of -10 to +10 can be added to match the car's speedometer.
- After a new GPS fix the speed is ignored for 5 s, and leaving standstill needs more than 1.0 m/s (2.2 mph) for 1 s. All three are on the web page. See [docs/gps-speed-cutoff-and-speed-display-rules.md](docs/gps-speed-cutoff-and-speed-display-rules.md).
- When the screen switches off after being parked, the trip (distance, max and average) resets, so each drive starts a new trip. Turn this off with *Reset trip when the screen turns off*.

## MQTT

While Wi-Fi is connected the device connects to an MQTT broker (default `192.168.1.55:1883`; set `MQTT_SERVER_IP`, `MQTT_SERVER_PORT` and `MQTT_FRIENDLYNAME` in `include/secrets.h`). It publishes `tele/esp32_Speedometer/alive` and `/ip`, an init stat on `stat/esp32_Speedometer/init`, and listens on `cmnd/esp32_Speedometer/#` and `cmnd/<friendly name>/#` for `sendstat` and `reset`.

## Remote API

When telemetry is enabled, GPS points are queued in PSRAM and sent to the configured endpoint. Requests use JSON and optionally include a bearer token. See `tools/test_api_server.py` and the web settings page for configuration details.

## Project Layout

```text
platformio.ini          board, flash/PSRAM settings and libraries
src/main.cpp            main loop, BOOT key, settings, trip and time
src/waveshare_board.cpp ST7701 RGB panel, backlight and button setup
src/ui_waveshare.cpp    landscape display renderer and HUD warp
src/mqtt.*              MQTT client
src/gps.*               AT6668 UART detection/configuration and NMEA parsing
src/brightness.*        sun position and brightness steps
src/net.*, web_page.h   Wi-Fi, settings page and OTA updates
src/telemetry.*         queued point upload
tools/render_hud.py     preview images of the HUD warp (docs/images/hud-*.png)
src/settings.*          saved settings and colour presets
```

## Hardware Validation

The firmware build has been verified, but a physical display/GPS test has not yet been performed. Confirm panel orientation and colours, backlight polarity, UART GPS fix, and receiver update rate on the target board.
