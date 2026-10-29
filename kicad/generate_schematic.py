#!/usr/bin/env python3
"""Generates mission_control.kicad_sch (KiCad 8) for the two-Arduino mission control prop.

Run:  python3 generate_schematic.py
Parts are simple generic symbols joined by net labels (no wires), so connections
are defined purely by the net names in the PARTS table below.
"""
import uuid

U = 2.54          # one grid unit in mm
PITCH = 2 * U     # pin spacing
FONT = "(effects (font (size 1.27 1.27)))"
HIDDEN = "(effects (font (size 1.27 1.27)) (hide yes))"


def uid():
    return str(uuid.uuid4())


# ---------- Symbol definitions: name -> (ref prefix, half-width units, left pins, right pins)
SYMBOLS = {
    "ARDUINO_A": ("U", 5, ["D3", "D4", "D5", "D6", "D7", "D8", "D2"],
                  ["A0", "A1", "A2", "A3", "A4", "A5", "5V", "GND"]),
    "ARDUINO_B": ("U", 5, ["D0_RX", "D2", "5V", "GND"],
                  ["D9", "D10", "D11", "D12", "D13", "A4_SDA", "A5_SCL"]),
    "SW_SPDT":   ("SW", 3, ["COM"], ["ACTIVE", "NC"]),
    "SW_PUSH":   ("SW", 2, ["1"], ["2"]),
    "R":         ("R", 2, ["1"], ["2"]),
    "LED":       ("D", 2, ["A"], ["K"]),
    "DFPLAYER":  ("U", 5, ["VCC", "RX", "TX", "GND"], ["SPK1", "SPK2"]),
    "SPEAKER":   ("LS", 2, ["+", "-"], []),
    "LCD_I2C":   ("DS", 5, ["GND", "VCC", "SDA", "SCL"], []),
    "HW179":     ("DS", 5, ["VCC", "GND", "DIN", "CS", "CLK"], []),
}


def layout(name):
    prefix, hw, left, right = SYMBOLS[name]
    nmax = max(len(left), len(right))
    pins = []   # (name, number, side, x_mm, y_mm)
    n = 1
    for i, p in enumerate(left):
        pins.append((p, str(n), "L", -(hw + 2) * U, PITCH * (nmax - 1 - i))); n += 1
    for i, p in enumerate(right):
        pins.append((p, str(n), "R", (hw + 2) * U, PITCH * (nmax - 1 - i))); n += 1
    top = PITCH * (nmax - 1) + U
    return prefix, hw * U, top, pins


def lib_symbol(name):
    prefix, hw, top, pins = layout(name)
    s = f'    (symbol "mission:{name}" (exclude_from_sim no) (in_bom yes) (on_board yes)\n'
    s += f'      (property "Reference" "{prefix}" (at 0 {top + U:.2f} 0) {FONT})\n'
    s += f'      (property "Value" "{name}" (at 0 {-2 * U:.2f} 0) {FONT})\n'
    s += f'      (property "Footprint" "" (at 0 0 0) {HIDDEN})\n'
    s += f'      (property "Datasheet" "" (at 0 0 0) {HIDDEN})\n'
    s += f'      (symbol "{name}_0_1"\n'
    s += (f'        (rectangle (start {-hw:.2f} {top:.2f}) (end {hw:.2f} {-U:.2f}) '
          f'(stroke (width 0.254) (type default)) (fill (type background)))\n      )\n')
    s += f'      (symbol "{name}_1_1"\n'
    for pname, num, side, x, y in pins:
        ang = 0 if side == "L" else 180
        s += (f'        (pin passive line (at {x:.2f} {y:.2f} {ang}) (length {2 * U:.2f}) '
              f'(name "{pname}" {FONT}) (number "{num}" {FONT}))\n')
    s += '      )\n    )\n'
    return s


# ---------- Parts: (ref, symbol, value, x_units, y_units, {pin name: net or None for no-connect})
PARTS = []
for i in range(6):
    n = i + 1
    y = 14 + 8 * i
    PARTS.append((f"SW{n}", "SW_SPDT", f"Switch {n} (on-on)", 12, y,
                  {"COM": f"SW{n}_IN", "ACTIVE": "GND", "NC": None}))
    PARTS.append((f"R{n}", "R", "220", 58, y, {"1": f"LED{n}_DRV", "2": f"LED{n}_A"}))
    PARTS.append((f"D{n}", "LED", f"LED {n}", 74, y, {"A": f"LED{n}_A", "K": "GND"}))

PARTS.append(("U1", "ARDUINO_A", "Arduino Uno A - Switch Panel", 36, 40, {
    "D3": "SW1_IN", "D4": "SW2_IN", "D5": "SW3_IN", "D6": "SW4_IN", "D7": "SW5_IN", "D8": "SW6_IN",
    "D2": "LINK",
    "A0": "LED1_DRV", "A1": "LED2_DRV", "A2": "LED3_DRV", "A3": "LED4_DRV", "A4": "LED5_DRV",
    "A5": "LED6_DRV", "5V": "+5V", "GND": "GND"}))

PARTS += [
    ("U2", "ARDUINO_B", "Arduino Uno B - Mission Control", 112, 40, {
        "D0_RX": "LINK", "D2": "RESET_BTN", "5V": "+5V", "GND": "GND",
        "D9": "DFP_TX_SER", "D10": "DFP_RX_SER", "D11": "MAX_CLK", "D12": "MAX_DIN", "D13": "MAX_CS",
        "A4_SDA": "SDA", "A5_SCL": "SCL"}),
    ("SW7", "SW_PUSH", "Reset (push button)", 92, 36, {"1": "RESET_BTN", "2": "GND"}),
    ("R7", "R", "1k", 134, 18, {"1": "DFP_TX_SER", "2": "DFP_RX"}),
    ("U3", "DFPLAYER", "DFPlayer Mini", 152, 24, {
        "VCC": "+5V", "RX": "DFP_RX", "TX": "DFP_RX_SER", "GND": "GND", "SPK1": "SPK1", "SPK2": "SPK2"}),
    ("LS1", "SPEAKER", "Speaker", 152, 38, {"+": "SPK1", "-": "SPK2"}),
    ("DS1", "LCD_I2C", "LCD 16x2 I2C", 152, 50, {"GND": "GND", "VCC": "+5V", "SDA": "SDA", "SCL": "SCL"}),
    ("DS2", "HW179", "HW-179 (MAX7219)", 152, 70, {
        "VCC": "+5V", "GND": "GND", "DIN": "MAX_DIN", "CS": "MAX_CS", "CLK": "MAX_CLK"}),
]

NOTES = [
    (8, 6, "Board A - Switch Panel: on-on switches pull their pin LOW; matching LED lights; states sent on LINK"),
    (92, 6, "Board B - Mission Control: receives LINK on D0 (unplug while uploading), drives audio, LCD, display"),
    (8, 100, "Share GND between both Arduinos and all modules. Power both boards and all modules from 5V."),
]

root = uid()
out = ['(kicad_sch (version 20231120) (generator "eeschema") (generator_version "8.0")\n',
       f'  (uuid "{root}")\n  (paper "A3")\n  (lib_symbols\n']
for name in SYMBOLS:
    out.append(lib_symbol(name))
out.append("  )\n")

labels, nocons, syms = [], [], []
for ref, sym, value, xu, yu, nets in PARTS:
    prefix, hw, top, pins = layout(sym)
    sx, sy = xu * U, yu * U
    s = (f'  (symbol (lib_id "mission:{sym}") (at {sx:.2f} {sy:.2f} 0) (unit 1)\n'
         f'    (exclude_from_sim no) (in_bom yes) (on_board yes) (dnp no) (uuid "{uid()}")\n'
         f'    (property "Reference" "{ref}" (at {sx:.2f} {sy - top - U:.2f} 0) {FONT})\n'
         f'    (property "Value" "{value}" (at {sx:.2f} {sy + 2 * U:.2f} 0) {FONT})\n'
         f'    (property "Footprint" "" (at {sx:.2f} {sy:.2f} 0) {HIDDEN})\n'
         f'    (property "Datasheet" "" (at {sx:.2f} {sy:.2f} 0) {HIDDEN})\n')
    for pname, num, side, px, py in pins:
        s += f'    (pin "{num}" (uuid "{uid()}"))\n'
        net = nets[pname]
        x, y = sx + px, sy - py
        if net is None:
            nocons.append(f'  (no_connect (at {x:.2f} {y:.2f}) (uuid "{uid()}"))\n')
        else:
            ang, just = (180, "right bottom") if side == "L" else (0, "left bottom")
            labels.append(f'  (label "{net}" (at {x:.2f} {y:.2f} {ang})\n'
                          f'    (effects (font (size 1.27 1.27)) (justify {just})) (uuid "{uid()}"))\n')
    s += (f'    (instances (project "mission_control" (path "/{root}" '
          f'(reference "{ref}") (unit 1))))\n  )\n')
    syms.append(s)
    assert set(nets) == {p[0] for p in pins}, f"pin mismatch on {ref}"

out += syms + nocons + labels
for xu, yu, text in NOTES:
    out.append(f'  (text "{text}" (at {xu * U:.2f} {yu * U:.2f} 0)\n'
               f'    (effects (font (size 1.8 1.8)) (justify left bottom)) (uuid "{uid()}"))\n')
out.append('  (sheet_instances (path "/" (page "1")))\n)\n')

with open("mission_control.kicad_sch", "w") as f:
    f.write("".join(out))
print("wrote mission_control.kicad_sch")
