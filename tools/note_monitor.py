#!/usr/bin/env python3
"""
Laser Harp - live Note/String monitor.

Reads the ESP32's own USB-serial debug output (the same stream
`pio device monitor` shows) and renders a minimal live display of:
  - which instrument/"string" is currently selected
  - which note was last triggered

It does NOT go through PlatformIO -- it opens the serial port itself
with pyserial, so close any running `pio device monitor` /
Arduino Serial Monitor first (only one program can hold the port).

Usage:
    pip3 install pyserial rich      # one-time setup
    python3 note_monitor.py                     # auto-detect port
    python3 note_monitor.py --port /dev/cu.usbserial-0001
    python3 note_monitor.py --baud 115200        # default, matches Serial.begin(115200) in main.cpp

It parses exactly the lines esp32-fw/src/main.cpp already prints, e.g.:
    >>> ACTIVE INSTRUMENT [1/6]: Grand Piano <<<
    [Grand Piano] Beam 3 BLOCKED -> playing F4
No firmware changes needed -- this just reads what's already there.
"""

import argparse
import re
import sys
import time
from datetime import datetime

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("Missing dependency. Install with:  pip3 install pyserial", file=sys.stderr)
    sys.exit(1)

try:
    from rich.console import Console
    from rich.live import Live
    from rich.panel import Panel
    from rich.align import Align
    from rich.table import Table
    from rich.text import Text
    HAVE_RICH = True
except ImportError:
    HAVE_RICH = False

INSTRUMENT_RE = re.compile(r'>>> ACTIVE INSTRUMENT \[(\d+)/(\d+)\]: (.+?) <<<')
NOTE_RE = re.compile(r'^\[(.+?)\] Beam (\d+) BLOCKED -> playing (.+)$')

HISTORY_MAX = 8


def pick_port(explicit_port):
    if explicit_port:
        return explicit_port
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found. Plug in the ESP32 and try again, "
              "or pass --port explicitly.", file=sys.stderr)
        sys.exit(1)
    if len(ports) == 1:
        return ports[0].device
    print("Multiple serial ports found:")
    for i, p in enumerate(ports):
        print(f"  [{i}] {p.device}  ({p.description})")
    choice = input("Pick a port number: ").strip()
    try:
        return ports[int(choice)].device
    except (ValueError, IndexError):
        print("Invalid choice.", file=sys.stderr)
        sys.exit(1)


def render_rich(state):
    body = Table.grid(padding=(0, 2))
    body.add_column(justify="right", style="dim")
    body.add_column(justify="left")

    string_text = Text(state["instrument"] or "-- waiting --", style="bold cyan")
    note_text = Text(state["note"] or "--", style="bold yellow")
    body.add_row("STRING:", string_text)
    body.add_row("NOTE:", note_text)

    hist_lines = "\n".join(
        f"[dim]{t}[/dim]  {instr} -> {note}"
        for (t, instr, note) in state["history"]
    ) or "[dim](nothing played yet)[/dim]"

    inner = Table.grid()
    inner.add_row(Align.center(body))
    inner.add_row("")
    inner.add_row(Text("recent:", style="dim"))
    inner.add_row(hist_lines)

    return Panel(inner, title="Laser Harp - Live", border_style="green", padding=(1, 4))


def render_plain(state):
    print("\033[2J\033[H", end="")  # clear screen
    print("=== Laser Harp - Live ===")
    print(f"STRING: {state['instrument'] or '-- waiting --'}")
    print(f"NOTE:   {state['note'] or '--'}")
    print()
    print("recent:")
    if not state["history"]:
        print("  (nothing played yet)")
    for (t, instr, note) in state["history"]:
        print(f"  {t}  {instr} -> {note}")


def main():
    parser = argparse.ArgumentParser(description="Live note/string monitor for the laser harp ESP32.")
    parser.add_argument("--port", default=None, help="Serial port (e.g. /dev/cu.usbserial-0001). Auto-detected if omitted.")
    parser.add_argument("--baud", type=int, default=115200, help="Baud rate (default 115200, matches Serial.begin in main.cpp).")
    args = parser.parse_args()

    port = pick_port(args.port)
    print(f"Opening {port} @ {args.baud}...")

    try:
        ser = serial.Serial(port, args.baud, timeout=1)
    except serial.SerialException as e:
        print(f"Could not open {port}: {e}", file=sys.stderr)
        print("Is PlatformIO's serial monitor (or the Arduino IDE) already open on this port? "
              "Only one program can use it at a time -- close that first.", file=sys.stderr)
        sys.exit(1)

    time.sleep(1.5)   # let the board settle after the port opens (it may auto-reset)
    ser.reset_input_buffer()

    state = {"instrument": None, "note": None, "history": []}

    def process_line(line):
        line = line.strip()
        if not line:
            return
        m = INSTRUMENT_RE.search(line)
        if m:
            state["instrument"] = m.group(3)
            return
        m = NOTE_RE.match(line)
        if m:
            state["instrument"] = m.group(1)
            state["note"] = m.group(3)
            ts = datetime.now().strftime("%H:%M:%S")
            state["history"].insert(0, (ts, m.group(1), m.group(3)))
            del state["history"][HISTORY_MAX:]

    try:
        if HAVE_RICH:
            console = Console()
            with Live(render_rich(state), console=console, refresh_per_second=15) as live:
                while True:
                    raw = ser.readline()
                    if raw:
                        try:
                            line = raw.decode("utf-8", errors="ignore")
                        except Exception:
                            continue
                        process_line(line)
                        live.update(render_rich(state))
        else:
            print("(tip: `pip3 install rich` for a nicer live display)")
            while True:
                raw = ser.readline()
                if raw:
                    line = raw.decode("utf-8", errors="ignore")
                    process_line(line)
                    render_plain(state)
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        print("\nClosed serial connection.")


if __name__ == "__main__":
    main()
