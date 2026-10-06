> Current asset interface: a `.rigexec` file is one FlatBuffer holding a
> static exec graph and its inputs (attribute paths with bake-time defaults),
> no animation; Godot drives the inputs. The rolling-ball example carries its
> presentation as the nested buffer `File.presentation` (identifier REXP) and
> exposes named public controllers. Its Godot scene has no skeleton. The
> skeleton bridge described below remains the program-only adapter.
> See ../usdRig/docs/specs/rigexec-presentation.md for the current contract.

# Godot rigExec Runtime Plugin — Plan

Status: complete through M4; moved to the single-FlatBuffer `.rigexec`
(usdRig format version 7), whose inputs Godot drives.
Decisions D1–D7 locked (see below).

## 1. Goal / non-goals

- Goal (a): `usdRig` can bake a compiled rigExec character to a
  USD-independent binary (`.rigexec`, single file, no sidecar).
- Goal (b): new Godot 4.7.1 plugin in this repo packages that binary and
  executes it at runtime as a thin wrapper over `../usdRig`.
- Goal (c): usdview can play back `.rigexec` through the same runtime
  (playback mode in `rigExecImaging`, driven by an asset-path attribute
  on `RigExecRoot`; live path otherwise untouched).
- Non-goals: no USD runtime in the Godot player; no USD authoring in
  Godot; no `bake.py` USD-export change; `bridge.h` stays desktop-only.

## 2. Architecture: two-time split

- **Bake-time** (dev machine, full USD + usdRig): `RigEvaluator::Compile`
  + `bakedProgram` + `bakedSchedule` → `.rigexec` binary. Consumed by
  the Godot editor importer and usdview playback via tool binaries.
- **Runtime** (Godot player AND usdview playback): zero-USD static lib
  `rigExecRuntime` + thin wrappers. Executes the serialized step graph
  over dense slots — no exec round trip per frame.
- Why the split is forced: `rigExec` links
  `plug/sdf/usd/usdGeom/usdSkel/work/exec/execUsd/ef/vdf` (unshippable
  in a player); only `rigExecMath` is STATIC and narrow. The runtime is
  a new leaf with no `find_package(pxr)`.
- Sharing model: `rigExecRuntime` is shared as **sources statically
  linked into each consumer** (Godot GDExtension, `rigExecPose
  --verify-binary`, imaging playback) — never one shared DLL, since a
  single DLL cannot serve both zero-USD and full-USD consumers.

## 3. Bake → load → execute flow

1. Author/validate USD via `rigBuilder` + `MoverAPI`.
2. `rigExecBake source.usdc --rig /path --time T [--presentation file.rexp]
   -o char.rigexec` (extends the `rigExecPose` CLI pattern). Every input's
   value at T becomes its default.
3. Godot editor import: `.rigexec` → `RigExecCharacter` resource +
   skeleton/mesh binding. usdview: `RigExecRoot` asset attribute →
   imaging playback mode.
4. Runtime per frame: set inputs → execute cluster DAG (Godot:
   `WorkerThreadPool`; OpenUSD side: existing dispatcher; serial
   fallback everywhere) → joint matrices (row-major 16-float) +
   deformed points → `Skeleton3D`/mesh, or the existing Hydra
   pose-publish path.

## 4. Locked decisions

- **D1 — layouts:** data-oriented, cache-friendly structures (SoA,
  aligned, per-op optimal). Throughput-first; the exact
  landmark-vs-matrix choice is made per op in the design doc.
- **D2 — threading:** keep `bakedSchedule` parallel clusters; Godot
  runs them on `WorkerThreadPool`, OpenUSD side on its dispatcher. No
  parallelism or speed regressions vs the baked path.
- **D3 — skinning:** the runtime skins; it outputs deformed points
  (plus joint matrices).
- **D4 — op coverage:** ALL ops bake and execute — movers, reverse
  joints, all read phases. No refusal subset (remaining `Refuse` sites
  get implemented, not kept).
- **D5 — Godot:** godot-cpp (pinned submodule), win + linux + mac.
- **D6 — packaging:** single `.rigexec` file, no sidecar; compile
  diagnostics travel as a typed vector the runtime replays.
- **D7 — playback select:** asset-path attribute on `RigExecRoot`;
  unset = live eval, set = baked playback.

## 5. Godot-side API + packaging (this repo)

- GDExtension (`godot-cpp`, 4.7.1): `addons/rigexec/` with
  `rigexec.gdextension`, `bin/<platform>/` runtime lib,
  `RigExecCharacter` (Resource wrapping `.rigexec` bytes + binding),
  `RigExecPlayer` (Node3D: `character`, `set_input`/`get_inputs`/
  `reset_inputs`/`touch_animated_inputs`, `set_control`/`get_controls`/
  `reset_controls`, `evaluate`, `apply_to_skeleton`). Nothing advances
  on its own; the game sets inputs, then evaluates.
- Editor-only import plugin for `.rigexec`; baking runs `rigExecBake`
  outside the editor (needs `../usd-install` on dev machines, never
  shipped).
- Packaging: `rigexec-addon.zip`; sample under `demo/` with its own
  `project.godot` (no `project.godot` in the plugin root).
- Transform mapping: row-major 16-float → `Transform3D`/`Basis`;
  bone order from the binary's joint table; handedness/scale
  documented.

## 6. Build / test gates

- CMake ≥ 3.26, C++17, `RIGEXEC_BAKE_DIR`/`USD_INSTALL_DIR`/
  `GODOT_CPP_DIR` options.
- `usdRig` CTest + new `binary_parity`: dynamic == baked == binary ==
  imaging-playback on every example stage; cone cases; headless
  Godot demo run vs `rigExecPose` goldens; `dumpbin`/`ldd` check that
  the runtime DLL has no USD imports; addon zip installs into an
  empty 4.7.1 project.

## 7. Risks

- USD leakage via `tf`/`gf`/`vt` through `rigExecMath` → vendored
  scalar math in the runtime + header allowlist + import check.
- D4 scope: every current bake refusal must be implemented.
- VDF geometry-chain fidelity in the slot interpreter; cone
  shadow-verify now an embedded section.
- Skeleton/mesh mapping conventions (highest visual risk).
- GDExtension ABI × win/linux/mac matrix.

## 8. Milestones

- M0 Approval — done (D1–D7 locked).
- M1 usdRig bake — done: `BakeToBinary` + `rigExecBake` CLI + D4
  refusal implementations + CTest parity (`testRigExecBinary`
  bake-conformance green, no Godot yet).
- M2 Runtime core — done: zero-USD `rigExecRuntime` (pose,
  weights, geometry ports) + loader/framework tests +
  `rigExecPose --verify-binary` gate at 32/32 fixtures green,
  including the manifest `compileDiagnostics` seed replay and the
  curvenet factor-nonzeros tally match.
- M2b usdview playback — done: `RigExecRoot` asset attribute +
  `RigExecImagingRigAdapter` playback mode +
  `testRigExecImagingPlayback` green.
- M3 Godot plugin — done: GDExtension + `RigExecCharacter` /
  `RigExecPlayer` + editor import plugin + demo + headless gate
  (`demo/check_verify.py`: 96/96 joint elements bitwise equal to
  `float(golden)` on Windows and Linux; import round-trip asserted).
  Windows debug+release DLLs and Linux debug+release `.so` built and
  import-checked; macOS builds via `.github/workflows/build.yml`.
- M4 Packaging/hardening — done: `rigexec-addon.zip` via
  `tools/package_addon.py`, fresh-install gate
  (`tools/check_install.py`) green, per-platform no-USD-imports
  checks green, docs closed out.
