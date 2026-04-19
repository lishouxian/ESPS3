// ST7305 reflective LCD driver for Waveshare ESP32-S3-RLCD-4.2
// Adapted from https://github.com/waveshareteam/ESP32-S3-RLCD-4.2
//   - Simplified: only keeps the shift-based pixel addressing (no LUT).
//   - Landscape orientation (400 × 300) hard-wired.
#pragma once

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>

enum ColorSelection : uint8_t {
  ColorBlack = 0x00,
  ColorWhite = 0xFF,
};

class DisplayPort {
 public:
  DisplayPort(int mosi, int scl, int dc, int cs, int rst,
              int width, int height,
              spi_host_device_t spihost = SPI3_HOST);

  void RLCD_Init();
  void RLCD_ColorClear(uint8_t color);
  void RLCD_Display();
  void RLCD_SetPixel(uint16_t x, uint16_t y, uint8_t color);  // landscape

 private:
  void RLCD_Reset();
  void RLCD_SendCommand(uint8_t reg);
  void RLCD_SendData(uint8_t data);
  void RLCD_SendBuffer(uint8_t* data, int len);
  void SetResetIOLevel(uint8_t level);

  esp_lcd_panel_io_handle_t io_handle_ = nullptr;
  int mosi_, scl_, dc_, cs_, rst_;
  int width_, height_;
  uint8_t* disp_buffer_ = nullptr;
  int disp_len_ = 0;
};
