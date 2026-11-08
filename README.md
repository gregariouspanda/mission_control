# Mission Control

Two Arduino Uno R3 boards for a mission-control prop:

| Board | Sketch | Job |
|---|---|---|
| A: Switch Panel | [`switch_panel/switch_panel.ino`](switch_panel/switch_panel.ino) | Reads six on-on toggle switches, lights the six LEDs, plays the DFPlayer audio for each switch position, and sends the switch states to board B |
| B: Mission Control | [`mission_control.ino`](mission_control.ino) | Receives the switch states and drives the LCD (message follows the switch position), and 8-digit timer display |

## Link between the boards

| Board A (Switch Panel) | Board B (Mission Control) |
|---|---|
| D2 (TX) | D0 (RX) |
| GND | GND |

Unplug the D0 wire from board B while uploading its sketch. Board A sends a text line like `S0100012` at 9600 baud: six characters that are `1` when that switch is active (its pin pulled LOW), then a check digit equal to the number of `1`s. Board B ignores any line whose check digit doesn't match, and its LCD shows "No panel link / Check D0 wire" if no valid line arrives for 2 seconds. It sends on every change and repeats every 250 ms.

## Pin chart

| Uno pin | Board A: Switch Panel | Board B: Mission Control |
|---|---|---|
| D0 (RX) | USB serial, keep free | Link from board A D2 (unplug while uploading) |
| D1 (TX) | USB serial, keep free | USB serial, keep free |
| D2 | Link TX to board B D0 | free |
| D3 | Switch 1 | free |
| D4 | Switch 2 | free |
| D5 | Switch 3 | free |
| D6 | Switch 4 | free |
| D7 | Switch 5 | free |
| D8 | Switch 6 | free |
| D9 | DFPlayer RX (through ~1k) | free |
| D10 | DFPlayer TX | free |
| D11 | unused (dummy link RX in code) | HW-179 CLK |
| D12 | free | HW-179 DIN |
| D13 | free | HW-179 CS (onboard LED flickers) |
| A0 | LED 1 | free |
| A1 | LED 2 | free |
| A2 | LED 3 | free |
| A3 | LED 4 | free |
| A4 | LED 5 | LCD SDA |
| A5 | LED 6 | LCD SCL |
| 5V | DFPlayer VCC | HW-179 VCC, LCD VCC |
| GND | DFPlayer GND, link GND | HW-179 GND, LCD GND, link GND |

Switches are active LOW (`INPUT_PULLUP`). All grounds must be common, including between the two boards.

## Board A (Switch Panel) wiring

| Part | Arduino Uno R3 |
|---|---|
| Switch 1-6 | D3, D4, D5, D6, D7, D8 (active throw to GND) |
| LED 1-6 | A0, A1, A2, A3, A4, A5 (through the PCB resistors) |
| DFPlayer Mini TX -> Arduino | D10 (Arduino RX) |
| Arduino -> DFPlayer RX | D9 (Arduino TX), through ~1k resistor |
| DFPlayer VCC / GND | 5V / GND |
| DFPlayer SPK_1 / SPK_2 | speaker |
| Link to board B | D2 |

Each LED is powered from its Arduino pin and is on while its switch is active. Moving a switch to a position plays that position's track (see SD card tracks below).

## Parts

- [Arduino Uno R3](https://store-usa.arduino.cc/products/arduino-uno-rev3?utm_source=google&utm_medium=cpc&utm_campaign=US-Pmax&gad_source=1&gad_campaignid=21317508903&gbraid=0AAAAACbEa84YkpgRf37rKlTbKfjsyDoIh&gclid=Cj0KCQjwrvHWBhDKARIsABmp7h3uA3D3UE52SMWmsBiArJ4GiXsLNI7Favq0q5SRHtq013amH8c-VRcaAp9uEALw_wcB)
- DFPlayer Mini + microSD card + [speaker](https://www.amazon.com/dp/B0C6F9RGXF?ref=ppx_yo2ov_dt_b_fed_asin_title)
- [HW-179 8-digit display (MAX7219)](https://www.amazon.com/dp/B01D0WSCJA?ref=ppx_yo2ov_dt_b_fed_asin_title)
- [16x2 I2C LCD](https://www.amazon.com/dp/B019K5X53O?ref=ppx_yo2ov_dt_b_fed_asin_title)
- [6 toggle switches](https://www.amazon.com/dp/B08MZRB4T1?ref=ppx_yo2ov_dt_b_fed_asin_title), 1 momentary push button
- ~1k resistor (DFPlayer RX line)
- [6 x LED](https://www.amazon.com/dp/B0DHX6DD85?ref=ppx_yo2ov_dt_b_fed_asin_title) + resistors

## Libraries (Arduino Library Manager)

- LiquidCrystal I2C (Frank de Brabander)
- DFRobotDFPlayerMini

The MAX7219 display is driven directly; no library needed.

## Board B (Mission Control) wiring

| Part | Part pin | Arduino Uno R3 |
|---|---|---|
| HW-179 (MAX7219) | CLK | D11 |
| | DIN | D12 |
| | CS | D13 |
| | VCC / GND | 5V / GND |
| LCD 16x2 I2C | SDA | SDA (same line as A4) |
| | SCL | SCL (same line as A5) |
| | VCC / GND | 5V / GND |

Notes:

- LCD VCC goes to 5V, not AREF.
- All grounds must be common.
- Free pins on board B: D2-D10, A0-A3. D0 is the link from board A.
- D13 also drives the onboard LED, which may flicker with display updates.

## SD card tracks

Files are named `0001.mp3`, `0002.mp3`, ... in the SD card root of the DFPlayer on board A.

| Trigger | Track(s) |
|---|---|
| Boot | 1 |
| Switch 1 | 10 |
| Switch 2 "Comms" | random of 2, 3, 4, 5, 7 |
| Switch 3 "Navigation" | 6 or 8 |
| Switch 4 "Mission Brief" | random of 12-16 |
| Switch 5 "Spacewalk" | random of 17-19 |
| Switch 6 | 9 (placeholder) |

Switches are ignored for 2 seconds after boot so the boot track isn't cut off. Audio plays when a switch moves to its ON position. When a switch moves from ON to OFF it starts a new record: a random track from `FIRST_TRACK` to `LAST_TRACK` (2 to 19) that is never the one just played. To use a fixed track for a switch's OFF position instead, set its entry in `OFF_TRACKS` in `switch_panel.ino` (0 = random).

## LCD

Rotates every 5 seconds through:

1. Distance to / Moon: 238,900mi
2. Happy Birthday / `<astronaut name>!`
3. Select a Mission

When a switch moves, the idle pages stop and the LCD shows that switch's name on line 1 and "Switch X active" (or "Switch X off") on line 2, from `SWITCH_MSGS` in `mission_control.ino`. The most recent toggle controls the LCD.

Set the name by editing `ASTRONAUT_NAME` in the Configuration section at the top of `mission_control.ino` (max 15 characters).

If the screen is blank, adjust the contrast pot on the backpack, or change `LCD_ADDRESS` from `0x27` to `0x3F`.

## Timer

- Shown as `HH.MM.SS.cc` on the HW-179 (`cc` = hundredths of a second).
- `COUNT_DOWN = false` counts up from 0; set it to `true` to count down from `MISSION_SECONDS` (default 60 minutes).
- `TEST_TIMER_ROTATION = true` runs a display test instead: it cycles through count-up, count-down and hour-rollover phases, 10 seconds each (edit `TEST_PHASES`). Set it to `false` for normal use.
- The timer restarts when board B is reset or powered on. There is no reset button for now.

## KiCad schematic

[`kicad/mission_control.kicad_sch`](kicad/mission_control.kicad_sch) is the wiring for both boards (open it in KiCad 8 or newer). It is generated by [`kicad/generate_schematic.py`](kicad/generate_schematic.py): edit the pin and net names in the `PARTS` table, then run `python3 generate_schematic.py` to rebuild it.

Parts are simple generic symbols joined by net labels instead of wires. They have no footprints yet, so assign footprints in KiCad before making a PCB. KiCad's ERC reports a missing-library warning for each part because the symbols are embedded in the file.
