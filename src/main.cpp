// ESPS3 — "Ink on Paper" edition, v2.
// Dashboard for mole system stats + Claude session state.
// Data arrives via USB-CDC as one JSON line per frame from tools/mole-bridge.py.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "ble_transport.h"
#include "display_bsp.h"
#include "rlcd_gfx.h"

// ── Pin map ──────────────────────────────────────────────────────────
constexpr int PIN_LCD_MOSI = 12;
constexpr int PIN_LCD_SCK  = 11;
constexpr int PIN_LCD_DC   =  5;
constexpr int PIN_LCD_CS   = 40;
constexpr int PIN_LCD_RST  = 41;
constexpr int LCD_W = 400;
constexpr int LCD_H = 300;

// Battery voltage on ADC1_CH3 (GPIO4), per docs/01-hardware.md. Measured
// empirically: a full-looking cell reads ~1.4 V at the pin, so the board
// uses a ~3:1 divider (200k + 100k, typical for 1S-LiPo sense on 3.3V ADC).
constexpr int   PIN_BATT_ADC = 4;
constexpr float BATT_DIV     = 3.0f;   // V_bat = V_adc * BATT_DIV
// Curve is a straight-line approximation of a LiPo/Li-ion discharge.
constexpr int   BATT_MV_EMPTY = 3300;  // treat as 0%
constexpr int   BATT_MV_FULL  = 4200;  // treat as 100%
// Below this we assume the ADC pin is floating (no cell installed).
constexpr int   BATT_MV_PRESENT = 2500;

DisplayPort          rlcd(PIN_LCD_MOSI, PIN_LCD_SCK, PIN_LCD_DC,
                          PIN_LCD_CS, PIN_LCD_RST, LCD_W, LCD_H);
RlcdGfx               gfx(rlcd, LCD_W, LCD_H);
U8G2_FOR_ADAFRUIT_GFX u8g2;

// ── Layout (y = baseline for u8g2 text, unless noted) ────────────────
constexpr int PAD_X                    = 10;

// Top strap (no underline rule — cleaner)
constexpr int Y_STRAP_BASELINE         = 20;

// Hero (CPU | MEM). No lines anywhere — typography + whitespace carry the
// structure. Bigger label (helvB12), thicker progress bar (14 px).
constexpr int Y_HERO_LABEL             = 56;
constexpr int Y_HERO_NUMBER            = 100;
constexpr int Y_HERO_BAR_TOP           = 110;
constexpr int HERO_BAR_H               = 14;

// Claude Session block — row spacing 32 px, bigger key/value fonts, 8-px bar.
constexpr int Y_CLAUDE_HDR             = 166;
constexpr int Y_CLAUDE_R1              = 196;
constexpr int Y_CLAUDE_R2              = 228;
constexpr int Y_CLAUDE_R3              = 260;

// Footer — sits close to the bottom edge, no line above it.
constexpr int Y_FOOTER                 = 294;

// ── Snapshot ─────────────────────────────────────────────────────────
struct Snapshot {
  char hw[96]    = "Mac mini ...";
  char up[24]    = "";

  int   cpu_pct  = -1;
  float load1    = 0;

  int   mem_pct  = -1;
  float mem_used = 0;
  float mem_tot  = 0;

  float net_rx   = 0;
  float net_tx   = 0;

  int   disk_pct   = -1;
  float disk_used  = 0;
  float disk_tot   = 0;

  int   cld_ctx  = -1;
  int   cld_5h   = -1;
  int   cld_7d   = -1;
  char  cld_5h_r[12] = "";
  char  cld_7d_r[12] = "";

  uint32_t last_update_ms = 0;
} snap;

// ── Board-local state (battery, active transport) ────────────────────
struct Battery {
  int mv  = -1;   // last reading in millivolts, -1 = never read
  int pct = -1;   // 0..100, or -1 = not present / unknown
} batt;

enum class Src { USB, BLE };
static Src      last_src    = Src::USB;
static uint32_t last_src_ms = 0;

// ── Drawing helpers ──────────────────────────────────────────────────
static void progress_bar(int x, int y, int w, int h, int pct) {
  gfx.drawRect(x, y, w, h, 1);
  if (pct < 0) return;
  int fill = (w - 2) * constrain(pct, 0, 100) / 100;
  if (fill > 0) gfx.fillRect(x + 1, y + 1, fill, h - 2, 1);
}

// ── Battery + transport status ───────────────────────────────────────
static void update_battery() {
  constexpr int N = 16;  // oversample to knock down ADC noise
  uint32_t sum = 0;
  for (int i = 0; i < N; ++i) sum += analogReadMilliVolts(PIN_BATT_ADC);
  int adc_mv = sum / N;
  int vbat   = (int)(adc_mv * BATT_DIV);
  batt.mv = vbat;

  if (vbat < BATT_MV_PRESENT) { batt.pct = -1; return; }
  int pct = (vbat - BATT_MV_EMPTY) * 100 / (BATT_MV_FULL - BATT_MV_EMPTY);
  batt.pct = constrain(pct, 0, 100);
}

// Name the transport that most recently delivered a data line, or the one
// currently connected. Returns nullptr when there's nothing to show.
static const char* active_transport() {
  uint32_t age = millis() - last_src_ms;
  if (age < 5000) return (last_src == Src::BLE) ? "BLE" : "USB";
  if (ble_connected()) return "BLE";
  return nullptr;
}

// A 1S lithium cell can't hold >4.1 V open-circuit, so seeing that on VBAT
// means current is flowing into it — i.e. USB power + charger are active.
static inline bool charging_hint() { return batt.mv > 4150; }

// ── Icons ────────────────────────────────────────────────────────────
// Small zigzag bolt, 4 wide × 7 tall. Drawn in `color` so it can appear
// either as black ink on white paper, or punched through a filled battery
// body (color 0 = erase).
static void draw_bolt(int x, int y, int color) {
  gfx.drawLine(x+2, y+0, x+3, y+0, color);
  gfx.drawLine(x+1, y+1, x+3, y+1, color);
  gfx.drawLine(x+0, y+2, x+3, y+2, color);
  gfx.drawLine(x+0, y+3, x+2, y+3, color);
  gfx.drawLine(x+1, y+4, x+3, y+4, color);
  gfx.drawLine(x+0, y+5, x+2, y+5, color);
  gfx.drawPixel(x+0, y+6, color);
}

// iOS-style battery pill: 15 × 9 body, 2 × 3 terminal nub (total 17 wide).
// Charging → solid black fill with a white bolt punched through, same
// idiom as macOS / iOS so it reads instantly.
static void draw_batt_icon(int x, int y, int pct, bool charging) {
  constexpr int BW = 15, BH = 9;
  gfx.drawRect(x, y, BW, BH, 1);
  gfx.fillRect(x + BW, y + 3, 2, 3, 1);

  if (charging) {
    gfx.fillRect(x + 1, y + 1, BW - 2, BH - 2, 1);
    // Bolt nicely centered inside the body.
    draw_bolt(x + (BW - 4) / 2, y + 1, 0);
  } else {
    int inner = BW - 4;
    int fill  = inner * constrain(pct, 0, 100) / 100;
    if (fill > 0) gfx.fillRect(x + 2, y + 2, fill, BH - 4, 1);
  }
}

// ── Drawing ──────────────────────────────────────────────────────────
static void draw_top_strap() {
  gfx.fillRect(0, 0, LCD_W, 30, 0);

  u8g2.setFont(u8g2_font_ncenR12_tf);
  u8g2.setCursor(PAD_X, Y_STRAP_BASELINE);
  u8g2.print(snap.hw);

  // Right cluster: right-aligned. Compose
  //     "<transport> \u00B7 " [battery-icon] "<n>%"
  // The bolt lives *inside* the battery when charging (iOS idiom), so
  // there's no separate lightning glyph competing for horizontal space.
  const char* tp       = active_transport();
  bool        has_cell = batt.pct >= 0;

  char prefix[16] = {0};
  if (tp) snprintf(prefix, sizeof(prefix), "%s \u00B7 ", tp);

  char pct_txt[8] = {0};
  if (has_cell) snprintf(pct_txt, sizeof(pct_txt), "%d%%", batt.pct);

  constexpr int BATT_W = 17;  // 15 body + 2 terminal nub
  constexpr int GAP    = 4;

  int prefix_w = prefix[0]  ? u8g2.getUTF8Width(prefix)  : 0;
  int pct_w    = pct_txt[0] ? u8g2.getUTF8Width(pct_txt) : 0;
  int icon_w   = has_cell   ? BATT_W + GAP               : 0;

  int total = prefix_w + icon_w + pct_w;
  if (total == 0) return;

  int x = LCD_W - PAD_X - total;
  if (prefix[0]) {
    u8g2.setCursor(x, Y_STRAP_BASELINE);
    u8g2.print(prefix);
    x += prefix_w;
  }
  if (has_cell) {
    draw_batt_icon(x, Y_STRAP_BASELINE - 12, batt.pct, charging_hint());
    x += icon_w;
  }
  if (pct_txt[0]) {
    u8g2.setCursor(x, Y_STRAP_BASELINE);
    u8g2.print(pct_txt);
  }
}

// Draws a hero half (CPU or MEM):
//   LABEL ─────────────
//   29%
//   [████████░░░░░░]
static void draw_hero_half(int col_x, int col_w,
                           const char* label, int pct) {
  // Label (helvB12, no trailing rule — the label carries its own weight)
  u8g2.setFont(u8g2_font_helvB12_tf);
  u8g2.setCursor(col_x + PAD_X, Y_HERO_LABEL);
  u8g2.print(label);

  // Big number
  char num[8];
  if (pct < 0) snprintf(num, sizeof(num), "--");
  else         snprintf(num, sizeof(num), "%d", pct);
  u8g2.setFont(u8g2_font_ncenB24_tn);
  u8g2.setCursor(col_x + PAD_X, Y_HERO_NUMBER);
  u8g2.print(num);
  int numw = u8g2.getUTF8Width(num);

  // larger "%"
  u8g2.setFont(u8g2_font_ncenB18_tr);
  u8g2.setCursor(col_x + PAD_X + numw + 3, Y_HERO_NUMBER);
  u8g2.print("%");

  // Progress bar
  progress_bar(col_x + PAD_X, Y_HERO_BAR_TOP,
               col_w - 2 * PAD_X, HERO_BAR_H, pct);
}

static void draw_hero_row() {
  gfx.fillRect(0, 28, LCD_W, (Y_HERO_BAR_TOP + HERO_BAR_H + 4) - 28, 0);
  draw_hero_half(0,   200, "CPU LOAD", snap.cpu_pct);
  draw_hero_half(200, 200, "MEMORY",   snap.mem_pct);
}

static void draw_claude_row(int y_baseline, const char* key,
                            int pct, const char* reset_str) {
  u8g2.setFont(u8g2_font_ncenR12_tf);
  u8g2.setCursor(PAD_X + 4, y_baseline);
  u8g2.print(key);

  u8g2.setFont(u8g2_font_ncenB14_tr);
  char pbuf[8];
  if (pct < 0) snprintf(pbuf, sizeof(pbuf), "--");
  else         snprintf(pbuf, sizeof(pbuf), "%d%%", pct);
  u8g2.setCursor(PAD_X + 72, y_baseline);
  u8g2.print(pbuf);

  int bx = PAD_X + 120, bw = 170, by = y_baseline - 9;
  progress_bar(bx, by, bw, 10, pct);

  u8g2.setFont(u8g2_font_ncenR12_tf);
  const char* rs = (reset_str && reset_str[0]) ? reset_str : "\u2014";  // em dash
  int rw = u8g2.getUTF8Width(rs);
  u8g2.setCursor(LCD_W - PAD_X - rw, y_baseline);
  u8g2.print(rs);
}

static void draw_claude() {
  gfx.fillRect(0, Y_CLAUDE_HDR - 14, LCD_W,
               (Y_CLAUDE_R3 + 6) - (Y_CLAUDE_HDR - 14), 0);

  u8g2.setFont(u8g2_font_helvB12_tf);
  u8g2.setCursor(PAD_X, Y_CLAUDE_HDR);
  u8g2.print("CLAUDE SESSION");

  draw_claude_row(Y_CLAUDE_R1, "Context", snap.cld_ctx, nullptr);
  draw_claude_row(Y_CLAUDE_R2, "5 h",     snap.cld_5h,  snap.cld_5h_r);
  draw_claude_row(Y_CLAUDE_R3, "7 d",     snap.cld_7d,  snap.cld_7d_r);
}

static void draw_footer() {
  gfx.fillRect(0, Y_FOOTER - 14, LCD_W, LCD_H - (Y_FOOTER - 14), 0);

  // Left: DISK <used> / <total> GB — no bar, just the numbers, no rule above.
  u8g2.setFont(u8g2_font_helvB12_tf);
  u8g2.setCursor(PAD_X, Y_FOOTER);
  u8g2.print("DISK");

  u8g2.setFont(u8g2_font_ncenR12_tf);
  char dbuf[32];
  snprintf(dbuf, sizeof(dbuf), "%.0f / %.0f GB", snap.disk_used, snap.disk_tot);
  u8g2.setCursor(PAD_X + 44, Y_FOOTER);
  u8g2.print(dbuf);

  // Right: "last Xs · by mole & xian"
  u8g2.setFont(u8g2_font_ncenR12_tf);
  char buf[64];
  if (snap.last_update_ms == 0)
    snprintf(buf, sizeof(buf), "waiting  \u00B7  by mole & xian");
  else {
    uint32_t age = (millis() - snap.last_update_ms) / 1000;
    snprintf(buf, sizeof(buf), "last %lus  \u00B7  by mole & xian",
             (unsigned long)age);
  }
  int w = u8g2.getUTF8Width(buf);
  u8g2.setCursor(LCD_W - PAD_X - w, Y_FOOTER);
  u8g2.print(buf);
}

static void render_all() {
  gfx.fillRect(0, 0, LCD_W, LCD_H, 0);
  draw_top_strap();
  draw_hero_row();
  draw_claude();
  draw_footer();
  gfx.flush();
}

// ── JSON ingest ──────────────────────────────────────────────────────
static String rx_buf;

static void copy_clipped(char* dst, size_t cap, const char* src) {
  if (!src) { dst[0] = 0; return; }
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = 0;
}

static void apply_snapshot(JsonDocument& doc) {
  copy_clipped(snap.hw, sizeof(snap.hw), doc["hw"] | "");
  copy_clipped(snap.up, sizeof(snap.up), doc["up"] | "");

  JsonObjectConst cpu = doc["cpu"];
  snap.cpu_pct = cpu["pct"]  | -1;
  snap.load1   = cpu["load"][0] | 0.0f;

  JsonObjectConst mem = doc["mem"];
  snap.mem_pct  = mem["pct"]      | -1;
  snap.mem_used = mem["used_gb"]  | 0.0f;
  snap.mem_tot  = mem["total_gb"] | 0.0f;

  JsonObjectConst net = doc["net"];
  snap.net_rx = net["rx"] | 0.0f;
  snap.net_tx = net["tx"] | 0.0f;

  JsonObjectConst disk = doc["disk"];
  snap.disk_pct  = disk["pct"]      | -1;
  snap.disk_used = disk["used_gb"]  | 0.0f;
  snap.disk_tot  = disk["total_gb"] | 0.0f;

  JsonObjectConst cld = doc["claude"];
  snap.cld_ctx = cld["ctx"] | -1;
  snap.cld_5h  = cld["h5"]  | -1;
  snap.cld_7d  = cld["d7"]  | -1;
  copy_clipped(snap.cld_5h_r, sizeof(snap.cld_5h_r), cld["h5_r"] | "");
  copy_clipped(snap.cld_7d_r, sizeof(snap.cld_7d_r), cld["d7_r"] | "");

  snap.last_update_ms = millis();
}

// Dump current framebuffer over USB-CDC in a framed binary envelope so a
// host tool can reconstruct an exact PNG of what's on screen.
static void handle_shot() {
  Serial.printf("SHOT-BEGIN %d %d %d\n",
                rlcd.buffer_len(), rlcd.width(), rlcd.height());
  Serial.flush();
  Serial.write(rlcd.buffer(), rlcd.buffer_len());
  Serial.flush();
  Serial.println();
  Serial.println("SHOT-END");
  Serial.flush();
}

static void consume_line_impl(const String& line, Src src) {
  if (line == "SHOT") { handle_shot(); return; }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    Serial.printf("JSON parse error: %s (len=%d)\n", err.c_str(), line.length());
    return;
  }
  last_src    = src;
  last_src_ms = millis();
  apply_snapshot(doc);
  render_all();
}

static void consume_line_from_usb(const String& line) {
  consume_line_impl(line, Src::USB);
}

static void consume_line_from_ble(const String& line) {
  consume_line_impl(line, Src::BLE);
}

static void pump_serial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      if (rx_buf.length() > 0) consume_line_from_usb(rx_buf);
      rx_buf = "";
    } else if (c != '\r') {
      if (rx_buf.length() < 4096) rx_buf += c;
      else rx_buf = "";
    }
  }
}

// ── Arduino entry points ─────────────────────────────────────────────
void setup() {
  Serial.setRxBufferSize(4096);
  Serial.begin(115200);
  delay(300);
  Serial.println("\n[ESPS3] ink-on-paper v2");

  rlcd.RLCD_Init();
  u8g2.begin(gfx);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(1);
  u8g2.setBackgroundColor(0);
  u8g2.setFontDirection(0);

  // Prime the battery reading so the strap comes up with a real number.
  analogReadResolution(12);  // default but make it explicit
  update_battery();

  render_all();

  // Bring up BLE last — splash is already on screen, and ble_begin() takes
  // a few hundred ms for NimBLE stack init.
  ble_begin(consume_line_from_ble);
  Serial.printf("[ESPS3] BLE up as \"%s\"\n", ble_advertised_name());

  Serial.println("[ESPS3] ready, awaiting JSON on stdin");
}

void loop() {
  pump_serial();
  ble_poll();

  uint32_t now = millis();

  // Footer "last Xs ..." tick — once per second.
  static uint32_t last_footer_ms = 0;
  if (now - last_footer_ms > 1000) {
    last_footer_ms = now;
    draw_footer();
    gfx.flush();
  }

  // Battery + strap tick — 5s. Battery changes slowly; re-reading the ADC
  // this often is essentially free and keeps the displayed % fresh.
  static uint32_t last_batt_ms = 0;
  if (now - last_batt_ms > 5000) {
    last_batt_ms = now;
    update_battery();
    draw_top_strap();
    gfx.flush();
  }

  // Re-render the strap immediately when BLE (dis)connects so the transport
  // tag flips without waiting for the 5s tick.
  static bool last_ble = false;
  bool now_ble = ble_connected();
  if (now_ble != last_ble) {
    last_ble = now_ble;
    draw_top_strap();
    gfx.flush();
  }
}
