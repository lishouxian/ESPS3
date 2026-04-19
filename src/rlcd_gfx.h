// Thin Adafruit_GFX adapter over ST7305 DisplayPort.
// Gives us text, lines, shapes, bitmaps via the Adafruit GFX API while
// keeping our low-level SPI driver unchanged.
#pragma once

#include <Adafruit_GFX.h>
#include "display_bsp.h"

class RlcdGfx : public Adafruit_GFX {
 public:
  RlcdGfx(DisplayPort& port, int16_t w, int16_t h)
      : Adafruit_GFX(w, h), port_(port) {}

  // Adafruit_GFX fills black/white via 16-bit color; treat non-zero as black.
  // (ColorBlack = 0 draws ink, ColorWhite = 0xFF clears.)
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    port_.RLCD_SetPixel(x, y, color ? ColorBlack : ColorWhite);
  }

  void clear(uint8_t color = ColorWhite) { port_.RLCD_ColorClear(color); }
  void flush() { port_.RLCD_Display(); }

 private:
  DisplayPort& port_;
};
