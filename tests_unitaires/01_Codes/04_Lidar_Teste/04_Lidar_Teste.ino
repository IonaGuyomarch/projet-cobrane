#include <RPLidar.h>

RPLidar lidar;

#define RPLIDAR_SERIAL Serial2
#define RPLIDAR_BAUDRATE 115200  // Essaye 256000 si rien ne répond

void setup() {
  Serial.begin(115200);
  while(!Serial);

  Serial.println("Init LIDAR...");

  // UART vers LIDAR
  RPLIDAR_SERIAL.begin(RPLIDAR_BAUDRATE);
  lidar.begin(RPLIDAR_SERIAL);

  delay(1000);

  // Vérif device
  rplidar_response_device_info_t info;
  if (IS_OK(lidar.getDeviceInfo(info, 2000))) {
    Serial.println("LIDAR detecte !");
    Serial.print("Model: "); Serial.println(info.model);
    Serial.print("Firmware: "); Serial.println(info.firmware_version);
  } else {
    Serial.println("ERROR : Impossible d'obtenir les infos du device");
    Serial.println("-> Vérifier RX/TX, Bauds, GND, moteur");
  }
}

void loop() {
  if (IS_OK(lidar.waitPoint())) {
    float angle = lidar.getCurrentPoint().angle;
    float distance = lidar.getCurrentPoint().distance;
    byte quality = lidar.getCurrentPoint().quality;

    Serial.print("Angle: ");
    Serial.print(angle);
    Serial.print(" Dist: ");
    Serial.print(distance);
    Serial.print(" Q: ");
    Serial.println(quality);
  }
}
