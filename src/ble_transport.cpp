#include "ble_transport.h"

#include <NimBLEDevice.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/stream_buffer.h>

namespace {

// Nordic UART Service — a de-facto standard for BLE "serial".
constexpr const char* NUS_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* NUS_RX_UUID      = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* NUS_TX_UUID      = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";

BleLineCallback       g_cb        = nullptr;
NimBLECharacteristic* g_tx_char   = nullptr;
volatile bool         g_connected = false;
char                  g_name[16]  = {0};
String                g_line_buf;
// BLE writes arrive on the NimBLE task; shuttle bytes through a stream
// buffer so line assembly + user callback run from loop() context.
StreamBufferHandle_t  g_rx_stream = nullptr;

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo&) override {
    g_connected = true;
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    g_connected = false;
    NimBLEDevice::startAdvertising();
  }
};

class RxCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    auto v = c->getValue();
    if (!g_rx_stream || v.size() == 0) return;
    xStreamBufferSend(g_rx_stream, v.data(), v.size(), 0);
  }
};

ServerCb g_server_cb;
RxCb     g_rx_cb;

}  // namespace

void ble_begin(BleLineCallback cb) {
  g_cb = cb;

  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(g_name, sizeof(g_name), "ESPS3-%02X%02X", mac[4], mac[5]);

  g_rx_stream = xStreamBufferCreate(4096, 1);

  NimBLEDevice::init(g_name);
  NimBLEDevice::setMTU(247);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&g_server_cb);

  NimBLEService* svc = server->createService(NUS_SERVICE_UUID);

  NimBLECharacteristic* rx = svc->createCharacteristic(
      NUS_RX_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(&g_rx_cb);

  g_tx_char = svc->createCharacteristic(
      NUS_TX_UUID,
      NIMBLE_PROPERTY::NOTIFY);

  svc->start();

  // Advertisement packet is capped at 31 bytes. Name + 128-bit service UUID
  // together overflow, so put the name in the primary advert and the service
  // UUID in the scan response (the central fetches both automatically).
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setName(g_name);

  NimBLEAdvertisementData scanData;
  scanData.addServiceUUID(NimBLEUUID(NUS_SERVICE_UUID));
  adv->setScanResponseData(scanData);
  adv->enableScanResponse(true);
  adv->start();
}

void ble_poll() {
  if (!g_rx_stream) return;
  uint8_t buf[128];
  for (;;) {
    size_t n = xStreamBufferReceive(g_rx_stream, buf, sizeof(buf), 0);
    if (n == 0) break;
    for (size_t i = 0; i < n; ++i) {
      char c = (char)buf[i];
      if (c == '\n') {
        if (g_line_buf.length() > 0 && g_cb) g_cb(g_line_buf);
        g_line_buf = "";
      } else if (c != '\r') {
        if (g_line_buf.length() < 4096) g_line_buf += c;
        else                             g_line_buf = "";
      }
    }
  }
}

bool ble_connected()            { return g_connected; }
const char* ble_advertised_name() { return g_name; }
