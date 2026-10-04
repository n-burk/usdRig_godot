extends SceneTree

const Motion = preload("res://rolling_motion.gd")
const MOVE := "Move"
const ROLL := "Roll"
var failures := 0
var checks := 0

func check(value: bool, description: String) -> void:
	checks += 1
	if not value:
		failures += 1
		push_error("FAIL: " + description)

func close_basis(a: Basis, b: Basis) -> bool:
	return a.x.distance_to(b.x) < 0.0001 and a.y.distance_to(b.y) < 0.0001 and a.z.distance_to(b.z) < 0.0001

func _initialize() -> void:
	call_deferred("run")

func run() -> void:
	var character := RigExecCharacter.new()
	character.set_data(FileAccess.get_file_as_bytes("res://rolling_ball.rigexec"))
	check(character.bind(), "free tutorial binary binds")
	var player := RigExecPlayer.new()
	root.add_child(player)
	player.set_character(character)
	player.set_frame(1001)
	check(player.evaluate(), "neutral pose evaluates")
	var paths := character.get_joint_paths()
	var tip := paths.find("/BallAsset/Rig/Joints/Root/Squash/Roll/Spin")
	check(tip >= 0, "tutorial FK tip exists")
	if tip < 0:
		quit(1)
		return
	var neutral: Transform3D = player.get_joint_pose_transforms()[tip]
	check(neutral.origin.distance_to(Vector3.UP) < 0.0001, "centre is one radius above floor")
	check(close_basis(neutral.basis, Basis.IDENTITY), "free mode removes the travel-X gain")
	var q := Motion.advance(Quaternion.IDENTITY, Vector3.RIGHT, Vector3.UP, 1)
	check(close_basis(Basis(q), Basis(Vector3.FORWARD, 1.0)), "+X roll has negative Z axis and distance/radius angle")
	q = Motion.advance(q, Vector3.LEFT, Vector3.UP, 1)
	check(close_basis(Basis(q), Basis.IDENTITY), "retracing travel restores orientation")
	check(close_basis(Basis(Motion.advance(q, Vector3.ZERO, Vector3.UP, 1)), Basis(q)), "blocked ball does not rotate")
	check(close_basis(Basis(Motion.advance(q, Vector3(0, 10, 0), Vector3.UP, 1)), Basis(q)), "normal displacement is not rolling distance")
	q = Motion.advance(Quaternion.IDENTITY, Vector3(TAU, 0, 0), Vector3.UP, 1)
	check(close_basis(Basis(q), Basis.IDENTITY), "one circumference is one revolution")
	# A closed square has nonzero accumulated rotation; position-derived
	# Euler angles would incorrectly return to identity here.
	q = Quaternion.IDENTITY
	for d in [Vector3.RIGHT, Vector3.BACK, Vector3.LEFT, Vector3.FORWARD]:
		q = Motion.advance(q, d, Vector3.UP, 1)
	check(not close_basis(Basis(q), Basis.IDENTITY), "turns preserve noncommuting rotation history")
	var rng := RandomNumberGenerator.new()
	rng.seed = 314159
	for i in 160:
		var normal := Vector3(0.25, 1, -0.15).normalized() if i % 3 == 0 else Vector3.UP
		q = Motion.advance(q, Vector3(rng.randf_range(-1, 1), 0, rng.randf_range(-1, 1)), normal, 1)
		var angles := Motion.rig_degrees(q)
		check(player.set_control(MOVE + ".tx", 3.0), "live translation accepted")
		check(player.set_control(MOVE + ".tz", -2.0), "second translation accepted")
		for axis in 3:
			check(player.set_control(ROLL + ".r" + "xyz"[axis], angles[axis]), "live rotation accepted")
		check(player.evaluate(), "same-frame runtime reevaluation")
		var posed: Transform3D = player.get_joint_pose_transforms()[tip]
		check(close_basis(posed.basis, Basis(q)), "native FK matches quaternion through arbitrary turns/slopes")
		check(posed.origin.distance_to(Vector3(3, 1, -2)) < 0.0001, "live FK centre follows Move")
	check(not player.set_control(ROLL + ".bogus", 0), "unknown channel refused")
	check(not player.set_control("/missing.avars:rx", 0), "unknown control refused")
	check(not player.set_control(ROLL + ".rx", NAN), "NaN refused")
	check(not player.set_control(ROLL + ".rx", INF), "infinity refused")
	for angle in [PI / 2, -PI / 2]:
		q = Quaternion(Vector3.UP, angle)
		var euler := Motion.rig_degrees(q)
		for axis in 3:
			player.set_control(ROLL + ".r" + "xyz"[axis], euler[axis])
		player.evaluate()
		check(close_basis((player.get_joint_pose_transforms()[tip] as Transform3D).basis, Basis(q)), "exact Euler singularity preserves quaternion orientation")
	player.clear_avars()
	check(player.evaluate(), "cleared overrides evaluate")
	var cleared: Transform3D = player.get_joint_pose_transforms()[tip]
	check(close_basis(cleared.basis, neutral.basis) and cleared.origin.distance_to(neutral.origin) < 0.0001, "clear restores constant and sampled channels")
	check(player.get_child_count(true) == 1, "one internal render node, no skeleton hierarchy")
	var bad_bytes := character.get_data().duplicate()
	for section in bad_bytes.decode_u32(8):
		var entry := 16 + section * 20
		if bad_bytes.decode_u32(entry) == 13:
			var offset := bad_bytes.decode_u64(entry + 4)
			bad_bytes[offset + 11] = 57 # Presentation JSON version 1 -> 9, same byte count.
	var bad_character := RigExecCharacter.new()
	bad_character.set_data(bad_bytes)
	player.set_character(bad_character)
	check(not player.evaluate() and not player.has_presentation(), "unsupported embedded version fails closed")
	player.set_character(character)
	check(player.evaluate() and player.get_child_count(true) == 1, "replacing an asset rebuilds one clean render instance")
	player.queue_free()
	var game = load("res://rolling_game.tscn").instantiate()
	root.add_child(game)
	await process_frame
	game.set_physics_process(false)
	check(game.ball.rig_ok, "game starts with native rig")
	check(game.ball.get_node_or_null("Skeleton3D") == null, "no Godot skeleton in the game object")
	check(game.ball.player.has_presentation(), "presentation is embedded in rigexec")
	check(game.ball.player.get_controls().size() == 6, "only six authored controller channels exposed")
	var exposed = game.ball.player.get_controls()
	for name in ["Move.tx", "Move.ty", "Move.tz", "Roll.rx", "Roll.ry", "Roll.rz"]:
		check(exposed.has(name) and not exposed[name].has("path"), "public controller metadata hides internal wiring")
	var internal_property_visible := false
	for property in game.ball.player.get_property_list():
		if property.name in ["skeleton_path", "frame", "autoplay"] and property.usage & PROPERTY_USAGE_EDITOR:
			internal_property_visible = true
	check(not internal_property_visible, "inspector exposes controls, not skeleton/frame setup")
	check(not game.ball.player.set_control("Squash.sx", 2), "unexposed controllers rejected")
	check(not game.ball.player.set_avar("/BallAsset/Rig/Controls/Move.avars:tx", 1), "internal USD paths cannot bypass public controls")
	var visual: MeshInstance3D = game.ball.player.get_children(true)[0]
	check(visual.mesh is ArrayMesh, "ball object owns exported USD mesh, not a generated sphere")
	check(visual.material_override is ShaderMaterial, "ball object owns the USD material translation")
	var mat := visual.material_override as ShaderMaterial
	check(mat.get_shader_parameter("diffuse_scale").is_equal_approx(Vector3.ONE * 0.75), "USD diffuse texture scale preserved")
	check(mat.get_shader_parameter("emission_scale").is_equal_approx(Vector3.ONE * 0.5), "USD emissive texture scale preserved")
	check(is_equal_approx(mat.get_shader_parameter("roughness"), 0.5), "USD roughness preserved")
	check(is_equal_approx(mat.get_shader_parameter("specular"), 0.35), "USD specular preserved")
	check(is_equal_approx(mat.get_shader_parameter("metallic"), 0.0), "USD metallic preserved")
	var manifest = game.ball.player.get_source_info()
	var embedded_texture: Texture2D = mat.get_shader_parameter("ball_texture")
	check(embedded_texture.get_width() == 2 * embedded_texture.get_height() and embedded_texture.get_height() >= 512, "embedded 2:1 source PNG decodes without external texture files")
	var texture_pixels := embedded_texture.get_image()
	var texture_width := texture_pixels.get_width()
	var texture_height := texture_pixels.get_height()
	var stripe_continuous := true
	var stripe_centred := true
	var seam_matches := true
	for x in texture_width:
		var mid := texture_pixels.get_pixel(x, texture_height / 2)
		stripe_continuous = stripe_continuous and mid.b > mid.r * 1.5 and mid.b > mid.g * 1.5
		# Band edges must be symmetric about the equator, for every longitude.
		for fraction in [0.04, 0.07, 0.10, 0.12]:
			var above := texture_pixels.get_pixel(x, int(texture_height * (0.5 - fraction)))
			var below := texture_pixels.get_pixel(x, int(texture_height * (0.5 + fraction)))
			stripe_centred = stripe_centred and ((above.b > above.r) == (below.b > below.r))
	for y in texture_height:
		var left := texture_pixels.get_pixel(0, y)
		var right := texture_pixels.get_pixel(texture_width - 1, y)
		seam_matches = seam_matches and ((left.b > left.r) == (right.b > right.r))
	check(stripe_continuous, "blue stripe has no gaps at any longitude")
	check(stripe_centred, "stripe is centred on the UV equator")
	check(seam_matches, "stripe boundaries match across U wrap")
	var arrays := visual.mesh.surface_get_arrays(0)
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
	var uvs: PackedVector2Array = arrays[Mesh.ARRAY_TEX_UV]
	check(vertices.size() > manifest.points and vertices.size() == uvs.size(), "subdivided source geometry has per-corner UVs")
	var initial_center: Vector3 = game.ball.body.position
	var outward := true
	var wrapped := false
	for i in vertices.size():
		outward = outward and normals[i].dot(vertices[i] - initial_center) > 0.9
		wrapped = wrapped or uvs[i].x > 1.01
	check(outward, "USD inward winding corrected for Godot back-face culling")
	check(wrapped, "USD U-offset and wrap seam preserved")
	check(game.cores.size() == 6, "six rings spawn")
	# Real collision movement, with input injected through Godot.
	var key := InputEventKey.new()
	key.physical_keycode = KEY_D
	key.pressed = true
	Input.parse_input_event(key.duplicate())
	for i in 90:
		await physics_frame
		game._physics_process(1.0 / 60)
	key.pressed = false
	Input.parse_input_event(key.duplicate())
	check(game.ball.body.position.x > 3, "keyboard moves collision body")
	check(not close_basis(Basis(game.ball.orientation), Basis.IDENTITY), "actual displacement rotates rig")
	check(game.ball.evaluations > 90, "rig reevaluates every movement step")
	var moved: PackedVector3Array = visual.mesh.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]
	var expected_point: Vector3 = Basis(game.ball.orientation) * (vertices[0] - initial_center) + game.ball.body.position
	check(moved[0].distance_to(expected_point) < 0.001, "runtime deformed points carry actual rolling orientation and translation")
	var all_points_match := true
	for i in range(0, moved.size(), 97):
		var expected: Vector3 = Basis(game.ball.orientation) * (vertices[i] - initial_center) + game.ball.body.position
		all_points_match = all_points_match and moved[i].distance_to(expected) < 0.001
	check(all_points_match, "deformed render surface matches rolling geometry across the sphere")
	check(visual.transform.is_equal_approx(Transform3D.IDENTITY), "renderer never substitutes a bone or node transform for rig geometry")
	game.ball.body.position.y = 5
	game.ball.angular_velocity = Vector3(0.6, 0.2, -0.4)
	var in_air: Quaternion = game.ball.orientation
	await physics_frame
	game._physics_process(1.0 / 60)
	check(not game.ball.body.is_on_floor() and not close_basis(Basis(in_air), Basis(game.ball.orientation)), "airborne ball retains angular motion")
	# Run into the first slalom wall and settle: distance, not requested
	# velocity, must determine roll, including when blocked.
	game.ball.body.position = Vector3(-5, 1.01, 7)
	game.ball.body.velocity = Vector3.ZERO
	key.physical_keycode = KEY_W
	key.pressed = true
	Input.parse_input_event(key.duplicate())
	for i in 120:
		await physics_frame
		game._physics_process(1.0 / 60)
	check(game.ball.body.position.z > 5.3, "solid slalom wall blocks travel")
	var stopped: Quaternion = game.ball.orientation
	for i in 20:
		await physics_frame
		game._physics_process(1.0 / 60)
	check(close_basis(Basis(stopped), Basis(game.ball.orientation)), "pushing into wall does not spin ball")
	key.pressed = false
	Input.parse_input_event(key.duplicate())
	game.ball.body.position.y = -10
	game._physics_process(1.0 / 60)
	check(game.falls == 1 and game.ball.body.position.distance_to(game.SPAWN) < 0.01, "fall respawns")
	for core in game.cores:
		game.ball.body.position = core.position
		game._update_collectibles(0)
	check(game.collected == 6, "each ring collected once")
	game._update_collectibles(0)
	check(game.collected == 6, "collected ring cannot count twice")
	game.ball.body.position = game.goal.position + Vector3.UP
	game._update_collectibles(0)
	check(game.won, "all rings unlock finish")
	game._reset_game()
	check(not game.won and game.collected == 0 and game.falls == 0, "restart resets game")
	check(close_basis(Basis(game.ball.orientation), Basis.IDENTITY), "restart resets rolling history")
	if "--capture" in OS.get_cmdline_user_args():
		await process_frame
		await RenderingServer.frame_post_draw
		root.get_texture().get_image().save_png("res://rolling_game_preview.png")
	game.queue_free()
	await process_frame
	print("ROLLING CHECK: %d checks, %d failures" % [checks, failures])
	quit(1 if failures else 0)

