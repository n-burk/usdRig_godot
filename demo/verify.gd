extends SceneTree

# Headless demo check (M4 gate): evaluates the sample character and prints
# every joint matrix at %.17f, one joint per line, for `cmp` against the
# golden rigExecPose --pose-out produced. Usage:
#   godot --headless --path demo --script verify.gd > run.txt
#   cmp run.txt fk.golden.txt

func _init() -> void:
	var character := RigExecCharacter.new()
	character.set_data(FileAccess.get_file_as_bytes("fk.rigexec"))
	if not character.bind():
		print("VERIFY: bind failed: ", character.get_bind_error())
		quit(1)
		return
	var player := RigExecPlayer.new()
	player.set_character(character)
	for frame in character.get_frame_times():
		player.set_frame(frame)
		if not player.evaluate():
			print("VERIFY: frame ", frame, ": ", player.get_last_error())
			quit(1)
			return
		print("frame ", frame)
		var paths := character.get_joint_paths()
		var transforms := player.get_joint_transforms()
		for i in paths.size():
			var t: Transform3D = transforms[i]
			print("joint ", paths[i], " ",
				"%.17f %.17f %.17f " % [t.basis.x.x, t.basis.x.y, t.basis.x.z],
				"%.17f %.17f %.17f " % [t.basis.y.x, t.basis.y.y, t.basis.y.z],
				"%.17f %.17f %.17f " % [t.basis.z.x, t.basis.z.y, t.basis.z.z],
				"%.17f %.17f %.17f" % [t.origin.x, t.origin.y, t.origin.z])
	var imported: Resource = load("res://fk.rigexec")
	if not (imported is RigExecCharacter):
		print("VERIFY: imported resource is not a RigExecCharacter")
		quit(1)
		return
	var imported_character := imported as RigExecCharacter
	if not imported_character.bind():
		print("VERIFY: imported bind failed: ",
			imported_character.get_bind_error())
		quit(1)
		return
	if imported_character.get_joint_paths() != character.get_joint_paths():
		print("VERIFY: imported joint paths differ")
		quit(1)
		return
	if imported_character.get_frame_times() != character.get_frame_times():
		print("VERIFY: imported frame times differ")
		quit(1)
		return
	print("import ok")
	quit(0)
