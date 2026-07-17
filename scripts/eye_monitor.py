#!/usr/bin/env python3
"""Stream AI Pet eye + MCP tool logs from COM3 to stdout (one matching line per event)."""
import re
import sys
import serial

# Force UTF-8 on stdout so Chinese chat logs aren't mangled by the GBK console.
sys.stdout.reconfigure(encoding="utf-8", errors="replace")

PATTERN = re.compile(
    r"PetEye|EyeController|self\.eye|tools/call|tools/list|Emotion ->|Gaze ->|Blink|MCP:|mcp_server"
)


def main() -> None:
    port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
    ser = serial.Serial(port, 115200, timeout=1.0)
    ser.dtr = False
    ser.rts = False
    try:
        ser.reset_input_buffer()
        while True:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            if PATTERN.search(line):
                # flush per line so the monitor sees it immediately
                print(line, flush=True)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
