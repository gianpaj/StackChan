#!/usr/bin/env python3
"""Compile the production conversation controller with host OS/engine boundaries."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SOURCE = HERE.parent / "main/conversation/controller.cpp"
ITO = HERE.parent / "ito/esp32/runtime"


def main():
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    production = re.sub(r"^\s*#\s*include[^\n]*", "", SOURCE.read_text(), flags=re.MULTILINE)
    with tempfile.TemporaryDirectory(prefix="stackchan-controller-test-") as directory:
        temporary = Path(directory)
        (temporary / "production_controller.inc").write_text(production)
        (temporary / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n")
        executable = temporary / "controller-test"
        subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                                   "-I", str(temporary), "-I", str(ITO), "-I", str(SOURCE.parent),
                                   str(HERE / "controller_test.cpp"), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True, timeout=10)


if __name__ == "__main__":
    main()
