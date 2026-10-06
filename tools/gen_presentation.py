#!/usr/bin/env python3
"""Regenerate the Python presentation builder code in tools/generated.

Runs flatc 25.12.19 (the release of usdRig's FlatBuffers headers and of
the vendored Python runtime in tools/thirdparty/flatbuffers) on the
sibling usdRig's libs/rigExecBinary/presentation.fbs. The output is
checked in, so only a schema change needs this; the exporter never runs
flatc.

Usage: gen_presentation.py
flatc comes from the FLATC environment variable, else from PATH. Use the
v25.12.19 release binary: later development builds still print 25.12.19
but emit Builder calls the release runtime lacks, which this refuses.
"""

import os
from pathlib import Path
import shutil
import subprocess
import sys

FLATC_VERSION = "25.12.19"
PLUGIN = Path(__file__).resolve().parents[1]
SCHEMA = PLUGIN.parent / "usdRig" / "libs" / "rigExecBinary" / "presentation.fbs"
OUT = PLUGIN / "tools" / "generated"


def main():
    flatc = os.environ.get("FLATC") or shutil.which("flatc")
    if not flatc:
        print("gen_presentation: FAIL: no flatc; set FLATC or put flatc on PATH")
        return 1
    version = subprocess.run([flatc, "--version"], stdout=subprocess.PIPE,
                             text=True, check=True).stdout.strip()
    if version != "flatc version " + FLATC_VERSION:
        print("gen_presentation: FAIL: need flatc version %s, got %r"
              % (FLATC_VERSION, version))
        return 1
    if not SCHEMA.is_file():
        print("gen_presentation: FAIL: no schema at %s" % SCHEMA)
        return 1
    shutil.rmtree(OUT / "rigExec", ignore_errors=True)
    subprocess.run([flatc, "--python", "-o", str(OUT), str(SCHEMA)], check=True)
    for path in sorted((OUT / "rigExec").rglob("*.py")):
        if "CreateVectorOfTables" in path.read_text():
            shutil.rmtree(OUT / "rigExec", ignore_errors=True)
            print("gen_presentation: FAIL: %s calls Builder.CreateVectorOfTables, "
                  "which the vendored %s runtime lacks; use the release flatc"
                  % (path.name, FLATC_VERSION))
            return 1
    print("gen_presentation: OK: %s" % (OUT / "rigExec"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
