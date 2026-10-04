#!/usr/bin/env python
"""Builds the rigExec GDExtension against godot-cpp.

The zero-USD runtime (rigExecBinary + rigExecRuntime) compiles from SOURCES
straight into the extension -- never a shared DLL -- per PLAN.md section 2.
Floating point stays precise (/fp:precise, -fno-fast-math): the runtime is
bit-identical to the baked path only when the compiler does not reassociate.

Usage:
    scons platform=<windows|linux|macos> target=<template_debug|template_release>
    scons platform=windows target=template_release api_version=4.7
"""

import os

from SCons.Script import ARGUMENTS, Environment

godot_cpp = os.path.join("thirdparty", "godot-cpp")


def _godot_cpp_env():
    scons_script = os.path.join(godot_cpp, "SConstruct")
    if not os.path.isfile(scons_script):
        print("godot-cpp not found at thirdparty/godot-cpp")
        print("D5 pin: master @ 507ed9d840c01a3c5b2a39af8bb4000bfac30bf5")
        raise SystemExit(1)
    return SConscript(scons_script)


env = _godot_cpp_env()

# The runtime lives one directory up, as sibling sources.
usdrig = os.path.abspath(os.path.join("..", "usdRig", "libs"))
env.Append(CPPPATH=[usdrig])

if env["platform"] == "windows":
    env.Append(CXXFLAGS=["/fp:precise", "/std:c++17"])
else:
    env.Append(CXXFLAGS=["-fno-fast-math", "-std=c++17"])

runtime_sources = [
    "rigExecBinary/container.cpp",
    "rigExecBinary/geometry.cpp",
    "rigExecBinary/inputTable.cpp",
    "rigExecBinary/pose.cpp",
    "rigExecBinary/program.cpp",
    "rigExecRuntime/open.cpp",
    "rigExecRuntime/exec.cpp",
    "rigExecRuntime/closure.cpp",
    "rigExecRuntime/publish.cpp",
    "rigExecRuntime/kernels.cpp",
    "rigExecRuntime/pose.cpp",
    "rigExecRuntime/geometry.cpp",
    "rigExecRuntime/weights.cpp",
]

sources = [os.path.join(usdrig, path) for path in runtime_sources]
sources += Glob("addons/rigexec/src/*.cpp")

if env["platform"] == "macos":
    library_path = os.path.join(
        "addons", "rigexec", "bin",
        "librigexec{}.framework".format(env["suffix"]),
        "librigexec{}".format(env["suffix"]),
    )
else:
    library_path = os.path.join(
        "addons", "rigexec", "bin",
        "librigexec{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
    )
library = env.SharedLibrary(library_path, source=sources)
Default(library)
