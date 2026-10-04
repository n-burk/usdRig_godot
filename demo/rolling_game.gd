extends Node3D

const SPAWN := Vector3(0, 1.05, 11)
const CORE_POSITIONS := [Vector3(-10, 1.25, 9), Vector3(10, 1.25, 8),
	Vector3(-10, 1.25, -2), Vector3(10, 1.25, -4),
	Vector3(-5, 1.25, -12), Vector3(6, 1.25, -12)]

@onready var ball = $Ball
var camera: Camera3D
var cores: Array[Node3D] = []
var collected := 0
var elapsed := 0.0
var won := false
var falls := 0
var score_label: Label
var status_label: Label
var message_label: Label
var time_label: Label
var goal: MeshInstance3D

func _ready() -> void:
	_build_world()
	_build_hud()
	if not ball.rig_ok:
		message_label.text = "Rig could not load. See the Godot error log."
		set_physics_process(false)
		return
	_reset_game()

func _material(color: Color, glow: float = 0.0) -> StandardMaterial3D:
	var m := StandardMaterial3D.new()
	m.albedo_color = color
	m.roughness = 0.55
	if glow > 0:
		m.emission_enabled = true
		m.emission = color
		m.emission_energy_multiplier = glow
	return m

func _box(at: Vector3, size: Vector3, color: Color,
		rotation_z: float = 0.0) -> StaticBody3D:
	var solid := StaticBody3D.new()
	add_child(solid)
	solid.position = at
	solid.rotation.z = rotation_z
	var mesh := MeshInstance3D.new()
	var shape := BoxMesh.new()
	shape.size = size
	mesh.mesh = shape
	mesh.material_override = _material(color)
	solid.add_child(mesh)
	var collision := CollisionShape3D.new()
	var box := BoxShape3D.new()
	box.size = size
	collision.shape = box
	solid.add_child(collision)
	return solid

func _ring(at: Vector3, inner: float, outer: float, color: Color) -> MeshInstance3D:
	var node := MeshInstance3D.new()
	var mesh := TorusMesh.new()
	mesh.inner_radius = inner
	mesh.outer_radius = outer
	mesh.rings = 32
	mesh.ring_segments = 12
	node.mesh = mesh
	node.material_override = _material(color, 0.6)
	add_child(node)
	node.position = at
	return node

func _build_world() -> void:
	var environment := WorldEnvironment.new()
	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color("101b30")
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color("aecae1")
	env.ambient_light_energy = 0.65
	environment.environment = env
	add_child(environment)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-55, -25, 0)
	light.light_color = Color("fff0d4")
	light.light_energy = 1.5
	light.shadow_enabled = true
	add_child(light)
	_box(Vector3(0, -0.6, 0), Vector3(30, 1.2, 34), Color("223e53"))
	# Thin inset tiles make the travelled distance easy to read.
	for x in range(-14, 15, 2):
		for z in range(-16, 17, 2):
			var tile := MeshInstance3D.new()
			var plane := PlaneMesh.new()
			plane.size = Vector2(1.96, 1.96)
			tile.mesh = plane
			tile.material_override = _material(Color("2c4b60") if (x + z) % 4 == 0 else Color("28465a"))
			add_child(tile)
			tile.position = Vector3(x, 0.005, z)
	# A slalom, a low ramp and open edges to make momentum matter.
	_box(Vector3(-5, 0.7, 4), Vector3(9, 1.4, 1), Color("446579"))
	_box(Vector3(6, 0.7, -5), Vector3(9, 1.4, 1), Color("446579"))
	_box(Vector3(-1, 0.6, -5), Vector3(1, 1.2, 6), Color("446579"))
	_box(Vector3(7, 0.45, 2), Vector3(5, 0.25, 4), Color("427c88"), 0.20)
	for z in [-17.0, 17.0]:
		_box(Vector3(0, -0.03, z), Vector3(30, 0.10, 0.14), Color("41cfc5"))
	for x in [-15.0, 15.0]:
		_box(Vector3(x, -0.03, 0), Vector3(0.14, 0.10, 34), Color("41cfc5"))
	goal = _ring(Vector3(0, 0.09, -14), 1.6, 1.85, Color("50697d"))
	_ring(Vector3(SPAWN.x, 0.05, SPAWN.z), 1.5, 1.6, Color("72b9cd"))
	camera = Camera3D.new()
	add_child(camera)
	camera.current = true
	camera.fov = 56

func _label(parent: Node, text: String, size: int, color: Color) -> Label:
	var label := Label.new()
	label.text = text
	label.add_theme_font_size_override("font_size", size)
	label.add_theme_color_override("font_color", color)
	parent.add_child(label)
	return label

func _build_hud() -> void:
	var canvas := CanvasLayer.new()
	add_child(canvas)
	var margin := MarginContainer.new()
	canvas.add_child(margin)
	margin.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	for side in ["left", "top", "right", "bottom"]:
		margin.add_theme_constant_override("margin_" + side, 28)
	margin.mouse_filter = Control.MOUSE_FILTER_IGNORE
	var column := VBoxContainer.new()
	margin.add_child(column)
	_label(column, "USD RIG  /  INTERACTIVE SAMPLE", 14, Color("5de0d3"))
	_label(column, "ROLL / COLLECT", 34, Color("f4f7ff"))
	_label(column, "Find six energy rings. Reach the exit. Keep your balance.", 16, Color("bfd0df"))
	score_label = _label(column, "", 24, Color("ffdc7b"))
	time_label = _label(column, "", 16, Color("bfd0df"))
	var space := Control.new()
	space.size_flags_vertical = Control.SIZE_EXPAND_FILL
	column.add_child(space)
	message_label = _label(column, "", 23, Color("ffdc7b"))
	status_label = _label(column, "", 14, Color("5de0d3"))
	_label(column, "WASD / ARROWS  move     SPACE  hop     SHIFT  brake     R  restart", 16, Color("edf4ff"))

func _reset_game() -> void:
	for core in cores:
		core.queue_free()
	cores.clear()
	for at in CORE_POSITIONS:
		var core := _ring(at, 0.42, 0.66, Color("64f1d5"))
		core.rotation.x = PI / 2
		cores.append(core)
	collected = 0
	elapsed = 0
	falls = 0
	won = false
	goal.material_override = _material(Color("50697d"))
	message_label.text = "Six rings. One rolling rig."
	_respawn()
	camera.position = ball.body.position + Vector3(0, 17, 20)
	camera.look_at(ball.body.position + Vector3(0, 0, -2))
	_update_hud()

func _respawn() -> void:
	ball.reset(SPAWN)

func _unhandled_key_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_R:
			_reset_game()

func _physics_process(delta: float) -> void:
	if not ball.rig_ok:
		message_label.text = "Ball rig failed: " + ball.player.get_last_error()
		return
	if not won:
		elapsed += delta
	ball.advance(delta, not won)
	if ball.body.position.y < -7:
		falls += 1
		_respawn()
		message_label.text = "Back on the pad. Your collected rings are safe."
	_update_collectibles(delta)
	camera.position = camera.position.lerp(ball.body.position + Vector3(0, 17, 20), 1.0 - exp(-4.0 * delta))
	camera.look_at(ball.body.position + Vector3(0, 0, -2))
	_update_hud()

func _update_hud() -> void:
	score_label.text = "%02d / 06  RINGS" % collected
	time_label.text = "%05.1f s     %d falls" % [elapsed, falls]
	status_label.text = "USD RIG LIVE  /  6 CONTROLS  /  %.1f m/s\nROLL  X %+.0f°   Y %+.0f°   Z %+.0f°" % [Vector2(ball.body.velocity.x, ball.body.velocity.z).length(), ball.last_angles.x, ball.last_angles.y, ball.last_angles.z]

func _update_collectibles(delta: float) -> void:
	for core in cores:
		if not core.visible:
			continue
		core.rotate_y(delta * 1.5)
		if ball.body.position.distance_to(core.position) < 1.65:
			core.hide()
			collected += 1
			message_label.text = "Ring collected. Keep rolling."
			if collected == CORE_POSITIONS.size():
				goal.material_override = _material(Color("ffcf70"), 1.0)
				message_label.text = "All six! Roll onto the gold exit at the far end."
	if not won and collected == CORE_POSITIONS.size() and ball.body.position.distance_to(goal.position + Vector3.UP) < 1.8:
		won = true
		message_label.text = "COURSE COMPLETE  /  %.1f seconds  /  R to play again" % elapsed

