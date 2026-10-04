# Roll / Collect — one self-contained rigExec asset

Open project.godot in Godot 4.7+ and press F5. WASD/arrows move, Space hops,
Shift brakes, R resets. Collect six rings and reach the exit.

The reusable rolling_ball.tscn owns a collision body and RigExecPlayer.
The player takes just rolling_ball.rigexec, which embeds the execution
program, deformed-point bindings, subdivision stencils, UVs, material,
original PNG and public controller metadata. No Godot skeleton or loose
mesh/material/texture files are needed.

rolling_ball.gd computes movement and accumulated rolling orientation. Its
only rig inputs are Move.tx/ty/tz and Roll.rx/ry/rz, set through set_control().
evaluate() executes the rig and updates its render geometry. reset_controls()
restores baseline inputs. get_controls() lists authored public controls.

The source wrapper declares rigExec:exposedAvars and rigExec:publicName on
Move and Roll. Internal USD paths, joints, rest matrices and skinning remain
encapsulated. The legacy skeleton adapter remains available for older
program-only .rigexec files; this example does not use it.

The native renderer consumes rigExec's deformed points and baked OpenSubdiv
limit/derivative stencils. Original face-varying UVs and texture bytes are
preserved. Materials use the authored diffuse 0.75, emission 0.5, roughness
0.5, metallic 0, specular 0.35, repeat S and clamp T. Lighting/tonemapping
remain renderer-specific.

Rebuild with sibling usdRig and usd-install, matching USD Python bindings,
OpenSubdiv and a C++ toolchain. Close the Godot editor before setup.

    python demo/setup_rolling.py --build

Omit --build when native libraries are current. Setup bakes the program,
embeds presentation, installs the addon, imports, and verifies. Intermediate
geometry files stay under build/rolling_ball, outside the Godot project.
The exporter currently supports this tutorial's mesh/material graph.

    godot --headless --path demo --script verify_rolling.gd
    mkdir demo/captures
    godot --path demo --fixed-fps 30 --write-movie captures/game.png --script record_rolling.gd

For the material close-up and a rendered seam check at 180 rotation angles:

    godot --path demo --fixed-fps 30 --write-movie captures/material.png --script record_ball_material.gd

These capture lossless PNG sequences at 1280x800 and 30 fps with 4x MSAA.
The material recording also verifies star centering from 12 top-down angles.
Encode either sequence with ffmpeg (replace game with material as needed):

    ffmpeg -framerate 30 -i demo/captures/game%08d.png -filter_complex "split[a][b];[a]palettegen=max_colors=256:reserve_transparent=0:stats_mode=single[p];[b][p]paletteuse=new=1:dither=none" -loop 0 game.gif

The current texture has a centered, continuous blue band and a red star on
the yellow upper hemisphere. Reimport after rebaking to refresh Godot's
cached asset. The headless suite also checks stripe placement and continuity.

Tutorial: usdRig/docs/concepts/tutorial-godot-baked-rig.md
Format: usdRig/docs/specs/rigexec-presentation.md
