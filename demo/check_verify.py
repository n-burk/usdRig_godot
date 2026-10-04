#!/usr/bin/env python3
"""Headless Godot gate: the player's joint transforms equal the baked path.

Runs demo/verify.gd under headless Godot and rigExecPose --pose-out over
the same stage and frames, then compares joint by joint. The extension
publishes Transform3D (float), so each printed value must equal the
golden double rounded exactly once to float -- bitwise, not within a
tolerance. A tolerance would let a second rounding (or a wrong matrix)
hide inside the epsilon.

Usage: check_verify.py [demo-dir] [usdrig-build-dir]
   or: check_verify.py --compare-only <run.txt> <golden.txt>
The second form skips both subprocesses and only compares texts, which
is how a Linux run (no Linux USD build to bake or dump with) is checked
against the Windows-produced golden: the .rigexec bytes and the golden
text are both platform-independent.
Exit 0 on match; exit 1 naming the first mismatch.
"""

import os
import struct
import subprocess
import sys

# The 12 Transform3D elements inside a row-major 16-float matrix: rows
# 0-2 XYZ (Basis columns) plus row 3 XYZ (origin). Indices 3, 7, 11, 15
# (the W column) are asserted separately so the subset cannot silently
# drop a perspective nobody expected in a joint matrix.
SUBSET = (0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14)


def _fail(message):
    print("check_verify: FAIL: " + message)
    return 1


def _run(argv, cwd, env):
    proc = subprocess.run(
        argv, cwd=cwd, env=env, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True)
    return proc.returncode, proc.stdout


def _parse_run(text):
    frames = []
    current = None
    for line in text.splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "frame" and len(parts) == 2:
            current = (float(parts[1]), [])
            frames.append(current)
        elif parts[0] == "joint" and current is not None:
            values = [float(v) for v in parts[2:]]
            if len(values) != 12:
                raise ValueError("joint line has %d values: %s"
                                 % (len(values), line[:80]))
            current[1].append((parts[1], values))
        elif line.startswith("VERIFY:"):
            raise ValueError(line)
    return frames


def _parse_golden(text):
    frames = []
    current = None
    for line in text.splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "frame":
            current = (float(parts[1]), {})
            frames.append(current)
        elif parts[0] == "jointMatricesFinal" and current is not None:
            values = [float(v) for v in parts[2:]]
            if len(values) != 16:
                raise ValueError("golden line has %d values" % len(values))
            current[1][parts[1]] = values
    return frames


def _f32_bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def _compare(run_text, golden_text):
    try:
        run_frames = _parse_run(run_text)
    except ValueError as exc:
        return _fail("cannot parse godot output: %s" % exc)
    if not run_frames:
        return _fail("godot printed no frames")
    golden_frames = _parse_golden(golden_text)
    if not golden_frames:
        return _fail("golden has no frames")
    if [f for f, _ in run_frames] != [f for f, _ in golden_frames]:
        return _fail("frame lists differ: %s vs %s"
                     % ([f for f, _ in run_frames],
                        [f for f, _ in golden_frames]))
    checked = 0
    for (frame, joints), (_, golden) in zip(run_frames, golden_frames):
        if sorted(p for p, _ in joints) != sorted(golden):
            return _fail("frame %g: joint sets differ" % frame)
        for path, values in joints:
            matrix = golden[path]
            if (matrix[3] != 0.0 or matrix[7] != 0.0 or
                    matrix[11] != 0.0 or matrix[15] != 1.0):
                return _fail("frame %g %s: golden is not rigid-affine "
                             "(w column %g %g %g %g)"
                             % (frame, path, matrix[3], matrix[7],
                                matrix[11], matrix[15]))
            for slot, index in enumerate(SUBSET):
                want = _f32_bits(matrix[index])
                got = _f32_bits(values[slot])
                if want != got:
                    return _fail(
                        "frame %g %s element %d: godot %.17f "
                        "is not float(golden %.17g)"
                        % (frame, path, index, values[slot],
                           matrix[index]))
                checked += 1
    print("check_verify: OK: %d elements over %d frame(s) equal "
          "float(golden) bitwise" % (checked, len(run_frames)))
    return 0


def main(argv):
    if len(argv) == 4 and argv[1] == "--compare-only":
        with open(argv[2]) as handle:
            run_text = handle.read()
        with open(argv[3]) as handle:
            golden_text = handle.read()
        return _compare(run_text, golden_text)
    demo = os.path.abspath(argv[1] if len(argv) > 1 else ".")
    if len(argv) > 2:
        build = os.path.abspath(argv[2])
    else:
        build = os.path.abspath(os.path.join(demo, "..", "..", "usdRig",
                                             "build"))
    stage = os.path.join(build, "..", "examples", "01_FkChainTail.usda")
    pose = os.path.join(build, "rigExecPose.exe"
                        if os.name == "nt" else "rigExecPose")
    golden_path = os.path.join(demo, "fk.golden.txt")

    env = dict(os.environ)
    usd_install = os.path.abspath(os.path.join(build, "..", "..",
                                               "usd-install"))
    if os.name == "nt":
        env["PATH"] = (build + ";" + os.path.join(usd_install, "lib") +
                       ";" + os.path.join(usd_install, "bin") + ";" +
                       env.get("PATH", ""))
    else:
        env["LD_LIBRARY_PATH"] = (
            os.path.join(usd_install, "lib") + ":" +
            env.get("LD_LIBRARY_PATH", ""))
    env["PXR_PLUGINPATH_NAME"] = os.path.join(
        build, "usd", "rigExecSchema", "resources")

    print("check_verify: godot --headless verify.gd ...")
    rc, run_text = _run(["godot", "--headless", "--path", demo,
                         "--script", "verify.gd"], demo, env)
    if rc != 0:
        print(run_text[-2000:])
        return _fail("godot exited %d" % rc)

    print("check_verify: rigExecPose --pose-out ...")
    rc, out = _run([pose, stage, "--frames", "1001,1002",
                    "--pose-out", golden_path], demo, env)
    if rc != 0:
        print(out[-2000:])
        return _fail("rigExecPose exited %d" % rc)
    with open(golden_path) as handle:
        golden_text = handle.read()
    return _compare(run_text, golden_text)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
