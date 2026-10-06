extends SceneTree

# Headless demo check: evaluates the sample character and prints every
# joint matrix at %.17f, one joint per line, for check_verify.py to compare
# against the golden rigExecPose --pose-out produced. Frame 1001 is
# fk.rigexec at its input defaults (its bake time). Frame 1002 sets
# fk.rigexec's animated inputs to their values at 1002, read as the input
# defaults of fk_1002.rigexec (the same stage baked at 1002), then touches
# the animated inputs as a stage sampler does when time moves. Usage:
#   godot --headless --path demo --script verify.gd > run.txt

func fail(message: String) -> void:
	print("VERIFY: ", message)
	quit(1)


func bound(path: String) -> RigExecCharacter:
	var character := RigExecCharacter.new()
	character.set_data(FileAccess.get_file_as_bytes(path))
	if not character.bind():
		print("VERIFY: ", path, " bind failed: ", character.get_bind_error())
		return null
	return character


func print_frame(time: float, character: RigExecCharacter, player: RigExecPlayer) -> void:
	print("frame ", time)
	var paths := character.get_joint_paths()
	var transforms := player.get_joint_transforms()
	for i in paths.size():
		var t: Transform3D = transforms[i]
		print("joint ", paths[i], " ",
			"%.17f %.17f %.17f " % [t.basis.x.x, t.basis.x.y, t.basis.x.z],
			"%.17f %.17f %.17f " % [t.basis.y.x, t.basis.y.y, t.basis.y.z],
			"%.17f %.17f %.17f " % [t.basis.z.x, t.basis.z.y, t.basis.z.z],
			"%.17f %.17f %.17f" % [t.origin.x, t.origin.y, t.origin.z])


func _init() -> void:
	var character := bound("fk.rigexec")
	var later := bound("fk_1002.rigexec")
	if character == null or later == null:
		quit(1)
		return
	var player := RigExecPlayer.new()
	player.set_character(character)
	if not player.evaluate():
		fail("frame %s: %s" % [character.get_bake_time(), player.get_last_error()])
		player.free()
		return
	print_frame(character.get_bake_time(), character, player)

	# The second bake must be the same graph: same inputs, same static
	# defaults. Its animated defaults are the values the sampler reads at
	# its bake time; scalar inputs keep the gate exact.
	var source := RigExecPlayer.new()
	source.set_character(later)
	var inputs := player.get_inputs()
	var later_inputs := source.get_inputs()
	source.free()
	if inputs.keys() != later_inputs.keys():
		fail("fk_1002.rigexec lists other inputs than fk.rigexec")
		player.free()
		return
	var animated := 0
	for name in inputs:
		var info: Dictionary = inputs[name]
		var other: Dictionary = later_inputs[name]
		if info.type != other.type or info.animated != other.animated:
			fail("input %s differs in type or animation" % name)
			player.free()
			return
		if not info.animated:
			if var_to_bytes(info.default) != var_to_bytes(other.default):
				fail("static input %s differs between the bakes" % name)
				player.free()
				return
			continue
		if info.type != "double" and info.type != "float":
			fail("animated input %s is a %s; this gate drives scalars only" % [name, info.type])
			player.free()
			return
		if not player.set_input(name, other.default):
			fail("set_input %s: %s" % [name, player.get_last_error()])
			player.free()
			return
		animated += 1
	if animated == 0:
		fail("fk.rigexec has no animated inputs to drive")
		player.free()
		return
	player.touch_animated_inputs()
	if not player.evaluate():
		fail("frame %s: %s" % [later.get_bake_time(), player.get_last_error()])
		player.free()
		return
	print_frame(later.get_bake_time(), character, player)
	player.free()

	var imported: Resource = load("res://fk.rigexec")
	if not (imported is RigExecCharacter):
		fail("imported resource is not a RigExecCharacter")
		return
	var imported_character := imported as RigExecCharacter
	if not imported_character.bind():
		fail("imported bind failed: " + imported_character.get_bind_error())
		return
	if imported_character.get_joint_paths() != character.get_joint_paths():
		fail("imported joint paths differ")
		return
	if imported_character.get_bake_time() != character.get_bake_time():
		fail("imported bake time differs")
		return
	if imported_character.get_data() != character.get_data():
		fail("imported bytes differ")
		return
	print("import ok")
	quit(0)
