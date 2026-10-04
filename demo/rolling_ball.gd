extends Node3D

# Reusable ball object. Collision motion supplies the inputs; usdRig alone
# computes and renders geometry. Mesh, material and texture live inside the asset.
const Motion = preload("res://rolling_motion.gd")
const radius := 1.0 # Authored tutorial radius; mesh/collider/rig share this unit.
@export var automatic_physics := true
@onready var body: CharacterBody3D = $Physics
@onready var player: RigExecPlayer = $RigExecPlayer
var orientation := Quaternion.IDENTITY
var angular_velocity := Vector3.ZERO
var last_angles := Vector3.ZERO
var evaluations := 0
var rig_ok := false
var controls_enabled := true

func _ready() -> void:
	if not player.has_presentation() or not player.evaluate():
		_fail()
		return
	rig_ok = true
	sync_rig()

func reset(at: Vector3) -> void:
	body.position = at
	body.velocity = Vector3.ZERO
	orientation = Quaternion.IDENTITY
	angular_velocity = Vector3.ZERO
	player.reset_controls()
	sync_rig()

func _physics_process(delta: float) -> void:
	if automatic_physics:
		advance(delta)

func _unhandled_key_input(event: InputEvent) -> void:
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_SPACE and body.is_on_floor() and controls_enabled:
			body.velocity.y = 7

func advance(delta: float, enabled: bool = true) -> void:
	controls_enabled = enabled
	var direction := Vector3.ZERO
	if enabled:
		direction.x = float(Input.is_physical_key_pressed(KEY_D) or Input.is_physical_key_pressed(KEY_RIGHT)) - float(Input.is_physical_key_pressed(KEY_A) or Input.is_physical_key_pressed(KEY_LEFT))
		direction.z = float(Input.is_physical_key_pressed(KEY_S) or Input.is_physical_key_pressed(KEY_DOWN)) - float(Input.is_physical_key_pressed(KEY_W) or Input.is_physical_key_pressed(KEY_UP))
	drive(direction, delta, Input.is_physical_key_pressed(KEY_SHIFT) or not enabled)

# Other games can call drive() with an AI/controller direction instead of keys.
func drive(direction: Vector3, delta: float, braking: bool = false) -> void:
	if not rig_ok or delta <= 0:
		return
	direction = direction.normalized()
	var target := direction * (2.0 if braking else 8.0)
	var accel := 25.0 if braking else (12.0 if direction != Vector3.ZERO else 5.0)
	body.velocity.x = move_toward(body.velocity.x, target.x, accel * delta)
	body.velocity.z = move_toward(body.velocity.z, target.z, accel * delta)
	body.velocity.y -= 20.0 * delta
	var before := body.position
	body.move_and_slide()
	var displacement := body.position - before
	if body.is_on_floor():
		var normal := global_basis.inverse() * body.get_floor_normal()
		orientation = Motion.advance(orientation, displacement, normal, radius)
		angular_velocity = normal.cross(displacement.slide(normal)) / (radius * delta)
	elif angular_velocity.length_squared() > 0.000001:
		orientation = (Quaternion(angular_velocity.normalized(), angular_velocity.length() * delta) * orientation).normalized()
	sync_rig()

func sync_rig() -> void:
	last_angles = Motion.rig_degrees(orientation)
	var values := {
		"Move.tx": body.position.x,
		"Move.ty": body.position.y - radius,
		"Move.tz": body.position.z,
		"Roll.rx": last_angles.x,
		"Roll.ry": last_angles.y,
		"Roll.rz": last_angles.z,
	}
	for path in values:
		if not player.set_control(path, values[path]):
			_fail()
			return
	if not player.evaluate():
		_fail()
		return
	evaluations += 1

func _fail() -> void:
	rig_ok = false
	push_error("Ball usdRig execution failed: " + player.get_last_error())
