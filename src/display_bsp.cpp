// ST7305 reflective LCD driver — see display_bsp.h for attribution.
#include "display_bsp.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <esp_heap_caps.h>
#include <esp_err.h>

DisplayPort::DisplayPort(int mosi, int scl, int dc, int cs, int rst,
                         int width, int height, spi_host_device_t spihost)
    : mosi_(mosi), scl_(scl), dc_(dc), cs_(cs), rst_(rst),
      width_(width), height_(height) {
  int transfer = width_ * height_;

  spi_bus_config_t buscfg = {};
  buscfg.miso_io_num = -1;
  buscfg.mosi_io_num = mosi_;
  buscfg.sclk_io_num = scl_;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = transfer;
  ESP_ERROR_CHECK(spi_bus_initialize(spihost, &buscfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t io_cfg = {};
  io_cfg.dc_gpio_num = dc_;
  io_cfg.cs_gpio_num = cs_;
  io_cfg.pclk_hz = 10 * 1000 * 1000;  // 10 MHz
  io_cfg.lcd_cmd_bits = 8;
  io_cfg.lcd_param_bits = 8;
  io_cfg.spi_mode = 0;
  io_cfg.trans_queue_depth = 10;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
      (esp_lcd_spi_bus_handle_t)spihost, &io_cfg, &io_handle_));

  gpio_config_t gpio_conf = {};
  gpio_conf.intr_type = GPIO_INTR_DISABLE;
  gpio_conf.mode = GPIO_MODE_OUTPUT;
  gpio_conf.pin_bit_mask = (1ULL << rst_);
  gpio_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&gpio_conf));
  SetResetIOLevel(1);

  disp_len_ = transfer >> 3;  // 1 bit per pixel
  disp_buffer_ = (uint8_t*)heap_caps_malloc(disp_len_, MALLOC_CAP_DMA);
  assert(disp_buffer_);
}

void DisplayPort::RLCD_Reset() {
  SetResetIOLevel(1); vTaskDelay(pdMS_TO_TICKS(50));
  SetResetIOLevel(0); vTaskDelay(pdMS_TO_TICKS(20));
  SetResetIOLevel(1); vTaskDelay(pdMS_TO_TICKS(50));
}

void DisplayPort::SetResetIOLevel(uint8_t level) {
  gpio_set_level((gpio_num_t)rst_, level ? 1 : 0);
}

void DisplayPort::RLCD_SendCommand(uint8_t reg) {
  ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle_, reg, nullptr, 0));
}

void DisplayPort::RLCD_SendData(uint8_t data) {
  ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io_handle_, -1, &data, 1));
}

void DisplayPort::RLCD_SendBuffer(uint8_t* data, int len) {
  ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(io_handle_, -1, data, len));
}

// Init sequence from Waveshare demo — ST7305 magic registers.
void DisplayPort::RLCD_Init() {
  RLCD_Reset();

  RLCD_SendCommand(0xD6); RLCD_SendData(0x17); RLCD_SendData(0x02);       // NVM Load
  RLCD_SendCommand(0xD1); RLCD_SendData(0x01);                            // Booster Enable
  RLCD_SendCommand(0xC0); RLCD_SendData(0x11); RLCD_SendData(0x04);       // Gate Voltage
  RLCD_SendCommand(0xC1);                                                  // VSHP
  RLCD_SendData(0x69); RLCD_SendData(0x69); RLCD_SendData(0x69); RLCD_SendData(0x69);
  RLCD_SendCommand(0xC2);
  RLCD_SendData(0x19); RLCD_SendData(0x19); RLCD_SendData(0x19); RLCD_SendData(0x19);
  RLCD_SendCommand(0xC4);
  RLCD_SendData(0x4B); RLCD_SendData(0x4B); RLCD_SendData(0x4B); RLCD_SendData(0x4B);
  RLCD_SendCommand(0xC5);
  RLCD_SendData(0x19); RLCD_SendData(0x19); RLCD_SendData(0x19); RLCD_SendData(0x19);
  RLCD_SendCommand(0xD8); RLCD_SendData(0x80); RLCD_SendData(0xE9);
  RLCD_SendCommand(0xB2); RLCD_SendData(0x02);
  RLCD_SendCommand(0xB3);
  RLCD_SendData(0xE5); RLCD_SendData(0xF6); RLCD_SendData(0x05); RLCD_SendData(0x46);
  RLCD_SendData(0x77); RLCD_SendData(0x77); RLCD_SendData(0x77); RLCD_SendData(0x77);
  RLCD_SendData(0x76); RLCD_SendData(0x45);
  RLCD_SendCommand(0xB4);
  RLCD_SendData(0x05); RLCD_SendData(0x46); RLCD_SendData(0x77); RLCD_SendData(0x77);
  RLCD_SendData(0x77); RLCD_SendData(0x77); RLCD_SendData(0x76); RLCD_SendData(0x45);
  RLCD_SendCommand(0x62); RLCD_SendData(0x32); RLCD_SendData(0x03); RLCD_SendData(0x1F);
  RLCD_SendCommand(0xB7); RLCD_SendData(0x13);
  RLCD_SendCommand(0xB0); RLCD_SendData(0x64);
  RLCD_SendCommand(0x11);                                                  // Sleep Out
  vTaskDelay(pdMS_TO_TICKS(200));
  RLCD_SendCommand(0xC9); RLCD_SendData(0x00);
  RLCD_SendCommand(0x36); RLCD_SendData(0x48);                             // MADCTL
  RLCD_SendCommand(0x3A); RLCD_SendData(0x11);                             // Color format
  RLCD_SendCommand(0xB9); RLCD_SendData(0x20);
  RLCD_SendCommand(0xB8); RLCD_SendData(0x29);
  RLCD_SendCommand(0x21);                                                  // Display Inversion On
  RLCD_SendCommand(0x2A); RLCD_SendData(0x12); RLCD_SendData(0x2A);        // CASET
  RLCD_SendCommand(0x2B); RLCD_SendData(0x00); RLCD_SendData(0xC7);        // RASET
  RLCD_SendCommand(0x35); RLCD_SendData(0x00);
  RLCD_SendCommand(0xD0); RLCD_SendData(0xFF);
  RLCD_SendCommand(0x38);                                                  // Idle off
  RLCD_SendCommand(0x29);                                                  // Display ON

  RLCD_ColorClear(ColorWhite);
}

void DisplayPort::RLCD_ColorClear(uint8_t color) {
  memset(disp_buffer_, color, disp_len_);
}

void DisplayPort::RLCD_Display() {
  RLCD_SendCommand(0x2A); RLCD_SendData(0x12); RLCD_SendData(0x2A);
  RLCD_SendCommand(0x2B); RLCD_SendData(0x00); RLCD_SendData(0xC7);
  RLCD_SendCommand(0x2C);
  RLCD_SendBuffer(disp_buffer_, disp_len_);
}

// Landscape 400×300 pixel addressing (shift-based, no LUT).
void DisplayPort::RLCD_SetPixel(uint16_t x, uint16_t y, uint8_t color) {
  if (x >= width_ || y >= height_) return;

  uint16_t inv_y = (height_ - 1 - y);
  const uint16_t H4 = height_ >> 2;
  uint16_t byte_x  = x >> 1;
  uint16_t block_y = inv_y >> 2;
  uint32_t index   = byte_x * H4 + block_y;
  uint8_t  local_x = x & 0x01;
  uint8_t  local_y = inv_y & 0x03;
  uint8_t  bit     = 7 - ((local_y << 1) | local_x);
  uint8_t  mask    = 1 << bit;

  if (color) disp_buffer_[index] |= mask;
  else       disp_buffer_[index] &= ~mask;
}
