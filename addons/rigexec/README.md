# rigExec for Godot

Play back `.rigexec` files -- baked usdRig characters -- in Godot 4.7+ with
no USD anywhere. A `.rigexec` file is a static exec graph plus its inputs:
the attributes the rig reads, each with its value at the bake time. The
extension compiles the zero-USD runtime straight from the usdRig sources, so
for the same input values the results are bit-identical to the baked path
there (`rigExecPose --verify-binary` gates that on every shipped example).

## Install

Unzip the release into a Godot project so this folder lands at
`res://addons/rigexec`, then run the project once: the `.gdextension`
loads automatically, no enable step. The editor import plugin turns a
`.rigexec` file into a `RigExecCharacter` resource on import.

## Build (developers)

Requires the usdRig checkout beside this one (`../usdRig`), godot-cpp at
`thirdparty/godot-cpp` (pinned: master @ `507ed9d`), SCons, and MSVC:

```
scons platform=windows target=template_release api_version=4.7
```

For a checkout elsewhere, pass `usdrig_root=<path>` to SCons. That root
must contain `libs/rigExecRuntime`, `libs/rigExecGraph`, `libs/rigExecBinary`
and `thirdparty/flatbuffers/include`; the extension builds the runtime,
format reader and portable operation graph together.

Linux and macOS use the same command with their platform; the
`.gdextension` already lists all three library paths. The FlatBuffers
headers come from `../usdRig/thirdparty/flatbuffers/include`, and objects
go under `build/obj`, never into either source tree.

## Use

1. Import `character.rigexec` (produces `character.res`), or load bytes at
   runtime and call `RigExecCharacter.set_data()` + `bind()`.
   `get_bake_time()` is the time the file's input defaults were read at;
   it is metadata only and drives no evaluation.
2. Add a `RigExecPlayer` and set its `character`. That evaluates the input
   defaults at once: the rig as baked.
3. Set inputs, then call `evaluate()`. Nothing advances on its own: drive the
   inputs from your script, physics or an `AnimationPlayer` each tick.
   - Assets with a presentation (embedded meshes, material and public
     controls, like the rolling ball): `set_control(name, value)`,
     `get_controls()` and `reset_controls()`. Each control is one
     `controls/<Name>/<channel>` property, which an `AnimationPlayer` can
     key; setting the property also evaluates.
   - Program-only files: `get_inputs()`, `set_input(path, value)`,
     `reset_inputs()`, then the joint getters or `apply_to_skeleton()` with
     `skeleton_path` set. When replaying a timeline, call
     `touch_animated_inputs()` whenever time moves, as a stage sampler does.

For the interactive rolling-ball game, open `demo/project.godot` and play.
See `demo/README.md` for controls, rebuilding and headless verification.

### Inputs

`get_inputs()` maps each input's attribute path (for example
`/Rig/Controls/Hips.avars:tx`) to `{type, animated, default, value}`.
`animated` says the attribute is time-varying in the baked stage. A value
set with `set_input()` acts as that attribute's authored value at the next
`evaluate()` and stays until `reset_inputs()` or `set_character()`.

| Input type | `set_input` accepts | `get_inputs` returns |
|---|---|---|
| `double`, `float` | `float` or `int` (a `float` input rounds once) | `float` |
| `int` | `int` within 32 bits | `int` |
| `bool` | `bool` | `bool` |
| `token` | `String` or `StringName` | `String` |
| `vec3d`, `vec3f` | `Vector3`, or `PackedFloat64Array` of 3 | `PackedFloat64Array` of 3 |
| `matrix4d` | `Transform3D`, or `PackedFloat64Array` of 16, row-major | `PackedFloat64Array` of 16, row-major |

Unknown paths, other value types and non-finite components return false
with the reason in `get_last_error()`. A `Vector3` or `Transform3D` carries
Godot's `real_t` precision; pass a `PackedFloat64Array` for exact doubles.

Presentation assets refuse `set_input()` and list no inputs: their public
controls are the game interface. `get_controls()` reports each control's
`name`, `unit` (`asset_units`, `degrees` or `ratio`), `default` (from the
file's inputs) and current `value`.

## Mapping

- Joint matrices are row-major asset-space; each becomes a
  `Transform3D` (Basis columns = the matrix rows' XYZ, origin = row 3).
- A `Transform3D` given to a `matrix4d` input maps back the same way:
  row i is basis column i with w 0, row 3 is the origin with w 1.
- Bones bind by leaf name: the joint path's last element must equal the
  bone name. Joints the skeleton does not have are skipped.
- `get_joint_transforms()` returns skinning deltas for backward compatibility.
  Build skeleton rests with `get_joint_rest_transforms()` after binding via
  `evaluate()`; `get_joint_pose_transforms()` returns the posed frames.
  `apply_to_skeleton()` uses those poses, including the rest offset.
- Coordinates stay in authored asset units. The caller chooses world scale;
  the rolling game uses a radius-one, Y-up asset as one metre. No automatic
  metres-per-unit conversion is performed by the plugin.
- Plugin movers (types an external usdRig library registered) have no
  kernel in Godot: their points pass through, and the player warns when
  it opens such a file.

## Headless check

`demo/verify.gd` evaluates `fk.rigexec` at its defaults, then sets its
animated inputs to the defaults of `fk_1002.rigexec` (the same stage baked
at 1002) and evaluates again. It prints every joint matrix at `%.17f`
(GDScript has no `%g`) for `demo/check_verify.py` to compare against the
golden `rigExecPose --pose-out` produced: each printed value must equal the
golden double rounded once to `float`, exactly.

## Licenses

The add-on is MIT licensed (`LICENSE`). Its libraries also compile in
OpenUSD-derived math (TOST 1.0, `LICENSE-OpenUSD.txt`), the FlatBuffers
headers (Apache License 2.0, `LICENSE-flatbuffers.txt`) and godot-cpp
(MIT, `LICENSE-godot-cpp.md`); see `THIRD_PARTY_NOTICES.md`.

## Known issues

- Linux editor: the first `--import` on a clean tree can abort with
  exit 134 during editor teardown *after* the import succeeded (a
  second run exits 0). This reproduces with an empty one-class
  extension and no rigExec code on the abort's stack, so it is an
  upstream Godot quirk, not this addon; the game/runtime path always
  exits 0. `demo/setup_demo.sh` gates on the import product rather
  than the exit code for exactly this reason.
