#!/usr/bin/env python3
import argparse
import time

import serial


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("reset", "print"))
    parser.add_argument("--port", default="/dev/cu.usbserial-11310")
    args = parser.parse_args()

    connection = serial.Serial()
    connection.port = args.port
    connection.baudrate = 115200
    connection.timeout = 0.05
    connection.dtr = False
    connection.rts = False
    connection.open()
    try:
        connection.reset_input_buffer()
        connection.write(b"r" if args.command == "reset" else b"p")
        deadline = time.monotonic() + 2.0
        quiet_deadline = time.monotonic() + 0.5
        output = bytearray()
        while time.monotonic() < deadline and time.monotonic() < quiet_deadline:
            chunk = connection.read(4096)
            if chunk:
                output.extend(chunk)
                quiet_deadline = time.monotonic() + 0.5
        print(output.decode("utf-8", errors="replace"), end="")
    finally:
        connection.close()


if __name__ == "__main__":
    main()
