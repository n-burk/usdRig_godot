extends SceneTree

# Deterministic gameplay capture: drives the ordinary keyboard controller.
# Run with --write-movie <path>.avi --fixed-fps 30 --script record_rolling.gd.
const WAYPOINTS := [Vector2(-10, 9), Vector2(-11.5, 6),
	Vector2(-11.5, 1), Vector2(-10, -2), Vector2(-10, -11.8),
	Vector2(-5, -12), Vector2(6, -12)]
var game
var waypoint := 0
var recording_time := 0.0
var finished_time := -1.0
var held := {}

func _initialize() -> void:
	call_deferred("start")

func start() -> void:
	root.msaa_3d = Viewport.MSAA_4X
	game = load("res://rolling_game.tscn").instantiate()
	root.add_child(game)

func key(code: int, down: bool) -> void:
	if held.get(code, false) == down:
		return
	held[code] = down
	var event := InputEventKey.new()
	event.physical_keycode = code
	event.pressed = down
	Input.parse_input_event(event)

func _physics_process(delta: float) -> bool:
	if game == null:
		return false
	recording_time += delta
	var steer := Vector2.ZERO
	if recording_time > 1.0 and waypoint < WAYPOINTS.size():
		var location := Vector2(game.ball.body.position.x, game.ball.body.position.z)
		var offset: Vector2 = WAYPOINTS[waypoint] - location
		if offset.length() < 0.8:
			waypoint += 1
			print("CAPTURE waypoint ", waypoint, " rings ", game.collected)
		else:
			var velocity := Vector2(game.ball.body.velocity.x, game.ball.body.velocity.z)
			steer = offset - velocity * 0.38
	key(KEY_A, steer.x < -0.22)
	key(KEY_D, steer.x > 0.22)
	key(KEY_W, steer.y < -0.22)
	key(KEY_S, steer.y > 0.22)
	key(KEY_SHIFT, waypoint == WAYPOINTS.size())
	if waypoint == WAYPOINTS.size() and finished_time < 0:
		finished_time = recording_time
	if (finished_time > 0 and recording_time - finished_time > 1.5) or recording_time > 22:
		print("CAPTURE complete: ", game.collected, " rings, ", game.falls, " falls, ", game.ball.evaluations, " native rig evaluations")
		quit(0 if game.collected >= 4 and game.falls == 0 else 1)
	return false
