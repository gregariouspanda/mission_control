/*
  Switch Panel (Arduino A)

  Reads six on-on toggle switches, lights the matching LEDs, and sends the
  switch states to the Mission Control Arduino (Arduino B).

  Wiring:
    Switch 1-6     -> D3..D8 (common of each switch to the pin, active throw to GND)
                      A switch that is thrown to its active side pulls its pin LOW (0).
    LED 1-6        -> A0..A5 (through the PCB's resistors to GND)
                      The Arduino supplies the LED current; LED is ON while its switch is active.
    Link to Arduino B -> D2 (TX) to Arduino B D0 (RX), and GND to GND.

  Message sent to Arduino B (9600 baud, text line):
    "S" + six characters + newline, e.g. "S010001\n"
    character N is '1' when Switch N is active (pin LOW), '0' otherwise.
  The message is sent whenever a switch changes, and repeated every
  HEARTBEAT_MS so Arduino B can catch up if it was reset or missed a message.
*/

#include <SoftwareSerial.h>

// ---------- Configuration ----------
const uint8_t SWITCH_PINS[] = {3, 4, 5, 6, 7, 8};
const uint8_t LED_PINS[]    = {A0, A1, A2, A3, A4, A5};
const uint8_t NUM_SWITCHES  = sizeof(SWITCH_PINS) / sizeof(SWITCH_PINS[0]);

const uint8_t LINK_TX = 2;
const uint8_t LINK_RX = 9;                 // unused, but SoftwareSerial needs a pin

const unsigned long DEBOUNCE_MS  = 30;
const unsigned long HEARTBEAT_MS = 250;

SoftwareSerial link(LINK_RX, LINK_TX);

// ---------- State ----------
bool          switchState[NUM_SWITCHES];   // true = active (pin LOW)
bool          lastReading[NUM_SWITCHES];
unsigned long lastChangeMs[NUM_SWITCHES];
unsigned long lastSendMs = 0;

void setup() {
  Serial.begin(9600);
  link.begin(9600);

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);
    pinMode(LED_PINS[i], OUTPUT);
    switchState[i] = lastReading[i] = (digitalRead(SWITCH_PINS[i]) == LOW);
    digitalWrite(LED_PINS[i], switchState[i]);
  }
  sendState();
}

void loop() {
  unsigned long now = millis();
  bool changed = false;

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    bool reading = (digitalRead(SWITCH_PINS[i]) == LOW);
    if (reading != lastReading[i]) {
      lastReading[i]  = reading;
      lastChangeMs[i] = now;
    }
    if (reading != switchState[i] && now - lastChangeMs[i] >= DEBOUNCE_MS) {
      switchState[i] = reading;
      digitalWrite(LED_PINS[i], reading);
      changed = true;
    }
  }

  if (changed || now - lastSendMs >= HEARTBEAT_MS) sendState();
}

void sendState() {
  lastSendMs = millis();
  char msg[NUM_SWITCHES + 1];
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) msg[i] = switchState[i] ? '1' : '0';
  msg[NUM_SWITCHES] = '\0';

  link.print('S');
  link.println(msg);
  Serial.print(F("S"));
  Serial.println(msg);
}
