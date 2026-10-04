#!/usr/bin/env python3
"""Rebuild the tutorial rolling game using sibling usdRig and usd-install.

python demo/setup_rolling.py --build   # compile debug + release, bake, import
python demo/setup_rolling.py           # use existing native libraries
"""
import argparse
import filecmp
import os
from pathlib import Path
import shutil
import subprocess
import sys


def copy_changed(source, destination):
    # A running Godot editor holds the extension DLL open on Windows.
    # Identical binaries need no replacement during an asset-only rebuild.
    if Path(destination).is_file() and filecmp.cmp(source, destination, shallow=False):
        return str(destination)
    return shutil.copy2(source, destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", action="store_true")
    parser.add_argument("--godot", default=shutil.which("godot_console") or shutil.which("godot"))
    args = parser.parse_args()
    demo = Path(__file__).resolve().parent
    plugin = demo.parent
    rig = plugin.parent / "usdRig"
    usd = plugin.parent / "usd-install"
    platform = "windows" if os.name == "nt" else ("macos" if sys.platform == "darwin" else "linux")
    if not args.godot:
        parser.error("Godot 4.7+ must be on PATH, or pass --godot")
    if args.build:
        for target in ("template_debug", "template_release"):
            subprocess.run([sys.executable, "-m", "SCons", "platform=" + platform,
                            "target=" + target, "api_version=4.7", "-j8"], cwd=plugin, check=True)
    env = os.environ.copy()
    env["PATH"] = os.pathsep.join([str(rig / "build"), str(usd / "bin"), str(usd / "lib"), env.get("PATH", "")])
    env["PXR_PLUGINPATH_NAME"] = str(rig / "build/usd/rigExecSchema/resources")
    if os.name != "nt":
        env["LD_LIBRARY_PATH"] = os.pathsep.join([str(rig / "build"), str(usd / "lib"), env.get("LD_LIBRARY_PATH", "")])
    bake = rig / "build" / ("rigExecBake.exe" if os.name == "nt" else "rigExecBake")
    subprocess.run([str(bake), str(rig / "docs/examples/tutorial_rolling_ball_free.usda"),
                    "--frames", "1001", "-o", str(demo / "rolling_ball.rigexec")], env=env, check=True)
    subprocess.run([sys.executable, str(plugin / "tools/export_ball_assets.py")], env=env, check=True)
    shutil.copytree(plugin / "addons/rigexec", demo / "addons/rigexec", dirs_exist_ok=True,
                    copy_function=copy_changed,
                    ignore=shutil.ignore_patterns("src", "*.obj", "*.lib", "*.exp", "*.pdb"))
    subprocess.run([args.godot, "--headless", "--path", str(demo), "--import"], check=True)
    subprocess.run([args.godot, "--headless", "--path", str(demo), "--script", "verify_rolling.gd"], check=True)
    print("Ready: godot --path", demo)


if __name__ == "__main__":
    main()
