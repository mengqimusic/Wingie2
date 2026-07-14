#!/usr/bin/env python3
import argparse
import pathlib
import sys


START = "/**************************BEGIN USER SECTION **************************/"
END = "/***************************END USER SECTION ***************************/"


def extract(source):
    start = source.find(START)
    end = source.find(END, start + len(START))
    if start < 0 or end < 0 or end <= start:
        raise ValueError("generated user-section markers not found")
    return source[start + len(START):end].strip() + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input_cpp")
    parser.add_argument("output_header")
    arguments = parser.parse_args()
    try:
        source = pathlib.Path(arguments.input_cpp).read_text(encoding="utf-8")
        extracted = extract(source)
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        raise SystemExit(2)
    pathlib.Path(arguments.output_header).write_text(extracted, encoding="utf-8")


if __name__ == "__main__":
    main()
