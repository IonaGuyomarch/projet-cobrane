#include <Arduino.h>
#include <mbed.h>

using namespace mbed;

// UART0 : TX=PA_0, RX=PI_9
static UnbufferedSerial uart0(PA_0, PI_9, 115200);

void setup() {
  Serial.begin(115200);          // USB vers PC
  while (!Serial && millis() < 1500) {}
  Serial.println("Portenta: USB OK, UART0(custom) OK");
}

void loop() {
  if (uart0.readable()) {
    char c;
    if (uart0.read(&c, 1) == 1) {
      Serial.write(c);           // affiche sur USB ce qui arrive sur UART0
    }
  }
}
