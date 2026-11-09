/*
  Mission Control (Arduino B) – mission timer, LCD and 8-digit display

  Receives the six switch states from the Switch Panel Arduino (see
  switch_panel/switch_panel.ino) and drives the LCD and display. The DFPlayer Mini audio now lives on the Switch Panel.

  Libraries (Library Manager):
    - LiquidCrystal I2C  (Frank de Brabander)
  (The HW-179 / MAX7219 display is driven directly, no library needed.)

  Wiring:
    Switch Panel   -> D0 (RX) <- Switch Panel Arduino D2 (TX), GND to GND
                      (unplug this wire while uploading this sketch)
    HW-179 display -> DIN D12, CS D13, CLK D11, VCC 5V, GND GND
    LCD 16x2 I2C   -> SDA A4, SCL A5, VCC 5V, GND GND

  LCD message follows the most recently toggled switch and its position (ON or OFF).

  NOTE: D9 and D10 are free on this board now that the DFPlayer moved to the Switch Panel.
  Change the pin constants below to match your PCB.
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------- Configuration ----------
#define ASTRONAUT_NAME "Astronaut"   // your astronaut's name, max 15 characters (shown on the LCD)

const uint8_t NUM_SWITCHES = 6;


const uint8_t MAX_DIN = 12;
const uint8_t MAX_CS  = 13;
const uint8_t MAX_CLK = 11;

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
const unsigned long BOOT_GRACE_MS   = 2000;            // ignore switch changes right after power-up
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
  { { "Auxiliary",     "Switch 1 active" }, { "Auxiliary",     "Switch 1 off" } },
  { { "Comms",         "Switch 2 active" }, { "Comms",         "Switch 2 off" } },
  { { "Navigation",    "Switch 3 active" }, { "Navigation",    "Switch 3 off" } },
  { { "Mission Brief", "Switch 4 active" }, { "Mission Brief", "Switch 4 off" } },
  { { "Spacewalk",     "Switch 5 active" }, { "Spacewalk",     "Switch 5 off" } },
  { { "Auxiliary",     "Switch 6 ON"     }, { "Auxiliary",     "Switch 6 OFF"    } },
};

// ---------- Hardware objects ----------
LiquidCrystal_I2C   lcd(LCD_ADDRESS, 16, 2);

// ---------- State ----------
bool          switchState[NUM_SWITCHES];      // true = ON (as reported by the Switch Panel)
bool          haveBaseline = false;           // first message only sets the starting state
char          rxBuf[NUM_SWITCHES + 3];
uint8_t       rxLen = 0;

unsigned long missionStartMs;
unsigned long lastShownCs = 0xFFFFFFFF;   // last hundredth-of-a-second value drawn
bool          timerExpiredFired = false;

unsigned long inputsEnableAtMs;
bool          inputsEnabled = false;

uint8_t       lcdPage = 0;
unsigned long lcdLastSwapMs;
unsigned long lastRxMs = 0;                   // last valid message from the Switch Panel
bool          noLinkShown = false;
const unsigned long LINK_TIMEOUT_MS = 2000;   // no valid message for this long = link down
bool          lcdRotating = true;             // stops once the timer expires

// ---------- Setup ----------
void setup() {
  Serial.begin(9600);

  setupTimerDisplay();
  setupLcd();

  inputsEnableAtMs = millis() + BOOT_GRACE_MS;
  missionStartMs = millis();
}

void setupTimerDisplay() {
  digitalWrite(MAX_CS, HIGH);      // pull-up first, so CS never glitches low at startup
  pinMode(MAX_CS,  OUTPUT);
  pinMode(MAX_DIN, OUTPUT);
  pinMode(MAX_CLK, OUTPUT);

  delay(100);                      // let the MAX7219 finish powering up
  maxInit();
  showTimer(COUNT_DOWN ? MISSION_SECONDS * 1000UL : 0);
}

// Sends the MAX7219 configuration. Called at startup and again every second, so the
// display recovers if the chip missed the first commands (e.g. it powered up late).
void maxInit() {
  maxWrite(0x0F, 0x00);            // display test off
  maxWrite(0x0C, 0x01);            // normal operation (not shutdown)
  maxWrite(0x0B, 0x07);            // scan all 8 digits
  maxWrite(0x09, 0xFF);            // BCD decode on all digits
  maxWrite(0x0A, DISPLAY_BRIGHT);  // brightness
}

void setupLcd() {
  lcd.init();
  lcd.backlight();
  showPage(lcdPage);
  lcdLastSwapMs = millis();
}

// ---------- Main loop ----------
void loop() {
  if (!inputsEnabled && millis() >= inputsEnableAtMs) inputsEnabled = true;

  updateLink();
  updateTimer();
  updateLcd();
}

// ---------- Link from the Switch Panel ----------
// Messages look like "S0100012\n" (six states plus a check digit) ('1' = that switch is active). Calls
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
    if (rxLen == NUM_SWITCHES + 2 && rxBuf[0] == 'S' && checkDigitOk(rxBuf + 1)) {
      lastRxMs = millis();
      applyStates(rxBuf + 1);
    }
    rxLen = 0;
  }
}

// The last character is the count of '1's; a garbled message fails this and is ignored.
bool checkDigitOk(const char* states) {
  uint8_t ones = 0;
  for (uint8_t i = 0; i < NUM_SWITCHES; i++) {
    if (states[i] != '0' && states[i] != '1') return false;
    if (states[i] == '1') ones++;
  }
  return states[NUM_SWITCHES] == '0' + ones;
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

// ---------- Commands ----------
// index is 0-5 (Switch 1-6); isOn is true when the switch is ON.
// Audio for each position is played by the Switch Panel board.
void onSwitchChanged(uint8_t index, bool isOn) {
  Serial.print(F("Switch "));
  Serial.print(index + 1);
  Serial.println(isOn ? F(" ON") : F(" OFF"));

  // The most recent toggle controls the LCD: stop the idle pages and show its message
  lcdRotating = false;
  const Page& msg = isOn ? SWITCH_MSGS[index].on : SWITCH_MSGS[index].off;
  showMessage(msg.line1, msg.line2);
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
  unsigned long now = millis();

  // Link status: the panel sends every 250 ms, so silence means a wiring problem.
  bool linkUp = (lastRxMs != 0) && (now - lastRxMs <= LINK_TIMEOUT_MS);
  if (!linkUp) {
    if (!noLinkShown && now >= 3000) {
      noLinkShown = true;
      showMessage("No panel link", "Check D0 wire");
    }
    return;
  }
  if (noLinkShown) {                        // link came back: restore the display
    noLinkShown = false;
    lcdLastSwapMs = now;
    showPage(lcdPage);
  }

  if (!lcdRotating) return;
  if (now - lcdLastSwapMs >= LCD_ROTATE_MS) {
    lcdLastSwapMs = now;
    lcdPage = (lcdPage + 1) % NUM_PAGES;
    showPage(lcdPage);
  }
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

  static unsigned long lastInitMs = 0;
  if (millis() - lastInitMs >= 1000) {      // re-send the display setup once a second
    lastInitMs = millis();
    maxInit();
    lastShownCs = 0xFFFFFFFF;               // force a redraw
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
}
