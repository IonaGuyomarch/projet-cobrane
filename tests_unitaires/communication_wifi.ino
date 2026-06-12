#include <WiFiS3.h>
#include <ArduinoHttpClient.h>

// WiFi
const char* WIFI_SSID     = "CFAI-APP";
const char* WIFI_PASSWORD = "IletaitUneFoisauCF@1";

// InfluxDB
const char* INFLUX_HOST   = "172.16.5.20"; //Adresse IP interne à Docker, attention si utilisation de docker compose l'IP comme le port changent
const int   INFLUX_PORT   = 8181;
const char* INFLUX_DB     = "projet-trone";
const char* INFLUX_TOKEN  = "Bearer apiv3_1G6zaVB4vaPWUudp7UbsU45E6hBcawlsApycoFgfGJTFpE3bLU3-AxzmuDXXAh1WUnP0SrTnbISZEZP-DB5H2A"; // A modifier en fonction du token de Grafana

const int trigPin = 9;
const int echoPin = 10;
long duration;
int distance;

WiFiClient wifi;
HttpClient client = HttpClient(wifi, INFLUX_HOST, INFLUX_PORT);

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.print("Connexion WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connecté !");
  Serial.print("IP Arduino : ");
  Serial.println(WiFi.localIP());
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
}

void loop() {
  // Valeur simulée entre 18.0 et 28.0

  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  duration = pulseIn(echoPin, HIGH);
  distance = duration * 0.034 / 2;


  // Line protocol InfluxDB : measurement,tags field=value
  String body = "distance,capteur=ultrason value=";
  body += String(distance);

  Serial.print("Envoi distance : ");
  Serial.println(distance);

  client.beginRequest();
  client.post("/api/v3/write_lp?db=" + String(INFLUX_DB));
  client.sendHeader("Authorization", String(INFLUX_TOKEN));
  client.sendHeader("Content-Type", "text/plain");
  client.sendHeader("Content-Length", body.length());
  client.beginBody();
  client.print(body);
  client.endRequest();

  int statusCode = client.responseStatusCode();
  String response = client.responseBody();

  Serial.print("Status : ");
  Serial.println(statusCode);
  if (response.length() > 0) {
    Serial.println(response);
  }

  delay(5000); 
}
