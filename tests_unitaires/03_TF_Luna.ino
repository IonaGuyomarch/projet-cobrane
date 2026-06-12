#include <Wire.h>
 
#define TF_LUNA_ADDR 0x10
 
void setup() {
  Wire.begin();
  Serial.begin(115200);
}
 
void loop() {
  Wire.beginTransmission(TF_LUNA_ADDR);
  Wire.write(0x00);
  Wire.endTransmission();
  Wire.requestFrom(TF_LUNA_ADDR, 2);
 
  if (Wire.available() == 2) {
    int dist = Wire.read();
    dist |= Wire.read() << 8;
    Serial.print("Distance: ");
    Serial.print(dist);
    Serial.println(" cm");
  }
 
  delay(200);
}
