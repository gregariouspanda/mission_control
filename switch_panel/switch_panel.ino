/*
  Switch Panel (Arduino A)

  Reads six on-on toggle switches, lights the matching LEDs, plays the audio for
  each switch position on the DFPlayer Mini, and sends the switch states to the
  Mission Control Arduino (Arduino B), which handles the LCD and 8-digit display.

  Libraries (Library Manager):
    - DFRobotDFPlayerMini

  Wiring:
    Switch 1-6     -> D3..D8 (common of each switch to the pin, active throw to GND)
                      A switch that is thrown to its active side pulls its pin LOW (0).
    LED 1-6        -> A0..A5 (through the PCB's resistors to GND)
                      The Arduino supplies the LED current; LED is ON while its switch is active.
    DFPlayer Mini  -> Arduino D10 (RX) <- DFPlayer TX
                      Arduino D9 (TX) -> DFPlayer RX (through ~1k resistor)
                      VCC 5V, GND GND, speaker on SPK_1 / SPK_2
    Link to Arduino B -> D2 (TX) to Arduino B D0 (RX), and GND to GND.

  SD card tracks (0001.mp3, 0002.mp3, ...), played when a switch moves to a position:
    1        boot track, plays at power-up
    10       Switch 1 ON
    2-5, 7   Switch 2 ON "Comms"         (one picked at random)
    6, 8     Switch 3 ON "Navigation"    (one picked at random)
    12-16    Switch 4 ON "Mission Brief" (one picked at random)
    17-19    Switch 5 ON "Spacewalk"     (one picked at random)
    9        Switch 6 ON (placeholder - set your own track)
    OFF position: plays a new track (random from FIRST_TRACK..LAST_TRACK, never the
                  one just played). Set a switch's OFF_TRACKS entry to force a fixed track.

  Message sent to Arduino B (9600 baud, text line):
    "S" + six characters + newline, e.g. "S010001\n"
    character N is '1' when Switch N is active (pin LOW), '0' otherwise.
  The message is sent whenever a switch changes, and repeated every
  HEARTBEAT_MS so Arduino B can catch up if it was reset or missed a message.
*/

#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>

// ---------- Configuration ----------
const uint8_t SWITCH_PINS[] = {3, 4, 5, 6, 7, 8};
const uint8_t LED_PINS[]    = {A0, A1, A2, A3, A4, A5};
const uint8_t NUM_SWITCHES  = sizeof(SWITCH_PINS) / sizeof(SWITCH_PINS[0]);

const uint8_t LINK_TX = 2;
const uint8_t LINK_RX = 11;                // unused, but SoftwareSerial needs a pin

const uint8_t DFPLAYER_RX = 10;            // Arduino receives on this pin
const uint8_t DFPLAYER_TX = 9;             // Arduino transmits on this pin
const uint8_t VOLUME      = 7;             // 0-30
const uint16_t BOOT_TRACK = 1;
const unsigned long BOOT_GRACE_MS = 2000;  // no switch audio while the boot track plays

// Track played when a switch moves to its OFF position (0 = no audio). Order = Switch 1..6.
const uint16_t OFF_TRACKS[] = {0, 0, 0, 0, 0, 0};   // 0 = pick a new random track
const uint16_t FIRST_TRACK  = 2;                    // pool for OFF-position tracks
const uint16_t LAST_TRACK   = 19;                   // highest track number on the SD card

const unsigned long DEBOUNCE_MS  = 30;
const unsigned long HEARTBEAT_MS = 250;

SoftwareSerial link(LINK_RX, LINK_TX);
SoftwareSerial playerSerial(DFPLAYER_RX, DFPLAYER_TX);
DFRobotDFPlayerMini player;

// ---------- State ----------
bool          switchState[NUM_SWITCHES];   // true = active (pin LOW)
bool          lastReading[NUM_SWITCHES];
unsigned long lastChangeMs[NUM_SWITCHES];
unsigned long lastSendMs = 0;
bool          playerReady = false;
bool          audioEnabled = false;        // true once the boot grace period has passed
unsigned long audioEnableAtMs;

void setup() {
  Serial.begin(9600);
  randomSeed(analogRead(A0));              // A0 floats here, before it becomes an LED output
  link.begin(9600);

  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    pinMode(SWITCH_PINS[i], INPUT_PULLUP);
    pinMode(LED_PINS[i], OUTPUT);
    switchState[i] = lastReading[i] = (digitalRead(SWITCH_PINS[i]) == LOW);
    digitalWrite(LED_PINS[i], switchState[i]);
  }
  setupPlayer();
  playTrack(BOOT_TRACK);
  audioEnableAtMs = millis() + BOOT_GRACE_MS;
  sendState();
}

void setupPlayer() {
  playerSerial.begin(9600);                // begun last so the DFPlayer is the listening port
  delay(300);
  // ACK off + reset on, with a settle delay (matches the tested DFPlayer sketch)
  playerReady = player.begin(playerSerial, /*isACK=*/false, /*doReset=*/true);
  if (playerReady) {
    player.setTimeOut(500);
    player.EQ(DFPLAYER_EQ_NORMAL);
    player.outputDevice(DFPLAYER_DEVICE_SD);
    player.volume(VOLUME);
    delay(1200);
  } else {
    Serial.println(F("DFPlayer not found - check wiring/SD card"));
  }
}

void loop() {
  unsigned long now = millis();
  if (!audioEnabled && now >= audioEnableAtMs) audioEnabled = true;
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
      if (audioEnabled) onSwitchChanged(i, reading);
    }
  }

  if (changed || now - lastSendMs >= HEARTBEAT_MS) sendState();
}

void sendState() {
  lastSendMs = millis();
  char msg[NUM_SWITCHES + 1];
  uint8_t ones = 0;
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    msg[i] = switchState[i] ? '1' : '0';
    if (switchState[i]) ones++;
  }
  msg[NUM_SWITCHES] = '\0';

  link.print('S');
  link.print(msg);
  link.println(ones);                 // check digit: how many switches are active
  Serial.print(F("S"));
  Serial.println(msg);
}

// ---------- Audio: the switch position picks the track ----------
// index is 0-5 (Switch 1-6); isOn is true when the switch is in its active position.
void onSwitchChanged(uint8_t index, bool isOn) {
  if (!isOn) {                              // OFF position
    playTrack(OFF_TRACKS[index] ? OFF_TRACKS[index] : pickNewTrack());
    return;
  }

  switch (index) {
    case 0:   // Switch 1
      playTrack(10);
      break;
    case 1: { // Switch 2 "Comms": random 2-5 or 7 (skips 6)
      uint16_t t = random(2, 7);            // 2..6
      if (t == 6) t = 7;
      playTrack(t);
      break;
    }
    case 2:   // Switch 3 "Navigation": 6 or 8
      playTrack(random(0, 2) == 0 ? 6 : 8);
      break;
    case 3:   // Switch 4 "Mission Brief": 12-16
      playTrack(random(12, 17));
      break;
    case 4:   // Switch 5 "Spacewalk": 17-19
      playTrack(random(17, 20));
      break;
    case 5:   // Switch 6: placeholder track
      playTrack(9);
      break;
  }
}

uint16_t lastTrack = 0;                     // most recently started track

void playTrack(uint16_t track) {
  lastTrack = track;
  if (playerReady) player.play(track);
}

// Random track from FIRST_TRACK..LAST_TRACK that differs from the last one played.
uint16_t pickNewTrack() {
  uint16_t t;
  do {
    t = random(FIRST_TRACK, LAST_TRACK + 1);
  } while (t == lastTrack);
  return t;
}
