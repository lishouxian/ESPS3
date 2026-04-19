// ESPS3 — "Ink on Paper" edition, v2.
// Dashboard for mole system stats + Claude session state.
// Data arrives via USB-CDC as one JSON line per frame from tools/mole-bridge.py.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <U8g2_for_Adafruit_GFX.h>

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

DisplayPort          rlcd(PIN_LCD_MOSI, PIN_LCD_SCK, PIN_LCD_DC,
                          PIN_LCD_CS, PIN_LCD_RST, LCD_W, LCD_H);
RlcdGfx               gfx(rlcd, LCD_W, LCD_H);
U8G2_FOR_ADAFRUIT_GFX u8g2;

// ── Layout (y = baseline for u8g2 text, unless noted) ────────────────
constexpr int PAD_X                    = 10;

// Top strap
constexpr int Y_STRAP_BASELINE         = 14;
constexpr int Y_STRAP_RULE             = 22;

// Hero (CPU | MEM). Label's trailing rule floats above the label baseline.
constexpr int Y_HERO_LABEL             = 44;
constexpr int Y_HERO_LABEL_RULE        = 40;
constexpr int Y_HERO_NUMBER            = 76;
constexpr int Y_HERO_NUMBER_RULE       = 84;
constexpr int Y_HERO_BAR_TOP           = 88;
constexpr int HERO_BAR_H               = 8;
constexpr int Y_HERO_META              = 112;

// Network row (single serif line, no sparkline)
constexpr int Y_NET                    = 136;

// Claude Session block
constexpr int Y_CLAUDE_HDR             = 164;
constexpr int Y_CLAUDE_HDR_RULE        = 160;
constexpr int Y_CLAUDE_R1              = 184;
constexpr int Y_CLAUDE_R2              = 202;
constexpr int Y_CLAUDE_R3              = 220;

// Footer
constexpr int Y_FOOT_RULE              = 248;
constexpr int Y_FOOTER                 = 268;

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

// ── Drawing helpers ──────────────────────────────────────────────────
static inline void hline(int x0, int x1, int y) { gfx.drawLine(x0, y, x1, y, 1); }

static void progress_bar(int x, int y, int w, int h, int pct) {
  gfx.drawRect(x, y, w, h, 1);
  if (pct < 0) return;
  int fill = (w - 2) * constrain(pct, 0, 100) / 100;
  if (fill > 0) gfx.fillRect(x + 1, y + 1, fill, h - 2, 1);
}

// ── Drawing ──────────────────────────────────────────────────────────
static void draw_top_strap() {
  gfx.fillRect(0, 0, LCD_W, 28, 0);

  u8g2.setFont(u8g2_font_ncenR12_tf);
  u8g2.setCursor(PAD_X, Y_STRAP_BASELINE);
  u8g2.print(snap.hw);
  if (snap.up[0]) { u8g2.print("  \u00B7  up "); u8g2.print(snap.up); }

  hline(PAD_X, LCD_W - PAD_X - 1, Y_STRAP_RULE);
}

// Draws a hero half (CPU or MEM):
//   LABEL ─────────────
//   29%
//   ─────────────
//   [████████░░░░░░]
//   meta
static void draw_hero_half(int col_x, int col_w,
                           const char* label,
                           int pct,
                           const char* meta) {
  int x_right = col_x + col_w - PAD_X - 1;

  // Label (small caps tracked)
  u8g2.setFont(u8g2_font_helvB08_tf);
  u8g2.setCursor(col_x + PAD_X, Y_HERO_LABEL);
  u8g2.print(label);
  int lbl_w = u8g2.getUTF8Width(label);

  // Trailing rule to the column edge
  hline(col_x + PAD_X + lbl_w + 6, x_right, Y_HERO_LABEL_RULE);

  // Big number
  char num[8];
  if (pct < 0) snprintf(num, sizeof(num), "--");
  else         snprintf(num, sizeof(num), "%d", pct);
  u8g2.setFont(u8g2_font_ncenB24_tn);
  u8g2.setCursor(col_x + PAD_X, Y_HERO_NUMBER);
  u8g2.print(num);
  int numw = u8g2.getUTF8Width(num);

  // smaller "%"
  u8g2.setFont(u8g2_font_ncenB14_tr);
  u8g2.setCursor(col_x + PAD_X + numw + 3, Y_HERO_NUMBER);
  u8g2.print("%");

  // Thin rule right under number
  hline(col_x + PAD_X, x_right, Y_HERO_NUMBER_RULE);

  // Progress bar
  progress_bar(col_x + PAD_X, Y_HERO_BAR_TOP,
               col_w - 2 * PAD_X, HERO_BAR_H, pct);

  // Meta line (serif)
  u8g2.setFont(u8g2_font_ncenR10_tf);
  u8g2.setCursor(col_x + PAD_X, Y_HERO_META);
  u8g2.print(meta);
}

static void draw_hero_row() {
  gfx.fillRect(0, 28, LCD_W, Y_HERO_META + 6 - 28, 0);

  char cpu_meta[32];
  snprintf(cpu_meta, sizeof(cpu_meta), "load %.2f", snap.load1);
  draw_hero_half(0,   200, "CPU LOAD", snap.cpu_pct, cpu_meta);

  char mem_meta[32];
  snprintf(mem_meta, sizeof(mem_meta), "%.1f / %.1f GB",
           snap.mem_used, snap.mem_tot);
  draw_hero_half(200, 200, "MEMORY",   snap.mem_pct, mem_meta);
}

static void draw_net() {
  gfx.fillRect(0, Y_NET - 14, LCD_W, 20, 0);

  // Left: DOWN  0.00 MB/s
  u8g2.setFont(u8g2_font_helvB08_tf);
  u8g2.setCursor(PAD_X, Y_NET);
  u8g2.print("DOWN");

  u8g2.setFont(u8g2_font_ncenB12_tr);
  char rx[20]; snprintf(rx, sizeof(rx), "%.2f MB/s", snap.net_rx);
  u8g2.setCursor(PAD_X + 40, Y_NET);
  u8g2.print(rx);

  // Right: UP  0.00 MB/s
  u8g2.setFont(u8g2_font_helvB08_tf);
  u8g2.setCursor(200 + PAD_X, Y_NET);
  u8g2.print("UP");

  u8g2.setFont(u8g2_font_ncenB12_tr);
  char tx[20]; snprintf(tx, sizeof(tx), "%.2f MB/s", snap.net_tx);
  u8g2.setCursor(200 + PAD_X + 28, Y_NET);
  u8g2.print(tx);
}

static void draw_claude_row(int y_baseline, const char* key,
                            int pct, const char* reset_str) {
  u8g2.setFont(u8g2_font_ncenR10_tf);
  u8g2.setCursor(PAD_X + 4, y_baseline);
  u8g2.print(key);

  u8g2.setFont(u8g2_font_ncenB12_tr);
  char pbuf[8];
  if (pct < 0) snprintf(pbuf, sizeof(pbuf), "--");
  else         snprintf(pbuf, sizeof(pbuf), "%d%%", pct);
  u8g2.setCursor(PAD_X + 64, y_baseline);
  u8g2.print(pbuf);

  int bx = PAD_X + 108, bw = 180, by = y_baseline - 7;
  progress_bar(bx, by, bw, 6, pct);

  u8g2.setFont(u8g2_font_ncenR10_tf);
  const char* rs = (reset_str && reset_str[0]) ? reset_str : "\u2014";  // em dash
  int rw = u8g2.getUTF8Width(rs);
  u8g2.setCursor(LCD_W - PAD_X - rw, y_baseline);
  u8g2.print(rs);
}

static void draw_claude() {
  gfx.fillRect(0, Y_CLAUDE_HDR - 12, LCD_W,
               (Y_CLAUDE_R3 + 6) - (Y_CLAUDE_HDR - 12), 0);

  u8g2.setFont(u8g2_font_helvB08_tf);
  u8g2.setCursor(PAD_X, Y_CLAUDE_HDR);
  u8g2.print("CLAUDE SESSION");
  int hdrw = u8g2.getUTF8Width("CLAUDE SESSION");

  hline(PAD_X + hdrw + 8, LCD_W - PAD_X - 1, Y_CLAUDE_HDR_RULE);

  draw_claude_row(Y_CLAUDE_R1, "Context", snap.cld_ctx, nullptr);
  draw_claude_row(Y_CLAUDE_R2, "5 h",     snap.cld_5h,  snap.cld_5h_r);
  draw_claude_row(Y_CLAUDE_R3, "7 d",     snap.cld_7d,  snap.cld_7d_r);
}

static void draw_footer() {
  gfx.fillRect(0, Y_FOOT_RULE - 2, LCD_W, LCD_H - (Y_FOOT_RULE - 2), 0);
  hline(PAD_X, LCD_W - PAD_X - 1, Y_FOOT_RULE);

  // Left: DISK + bar
  u8g2.setFont(u8g2_font_helvB08_tf);
  u8g2.setCursor(PAD_X, Y_FOOTER);
  u8g2.print("DISK");

  u8g2.setFont(u8g2_font_ncenR10_tf);
  char dbuf[32];
  snprintf(dbuf, sizeof(dbuf), "%.0f / %.0f GB", snap.disk_used, snap.disk_tot);
  u8g2.setCursor(PAD_X + 32, Y_FOOTER);
  u8g2.print(dbuf);
  int dw = u8g2.getUTF8Width(dbuf);
  progress_bar(PAD_X + 32 + dw + 8, Y_FOOTER - 7, 50, 5, snap.disk_pct);

  // Right: "last Xs · by mole & xian"
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
  draw_net();
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

static void consume_line(const String& line) {
  if (line == "SHOT") { handle_shot(); return; }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line);
  if (err) {
    Serial.printf("JSON parse error: %s (len=%d)\n", err.c_str(), line.length());
    return;
  }
  apply_snapshot(doc);
  render_all();
}

static void pump_serial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      if (rx_buf.length() > 0) consume_line(rx_buf);
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

  render_all();
  Serial.println("[ESPS3] ready, awaiting JSON on stdin");
}

void loop() {
  pump_serial();
  static uint32_t last_footer_ms = 0;
  if (millis() - last_footer_ms > 1000) {
    last_footer_ms = millis();
    draw_footer();
    gfx.flush();
  }
}
