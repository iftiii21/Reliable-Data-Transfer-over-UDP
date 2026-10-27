#!/usr/bin/env python3
"""Run a bounded child command and record elapsed monotonic seconds."""
import subprocess
import sys
import time


def main() -> int:
    if len(sys.argv) < 3:
        print("usage: monotonic_time.py OUTPUT_FILE COMMAND [ARG ...]", file=sys.stderr)
        return 2
    output_path = sys.argv[1]
    started = time.monotonic()
    try:
        result = subprocess.run(sys.argv[2:], check=False)
    except OSError as exc:
        print(f"could not run command: {exc}", file=sys.stderr)
        return 127
    elapsed = time.monotonic() - started
    try:
        with open(output_path, "w", encoding="ascii") as output:
            output.write(f"{elapsed:.6f}\n")
    except OSError as exc:
        print(f"could not write elapsed time: {exc}", file=sys.stderr)
        return 125
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
