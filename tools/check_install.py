#!/usr/bin/env python3
"""Fresh-install gate (M4): the addon zip installs into an empty project.

Builds rigexec-addon.zip via package_addon.py, unzips it into a scratch
4.7 project holding only a .rigexec sample, imports, and headlessly
loads, binds, and evaluates the character. Fails on any step.

Usage: check_install.py [godot_rigExec-dir] [sample.rigexec]
Defaults: the script's parent dir and demo/fk.rigexec (run setup_demo
first so the sample exists).
"""

import os
import shutil
import subprocess
import sys
import tempfile
import zipfile

PROJECT_GODOT = """\
; Engine configuration file.
config_version=5

[application]

config/name="rigExec install check"
config/features=PackedStringArray("4.7")

[rendering]

renderer/rendering_method="gl_compatibility"

[editor_plugins]

enabled=PackedStringArray("res://addons/rigexec/plugin.cfg")
"""

LOAD_GD = """\
extends SceneTree

func _init() -> void:
\tvar character: RigExecCharacter = load("res://sample.rigexec")
\tif character == null:
\t\tprint("INSTALL: load returned null")
\t\tquit(1)
\t\treturn
\tif not character.bind():
\t\tprint("INSTALL: bind failed: ", character.get_bind_error())
\t\tquit(1)
\t\treturn
\tvar player := RigExecPlayer.new()
\tplayer.set_character(character)
\tvar frames := character.get_frame_times()
\tplayer.set_frame(frames[0])
\tif not player.evaluate():
\t\tprint("INSTALL: evaluate failed: ", player.get_last_error())
\t\tquit(1)
\t\treturn
\tvar transforms := player.get_joint_transforms()
\tprint("INSTALL: ok: ", character.get_joint_paths().size(),
\t\t" joints, ", frames.size(), " frames, ",
\t\ttransforms.size(), " transforms")
\tquit(0)
"""


def _fail(message):
    print("check_install: FAIL: " + message)
    return 1


def _run(argv, cwd):
    proc = subprocess.run(argv, cwd=cwd, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True)
    return proc.returncode, proc.stdout


def main(argv):
    root = os.path.abspath(argv[1] if len(argv) > 1
                           else os.path.join(os.path.dirname(argv[0]), ".."))
    sample = (os.path.abspath(argv[2]) if len(argv) > 2
              else os.path.join(root, "demo", "fk.rigexec"))
    if not os.path.isfile(sample):
        return _fail("no sample binary; run demo/setup_demo first")
    scratch = tempfile.mkdtemp(prefix="rigexec-install-")
    try:
        zip_path = os.path.join(scratch, "rigexec-addon.zip")
        rc = subprocess.call(
            [sys.executable, os.path.join(root, "tools", "package_addon.py"),
             "--out", zip_path, root])
        if rc != 0:
            return _fail("package_addon exited %d" % rc)
        project = os.path.join(scratch, "project")
        os.mkdir(project)
        with zipfile.ZipFile(zip_path) as archive:
            archive.extractall(project)
        with open(os.path.join(project, "project.godot"), "w") as handle:
            handle.write(PROJECT_GODOT)
        with open(os.path.join(project, "load.gd"), "w") as handle:
            handle.write(LOAD_GD)
        shutil.copy(sample, os.path.join(project, "sample.rigexec"))
        print("check_install: godot --headless --import ...")
        rc, out = _run(["godot", "--headless", "--path", project,
                        "--import"], project)
        receipt = os.path.join(project, "sample.rigexec.import")
        if not os.path.isfile(receipt):
            print(out[-2000:])
            return _fail("import produced no .import receipt (exit %d)"
                         % rc)
        print("check_install: godot --headless load.gd ...")
        rc, out = _run(["godot", "--headless", "--path", project,
                        "--script", "load.gd"], project)
        if rc != 0 or "INSTALL: ok:" not in out:
            print(out[-2000:])
            return _fail("load script failed (exit %d)" % rc)
        print("check_install: OK: " +
              [line for line in out.splitlines()
               if "INSTALL: ok:" in line][0])
        return 0
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
