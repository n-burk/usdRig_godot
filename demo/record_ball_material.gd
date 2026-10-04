extends SceneTree

# Real native renderer capture, including a pixel check through a complete turn.
# godot --path demo --fixed-fps 30 --write-movie material.avi --script record_ball_material.gd
var player: RigExecPlayer
var tick := 0
var bad_frames := 0
var off_centre_frames := 0
var label: Label

func _initialize() -> void:
	call_deferred("start")

func start() -> void:
	root.msaa_3d = Viewport.MSAA_4X
	var world := Node3D.new()
	root.add_child(world)
	var environment := WorldEnvironment.new()
	var settings := Environment.new()
	settings.background_mode = Environment.BG_COLOR
	settings.background_color = Color("142234")
	settings.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	settings.ambient_light_color = Color.WHITE
	settings.ambient_light_energy = 0.7
	environment.environment = settings
	world.add_child(environment)
	var light := DirectionalLight3D.new()
	light.rotation_degrees = Vector3(-25, -25, 0)
	light.light_energy = 0.8
	world.add_child(light)
	var camera := Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 3.2
	camera.position = Vector3(0, 1, 5)
	world.add_child(camera)
	camera.look_at(Vector3(0, 1, 0))
	camera.current = true
	player = RigExecPlayer.new()
	world.add_child(player)
	player.set_character(load("res://rolling_ball.rigexec"))
	label = Label.new()
	label.position = Vector2(28, 24)
	label.add_theme_font_size_override("font_size", 25)
	root.add_child(label)
	for frame in 360:
		var phase := frame / 30.0
		if frame < 180:
			player.set_control("Roll.ry", frame * 2.0)
			label.text = "360-degree seam check | Roll.ry = %03d" % (frame * 2)
		elif frame < 240:
			camera.position = Vector3(0, 6, 0)
			camera.look_at(Vector3(0, 1, 0), Vector3(0, 0, -1))
			player.set_control("Roll.ry", (frame - 180) * 6.0)
			label.text = "Top view | star centred on the north pole"
		else:
			camera.position = Vector3(0, 3.6, 5)
			camera.look_at(Vector3(0, 1, 0))
			var q := Quaternion(Vector3(0.7, 0.3, 0.6).normalized(), (phase - 8.0) * TAU / 4.0)
			var angles: Vector3 = Basis(q).get_euler(EULER_ORDER_ZYX) * (180.0 / PI)
			for axis in 3:
				player.set_control("Roll.r" + "xyz"[axis], angles[axis])
			label.text = "Free rotation | Roll.rx / Roll.ry / Roll.rz"
		if not player.evaluate():
			push_error(player.get_last_error())
			quit(1)
			return
		await process_frame
		await RenderingServer.frame_post_draw
		if frame == 180:
			root.get_texture().get_image().save_png("res://ball_top_preview.png")
		if frame == 240:
			root.get_texture().get_image().save_png("res://ball_reference_preview.png")
		if frame >= 180 and frame < 240 and frame % 5 == 0:
			var top := root.get_texture().get_image()
			var centre := Vector2(top.get_size()) / 2.0
			var total := Vector2.ZERO
			var count := 0
			var extent := top.get_height() * 0.275
			for y in range(int(centre.y - extent), int(centre.y + extent)):
				for x in range(int(centre.x - extent), int(centre.x + extent)):
					var c := top.get_pixel(x, y)
					if c.r > c.g * 1.5 and c.r > c.b * 1.5:
						total += Vector2(x, y)
						count += 1
			if count == 0 or (total / max(count, 1)).distance_to(centre) > top.get_height() / 160.0:
				off_centre_frames += 1
		if frame < 180:
			var shot := root.get_texture().get_image()
			var centre := shot.get_size() / 2
			var radius := int(shot.get_height() / 3.2 * 0.75)
			var solid := true
			for x in range(centre.x - radius, centre.x + radius):
				var c := shot.get_pixel(x, centre.y)
				solid = solid and c.b > c.r * 1.3 and c.b > c.g * 1.1
			if not solid:
				bad_frames += 1
	print("MATERIAL CAPTURE: 180 seam angles checked; ", bad_frames, " frames with a broken equatorial stripe")
	print("TOP CENTRE CHECK: 12 orientations; ", off_centre_frames, " off-centre star silhouettes")
	quit(0 if bad_frames == 0 and off_centre_frames == 0 else 1)
