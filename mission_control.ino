/*
  Mission Control (Arduino B) – mission timer, LCD, DFPlayer Mini

  Receives the six switch states from the Switch Panel Arduino (see
  switch_panel/switch_panel.ino) and produces the audio, LCD and display output.

  Libraries (Library Manager):
    - LiquidCrystal I2C  (Frank de Brabander)
    - DFRobotDFPlayerMini
  (The HW-179 / MAX7219 display is driven directly, no library needed.)

  Wiring:
    Switch Panel   -> D0 (RX) <- Switch Panel Arduino D2 (TX), GND to GND
                      (unplug this wire while uploading this sketch)
    Reset button   -> D2 to GND (internal pull-up, LOW = pressed)
    HW-179 display -> DIN D12, CS D13, CLK D11, VCC 5V, GND GND
    DFPlayer Mini  -> Arduino D10 (RX) <- DFPlayer TX
                      Arduino D9 (TX) -> DFPlayer RX (through ~1k resistor)
    LCD 16x2 I2C   -> SDA A4, SCL A5, VCC 5V, GND GND

  SD card tracks (0001.mp3, 0002.mp3, ...):
    1        boot track, plays at power-up
    10       Switch 1
    2-5, 7   Switch 2 "Comms"        (one picked at random)
    6, 8     Switch 3 "Navigation"   (one picked at random)
    12-16    Switch 4 "Mission Brief" (one picked at random)
    17-19    Switch 5 "Spacewalk"    (one picked at random)
    9        Switch 6 (placeholder - set your own track)

  NOTE: pins match the tested DFPlayer and timer sketches; nothing overlaps.
  Change the pin constants below to match your PCB.
*/

#include <SoftwareSerial.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DFRobotDFPlayerMini.h>

// ---------- Configuration ----------
#define ASTRONAUT_NAME "Astronaut"   // your astronaut's name, max 15 characters (shown on the LCD)

const uint8_t NUM_SWITCHES = 6;

const uint8_t RESET_PIN = 2;      // button to GND; restarts the mission timer

const uint8_t MAX_DIN = 12;
const uint8_t MAX_CS  = 13;
const uint8_t MAX_CLK = 11;

const uint8_t DFPLAYER_RX = 10;   // Arduino receives on this pin
const uint8_t DFPLAYER_TX = 9;    // Arduino transmits on this pin
const uint8_t LCD_ADDRESS = 0x27; // try 0x3F if the LCD stays blank

const bool          COUNT_DOWN      = false;           // false = count up from 0
const unsigned long MISSION_SECONDS = 60UL * 60UL;     // countdown start (max 99:59:59)

// Test rotation: when true, the timer ignores COUNT_DOWN/MISSION_SECONDS and cycles through
// the TEST_PHASES below, TEST_PHASE_MS each, so you can check the display. Set false for normal use.
const bool          TEST_TIMER_ROTATION = true;
const unsigned long TEST_PHASE_MS       = 10000;
struct TestPhase { bool countDown; unsigned long startMs; };
const TestPhase TEST_PHASES[] = {
  { false, 0UL },                          // count up from 00.00.00.00
  { true,  10000UL },                      // count down from 00.00.10.00 to zero
  { false, 59UL * 60000UL + 55000UL },     // count up from 00.59.55.00 (tests minute/hour rollover)
  { true,  3600000UL + 5000UL },           // count down from 01.00.05.00 (tests rollover downward)
};
const uint8_t NUM_TEST_PHASES = sizeof(TEST_PHASES) / sizeof(TEST_PHASES[0]);
const uint8_t       DISPLAY_BRIGHT  = 8;               // 0-15
const unsigned long DEBOUNCE_MS     = 30;
const uint8_t       VOLUME          = 7;               // 0-30
const uint16_t      BOOT_TRACK      = 1;
const unsigned long BOOT_GRACE_MS   = 2000;            // ignore switches while boot track plays
const unsigned long LCD_ROTATE_MS   = 5000;            // seconds each LCD page stays up

// ---------- LCD pages (rotate every LCD_ROTATE_MS) ----------
struct Page { const char* line1; const char* line2; };
const Page LCD_PAGES[] = {
  { "Distance to",      "Moon: 238,900mi" },
  { "Happy Birthday",   ASTRONAUT_NAME "!" },
  { "Select a Mission", ""                },
};
const uint8_t NUM_PAGES = sizeof(LCD_PAGES) / sizeof(LCD_PAGES[0]);

// ---------- Switch messages (shown when that switch is the most recent one toggled) ----------
// Order = Switch 1..6. Edit the text to suit. Each line is max 16 characters.
struct SwitchMsg { Page on; Page off; };
const SwitchMsg SWITCH_MSGS[] = {
  { { "Switch 1",     "ON"  }, { "Switch 1",     "OFF" } },
  { { "Comms",        "ON"  }, { "Comms",        "OFF" } },
  { { "Navigation",   "ON"  }, { "Navigation",   "OFF" } },
  { { "Mission Brief","ON"  }, { "Mission Brief","OFF" } },
  { { "Spacewalk",    "ON"  }, { "Spacewalk",    "OFF" } },
  { { "Switch 6",     "ON"  }, { "Switch 6",     "OFF" } },
};

// ---------- Hardware objects ----------
LiquidCrystal_I2C   lcd(LCD_ADDRESS, 16, 2);
SoftwareSerial      playerSerial(DFPLAYER_RX, DFPLAYER_TX);
DFRobotDFPlayerMini player;

// ---------- State ----------
bool          switchState[NUM_SWITCHES];      // true = ON (as reported by the Switch Panel)
bool          haveBaseline = false;           // first message only sets the starting state
char          rxBuf[NUM_SWITCHES + 3];
uint8_t       rxLen = 0;

unsigned long missionStartMs;
unsigned long lastShownCs = 0xFFFFFFFF;   // last hundredth-of-a-second value drawn
bool          timerExpiredFired = false;
bool          playerReady = false;

bool          lastResetReading = HIGH;
bool          resetState       = HIGH;
unsigned long lastResetChangeMs = 0;

unsigned long inputsEnableAtMs;
bool          inputsEnabled = false;

uint8_t       lcdPage = 0;
unsigned long lcdLastSwapMs;
bool          lcdRotating = true;             // stops once the timer expires

// ---------- Setup ----------
void setup() {
  Serial.begin(9600);
  randomSeed(analogRead(A3));

  pinMode(RESET_PIN, INPUT_PULLUP);

  setupTimerDisplay();
  setupLcd();
  setupPlayer();

  playTrack(BOOT_TRACK);
  inputsEnableAtMs = millis() + BOOT_GRACE_MS;
  missionStartMs = millis();
}

void setupTimerDisplay() {
  pinMode(MAX_DIN, OUTPUT);
  pinMode(MAX_CS,  OUTPUT);
  pinMode(MAX_CLK, OUTPUT);
  digitalWrite(MAX_CS, HIGH);

  maxWrite(0x0F, 0x00);            // display test off
  maxWrite(0x0C, 0x01);            // normal operation (not shutdown)
  maxWrite(0x0B, 0x07);            // scan all 8 digits
  maxWrite(0x09, 0xFF);            // BCD decode on all digits
  maxWrite(0x0A, DISPLAY_BRIGHT);  // brightness
  showTimer(COUNT_DOWN ? MISSION_SECONDS * 1000UL : 0);
}

void setupLcd() {
  lcd.init();
  lcd.backlight();
  showPage(lcdPage);
  lcdLastSwapMs = millis();
}

void setupPlayer() {
  playerSerial.begin(9600);
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

// ---------- Main loop ----------
void loop() {
  if (!inputsEnabled && millis() >= inputsEnableAtMs) inputsEnabled = true;

  updateLink();
  updateResetButton();
  updateTimer();
  updateLcd();
}

// ---------- Link from the Switch Panel ----------
// Messages look like "S010001\n" ('1' = that switch is active). Calls
// onSwitchChanged() once per change. The first message only sets the baseline.
void updateLink() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (rxLen < sizeof(rxBuf) - 1) rxBuf[rxLen++] = c;
      continue;
    }
    rxBuf[rxLen] = '\0';
    if (rxLen == NUM_SWITCHES + 1 && rxBuf[0] == 'S') applyStates(rxBuf + 1);
    rxLen = 0;
  }
}

void applyStates(const char* states) {
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    bool on = (states[i] == '1');
    if (haveBaseline && inputsEnabled && on != switchState[i]) {
      switchState[i] = on;
      onSwitchChanged(i, on);
    } else {
      switchState[i] = on;
    }
  }
  haveBaseline = true;
}

// ---------- Reset button ----------
// Debounced; a press restarts the timer from 00-00-00 (or the countdown start).
void updateResetButton() {
  unsigned long now = millis();
  bool reading = digitalRead(RESET_PIN);
  if (reading != lastResetReading) {
    lastResetReading  = reading;
    lastResetChangeMs = now;
  }
  if (reading != resetState && now - lastResetChangeMs >= DEBOUNCE_MS) {
    resetState = reading;
    if (resetState == LOW) resetTimer();
  }
}

void resetTimer() {
  Serial.println(F("Timer reset"));
  missionStartMs   = millis();
  lastShownCs      = 0xFFFFFFFF;       // force the display to redraw
  timerExpiredFired = false;
  if (!lcdRotating) {                  // leave the "EXPIRED" screen
    lcdRotating   = true;
    lcdLastSwapMs = millis();
    showPage(lcdPage);
  }
}

// ---------- Commands ----------
// index is 0-5 (Switch 1-6); isOn is true when the switch is ON.
// Track numbers refer to files on the SD card: 0001.mp3, 0002.mp3, ...
void onSwitchChanged(uint8_t index, bool isOn) {
  Serial.print(F("Switch "));
  Serial.print(index + 1);
  Serial.println(isOn ? F(" ON") : F(" OFF"));

  // The most recent toggle controls the LCD: stop the idle pages and show its message
  lcdRotating = false;
  const Page& msg = isOn ? SWITCH_MSGS[index].on : SWITCH_MSGS[index].off;
  showMessage(msg.line1, msg.line2);

  if (!isOn) return;   // only trigger audio when a switch is turned ON

  switch (index) {
    case 0:   // Switch 1
      playTrack(10);
      break;
    case 1: { // Switch 2 "Comms": random 2-5 or 7 (skips 6)
      uint16_t t = random(2, 7);      // 2..6
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

// ---------- LCD ----------
void showMessage(const char* line1, const char* line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

void showPage(uint8_t idx) {
  showMessage(LCD_PAGES[idx].line1, LCD_PAGES[idx].line2);
}

void updateLcd() {
  if (!lcdRotating) return;
  unsigned long now = millis();
  if (now - lcdLastSwapMs >= LCD_ROTATE_MS) {
    lcdLastSwapMs = now;
    lcdPage = (lcdPage + 1) % NUM_PAGES;
    showPage(lcdPage);
  }
}

// ---------- Audio ----------
void playTrack(uint16_t track) {
  if (playerReady) player.play(track);
}

// ---------- MAX7219 (HW-179) ----------
void maxWrite(uint8_t reg, uint8_t value) {
  digitalWrite(MAX_CS, LOW);
  shiftOut(MAX_DIN, MAX_CLK, MSBFIRST, reg);
  shiftOut(MAX_DIN, MAX_CLK, MSBFIRST, value);
  digitalWrite(MAX_CS, HIGH);
}

// Shows HH.MM.SS.cc (cc = hundredths of a second) from a time in milliseconds.
// Register 1 is the rightmost digit, 8 the leftmost.
// In BCD decode mode: 0-9 = digit; OR with 0x80 to light that digit's decimal point.
void showTimer(unsigned long ms) {
  uint8_t hh = (ms / 3600000UL) % 100;
  uint8_t mm = (ms / 60000UL) % 60;
  uint8_t ss = (ms / 1000UL) % 60;
  uint8_t cs = (ms / 10UL) % 100;

  maxWrite(8, hh / 10);
  maxWrite(7, (hh % 10) | 0x80);
  maxWrite(6, mm / 10);
  maxWrite(5, (mm % 10) | 0x80);
  maxWrite(4, ss / 10);
  maxWrite(3, (ss % 10) | 0x80);
  maxWrite(2, cs / 10);
  maxWrite(1, cs % 10);
}

// ---------- Mission timer ----------
void updateTimer() {
  unsigned long elapsed = millis() - missionStartMs;
  unsigned long shown;

  if (TEST_TIMER_ROTATION) {
    unsigned long inCycle = elapsed % (TEST_PHASE_MS * NUM_TEST_PHASES);
    const TestPhase& ph   = TEST_PHASES[inCycle / TEST_PHASE_MS];
    unsigned long t       = inCycle % TEST_PHASE_MS;
    if (ph.countDown) shown = (ph.startMs > t) ? ph.startMs - t : 0;
    else              shown = ph.startMs + t;
  } else if (COUNT_DOWN) {
    unsigned long total = MISSION_SECONDS * 1000UL;
    shown = (elapsed >= total) ? 0 : total - elapsed;
  } else {
    shown = elapsed;
  }

  if (shown / 10UL != lastShownCs) {        // redraw only when the hundredths change
    lastShownCs = shown / 10UL;
    showTimer(shown);
    if (!TEST_TIMER_ROTATION && COUNT_DOWN && shown == 0 && !timerExpiredFired) {
      timerExpiredFired = true;
      onTimerExpired();
    }
  }
}

void onTimerExpired() {
  lcdRotating = false;
  showMessage("MISSION TIME", "EXPIRED");
  // playTrack(99);
}
