#!/usr/bin/env python3
"""Packages the Godot addon as rigexec-addon.zip.

Includes addons/rigexec with its .gdextension, editor scripts, README,
license texts (the repository LICENSE ships as addons/rigexec/LICENSE),
and every platform library present in bin/ -- debug AND release, since
the editor loads the debug one and exported games the release one.
Build byproducts (*.obj, *.exp, *.lib, *.pdb, .sconsign) and the demo,
sources, and thirdparty trees are never shipped. A platform whose
libraries do not bind the current player API is skipped with a warning.

Usage: package_addon.py [--out rigexec-addon.zip] [godot_rigExec-dir]
Fails when no platform library is present; warns per missing platform
(the .gdextension already lists all three, so a zip without macOS
libraries installs and runs everywhere else).
"""

import os
import sys
import zipfile

TOP_FILES = (
    "rigexec.gdextension",
    "plugin.cfg",
    "rigexec_editor.gd",
    "rigexec_import.gd",
    "README.md",
    "THIRD_PARTY_NOTICES.md",
    "LICENSE-OpenUSD.txt",
    "LICENSE-flatbuffers.txt",
    "LICENSE-godot-cpp.md",
    "LICENSE-LZMA-SDK.txt",
    "NOTICE-LZMA-SDK.md",
)

# Files from the repository root, by their path inside the zip.
ROOT_FILES = (
    ("LICENSE", "addons/rigexec/LICENSE"),
)

# A method name only the current RigExecPlayer binds. A library without it
# was built from older sources and would not match this add-on's scripts.
API_MARKER = b"touch_animated_inputs"

# bin/ entries that ship, by platform. Anything else in bin/ (import
# libraries, debug symbols, scons objects) stays on the dev machine.
PLATFORM_LIBS = (
    ("windows", ("librigexec.windows.template_debug.x86_64.dll",
                 "librigexec.windows.template_release.x86_64.dll")),
    ("linux", ("librigexec.linux.template_debug.x86_64.so",
               "librigexec.linux.template_release.x86_64.so")),
)


def _fail(message):
    print("package_addon: FAIL: " + message)
    return 1


def _binds_current_api(paths):
    for path in paths:
        with open(path, "rb") as handle:
            if API_MARKER in handle.read():
                return True
    return False


def main(argv):
    out = "rigexec-addon.zip"
    root = "."
    args = list(argv[1:])
    if "--out" in args:
        index = args.index("--out")
        try:
            out = args[index + 1]
        except IndexError:
            return _fail("--out needs a path")
        del args[index:index + 2]
    if len(args) > 1:
        return _fail("usage: package_addon.py [--out zip] [root]")
    if args:
        root = args[0]
    addon = os.path.join(root, "addons", "rigexec")
    bindir = os.path.join(addon, "bin")
    if not os.path.isdir(addon):
        return _fail("no addons/rigexec under " + root)

    # (source path, path inside the zip)
    members = []
    for name in TOP_FILES:
        path = os.path.join(addon, name)
        if not os.path.isfile(path):
            return _fail("missing " + path)
        members.append((path, os.path.relpath(path, root)))
    for name, arcname in ROOT_FILES:
        path = os.path.join(root, name)
        if not os.path.isfile(path):
            return _fail("missing " + path)
        members.append((path, arcname))
    shipped_platforms = []
    for platform, libs in PLATFORM_LIBS:
        paths = [os.path.join(bindir, name) for name in libs]
        if not all(os.path.isfile(p) for p in paths):
            print("package_addon: WARNING: %s libraries incomplete; "
                  "skipped" % platform)
            continue
        stale = [p for p in paths if not _binds_current_api([p])]
        if stale:
            print("package_addon: WARNING: %s libraries predate the "
                  "current player API (%s); rebuild them; skipped"
                  % (platform, ", ".join(os.path.basename(p)
                                         for p in stale)))
            continue
        members.extend((p, os.path.relpath(p, root)) for p in paths)
        shipped_platforms.append(platform)
    # macOS frameworks, when the mac leg has built them.
    frameworks = []
    if os.path.isdir(bindir):
        for name in sorted(os.listdir(bindir)):
            if name.endswith(".framework") and os.path.isdir(
                    os.path.join(bindir, name)):
                frameworks.append(name)
    shipped_frameworks = 0
    for name in frameworks:
        base = os.path.join(bindir, name)
        files = [os.path.join(dirpath, filename)
                 for dirpath, _, filenames in os.walk(base)
                 for filename in filenames]
        if not _binds_current_api(files):
            print("package_addon: WARNING: %s predates the current player "
                  "API; rebuild it; skipped" % name)
            continue
        members.extend((p, os.path.relpath(p, root)) for p in files)
        shipped_frameworks += 1
    if shipped_frameworks:
        shipped_platforms.append("macos")
    elif not frameworks:
        print("package_addon: WARNING: no macOS framework; build it "
              "via .github/workflows/build.yml and re-package")
    if not shipped_platforms:
        return _fail("no current platform library in " + bindir)

    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as archive:
        for path, arcname in members:
            archive.write(path, arcname)
    print("package_addon: OK: %s (%d files; platforms: %s)"
          % (out, len(members), ", ".join(shipped_platforms)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
