#include "ui.h"
#include "waveshare_board.h"
#include "settings.h"
#include "fonts_vlw.h"
#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>

static constexpr int WIDTH = 820;
static constexpr int HEIGHT = 320;

static lgfx::LGFX_Sprite canvas;            // the back frame buffer, rotated to landscape
static lgfx::LGFX_Sprite* g = &canvas;       // what the draw functions draw on

struct Region { int16_t x, y, w, h; };
static UiModel shown;                        // the model last drawn
static lgfx::PointerWrapper wSpeed, wSpeedM, wLabel, wSmall, wTitle;
static lgfx::VLWfont fSpeed, fSpeedM, fLabel, fSmall, fTitle;

static uint16_t color565(uint32_t color) {
  return lgfx::color565((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
}

static uint16_t scaleColour(uint32_t c, float k) {
  return lgfx::color565(((c >> 16) & 0xFF) * k, ((c >> 8) & 0xFF) * k, (c & 0xFF) * k);
}

static const ThemeColours& activeTheme(const UiModel& model) {
  return model.night ? settings.night : settings.day;
}

static void drawText(const char* text, int x, int y, const lgfx::VLWfont* font,
                     uint16_t color, textdatum_t datum = textdatum_t::middle_left) {
  g->setFont(font);
  g->setTextDatum(datum);
  g->setTextColor(color);
  g->drawString(text, x, y);
}

static void drawSettings(const UiModel& model) {
  const ThemeColours& theme = activeTheme(model);
  g->fillScreen(color565(theme.background));
  char header[40];
  snprintf(header, sizeof(header), "SETTINGS  %d / %d", model.setIndex + 1, model.setCount);
  drawText(header, 40, 42, &fSmall, color565(theme.status));
  drawText(model.setTitle, WIDTH / 2, 132, &fTitle, color565(theme.speed), textdatum_t::middle_center);
  drawText(model.setValue, WIDTH / 2, 194, &fLabel, color565(theme.ringFill), textdatum_t::middle_center);
  drawText("Short: change    Hold: next    Hold 3 seconds: save", WIDTH / 2, 274,
           &fSmall, color565(theme.info), textdatum_t::middle_center);
}

static void drawStatus(const UiModel& model) {
  const ThemeColours& theme = activeTheme(model);
  g->fillScreen(color565(theme.background));
  drawText("STATUS", 32, 30, &fLabel, color565(theme.ringFill));
  for (int i = 0; i < model.lineCount; ++i)
    drawText(model.lines[i], 36, 76 + i * 30, &fSmall, color565(theme.speed));
}

static void drawWifiSetup(const UiModel& model) {
  const ThemeColours& theme = activeTheme(model);
  g->fillScreen(color565(theme.background));
  drawText("WI-FI SETUP", 32, 32, &fLabel, color565(theme.ringFill));
  char qr[96];
  snprintf(qr, sizeof(qr), "WIFI:T:WPA;S:%s;P:%s;;", model.apSsid, model.apPass);
  g->fillRoundRect(44, 64, 190, 190, 8, TFT_WHITE);
  g->qrcode(qr, 54, 74, 170, 3);
  drawText(model.apSsid, 274, 102, &fTitle, color565(theme.speed));
  char password[40];
  snprintf(password, sizeof(password), "Password: %s", model.apPass);
  drawText(password, 274, 150, &fSmall, color565(theme.info));
  drawText("http://192.168.4.1", 274, 194, &fSmall, color565(theme.ringFill));
  drawText(model.staLine, 274, 236, &fSmall, color565(theme.status));
  drawText("Hold 3 seconds to close", 274, 278, &fSmall, color565(theme.info));
}

static void drawUpdate(const UiModel& model) {
  const ThemeColours& theme = activeTheme(model);
  g->fillScreen(color565(theme.background));
  drawText("UPDATING FIRMWARE", WIDTH / 2, 88, &fLabel,
           color565(theme.status), textdatum_t::middle_center);
  char percent[12];
  snprintf(percent, sizeof(percent), "%d%%", model.otaPct);
  drawText(percent, WIDTH / 2, 176, &fSpeed, color565(theme.speed), textdatum_t::middle_center);
  g->drawRoundRect(150, 238, 520, 14, 7, color565(theme.ringTrack));
  g->fillRoundRect(150, 238, 520 * constrain(model.otaPct, 0, 100) / 100, 14, 7,
                       color565(theme.ringFill));
  drawText("Keep power connected", WIDTH / 2, 286, &fSmall,
           color565(theme.info), textdatum_t::middle_center);
}

// =====================================================================================
// Head-up display
// =====================================================================================
// The HUD is drawn upright, as the driver should see it, into hudSrc (8-bit RGB332 in
// PSRAM). It is then warped into the frame buffer: every screen pixel looks up the hudSrc
// pixel it should show, through the inverse of the geometry in settings.hud and the
// mirroring that undoes the windscreen reflection. The inverse map is worked out exactly
// at the corners of 8 x 8 pixel cells and interpolated inside them, so the per-pixel cost
// is a few additions. Only cells whose source area changed are warped again, which keeps
// the PSRAM traffic as low as the partial redraws below (see docs/display.md).

static lgfx::LGFX_Sprite hudSrc;
static bool hudReady = false;
static uint16_t lut332[256];                 // RGB332 -> RGB565

static constexpr int CELL = 8;
static constexpr int CELLS_X = (WIDTH + CELL - 1) / CELL;    // 103
static constexpr int CELLS_Y = (HEIGHT + CELL - 1) / CELL;   // 40
static constexpr int NODES_X = CELLS_X + 1;
static constexpr int NODES_Y = CELLS_Y + 1;
struct Node { int32_t u, v; };               // source position, 16.16 fixed point
static Node* nodes = nullptr;                // NODES_X * NODES_Y
static uint8_t cellNow[CELLS_Y][CELLS_X];
static uint8_t cellLast[CELLS_Y][CELLS_X];

// Inverse geometry: screen position (in the panel's landscape frame) -> hudSrc position.
static void inverseMap(const HudSettings& h, bool flipX, bool flipY, float x, float y, float& u, float& v) {
  const float cx = (WIDTH - 1) * 0.5f, cy = (HEIGHT - 1) * 0.5f;
  const float w2 = WIDTH * 0.5f, h2 = HEIGHT * 0.5f;
  if (flipX) x = (WIDTH - 1) - x;            // undo the reflection: from here on, this is
  if (flipY) y = (HEIGHT - 1) - y;           // the image as the driver sees it
  float dx = x - cx - h.offsetX, dy = y - cy - h.offsetY;
  float a = h.rotateDeg * (float)M_PI / 180.0f, ca = cosf(a), sa = sinf(a);
  float px = dx * ca + dy * sa;              // undo the clockwise rotation
  float py = -dx * sa + dy * ca;
  px += h.skew * py;                         // undo the skew (forward: x -= skew * y)
  float X = px / w2, Y = py / h2;            // -1..1 across the screen
  Y -= h.bow * X * X;                        // undo the bow (forward: y += bow * x^2)
  float d = 1.0f - h.keystone * Y;           // undo the keystone (forward: x *= 1 - keystone * y)
  if (d < 0.1f) { u = v = -10000; return; }
  X /= d;
  d = 1.0f + h.taper * X;                    // undo the taper (forward: y *= 1 + taper * x)
  if (d < 0.1f) { u = v = -10000; return; }
  Y /= d;
  u = cx + X / h.scaleX * w2;
  v = cy + Y / h.scaleY * h2;
}

static void buildWarp(const HudSettings& h, uint8_t rotation) {
  bool turned = rotation == 2;
  bool flipX = h.mirrorH != turned, flipY = h.mirrorV != turned;
  for (int j = 0; j < NODES_Y; j++)
    for (int i = 0; i < NODES_X; i++) {
      float u, v;
      inverseMap(h, flipX, flipY, i * CELL, j * CELL, u, v);
      u = constrain(u, -8000.0f, 8000.0f) + 0.5f;   // +0.5: nearest pixel after truncation
      v = constrain(v, -8000.0f, 8000.0f) + 0.5f;
      nodes[j * NODES_X + i] = {(int32_t)(u * 65536.0f), (int32_t)(v * 65536.0f)};
    }
}

// Marks the cells whose source area overlaps r (in hudSrc coordinates).
static void markSourceRect(int rx, int ry, int rw, int rh) {
  int32_t x0 = rx - 1, y0 = ry - 1, x1 = rx + rw + 1, y1 = ry + rh + 1;
  for (int j = 0; j < CELLS_Y; j++)
    for (int i = 0; i < CELLS_X; i++) {
      if (cellNow[j][i]) continue;
      const Node& a = nodes[j * NODES_X + i];
      const Node& b = nodes[j * NODES_X + i + 1];
      const Node& c = nodes[(j + 1) * NODES_X + i];
      const Node& d = nodes[(j + 1) * NODES_X + i + 1];
      int32_t umin = min(min(a.u, b.u), min(c.u, d.u)) >> 16, umax = max(max(a.u, b.u), max(c.u, d.u)) >> 16;
      int32_t vmin = min(min(a.v, b.v), min(c.v, d.v)) >> 16, vmax = max(max(a.v, b.v), max(c.v, d.v)) >> 16;
      if (umax >= x0 && umin <= x1 && vmax >= y0 && vmin <= y1) cellNow[j][i] = 1;
    }
}

// Warps one cell from hudSrc into the frame buffer. The canvas frame is rotated 3, so
// landscape (x, y) is native row LCD_HEIGHT - 1 - x, column y: each x is one short run.
static void warpCell(uint16_t* fb, const uint8_t* src, int i, int j) {
  const Node a = nodes[j * NODES_X + i], b = nodes[j * NODES_X + i + 1];
  const Node c = nodes[(j + 1) * NODES_X + i], d = nodes[(j + 1) * NODES_X + i + 1];
  int32_t tdu = (b.u - a.u) / CELL, tdv = (b.v - a.v) / CELL;   // along the top edge
  int32_t bdu = (d.u - c.u) / CELL, bdv = (d.v - c.v) / CELL;   // along the bottom edge
  int x0 = i * CELL, y0 = j * CELL;
  int xn = min(CELL, WIDTH - x0), yn = min(CELL, HEIGHT - y0);
  for (int k = 0; k < xn; k++) {
    int32_t tu = a.u + tdu * k, tv = a.v + tdv * k;
    int32_t bu = c.u + bdu * k, bv = c.v + bdv * k;
    int32_t du = (bu - tu) / CELL, dv = (bv - tv) / CELL;
    uint16_t* out = fb + (LCD_HEIGHT - 1 - (x0 + k)) * LCD_WIDTH + y0;
    int32_t u = tu, v = tv;
    for (int y = 0; y < yn; y++) {
      int32_t su = u >> 16, sv = v >> 16;
      out[y] = ((uint32_t)su < (uint32_t)WIDTH && (uint32_t)sv < (uint32_t)HEIGHT) ? lut332[src[sv * WIDTH + su]] : 0;
      u += du;
      v += dv;
    }
  }
}

// ---- HUD layout (hudSrc coordinates, as the driver sees it) ----
static constexpr Region H_TOP   = {0, 0, WIDTH, 40};        // clock, GPS warning, messages
static constexpr Region H_GAUGE = {0, 266, WIDTH, HEIGHT - 266};
static constexpr int GAUGE_X = 60, GAUGE_W = WIDTH - 120, GAUGE_Y = 278, GAUGE_H = 22;
static constexpr int GAUGE_SEGMENTS = 24;

// Everything the HUD shows, reduced to what changes the picture, so small changes in the
// model (ring value in quarter units) only redraw when a pixel would change.
// The driving designs share one layout and are all drawn here: Clean (thin bar, day theme),
// Night (row of dots, night theme) and the HUD gauge design (bar / segments / off).
static constexpr uint8_t LOOK_HUD = 0xFF;    // HudFrame::look for the HUD gauge design
static constexpr int NIGHT_DOTS = 46;

struct HudFrame {
  uint8_t look;          // DESIGN_A, DESIGN_D or LOOK_HUD
  char speed[8];
  char clock[8];
  char toast[40];
  bool fix, gpsConnected, overLimit, night;
  int16_t fill;          // gauge fill in pixels, or lit segments / dots
  int16_t limitX;        // limit marker x, or -1
};

static HudFrame hudShown;
static bool hudShownValid = false;
static HudSettings hudShownSettings;
static uint8_t hudShownRotation = 0xFF;
static bool hudShownGrid = false;
static Overlay hudShownOverlay = Overlay::None;
static ThemeColours hudShownTheme;

static bool hasGauge(uint8_t look, const HudSettings& h) { return look != LOOK_HUD || h.gauge != HudGauge::Off; }
static int hudBaseline(uint8_t look, const HudSettings& h) { return hasGauge(look, h) ? 258 : 292; }
static Region hudDigitsRegion(uint8_t look, const HudSettings& h) {
  int top = 40, bottom = hasGauge(look, h) ? 266 : HEIGHT;
  return {0, (int16_t)top, WIDTH, (int16_t)(bottom - top)};
}

static HudFrame hudFrame(const UiModel& m, const HudSettings& h) {
  HudFrame f;
  memset(&f, 0, sizeof(f));
  if (m.fix && m.speed > 0) snprintf(f.speed, sizeof(f.speed), "%d", m.speed);
  else strcpy(f.speed, "--");
  if (h.showClock) strncpy(f.clock, m.clock, sizeof(f.clock) - 1);
  strncpy(f.toast, m.toast, sizeof(f.toast) - 1);
  f.fix = m.fix;
  f.gpsConnected = m.gpsConnected;
  f.overLimit = m.overLimit;
  f.night = m.night;
  f.limitX = -1;
  f.look = m.hud ? LOOK_HUD : m.design;
  if (m.ringMax > 0) {
    float frac = m.fix ? constrain(m.ringValue / m.ringMax, 0.0f, 1.0f) : 0;
    if (f.look == DESIGN_A) {
      f.fill = (int16_t)lroundf(frac * GAUGE_W);
    } else if (f.look == DESIGN_D) {
      f.fill = m.fix && m.ringValue > 0.5f ? min(NIGHT_DOTS, (int)(frac * (NIGHT_DOTS - 1)) + 1) : 0;
    } else if (h.gauge != HudGauge::Off) {
      f.fill = h.gauge == HudGauge::Bar ? (int16_t)lroundf(frac * GAUGE_W) : (int16_t)ceilf(frac * GAUGE_SEGMENTS - 0.01f);
      if (h.showLimit && m.limit > 0 && m.limit < m.ringMax) f.limitX = GAUGE_X + (int)lroundf(GAUGE_W * m.limit / m.ringMax);
    }
  }
  return f;
}

static void drawHud(const UiModel& m, const HudSettings& h, const HudFrame& f, bool grid) {
  // Colours from the day or night theme. The background stays black: black shows nothing
  // in the windscreen.
  const ThemeColours& t = f.night ? settings.night : settings.day;
  uint16_t fg = color565(t.speed);
  uint16_t dim = scaleColour(t.speed, 0.28f);
  uint16_t gaugeFg = color565(t.ringFill);
  uint16_t gaugeDim = scaleColour(t.ringFill, 0.28f);
  uint16_t alert = color565(t.alert);
  g->fillScreen(TFT_BLACK);

  if (grid) {
    // Calibration: straight, evenly spaced lines, a border and a circle. Adjust the
    // geometry until they look straight and square in the windscreen.
    for (int x = 0; x < WIDTH; x += 41) g->drawFastVLine(x, 0, HEIGHT, dim);
    for (int y = 0; y < HEIGHT; y += 40) g->drawFastHLine(0, y, WIDTH, dim);
    g->drawRect(0, 0, WIDTH, HEIGHT, fg);
    g->drawRect(1, 1, WIDTH - 2, HEIGHT - 2, fg);
    g->drawFastHLine(0, HEIGHT / 2, WIDTH, fg);
    g->drawFastVLine(WIDTH / 2, 0, HEIGHT, fg);
    g->drawCircle(WIDTH / 2, HEIGHT / 2, 120, fg);
    g->drawCircle(WIDTH / 2, HEIGHT / 2, 121, fg);
    drawText("TOP", WIDTH / 2, 20, &fTitle, fg, textdatum_t::middle_center);
    drawText("LEFT", 12, HEIGHT / 4, &fTitle, fg, textdatum_t::middle_left);
    drawText("RIGHT", WIDTH - 12, HEIGHT / 4, &fTitle, fg, textdatum_t::middle_right);
    return;
  }

  // Top line: clock (left), GPS warning (right), messages (centre)
  if (f.clock[0]) drawText(f.clock, 20, 20, &fTitle, color565(t.status), textdatum_t::middle_left);
  if (h.showGps && !f.fix)
    drawText(f.gpsConnected ? "NO GPS FIX" : "NO GPS", WIDTH - 20, 20, &fTitle, alert, textdatum_t::middle_right);
  if (f.toast[0]) drawText(f.toast, WIDTH / 2, 20, &fTitle, color565(t.ringFill), textdatum_t::middle_center);

  // Speed digits, centred; units to their right
  const lgfx::VLWfont* font = h.digitSize ? &fSpeedM : &fSpeed;
  uint16_t digits = !f.fix ? dim : (f.overLimit ? alert : fg);
  int base = hudBaseline(f.look, h);
  drawText(f.speed, WIDTH / 2, base, font, digits, textdatum_t::baseline_center);
  if (h.showUnits) {
    g->setFont(font);
    int half = g->textWidth(f.speed) / 2;
    drawText(m.units, WIDTH / 2 + half + 14, base, &fTitle, f.overLimit ? alert : color565(t.unit), textdatum_t::baseline_left);
  }

  // Gauge
  uint16_t fill = f.overLimit ? alert : gaugeFg;
  if (f.look == DESIGN_A) {
    g->fillRoundRect(GAUGE_X, 285, GAUGE_W, 8, 4, gaugeDim);
    if (f.fill >= 8) g->fillRoundRect(GAUGE_X, 285, f.fill, 8, 4, fill);
  } else if (f.look == DESIGN_D) {
    for (int i = 0; i < NIGHT_DOTS; i++) {
      bool on = i < f.fill;
      g->fillCircle(GAUGE_X + GAUGE_W * i / (NIGHT_DOTS - 1), 289, on ? 4 : 3, on ? fill : gaugeDim);
    }
  } else if (h.gauge == HudGauge::Bar) {
    g->drawRect(GAUGE_X - 3, GAUGE_Y - 3, GAUGE_W + 6, GAUGE_H + 6, gaugeDim);
    g->drawRect(GAUGE_X - 2, GAUGE_Y - 2, GAUGE_W + 4, GAUGE_H + 4, gaugeDim);
    if (f.fill > 0) g->fillRect(GAUGE_X, GAUGE_Y, f.fill, GAUGE_H, fill);
  } else if (h.gauge == HudGauge::Segments) {
    int pitch = GAUGE_W / GAUGE_SEGMENTS, gap = 6;
    for (int i = 0; i < GAUGE_SEGMENTS; i++) {
      int x = GAUGE_X + i * pitch;
      if (i < f.fill) g->fillRect(x, GAUGE_Y, pitch - gap, GAUGE_H, fill);
      else g->drawRect(x, GAUGE_Y, pitch - gap, GAUGE_H, gaugeDim);
    }
  }
  if (f.limitX >= 0) {
    g->fillRect(f.limitX - 3, GAUGE_Y - 12, 6, GAUGE_H + 24, color565(t.limitMark));
  }
}

static bool hudGeometryEqual(const HudSettings& a, const HudSettings& b) {
  return a.mirrorV == b.mirrorV && a.mirrorH == b.mirrorH && a.scaleX == b.scaleX &&
         a.scaleY == b.scaleY && a.keystone == b.keystone && a.taper == b.taper &&
         a.bow == b.bow && a.skew == b.skew && a.rotateDeg == b.rotateDeg &&
         a.offsetX == b.offsetX && a.offsetY == b.offsetY;
}

// Draws the HUD (or an overlay through the HUD warp) and presents it. False if the HUD
// buffers could not be allocated.
static bool renderHud(const UiModel& m) {
  if (!hudReady) return false;
  const HudSettings h = settings.hud;   // copy: the web task may change it mid-frame
  HudFrame f = hudFrame(m, h);
  bool driving = m.overlay == Overlay::None;

  memset(cellNow, 0, sizeof(cellNow));
  bool all = false;
  if (!hudShownValid || !hudGeometryEqual(h, hudShownSettings) || settings.rotation != hudShownRotation) {
    buildWarp(h, settings.rotation);
    all = true;
  }

  g = &hudSrc;
  bool fullSource = all || !driving || hudShownOverlay != Overlay::None || m.testGrid != hudShownGrid ||
                    memcmp(&h, &hudShownSettings, sizeof(h)) || strcmp(m.units, shown.units) ||
                    f.night != hudShown.night || f.look != hudShown.look || memcmp(&activeTheme(m), &hudShownTheme, sizeof(ThemeColours));
  if (!driving) {
    switch (m.overlay) {
      case Overlay::Settings: drawSettings(m); break;
      case Overlay::Status: drawStatus(m); break;
      case Overlay::Update: drawUpdate(m); break;
      default: drawSettings(m); break;
    }
    all = true;
  } else if (fullSource) {
    drawHud(m, h, f, m.testGrid);
    all = true;
  } else if (!m.testGrid) {
    const HudFrame& o = hudShown;
    auto redraw = [&](bool changed, const Region& r) {
      if (!changed) return;
      g->setClipRect(r.x, r.y, r.w, r.h);
      drawHud(m, h, f, false);
      g->clearClipRect();
      markSourceRect(r.x, r.y, r.w, r.h);
    };
    redraw(strcmp(f.clock, o.clock) || strcmp(f.toast, o.toast) || f.fix != o.fix ||
           f.gpsConnected != o.gpsConnected, H_TOP);
    redraw(strcmp(f.speed, o.speed) || f.fix != o.fix || f.overLimit != o.overLimit, hudDigitsRegion(f.look, h));
    if (hasGauge(f.look, h))
      redraw(f.fill != o.fill || f.limitX != o.limitX || f.overLimit != o.overLimit, H_GAUGE);
  }
  g = &canvas;

  bool any = all;
  if (all) memset(cellNow, 1, sizeof(cellNow));
  else for (int j = 0; j < CELLS_Y && !any; j++) for (int i = 0; i < CELLS_X; i++) if (cellNow[j][i]) { any = true; break; }

  if (any) {
    // The back buffer holds the frame before last: warp the cells that changed then and now.
    uint16_t* back = boardBackBuffer();
    const uint8_t* src = (const uint8_t*)hudSrc.getBuffer();
    for (int j = 0; j < CELLS_Y; j++)
      for (int i = 0; i < CELLS_X; i++)
        if (cellNow[j][i] || cellLast[j][i]) warpCell(back, src, i, j);
    boardPresent();
    memcpy(cellLast, cellNow, sizeof(cellLast));
  }

  hudShown = f;
  hudShownValid = true;
  hudShownSettings = h;
  hudShownRotation = settings.rotation;
  hudShownGrid = m.testGrid;
  hudShownOverlay = m.overlay;
  hudShownTheme = activeTheme(m);
  return true;
}

void uiBegin() {
  // Draws straight into the panel's frame buffers (see uiRender). They are in the panel's
  // native portrait layout, so the canvas is rotated to draw in landscape.
  canvas.setColorDepth(lgfx::color_depth_t::rgb565_nonswapped);
  wSpeed.set(font_speed, sizeof(font_speed));
  wSpeedM.set(font_speed_m, sizeof(font_speed_m));
  wLabel.set(font_label, sizeof(font_label));
  wSmall.set(font_small, sizeof(font_small));
  wTitle.set(font_title, sizeof(font_title));
  fSpeed.loadFont(&wSpeed);
  fSpeedM.loadFont(&wSpeedM);
  fLabel.loadFont(&wLabel);
  fSmall.loadFont(&wSmall);
  fTitle.loadFont(&wTitle);

  // HUD: source image in PSRAM (262 KB), warp grid in internal RAM if there is room (34 KB)
  hudSrc.setColorDepth(lgfx::color_depth_t::rgb332_1Byte);
  hudSrc.setPsram(true);
  nodes = (Node*)heap_caps_malloc(sizeof(Node) * NODES_X * NODES_Y, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!nodes) nodes = (Node*)heap_caps_malloc(sizeof(Node) * NODES_X * NODES_Y, MALLOC_CAP_8BIT);
  hudReady = nodes && hudSrc.createSprite(WIDTH, HEIGHT);
  if (!hudReady) Serial.println("[HUD] Not enough memory; using the dashboard designs");
  for (int c = 0; c < 256; c++) {
    int r = (c >> 5) & 7, gr = (c >> 2) & 7, b = c & 3;
    lut332[c] = (uint16_t)(((r * 31 / 7) << 11) | ((gr * 63 / 7) << 5) | (b * 31 / 3));
  }
}

// ---------------- unwarped screens ----------------
// The Wi-Fi setup page (so its QR code can be scanned from the screen), and everything if the
// HUD buffers could not be allocated. Always a full redraw: these change rarely.
static void renderDirect(const UiModel& model) {
  canvas.setBuffer(boardBackBuffer(), LCD_WIDTH, LCD_HEIGHT);
  canvas.setRotation(settings.rotation == 2 ? 1 : 3);
  g = &canvas;
  switch (model.overlay) {
    case Overlay::Settings: drawSettings(model); break;
    case Overlay::Status: drawStatus(model); break;
    case Overlay::WifiSetup: drawWifiSetup(model); break;
    case Overlay::Update: drawUpdate(model); break;
    default: {
      const HudSettings h = settings.hud;
      drawHud(model, h, hudFrame(model, h), model.testGrid);
      break;
    }
  }
  boardPresent();
}

void uiRender(const UiModel& model) {
  // Every driving design is drawn for the windscreen reflection (mirrored and warped).
  if (model.overlay == Overlay::WifiSetup || !renderHud(model)) {
    hudShownValid = false;   // this overwrites the frame buffers
    renderDirect(model);
  }
  shown = model;
}
