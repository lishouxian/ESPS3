#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== ESPS3 boot ===");
  Serial.printf("Chip model : %s rev %d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("CPU freq   : %lu MHz\n", ESP.getCpuFreqMHz());
  Serial.printf("Flash size : %lu MB\n", ESP.getFlashChipSize() / (1024UL * 1024UL));
  Serial.printf("PSRAM size : %lu bytes\n", (unsigned long)ESP.getPsramSize());
  Serial.printf("Free heap  : %lu bytes\n", (unsigned long)ESP.getFreeHeap());
}

void loop() {
  Serial.printf("[%lu] alive, free heap=%lu\n", millis(), (unsigned long)ESP.getFreeHeap());
  delay(2000);
}
