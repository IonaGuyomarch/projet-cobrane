// ESP32-CAM
#define LED_FLASH 4

void setup() {
  Serial.begin(115200);
  pinMode(LED_FLASH, OUTPUT);
  digitalWrite(LED_FLASH, LOW);
  Serial.println("ESP START");
}

void loop() {
  if (Serial.available()) {
    String msg = Serial.readStringUntil('\n');
    Serial.print("Recu: ");
    Serial.println(msg);
    // Si un message arrive → LED fixe
    digitalWrite(LED_FLASH, HIGH);
  }
}
