#include <Arduino.h>
#include "display_bsp.h"
#include "rlcd_gfx.h"

// ST7305 SPI pins — see docs/01-hardware.md
constexpr int PIN_LCD_MOSI = 12;
constexpr int PIN_LCD_SCK  = 11;
constexpr int PIN_LCD_DC   =  5;
constexpr int PIN_LCD_CS   = 40;
constexpr int PIN_LCD_RST  = 41;

constexpr int LCD_W = 400;
constexpr int LCD_H = 300;

DisplayPort rlcd(PIN_LCD_MOSI, PIN_LCD_SCK, PIN_LCD_DC,
                 PIN_LCD_CS, PIN_LCD_RST, LCD_W, LCD_H);
RlcdGfx gfx(rlcd, LCD_W, LCD_H);

static void draw_static_ui() {
  gfx.clear(ColorWhite);

  // Border
  gfx.drawRect(0, 0, LCD_W, LCD_H, 1);
  gfx.drawRect(1, 1, LCD_W - 2, LCD_H - 2, 1);

  // Title bar
  gfx.fillRect(0, 0, LCD_W, 40, 1);
  gfx.setTextColor(0);                 // white text on black bar
  gfx.setTextSize(3);
  gfx.setCursor(12, 10);
  gfx.print("ESPS3 / ST7305");

  // Sub-line: chip info
  gfx.setTextColor(1);                 // black text on white
  gfx.setTextSize(2);
  gfx.setCursor(12, 56);
  gfx.printf("Chip  : %s rev %d", ESP.getChipModel(), ESP.getChipRevision());
  gfx.setCursor(12, 76);
  gfx.printf("Flash : %lu MB", ESP.getFlashChipSize() / (1024UL * 1024UL));
  gfx.setCursor(12, 96);
  gfx.printf("PSRAM : %lu MB", ESP.getPsramSize() / (1024UL * 1024UL));
  gfx.setCursor(12, 116);
  gfx.printf("CPU   : %lu MHz", ESP.getCpuFreqMHz());

  // Divider
  gfx.drawLine(12, 150, LCD_W - 12, 150, 1);

  // Static caption for the counter
  gfx.setTextSize(2);
  gfx.setCursor(12, 164);
  gfx.print("Uptime:");
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== ESPS3 display+GFX demo ===");

  rlcd.RLCD_Init();
  draw_static_ui();
  gfx.flush();
  Serial.println("Static UI drawn.");
}

void loop() {
  static uint32_t last_ms = 0;
  uint32_t now = millis();
  if (now - last_ms < 1000) return;
  last_ms = now;

  uint32_t secs = now / 1000;
  uint32_t h = secs / 3600;
  uint32_t m = (secs / 60) % 60;
  uint32_t s = secs % 60;

  // Redraw just the counter region (clear a strip, then draw).
  gfx.fillRect(120, 160, LCD_W - 130, 50, 0);        // white
  gfx.setTextColor(1);
  gfx.setTextSize(4);
  gfx.setCursor(128, 168);
  gfx.printf("%02lu:%02lu:%02lu", h, m, s);

  uint32_t t0 = millis();
  gfx.flush();
  uint32_t t1 = millis();
  Serial.printf("tick %02lu:%02lu:%02lu  flush=%lu ms  free_heap=%lu\n",
                h, m, s, t1 - t0, (unsigned long)ESP.getFreeHeap());
}
