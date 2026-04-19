// BLE transport for ESPS3.
//
// Exposes Nordic UART Service (NUS) so a macOS `bleak`-based bridge can push
// the same newline-delimited JSON protocol that USB-CDC uses. The callback
// is invoked from loop() context (not from the BLE task), so it is safe to
// call into the full render pipeline from there.
#pragma once
#include <Arduino.h>

using BleLineCallback = void (*)(const String&);

// Start NimBLE, register NUS service, begin advertising. Advertised name is
// "ESPS3-XXXX" where XXXX is the last 2 bytes of the BT MAC in hex.
// `cb` will be called with each complete line received, from ble_poll().
void ble_begin(BleLineCallback cb);

// Drain the BLE rx queue and deliver completed lines to the callback.
// Call this from loop(); it is non-blocking.
void ble_poll();

// True while at least one BLE central is connected.
bool ble_connected();

// "ESPS3-XXXX". Valid after ble_begin() returns.
const char* ble_advertised_name();
