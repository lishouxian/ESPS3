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
NimBLEServer*         g_server    = nullptr;
volatile bool         g_connected = false;
char                  g_name[16]  = {0};
String                g_line_buf;
// BLE writes arrive on the NimBLE task; shuttle bytes through a stream
// buffer so line assembly + user callback run from loop() context.
StreamBufferHandle_t  g_rx_stream = nullptr;

volatile uint32_t g_last_rx_ms  = 0;   // millis() of most recent rx
uint16_t          g_last_handle = 0;

// Kill a connection that hasn't delivered any data for this long. Normal
// bridge cadence is 2 s/frame; this threshold gives ~7 missed frames of
// slack (mole occasionally stalls) before we suspect the central has gone
// silent without dropping the LL link — the macOS-lid-close case, where
// Bluetooth keeps the radio-layer alive but the app side is frozen.
constexpr uint32_t IDLE_DISCONNECT_MS = 15000;

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    g_connected = true;
    g_last_handle = info.getConnHandle();
    // Arm the idle watchdog against this new connection. Without this, the
    // stale timestamp from a previous session would fire immediately.
    g_last_rx_ms = millis();
    Serial.printf("[BLE] connect handle=%u peer=%s\n",
                  (unsigned)g_last_handle,
                  info.getAddress().toString().c_str());
    // Coax the central into a slow connection interval. The dashboard only
    // pushes a frame every 2 s, so 300–600 ms between connection events is
    // imperceptible to the user but lets the radio sleep much longer per
    // cycle. Supervision timeout = 4 s (well above the ~1.2 s minimum for
    // max_interval=480, latency=0).
    server->updateConnParams(info.getConnHandle(),
                             240,   // min interval = 300 ms (1.25 ms units)
                             480,   // max interval = 600 ms
                             0,     // slave latency
                             400);  // supervision timeout = 4 s (10 ms units)
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int reason) override {
    // Only flip the flag here. Restarting advertising from inside the host
    // callback is racy — the NimBLE host hasn't finished cleaning up the
    // connection yet, and start() can silently fail. The watchdog in
    // ble_poll() will (re)start advertising from loop context instead.
    g_connected = false;
    Serial.printf("[BLE] disconnect reason=0x%02x\n", reason & 0xff);
  }
};

class RxCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    auto v = c->getValue();
    if (!g_rx_stream || v.size() == 0) return;
    g_last_rx_ms = millis();
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

  g_server = NimBLEDevice::createServer();
  g_server->setCallbacks(&g_server_cb);

  NimBLEService* svc = g_server->createService(NUS_SERVICE_UUID);

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

  // BLE health watchdog, runs every 2 s. Two failure modes to recover from:
  //   1. NimBLE's own auto-readvertise after disconnect can silently drop
  //      (host not fully settled, etc.) — once the stack stops advertising
  //      with no connection there's nothing left to wake it up.
  //   2. macOS lid-close keeps the LL link alive (so supervision timeout
  //      never fires) while freezing the user-space app — board thinks it's
  //      connected forever, never re-advertises, central can't rediscover.
  static uint32_t last_check = 0;
  uint32_t now = millis();
  if (now - last_check < 2000) return;
  last_check = now;

  // Zombie-connection guard. macOS lid-close keeps the LL link up (Bluetooth
  // controller stays powered for HID wake) but freezes the user-space
  // process, so supervision timeout never fires. If we're "connected" but
  // have received no application bytes for a long while, force the peer off
  // so onDisconnect fires → advertising restarts → central can reconnect.
  if (g_connected && g_server && g_last_rx_ms != 0 &&
      now - g_last_rx_ms > IDLE_DISCONNECT_MS) {
    Serial.printf("[BLE] idle %lus; forcing disconnect\n",
                  (unsigned long)((now - g_last_rx_ms) / 1000));
    g_server->disconnect(g_last_handle);
    return;
  }

  if (g_connected) return;
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (!adv || adv->isAdvertising()) return;
  bool ok = adv->start();
  Serial.printf("[BLE] watchdog: advertising %s\n",
                ok ? "restarted" : "restart FAILED");
}

bool ble_connected()            { return g_connected; }
const char* ble_advertised_name() { return g_name; }
