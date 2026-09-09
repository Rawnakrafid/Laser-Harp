#include <Arduino.h>

const int DO_PIN = 34;      // LDR DO pin
const int BUZZER_PIN = 25;  // Buzzer I/O pin

bool blocked = false;
unsigned long lastStateChangeMs = 0;

void setup() {
  Serial.begin(115200);
  delay(1500);
  
  pinMode(DO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);  // Buzzer off initially
  
  Serial.println("=== Laser Harp - LDR + Buzzer Test ===");
  Serial.println("Block the laser/LDR to trigger buzzer");
  Serial.println("t_ms,event,ms_since_last_change");
  
  lastStateChangeMs = millis();
}

void loop() {
  int reading = digitalRead(DO_PIN);
  unsigned long now = millis();
  
  if (!blocked && reading == LOW) {
    // Laser blocked - turn ON buzzer
    blocked = true;
    digitalWrite(BUZZER_PIN, HIGH);
    unsigned long delta = now - lastStateChangeMs;
    lastStateChangeMs = now;
    Serial.printf("%lu,BLOCK,%lu\n", now, delta);
  } 
  else if (blocked && reading == HIGH) {
    // Laser clear - turn OFF buzzer
    blocked = false;
    digitalWrite(BUZZER_PIN, LOW);
    unsigned long delta = now - lastStateChangeMs;
    lastStateChangeMs = now;
    Serial.printf("%lu,CLEAR,%lu\n", now, delta);
  }
  
  delay(10);
}