const int trigPin = 9;
const int echoPin = 8;
long duration;
float distance_cm;

void setup() {
  Serial.begin(115200);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
}

digitalWrite(trigPin, LOW);
delayMicroseconds(2);
digitalWrite(trigPin, HIGH);
delayMicroseconds(10);
digitalWrite(trigPin, LOW);

duration = pulseIn(echoPin, HIGH);

distance_cm = duration * 0.034 / 2;
Serial.print("Distance : ");
Serial.print(distance_cm);
Serial.println(" cm");
delay(200);
