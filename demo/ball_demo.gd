extends Node3D

# Ball demo scene: plays docs/examples/tutorial_rolling_ball.usda (baked
# to ball.rigexec) on a runtime-built Skeleton3D, with a sphere riding
# the tip joint through a BoneAttachment3D. Press Play; the player
# advances the baked frames at 24fps and loops (see RigExecPlayer).

const CHARACTER_PATH := "res://ball.rigexec"


func _ready() -> void:
	var character: RigExecCharacter = load(CHARACTER_PATH)
	if character == null:
		push_error("ball_demo: cannot load ", CHARACTER_PATH)
		return
	if not character.bind():
		push_error("ball_demo: bind failed: ", character.get_bind_error())
		return
	var player := $RigExecPlayer as RigExecPlayer
	player.set_character(character)
	var frames := character.get_frame_times()
	player.set_frame(frames[0])
	if not player.evaluate():
		push_error("ball_demo: first evaluate failed: ",
			player.get_last_error())
		return
	var skeleton := _build_skeleton(character)
	add_child(skeleton)
	player.set_skeleton_path(skeleton.get_path())
	_ride_tip_joint(skeleton, character)
	player.play()
	$Camera3D.position = Vector3(4.0, 3.5, 9.0)
	$Camera3D.look_at(Vector3(4.0, 1.0, 0.0))


# Bones parent-first (depth order guarantees it), rests in parent space
# from the first baked frame. apply_to_skeleton sets global poses every
# frame, so the rests only matter before the first evaluate.
func _build_skeleton(character: RigExecCharacter) -> Skeleton3D:
	var player := $RigExecPlayer as RigExecPlayer
	var paths := character.get_joint_paths()
	var globals := player.get_joint_rest_transforms()
	var order: Array[int] = []
	order.resize(paths.size())
	for i in paths.size():
		order[i] = i
	order.sort_custom(func(a: int, b: int) -> bool:
		return paths[a].count("/") < paths[b].count("/"))
	var skeleton := Skeleton3D.new()
	skeleton.name = "Skeleton3D"
	var index_of: Dictionary = {}
	for slot in order.size():
		var i: int = order[slot]
		var leaf: String = paths[i].get_file()
		var bone := skeleton.get_bone_count()
		skeleton.add_bone(leaf)
		index_of[paths[i]] = bone
		var parent := -1
		var parent_path: String = paths[i].get_base_dir()
		if index_of.has(parent_path):
			parent = index_of[parent_path]
		skeleton.set_bone_parent(bone, parent)
		var rest: Transform3D = globals[i]
		if parent >= 0:
			var parent_global: Transform3D = globals[index_of[parent_path]]
			rest = parent_global.affine_inverse() * globals[i]
		skeleton.set_bone_rest(bone, rest)
	return skeleton


# Use the exported USD mesh, face-varying UVs and material on the tip joint.
func _ride_tip_joint(skeleton: Skeleton3D,
		character: RigExecCharacter) -> void:
	var paths := character.get_joint_paths()
	var tip := ""
	for path in paths:
		if path.count("/") >= tip.count("/"):
			tip = path
	var attachment := BoneAttachment3D.new()
	attachment.set_bone_name(tip.get_file())
	skeleton.add_child(attachment)
	var ball := MeshInstance3D.new()
	ball.mesh = load("res://ball_assets/tutorial_ball.obj")
	ball.material_override = load("res://ball_assets/tutorial_ball_material.tres")
	ball.position.y = -1.0 # USD mesh is centred at y=1; Spin pivots there.
	attachment.add_child(ball)
