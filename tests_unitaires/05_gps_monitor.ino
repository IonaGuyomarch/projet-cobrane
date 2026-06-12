#include <TinyGPSPlus.h>

#define GPS_SERIAL Serial1
#define GPS_BAUD   9600

TinyGPSPlus gps;

// Stats GSV (satellites vus + SNR)
int gsvSeen = 0;
int snrCount = 0;
int snrSum = 0;
int snrMax = 0;

String nmeaLine;

int fieldInt(const String &s, int idx) {
  int start = 0, f = 0;
  for (int i = 0; i <= (int)s.length(); i++) {
    if (i == (int)s.length() || s[i] == ',') {
      if (f == idx) {
        String t = s.substring(start, i);
        if (t.length() == 0) return -1;
        return t.toInt();
      }
      f++;
      start = i + 1;
    }
  }
  return -1;
}

bool isGSV(const String& s) {
  return s.startsWith("$GPGSV") || s.startsWith("$GNGSV") ||
         s.startsWith("$BDGSV") || s.startsWith("$GLGSV") ||
         s.startsWith("$GAGSV") || s.startsWith("$GBGSV");
}

void parseGSVStats(const String& s) {
  // Champs GSV: idx3 = total sats vus
  int total = fieldInt(s, 3);
  if (total >= 0) gsvSeen = total;

  // SNR sont aux champs 7,11,15,19 (idx 0-based)
  for (int k = 0; k < 4; k++) {
    int snr = fieldInt(s, 7 + k * 4);
    if (snr >= 0) {
      snrCount++;
      snrSum += snr;
      if (snr > snrMax) snrMax = snr;
    }
  }
}

void resetSNRWindow() {
  snrCount = 0;
  snrSum = 0;
  snrMax = 0;
}

void setup() {
  Serial.begin(115200);
  unsigned long t0 = millis();
  while (!Serial && (millis() - t0 < 3000)) {}

  GPS_SERIAL.begin(GPS_BAUD);
  Serial.println("GPS monitor: lat/lon, sats, hdop, SNR (Serial1@9600)");
}

void loop() {
  while (GPS_SERIAL.available()) {
    char c = GPS_SERIAL.read();
    gps.encode(c);

    // Capture de lignes NMEA pour extraire GSV (SNR)
    if (c == '\n') {
      String s = nmeaLine;
      nmeaLine = "";
      s.trim();
      if (isGSV(s)) parseGSVStats(s);
    } else if (c != '\r') {
      nmeaLine += c;
      if (nmeaLine.length() > 120) nmeaLine = ""; // sécurité
    }
  }

  static uint32_t last = 0;
  if (millis() - last >= 1000) {
    last = millis();

    Serial.println("------------------------");

    bool fix = gps.location.isValid();
    Serial.print("Fix: "); Serial.println(fix ? "YES" : "NO");

    Serial.print("Sat used: ");
    Serial.println(gps.satellites.isValid() ? gps.satellites.value() : 0);

    Serial.print("Sat seen (GSV): ");
    Serial.println(gsvSeen);

    Serial.print("HDOP: ");
    Serial.println(gps.hdop.isValid() ? gps.hdop.hdop() : 99.9);

    if (fix) {
      Serial.print("Lat: "); Serial.println(gps.location.lat(), 6);
      Serial.print("Lon: "); Serial.println(gps.location.lng(), 6);
    } else {
      Serial.println("Lat/Lon: --- (no fix yet)");
    }

    if (snrCount > 0) {
      float snrAvg = (float)snrSum / (float)snrCount;
      Serial.print("SNR avg/max: ");
      Serial.print(snrAvg, 1);
      Serial.print(" / ");
      Serial.println(snrMax);
    } else {
      Serial.println("SNR avg/max: ---");
    }

    // reset fenêtre SNR toutes les secondes
    resetSNRWindow();
  }
}