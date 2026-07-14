#!/usr/bin/env python3
import pathlib
import subprocess
import sys
import tempfile


SCRIPT = pathlib.Path(__file__).with_name("extract_faust_user.py")
START = "/**************************BEGIN USER SECTION **************************/"
END = "/***************************END USER SECTION ***************************/"


def main():
    with tempfile.TemporaryDirectory(prefix="wingie2-extract-test-") as directory:
        root = pathlib.Path(directory)
        source = root / "generated.cpp"
        output = root / "generated_user.h"
        source.write_text(
            "architecture\n" + START + "\nclass mydsp {};\n" + END +
            "\nwrapper\n",
            encoding="utf-8",
        )
        result = subprocess.run(
            [sys.executable, str(SCRIPT), str(source), str(output)],
            text=True,
            capture_output=True,
        )
        assert result.returncode == 0, result.stderr
        assert output.read_text(encoding="utf-8") == "class mydsp {};\n"

        source.write_text("no generated markers\n", encoding="utf-8")
        result = subprocess.run(
            [sys.executable, str(SCRIPT), str(source), str(output)],
            text=True,
            capture_output=True,
        )
        assert result.returncode == 2
        assert "generated user-section markers" in result.stderr


if __name__ == "__main__":
    main()
