// Portenta H7
void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  Serial.println("PORTENTA START");
}

void loop() {
  Serial1.println("HeLLO");   // message envoye a l'ESP32
  delay(1000);
}
