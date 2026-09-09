#include <Arduino.h>

const int DO_PIN = 34;  // D34 (GPIO34) - digital output from LDR module
bool blocked = false;
unsigned long lastStateChangeMs = 0;

void setup() {
  Serial.begin(115200);
  delay(1500);
  
  pinMode(DO_PIN, INPUT);
  
  Serial.println("=== Laser Harp Phase 1 - LDR Test (no laser) ===");
  Serial.println("Block the LDR with your hand to test.");
  Serial.println("t_ms,event,ms_since_last_change");
  
  lastStateChangeMs = millis();
}

void loop() {
  int reading = digitalRead(DO_PIN);
  unsigned long now = millis();
  
  // DO goes LOW when light is blocked, HIGH when clear
  if (!blocked && reading == LOW) {
    blocked = true;
    unsigned long delta = now - lastStateChangeMs;
    lastStateChangeMs = now;
    Serial.printf("%lu,BLOCK,%lu\n", now, delta);
  } 
  else if (blocked && reading == HIGH) {
    blocked = false;
    unsigned long delta = now - lastStateChangeMs;
    lastStateChangeMs = now;
    Serial.printf("%lu,CLEAR,%lu\n", now, delta);
  }
  
  delay(10);  // Small delay to debounce
}