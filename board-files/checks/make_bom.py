#!/usr/bin/env python3
"""Group the netlist's components into a fab-ready BOM.

atopile writes its own BOM, but it groups by part number alone, so two parts
that share a value in different packages collapse into one line and the wrong
reel gets ordered. This groups by (value, footprint), which is what an
assembly house actually needs.

Test points are dropped: they are pads, not ordered parts.

    ./checks/make_bom.py build/default.net fab/BOM.csv
"""

import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from netlist_check import load  # noqa: E402


def natural(ref):
    head = ref.rstrip("0123456789")
    tail = ref[len(head):]
    return (head, int(tail) if tail else 0)


def main(argv):
    if len(argv) != 3:
        print("usage: make_bom.py <netlist.net> <out.csv>", file=sys.stderr)
        return 2
    comps, _ = load(argv[1])

    groups = {}
    for ref, data in comps.items():
        if ref.startswith("TP"):
            continue
        key = (data["value"], data["footprint"])
        groups.setdefault(key, []).append(ref)

    rows = []
    for (value, footprint), refs in groups.items():
        refs.sort(key=natural)
        rows.append({
            "Quantity": len(refs),
            "Value": value,
            "Footprint": footprint.split(":")[-1],
            "Library": footprint.split(":")[0] if ":" in footprint else "",
            "Designators": ",".join(refs),
        })
    rows.sort(key=lambda r: (r["Footprint"], r["Value"]))

    with open(argv[2], "w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=[
            "Quantity", "Value", "Footprint", "Library", "Designators"])
        writer.writeheader()
        writer.writerows(rows)

    placements = sum(r["Quantity"] for r in rows)
    print("%d line items, %d placements (test points excluded)"
          % (len(rows), placements))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
