# Third-party notices

The rigExec libraries in `bin/` compile in the following third-party code.
Each part keeps its own license; none is relicensed under this add-on's MIT
terms (`LICENSE`).

- **OpenUSD** Gf math (Pixar, Tomorrow Open Source Technology License 1.0;
  see `LICENSE-OpenUSD.txt`). usdRig's `libs/rigExecRuntime/runtimeMath.h`
  is partly derived from it and keeps its OpenUSD notice. No OpenUSD
  library is linked.
- **FlatBuffers 25.12.19** C++ runtime headers (Google, Apache License 2.0;
  see `LICENSE-flatbuffers.txt`), from usdRig's `thirdparty/flatbuffers`.
  They read the `.rigexec` file.
- **godot-cpp** (Godot Engine contributors, MIT License; see
  `LICENSE-godot-cpp.md`), the GDExtension C++ bindings the libraries are
  built with.
