# rigExec for Godot

Play back `.rigexec` files -- baked usdRig characters -- in Godot 4.7+ with
no USD anywhere. The extension compiles the zero-USD runtime straight from
the usdRig sources, so a frame here is bit-identical to the baked path
there (`rigExecPose --verify-binary` gates that on every shipped example).

## Install

Unzip the release into a Godot project so this folder lands at
`res://addons/rigexec`, then run the project once: the `.gdextension`
loads automatically, no enable step. The editor import plugin turns a
`.rigexec` file into a `RigExecCharacter` resource on import.

## Build (developers)

Requires the usdRig checkout beside this one (`../usdRig`), godot-cpp at
`thirdparty/godot-cpp` (D5 pin: master @ `507ed9d`), SCons, and MSVC:

```
scons platform=windows target=template_release api_version=4.7
```

Linux and macOS use the same command with their platform; the
`.gdextension` already lists all three library paths.

## Use

1. Import `character.rigexec` (produces `character.res`), or load bytes at
   runtime and call `RigExecCharacter.set_data()` + `bind()`.
2. Add a `RigExecPlayer`, set its `character` and `skeleton_path`.
3. `player.set_frame(1001.0)`, `player.evaluate()`,
   `player.apply_to_skeleton()` -- or `play()` and let `_process` step.

For the interactive rolling-ball game, open `demo/project.godot` and play.
See `demo/README.md` for controls, rebuilding and headless verification.

`player.set_avar("/BallAsset/Rig/Controls/Move.avars:tx", 3.0)` overrides a
compiled control's local TRS channel before evaluation. Overrides persist
across `set_frame()` / `evaluate()` until `clear_avars()` or `set_character()`.
Angles are degrees. Invalid paths, nonfinite values, authored matrix poses,
unsupported channels and captured property-mover outputs return false with
`get_last_error()`. This is a TRS control API, not a general property-chain
reevaluator; select the tutorial's `free` variant for interactive rolling.

## Mapping

- Joint matrices are row-major asset-space; each becomes a
  `Transform3D` (Basis columns = the matrix rows' XYZ, origin = row 3).
- Bones bind by leaf name: the joint path's last element must equal the
  bone name. Joints the skeleton does not have are skipped.
- `get_joint_transforms()` returns skinning deltas for backward compatibility.
  Build skeleton rests with `get_joint_rest_transforms()` after binding via
  `evaluate()`; `get_joint_pose_transforms()` returns the posed frames.
  `apply_to_skeleton()` uses those poses, including the rest offset.
- Coordinates stay in authored asset units. The caller chooses world scale;
  the rolling game uses a radius-one, Y-up asset as one metre. No automatic
  metres-per-unit conversion is performed by the plugin.

## Headless check

`demo/verify.gd` evaluates the sample character and prints every joint
matrix at `%.17f` (GDScript has no `%g`) for the checker in `demo/` to
compare against the golden `rigExecPose --pose-out` produced: each
printed value must equal the golden double rounded once to `float`,
exactly. See `demo/` and PLAN.md section 5.

## Known issues

- Linux editor: the first `--import` on a clean tree can abort with
  exit 134 during editor teardown *after* the import succeeded (a
  second run exits 0). This reproduces with an empty one-class
  extension and no rigExec code on the abort's stack, so it is an
  upstream Godot quirk, not this addon; the game/runtime path always
  exits 0. `demo/setup_demo.sh` gates on the import product rather
  than the exit code for exactly this reason.
