#!/usr/bin/env python
"""Builds the rigExec GDExtension against godot-cpp.

The zero-USD runtime (rigExecRuntime, the portable operation graph compiler
and executor, and the rigExecBinary format reader) compiles from the sibling usdRig checkout straight into the
extension -- never a shared DLL. The FlatBuffers headers come from usdRig's
thirdparty/flatbuffers/include. Objects go under build/obj/<suffix>, never
into either source tree.
Floating point stays precise (/fp:precise; -fno-fast-math -ffp-contract=off):
the runtime is bit-identical to the baked path only when the compiler
neither reassociates nor contracts multiply-adds.

Usage:
    scons platform=<windows|linux|macos> target=<template_debug|template_release>
    scons platform=windows target=template_release api_version=4.7
    scons platform=windows usdrig_root=<usdRig checkout or SDK source root>
"""

import glob
import os

from SCons.Script import ARGUMENTS, Environment

godot_cpp = os.path.join("thirdparty", "godot-cpp")


def _godot_cpp_env():
    scons_script = os.path.join(godot_cpp, "SConstruct")
    if not os.path.isfile(scons_script):
        print("godot-cpp not found at thirdparty/godot-cpp")
        print("pin: master @ 507ed9d840c01a3c5b2a39af8bb4000bfac30bf5")
        raise SystemExit(1)
    return SConscript(scons_script)


env = _godot_cpp_env()

# The runtime compiles from the sibling usdRig checkout.
usdrig_root = os.path.abspath(ARGUMENTS.get("usdrig_root", os.path.join("..", "usdRig")))
usdrig_libs = os.path.join(usdrig_root, "libs")
flatbuffers_include = os.path.join(usdrig_root, "thirdparty", "flatbuffers",
                                   "include")
if not os.path.isfile(os.path.join(flatbuffers_include, "flatbuffers",
                                   "flatbuffers.h")):
    print(usdrig_root + " has no thirdparty/flatbuffers; check out a usdRig "
          "with the FlatBuffer .rigexec format")
    raise SystemExit(1)
env.Append(CPPPATH=[usdrig_libs, flatbuffers_include])

# Bitwise parity with the baked path: no reassociation, no FMA contraction.
if env.get("is_msvc", False):
    env.Append(CXXFLAGS=["/fp:precise", "/std:c++17"])
else:
    env.Append(CXXFLAGS=["-fno-fast-math", "-ffp-contract=off", "-std=c++17"])

# Runtime .cpp files plus the format reader and portable graph. Other
# rigExecGraph sources require USD and do not enter this extension.
runtime_sources = sorted(
    glob.glob(os.path.join(usdrig_libs, "rigExecRuntime", "*.cpp")))
format_source = os.path.join(usdrig_libs, "rigExecBinary", "format.cpp")
transport_source = os.path.join(usdrig_libs, "rigExecBinary", "transport.cpp")
lzma_dir = os.path.join(usdrig_root, "third_party", "lzma", "C")
lzma_sources = [os.path.join(lzma_dir, name) for name in
                ("LzmaEnc.c", "LzmaDec.c", "LzFind.c", "CpuArch.c")]
env.Append(CPPPATH=[lzma_dir])
graph_source = os.path.join(usdrig_libs, "rigExecGraph", "opGraph.cpp")
if not runtime_sources or not all(os.path.isfile(source) for source in (format_source, transport_source, graph_source, *lzma_sources)):
    print("missing runtime, format reader, or portable operation graph sources under " + usdrig_libs)
    raise SystemExit(1)
runtime_sources.extend((format_source, transport_source, graph_source))
addon_sources = sorted(glob.glob(os.path.join("addons", "rigexec", "src",
                                              "*.cpp")))

# One object dir per build suffix, so debug and release coexist and
# platforms whose object suffix omits it (.os) do not collide.
obj_root = os.path.join("build", "obj", env["suffix"].lstrip("."))
objects = [
    env.SharedObject(
        os.path.join(obj_root,
                     os.path.splitext(os.path.relpath(s, usdrig_libs))[0]), s)
    for s in runtime_sources
]
lzma_env = env.Clone()
lzma_env.Append(CPPDEFINES=["Z7_ST", "RIGEXEC_LZMA_PORTABLE_SCALAR"])
objects += [lzma_env.SharedObject(
    os.path.join(obj_root, "lzma", os.path.splitext(os.path.basename(s))[0]), s)
    for s in lzma_sources]
objects += [
    env.SharedObject(
        os.path.join(obj_root, "addon",
                     os.path.splitext(os.path.basename(s))[0]), s)
    for s in addon_sources
]

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
library = env.SharedLibrary(library_path, source=objects)
Default(library)
