# usdRig_godot — rigExec runtime plugin for Godot 4.7+

Play back `.rigexec` files — baked usdRig characters — in Godot with no USD
anywhere. A `.rigexec` file is a static exec graph whose inputs Godot drives;
it holds no animation. The GDExtension compiles the zero-USD
`rigExecRuntime` straight from the usdRig sources, so for the same input
values the results are bit-identical to the baked path there.

- Addon: `addons/rigexec` (see its README for install, use, and mapping)
- Demo game: `demo` ("Roll / Collect" — open `demo/project.godot` in
  Godot 4.7+ and press F5)
- Dev plan: `PLAN.md`

## Install

Download `rigexec-addon.zip` from the latest `godot-addon` workflow run
(Actions tab) or release, unzip it into a Godot project so the folder lands
at `res://addons/rigexec`, then run the project once. The `.gdextension`
loads automatically, no enable step; the editor import plugin turns a
`.rigexec` file into a `RigExecCharacter` resource on import.

## Build from source

Requires a sibling `usdRig` checkout (this directory and `usdRig` share a
parent directory), plus SCons and a C++ toolchain (MSVC on Windows):

```sh
git clone --recurse-submodules https://github.com/n-burk/usdRig_godot.git
git clone https://github.com/n-burk/usdRig.git   # sibling of the above
cd usdRig_godot
scons platform=windows target=template_release api_version=4.7
```

`thirdparty/godot-cpp` is a submodule pinned to master @ `507ed9d`.
Linux and macOS use the same command with their platform; the
`.gdextension` already lists all three library paths. The FlatBuffers
headers the runtime reads `.rigexec` files with come from the usdRig
sibling (`thirdparty/flatbuffers/include`); objects go under `build/obj`.

## Demo

The demo ships pre-baked (`.rigexec` assets, goldens) so it runs without a
USD build — but it needs the compiled extension first. After building:

- Windows: `demo\setup_demo.bat` (copies the addon in, bakes `fk.rigexec`
  and `fk_1002.rigexec`)
- Linux/macOS: `demo/setup_demo.sh`

then open `demo/project.godot` and play. See `demo/README.md` for controls,
the rolling-ball rebuild (`python demo/setup_rolling.py --build`), and
headless verification. `demo/addons` is a local copy made by setup, never
committed.

## CI

`.github/workflows/build.yml` builds the extension on Windows, Linux, and
macOS (debug + release), runs the no-USD-imports gate on every library, and
assembles `rigexec-addon.zip`. It checks out this repo plus a sibling
`usdRig` taken from the `USDRIG_REPO` / `USDRIG_REF` repository variables.
Push a `v*` tag (`git tag v1.0.0 && git push origin v1.0.0`) to publish the
zip as an asset on a GitHub release for that tag.

## License

MIT — see `LICENSE`. The built libraries also compile in third-party code
under its own terms; see `addons/rigexec/THIRD_PARTY_NOTICES.md`.
