#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// User settings, stored in flash (NVS) and editable on the device or from the
// web page. Colours are 0xRRGGBB.
// ---------------------------------------------------------------------------

enum class Units : uint8_t { MPH = 0, KMH = 1 };
enum class Smoothing : uint8_t { Low = 0, Medium = 1, High = 2 };
enum class AlertMode : uint8_t { Off = 0, Visual = 1, VisualReminder = 2 };
// Designs, in the order the BOOT key steps through them (then the HUD gauge design). All are
// drawn for the windscreen reflection (mirrored and warped as set in HudSettings).
enum Design : uint8_t { DESIGN_A = 0, DESIGN_D = 1, DESIGN_COUNT = 2 };   // Clean, Night
const char* designName(uint8_t d);
enum class BrightMode : uint8_t { AutoSun = 0, Fixed = 1 };

// Every colour on one screen design. "day" colours are used by Clean and the HUD by day;
// "night" colours by Night and the HUD after dark.
struct ThemeColours {
  uint32_t background;
  uint32_t speed;       // big speed digits
  uint32_t unit;        // "MPH" label
  uint32_t ringTrack;   // unlit part of the ring / unlit dots
  uint32_t ringFill;    // lit part of the ring / lit dots
  uint32_t status;      // top status line (sats, rate, night level)
  uint32_t statusIcon;  // satellite / moon icon
  uint32_t info;        // bottom line (clock, trip)
  uint32_t alert;       // over-limit colour for digits and ring
  uint32_t tickMajor;   // gauge (B) major ticks
  uint32_t tickMinor;   // gauge (B) minor ticks
  uint32_t tickLabel;   // gauge (B) numbers round the dial
  uint32_t limitMark;   // gauge (B) limit marker and pill outline
};

// Head-up display: the screen lies on the dashboard and is read as a reflection in the
// windscreen. Geometry is described as the driver sees the reflection; the mirror flags
// undo the reflection itself. See docs/hud.md.
enum class HudGauge : uint8_t { Off = 0, Bar = 1, Segments = 2 };
struct HudSettings {
  bool enabled = true;              // HUD gauge design; off = the Clean / Night designs
  bool mirrorV = true;              // flip top-bottom (screen's top edge towards the windscreen)
  bool mirrorH = false;             // flip left-right
  HudGauge gauge = HudGauge::Bar;
  bool showUnits = true;            // "MPH" beside the digits
  bool showLimit = true;            // alert limit marker on the gauge
  bool showClock = false;
  bool showGps = true;              // "NO GPS" warning while there is no fix
  uint8_t digitSize = 1;            // 0 medium (220 px font), 1 large (300 px font)
  uint16_t gaugeMax = 0;            // gauge full scale in display units, 0 = auto (120 mph / 200 km/h)
  // No longer used: the HUD takes its colours from the day / night theme. Kept so saved
  // settings keep their layout.
  uint32_t colour = 0xFFFFFF;
  uint32_t nightColour = 0x40FF60;
  uint32_t alertColour = 0xFF2A1A;
  // Geometry, applied in this order (all 0 / 1 = no correction)
  float scaleX = 1.0f;              // 0.5..1.5 width
  float scaleY = 1.0f;              // 0.5..1.5 height (the windscreen rake squashes the image)
  float keystone = 0.0f;            // -0.5..0.5: + makes the top wider than the bottom
  float taper = 0.0f;               // -0.5..0.5: + makes the right side taller than the left
  float bow = 0.0f;                 // -0.5..0.5: + bends the ends down (curved windscreen)
  float skew = 0.0f;                // -0.5..0.5: + leans the top to the right
  float rotateDeg = 0.0f;           // -15..15, + turns clockwise
  int16_t offsetX = 0;              // -300..300 px, + moves right
  int16_t offsetY = 0;              // -150..150 px, + moves down
};

// Slot 0 = Network 1 (phone hotspot, for travelling), slot 1 = Network 2 (home Wi-Fi)
static constexpr int MAX_WIFI = 2;

struct WifiCred {
  char ssid[33];
  char pass[65];
};

struct Settings {
  Units units = Units::MPH;
  uint8_t gpsHz = 10;               // 1, 5 or 10
  Smoothing smoothing = Smoothing::Medium;
  AlertMode alertMode = AlertMode::Visual;
  uint8_t alertLimit = 0;           // in display units, 0 = off
  BrightMode brightMode = BrightMode::AutoSun;
  uint8_t fixedLevel = 0;           // index into brightness steps (0 = brightest)
  int8_t brightOffset = 0;          // user nudge, steps
  uint8_t idleDimMin = 3;           // stationary this many minutes -> dim (0 = never)
  uint8_t idleOffMin = 1;           // then this many more minutes -> screen off (0 = stay dimmed)
  uint8_t design = DESIGN_A;        // chosen design for daytime
  bool autoNight = true;            // switch to D automatically when the sun is low
  float nightBelowDeg = -3.0f;      // sun elevation below which autoNight switches to D
  uint8_t dayPreset = 0;            // colour theme last picked (device, web page or serial)
  uint8_t nightPreset = 0;
  ThemeColours day;
  ThemeColours night;

  // Wi-Fi and telemetry
  bool wifiEnabled = true;
  WifiCred wifi[MAX_WIFI] = {};
  bool preferHome = true;           // when both are in range, use Network 2 (home)
  char otaPass[32] = "";            // password for over-the-air updates (PlatformIO and web upload)
  bool telemetryEnabled = false;
  char apiUrl[160] = "";
  char apiToken[96] = "";
  uint16_t sampleSec = 5;           // how often a point is recorded
  uint16_t uploadSec = 10;          // how often queued points are sent
  char tz[48] = "GMT0BST,M3.5.0/1,M10.5.0";  // UK time incl. BST
  char deviceName[32] = "";

  // Remembered between drives
  float lastLat = 51.40f;           // used for sun position before the first fix
  float lastLon = -1.32f;
  float tripMeters = 0;
  float tripMovingSec = 0;
  float tripMaxMs = 0;

  // ---- Added in settings version 5 (appended, so version 4 data still loads) ----
  // Standstill cut-off
  uint8_t fixSettleSec = 5;         // speed counts as 0 for this long after a fix is gained
  float startSpeedMs = 1.0f;        // ~2.2 mph: to leave standstill, speed must stay above this...
  uint16_t startHoldMs = 1000;      // ...for this long
  uint8_t rotation = 0;             // 0 = normal (USB connector on the right), 2 = turned 180 degrees
  int8_t speedOffset = 0;           // whole display units added to the shown speed, -10..+10
  bool resetTripOnSleep = true;     // clear the trip when the screen turns off while parked
  HudSettings hud;

  // ---- Added in settings version 6 ----
  bool simSpeed = false;            // show a simulated speed instead of GPS (for testing)
};

extern Settings settings;

void settingsDefaults(Settings& s);
void settingsLoad();
void settingsSave();
void settingsSaveTrip();
void settingsResetConfig();       // defaults for everything except Wi-Fi networks, OTA password, trip and last position; saves
void hudSanitise(HudSettings& h);  // clamps every HUD value into range          // light-weight save of trip + last position

// Colour presets (themes), selectable on the device, the web page and serial. The web
// page can also set any single colour.
int dayPresetCount();
int nightPresetCount();
const char* dayPresetName(int i);
const char* nightPresetName(int i);
void applyDayPreset(int i);
void applyNightPreset(int i);

float unitsFromMs(float ms);      // m/s -> display units
const char* unitsLabel();
