#!/usr/bin/env python3
"""Assert the things a netlist can be made to prove.

Reads the KiCad netlist atopile writes to build/default.net and checks:

  * the pin map, verbatim from the spec
  * that everything on the ext1 wake mask sits on an RTC GPIO
  * that the battery tap sits on ADC1
  * that no signal but the documented GPIO0 test pad touches a strapping pin
  * that each USB-C CC pin has its own 5.1k pulldown, and that both halves of
    D+ and of D- are tied
  * the RV-3028-C7's supply arrangement
  * that the I2C and interrupt pull-ups go to +3V3 and not +BATT
  * that the buttons are active low with no external pull-downs left over
  * that the battery divider is 1M/1M with a filter cap
  * that the blocks carried over from watchy-hardware v2.0 are still wired
    exactly as V2 wires them
  * that the parts this board deletes stay deleted

None of this says the board works. No board has been fabricated. These are
consistency checks against the spec, nothing more.

    ./checks/netlist_check.py build/default.net
"""

import re
import sys

# --------------------------------------------------------------------------
# ESP32-S3-MINI-1 datasheet Table 3-1. Pad number -> GPIO name.
MINI1_PADS = {
    4: "IO0", 5: "IO1", 6: "IO2", 7: "IO3", 8: "IO4", 9: "IO5", 10: "IO6",
    11: "IO7", 12: "IO8", 13: "IO9", 14: "IO10", 15: "IO11", 16: "IO12",
    17: "IO13", 18: "IO14", 19: "IO15", 20: "IO16", 21: "IO17", 22: "IO18",
    23: "IO19", 24: "IO20", 25: "IO21", 26: "IO26", 27: "IO47", 28: "IO33",
    29: "IO34", 30: "IO48", 31: "IO35", 32: "IO36", 33: "IO37", 34: "IO38",
    35: "IO39", 36: "IO40", 37: "IO41", 38: "IO42", 39: "TXD0", 40: "RXD0",
    41: "IO45", 44: "IO46", 45: "EN",
}
MINI1_GND_PADS = {1, 2, 42, 43} | set(range(46, 66))
MINI1_VDD_PAD = 3

# Only IO0-IO21 are RTC GPIOs, so only they can wake ext1 from deep sleep.
RTC_GPIOS = {"IO%d" % n for n in range(0, 22)}
# ADC1 only. ADC2 is unavailable while WiFi is running.
ADC1_GPIOS = {"IO%d" % n for n in range(1, 11)}
# Strapping pins: nothing but the documented GPIO0 test pad may touch these.
STRAPPING = {"IO0", "IO3", "IO45", "IO46"}

MCU = "U1"
RTC = "U6"
USB = "J2"

# The spec's pin map, verbatim.
PIN_MAP = {
    "BTN_MENU": "IO4",
    "BTN_BACK": "IO5",
    "BTN_UP": "IO6",
    "BTN_DOWN": "IO7",
    "RTC_INT": "IO8",
    "BATT_ADC": "IO9",
    "SCL": "IO11",
    "SDA": "IO12",
    "VIB_PWM": "IO17",
    "USB_DM": "IO19",
    "USB_DP": "IO20",
    "EPD_SCK": "IO47",
    "EPD_CS": "IO33",
    "EPD_DC": "IO34",
    "EPD_MOSI": "IO48",
    "EPD_RST": "IO35",
    "EPD_BUSY": "IO36",
    "TP_BOOT": "IO0",
    "TP_TX": "TXD0",
    "TP_RX": "RXD0",
}
BUTTON_NETS = ["BTN_MENU", "BTN_BACK", "BTN_UP", "BTN_DOWN"]
BUTTON_SWITCHES = {"SW1": "BTN_MENU", "SW2": "BTN_BACK",
                   "SW3": "BTN_UP", "SW4": "BTN_DOWN"}

# RV-3028-C7 pin numbers, datasheet "Pin description".
RV3028 = {"CLKOUT": 1, "INT": 2, "SCL": 3, "SDA": 4, "VSS": 5,
          "VBACKUP": 6, "VDD": 7, "EVI": 8}
RV3028_EXPECT = {"VDD": "+BATT", "VSS": "GND", "VBACKUP": "GND", "EVI": "GND",
                 "INT": "RTC_INT", "SDA": "SDA", "SCL": "SCL"}

BANNED_VALUES = ["ESP32-PICO-D4", "CP2102", "BMA423", "PCF8563",
                 "UMH3N", "SWRA117D", "32.768"]

# Blocks carried over from sqfmi/watchy-hardware v2.0 unchanged. Generated
# from V2's Watchy.kicad_pcb, with V2's anonymous Net-(...) names mapped to
# the readable ones this design uses. Any difference here means a block that
# was meant to be transcribed verbatim has drifted.
V2_CARRIED = {
    "C1": {"1": "EN", "2": "GND"},
    "C2": {"1": "EPD_C5", "2": "GND"},
    "C3": {"1": "CP_NEG", "2": "CP_POS"},
    "C4": {"1": "+3V3", "2": "GND"},
    "C5": {"1": "PREVGH", "2": "GND"},
    "C6": {"1": "EPD_C18", "2": "GND"},
    "C7": {"1": "+3V3", "2": "GND"},
    "C8": {"1": "EPD_C20", "2": "GND"},
    "C9": {"1": "EPD_C22", "2": "GND"},
    "C10": {"1": "EPD_C24", "2": "GND"},
    "C11": {"1": "GND", "2": "VBUS"},
    "C12": {"1": "+3V3", "2": "GND"},
    "C14": {"1": "+3V3", "2": "MOTOR_DRV"},
    "D1": {"1": "CP_NEG", "2": "PREVGL"},
    "D2": {"1": "GND", "2": "CP_NEG"},
    "D3": {"1": "PREVGH", "2": "CP_POS"},
    "D4": {"1": "VSYS", "2": "VBUS"},
    "D5": {"1": "LED_A", "2": "VBUS"},
    "D6": {"1": "+3V3", "2": "MOTOR_DRV"},
    "J1": {"2": "GDR", "3": "RESE", "5": "EPD_C5", "8": "GND",
           "9": "EPD_BUSY", "10": "EPD_RST", "11": "EPD_DC", "12": "EPD_CS",
           "13": "EPD_SCK", "14": "EPD_MOSI", "15": "+3V3", "16": "+3V3",
           "17": "GND", "18": "EPD_C18", "20": "EPD_C20", "21": "PREVGH",
           "22": "EPD_C22", "23": "PREVGL", "24": "EPD_C24"},
    "J3": {"1": "+BATT", "2": "GND"},
    "L1": {"1": "CP_POS", "2": "+3V3"},
    "M1": {"1": "+3V3", "2": "MOTOR_DRV"},
    "Q1": {"1": "GDR", "2": "RESE", "3": "CP_POS"},
    "Q4": {"1": "VBUS", "2": "VSYS", "3": "+BATT"},
    "Q5": {"1": "VIB_BASE", "2": "GND", "3": "MOTOR_DRV"},
    "R1": {"1": "+3V3", "2": "EN"},
    "R2": {"1": "+3V3", "2": "SCL"},
    "R3": {"1": "+3V3", "2": "SDA"},
    "R8": {"1": "GDR", "2": "GND"},
    "R9": {"1": "GND", "2": "RESE"},
    "R14": {"1": "VBUS", "2": "GND"},
    "R15": {"1": "GND", "2": "PROG"},
    "R16": {"1": "LED_A", "2": "CHRG"},
    "R17": {"1": "VIB_BASE", "2": "VIB_PWM"},
    "R20": {"1": "+3V3", "2": "EPD_RST"},
    "TP2": {"1": "SCL"},
    "TP3": {"1": "SDA"},
    "TP4": {"1": "+3V3"},
    "TP5": {"1": "GND"},
    "TP6": {"1": "GND"},
    "U2": {"1": "VSYS", "2": "GND", "3": "VSYS", "5": "+3V3"},
    "U5": {"1": "CHRG", "2": "GND", "3": "+BATT", "4": "VBUS", "5": "PROG"},
}

# V2's four button pull-downs. Active-low buttons use the S3's internal
# pull-ups instead, so these must be gone.
V2_BUTTON_PULLDOWNS = ["R4", "R5", "R6", "R10"]
# V2's CLKOUT attenuator, which fed the PICO-D4 its external 32kHz slow clock.
V2_CLKOUT_ATTENUATOR = ["R11", "R18", "R19"]


# --------------------------------------------------------------------------
_TOKENS = re.compile(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+')


def parse_sexp(text):
    """Minimal s-expression reader; the netlist is plain KiCad s-expressions."""
    stack = [[]]
    for tok in _TOKENS.findall(text):
        if tok == "(":
            stack.append([])
        elif tok == ")":
            top = stack.pop()
            stack[-1].append(top)
        elif tok.startswith('"'):
            stack[-1].append(tok[1:-1].replace('\\"', '"'))
        else:
            stack[-1].append(tok)
    return stack[0][0]


def children(node, key):
    return [c for c in node if isinstance(c, list) and c and c[0] == key]


def value_of(node, key, default=""):
    got = children(node, key)
    return got[0][1] if got and len(got[0]) > 1 else default


def load(path):
    """Parse the netlist. Returns (components, nets).

    components: ref -> {"value": str, "footprint": str}
    nets:       net name -> list of (ref, pad) tuples, pad as a string
    """
    root = parse_sexp(open(path, encoding="utf-8").read())

    comps = {}
    for section in children(root, "components"):
        for comp in children(section, "comp"):
            ref = value_of(comp, "ref")
            comps[ref] = {
                "value": value_of(comp, "value"),
                "footprint": value_of(comp, "footprint"),
            }

    nets = {}
    for section in children(root, "nets"):
        for net in children(section, "net"):
            name = value_of(net, "name")
            nodes = [(value_of(n, "ref"), value_of(n, "pin"))
                     for n in children(net, "node")]
            nets.setdefault(name, []).extend(nodes)
    return comps, nets


def pads_on(nets, net, ref):
    return sorted(p for r, p in nets.get(net, []) if r == ref)


def net_of(nets, ref, pad):
    pad = str(pad)
    for name, nodes in nets.items():
        if (ref, pad) in nodes:
            return name
    return None


def nets_of(nets, ref):
    """All nets a component touches."""
    return {n for n, nodes in nets.items() if any(r == ref for r, _ in nodes)}


def two_pin_between(comps, nets, net_a, net_b, prefix="R"):
    """Refdes of any 2-pin part with one pad on net_a and the other on net_b."""
    found = []
    for ref in {r for r, _ in nets.get(net_a, []) if r.startswith(prefix)}:
        if net_b in nets_of(nets, ref) - {net_a}:
            found.append(ref)
    return sorted(found)


def norm(value):
    return value.upper().replace(" ", "").replace("OHM", "")


# --------------------------------------------------------------------------
def check_pin_map(comps, nets):
    bad = []
    for net, gpio in PIN_MAP.items():
        pads = pads_on(nets, net, MCU)
        if not pads:
            bad.append("net %s has no connection to %s" % (net, MCU))
            continue
        got = {MINI1_PADS.get(int(p), "pad%s" % p) for p in pads
               if p.isdigit()}
        if gpio not in got:
            bad.append("net %s is on %s, spec says %s" %
                       (net, ",".join(sorted(got)), gpio))
    return bad


def check_wake_capable(comps, nets):
    bad = []
    for net in BUTTON_NETS + ["RTC_INT"]:
        for pad in pads_on(nets, net, MCU):
            gpio = MINI1_PADS.get(int(pad), "pad%s" % pad)
            if gpio not in RTC_GPIOS:
                bad.append("%s is on %s, which is not an RTC GPIO and cannot "
                           "wake ext1 from deep sleep" % (net, gpio))
    return bad


def check_adc1(comps, nets):
    bad = []
    for pad in pads_on(nets, "BATT_ADC", MCU):
        gpio = MINI1_PADS.get(int(pad), "pad%s" % pad)
        if gpio not in ADC1_GPIOS:
            bad.append("BATT_ADC is on %s, which is ADC2 and unusable with "
                       "WiFi on" % gpio)
    return bad


def check_strapping(comps, nets):
    bad = []
    for pad, gpio in MINI1_PADS.items():
        if gpio not in STRAPPING:
            continue
        net = net_of(nets, MCU, pad)
        if net is None:
            continue
        allowed = {"TP_BOOT"} if gpio == "IO0" else set()
        # An unconnected pin shows up as its own single-node net.
        if len(nets.get(net, [])) <= 1:
            continue
        if net not in allowed:
            bad.append("strapping pin %s is on net %s" % (gpio, net))
    return bad


def check_usb_c(comps, nets):
    bad = []
    for cc in ("CC1", "CC2"):
        rs = two_pin_between(comps, nets, cc, "GND")
        if not rs:
            bad.append("%s has no pulldown to GND; a C-to-C cable will "
                       "deliver no power" % cc)
            continue
        if len(rs) > 1:
            bad.append("%s has %d pulldowns (%s); expected exactly one" %
                       (cc, len(rs), ",".join(rs)))
        for r in rs:
            if norm(comps.get(r, {}).get("value", "")) != "5.1K":
                bad.append("%s pulldown %s is %r, expected 5.1k" %
                           (cc, r, comps.get(r, {}).get("value")))
    # A single resistor shared between both CC pins breaks reversibility.
    shared = set(two_pin_between(comps, nets, "CC1", "GND")) & \
        set(two_pin_between(comps, nets, "CC2", "GND"))
    if shared:
        bad.append("%s is shared between CC1 and CC2; each pin needs its own "
                   "5.1k" % ",".join(sorted(shared)))
    # Both halves of each differential pin must be tied, or the connector
    # only works one way up.
    for net, pads in (("USB_DP", {"A6", "B6"}), ("USB_DM", {"A7", "B7"})):
        got = set(pads_on(nets, net, USB))
        if not pads <= got:
            bad.append("%s reaches %s pads %s; both %s must be tied or the "
                       "connector only works in one orientation" %
                       (net, USB, sorted(got) or "none", sorted(pads)))
    return bad


def check_rtc(comps, nets):
    bad = []
    if RTC not in comps:
        return ["%s is missing from the design" % RTC]
    if "RV-3028" not in comps[RTC]["value"].upper().replace(" ", ""):
        bad.append("%s is %r, expected RV-3028-C7" % (RTC, comps[RTC]["value"]))
    for pin, want in RV3028_EXPECT.items():
        got = net_of(nets, RTC, RV3028[pin])
        if got != want:
            bad.append("%s pin %d (%s) is on %s, spec says %s" %
                       (RTC, RV3028[pin], pin, got, want))
    # VBACKUP must not be fed from a rail: in direct switching mode the part
    # runs from whichever supply is higher and disables SDA in the backup
    # state, which on this board would break I2C.
    backup = net_of(nets, RTC, RV3028["VBACKUP"])
    if backup in ("+BATT", "+3V3", "VBUS"):
        bad.append("%s VBACKUP is on %s; direct switchover would disable SDA "
                   "whenever that rail is the higher one" % (RTC, backup))
    return bad


def check_pullups(comps, nets):
    bad = []
    for net in ("SDA", "SCL", "RTC_INT"):
        if not two_pin_between(comps, nets, net, "+3V3"):
            bad.append("%s has no pull-up to +3V3" % net)
        if two_pin_between(comps, nets, net, "+BATT"):
            bad.append("%s is pulled up to +BATT; at 4.2V that overdrives the "
                       "S3 input, it must pull up to +3V3" % net)
    return bad


def check_buttons(comps, nets):
    bad = []
    for sw, net in BUTTON_SWITCHES.items():
        if sw not in comps:
            bad.append("%s is missing" % sw)
            continue
        touched = nets_of(nets, sw)
        if "GND" not in touched:
            bad.append("%s does not reach GND; the buttons are active low on "
                       "this board" % sw)
        if net not in touched:
            bad.append("%s is on %s, expected %s" %
                       (sw, ",".join(sorted(touched - {"GND"})) or "nothing",
                        net))
        if "+3V3" in touched:
            bad.append("%s still reaches +3V3; V2's active-high wiring has "
                       "not been removed" % sw)
    for net in BUTTON_NETS:
        downs = two_pin_between(comps, nets, net, "GND")
        if downs:
            bad.append("%s has a pull-down to GND (%s); active-low buttons "
                       "use the S3's internal pull-ups" %
                       (net, ",".join(downs)))
    for ref in V2_BUTTON_PULLDOWNS:
        if ref in comps:
            bad.append("%s is V2's button pull-down and should be deleted"
                       % ref)
    return bad


def check_batt_divider(comps, nets):
    bad = []
    top = two_pin_between(comps, nets, "BATT_ADC", "+BATT")
    bottom = two_pin_between(comps, nets, "BATT_ADC", "GND")
    if not top:
        bad.append("BATT_ADC has no resistor to +BATT")
    if not bottom:
        bad.append("BATT_ADC has no resistor to GND")
    for ref in top + bottom:
        if norm(comps.get(ref, {}).get("value", "")) != "1M":
            bad.append("battery divider leg %s is %r, spec says 1M (V2's "
                       "100k pair costs 21uA continuously)" %
                       (ref, comps.get(ref, {}).get("value")))
    if not two_pin_between(comps, nets, "BATT_ADC", "GND", prefix="C"):
        bad.append("BATT_ADC has no filter capacitor to GND; at 1M source "
                   "impedance the tap needs one")
    return bad


def check_mcu_power(comps, nets):
    bad = []
    if MCU not in comps:
        return ["%s is missing" % MCU]
    if "S3-MINI-1" not in comps[MCU]["value"].upper().replace(" ", ""):
        bad.append("%s is %r, expected ESP32-S3-MINI-1" %
                   (MCU, comps[MCU]["value"]))
    if "N4R2" in comps[MCU]["value"].upper():
        bad.append("%s is the -N4R2 variant, which consumes IO26 for PSRAM"
                   % MCU)
    if net_of(nets, MCU, MINI1_VDD_PAD) != "+3V3":
        bad.append("%s pad %d (3V3) is on %s, not +3V3" %
                   (MCU, MINI1_VDD_PAD, net_of(nets, MCU, MINI1_VDD_PAD)))
    stray = sorted(p for p in MINI1_GND_PADS
                   if net_of(nets, MCU, p) not in (None, "GND"))
    if stray:
        bad.append("%s GND pads on a non-GND net: %s" % (MCU, stray))
    missing = sorted(p for p in MINI1_GND_PADS if net_of(nets, MCU, p) is None)
    if missing:
        bad.append("%s GND pads not connected at all: %s" % (MCU, missing))
    if net_of(nets, MCU, 45) != "EN":
        bad.append("%s pad 45 (EN) is on %s, not EN" %
                   (MCU, net_of(nets, MCU, 45)))
    return bad


def check_v2_carried(comps, nets):
    """The blocks meant to come over from V2 verbatim really did."""
    bad = []
    for ref, pads in sorted(V2_CARRIED.items()):
        if ref not in comps:
            bad.append("%s is carried over from V2 but is missing" % ref)
            continue
        for pad, want in sorted(pads.items()):
            got = net_of(nets, ref, pad)
            if got != want:
                bad.append("%s pad %s is on %s, V2 has it on %s" %
                           (ref, pad, got, want))
    return bad


def check_deleted(comps, nets):
    bad = []
    for ref, data in sorted(comps.items()):
        for banned in BANNED_VALUES:
            if banned.upper() in data["value"].upper():
                bad.append("%s is %s; this board deletes that part" %
                           (ref, data["value"]))
    for ref in V2_CLKOUT_ATTENUATOR:
        if ref in comps:
            bad.append("%s is part of V2's CLKOUT attenuator; the S3 runs its "
                       "RTC domain on the internal RC (spec D5)" % ref)
    return bad


CHECKS = [
    ("pin map", check_pin_map),
    ("deep-sleep wake pins", check_wake_capable),
    ("battery ADC on ADC1", check_adc1),
    ("strapping pins", check_strapping),
    ("USB-C CC pulldowns and D+/D- tie", check_usb_c),
    ("RV-3028-C7 wiring", check_rtc),
    ("bus pull-ups", check_pullups),
    ("buttons active low", check_buttons),
    ("battery divider", check_batt_divider),
    ("MCU power", check_mcu_power),
    ("V2 blocks carried over unchanged", check_v2_carried),
    ("deleted parts", check_deleted),
]


def main(argv):
    if len(argv) != 2:
        print("usage: netlist_check.py <netlist.net>", file=sys.stderr)
        return 2
    comps, nets = load(argv[1])
    if not comps:
        print("no components parsed out of %s" % argv[1], file=sys.stderr)
        return 2
    failures = 0
    for title, fn in CHECKS:
        problems = fn(comps, nets)
        if problems:
            failures += len(problems)
            print("FAIL %s" % title)
            for p in problems:
                print("       %s" % p)
        else:
            print("ok   %s" % title)
    print()
    print("%d component(s), %d net(s), %d problem(s)"
          % (len(comps), len(nets), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
