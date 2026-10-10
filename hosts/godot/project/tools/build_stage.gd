extends SceneTree
## Regenerates demo/stage.tscn (the demo scene) from code:
##   godot --headless --path hosts/godot/project -s res://tools/build_stage.gd
## Overwrites any edits made to the scene in the editor.


func _init() -> void:
	var root := Node3D.new()
	root.name = "Stage"
	root.set_script(load("res://demo/stage.gd"))

	var env := Environment.new()
	env.background_mode = Environment.BG_COLOR
	env.background_color = Color(0.015, 0.015, 0.03)
	env.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR
	env.ambient_light_color = Color(0.5, 0.55, 0.7)
	env.ambient_light_energy = 0.2
	env.tonemap_mode = Environment.TONE_MAPPER_FILMIC
	env.glow_enabled = true
	env.glow_intensity = 1.0
	env.glow_bloom = 0.1
	var world := WorldEnvironment.new()
	world.name = "Environment"
	world.environment = env
	_add(root, root, world)

	var floor_mat := StandardMaterial3D.new()
	floor_mat.albedo_color = Color(0.12, 0.12, 0.14)
	floor_mat.roughness = 0.35
	floor_mat.metallic = 0.2
	var plane := PlaneMesh.new()
	plane.size = Vector2(40, 40)
	plane.material = floor_mat
	var floor_mesh := MeshInstance3D.new()
	floor_mesh.name = "Floor"
	floor_mesh.mesh = plane
	_add(root, root, floor_mesh)

	var orb_mat := ShaderMaterial.new()
	orb_mat.shader = load("res://demo/orb.gdshader")
	# Set every uniform: headless runs can't read the shader's defaults.
	orb_mat.set_shader_parameter("base_color", Color(0.15, 0.45, 1.0))
	orb_mat.set_shader_parameter("glow", 0.5)
	orb_mat.set_shader_parameter("pulse", 0.0)
	var sphere := SphereMesh.new()
	sphere.radius = 1.0
	sphere.height = 2.0
	sphere.radial_segments = 64
	sphere.rings = 32
	var orb := MeshInstance3D.new()
	orb.name = "Orb"
	orb.mesh = sphere
	orb.material_override = orb_mat
	orb.position = Vector3(0, 1.3, 0)
	_add(root, root, orb)

	var pillar_mat := StandardMaterial3D.new()
	pillar_mat.albedo_color = Color(0.55, 0.55, 0.6)
	pillar_mat.roughness = 0.5
	var pillar_mesh := BoxMesh.new()
	pillar_mesh.size = Vector3(0.5, 3.0, 0.5)
	pillar_mesh.material = pillar_mat
	var pillars := Node3D.new()
	pillars.name = "Pillars"
	_add(root, root, pillars)
	for i in 7:
		# An arc behind and beside the orb, open towards the camera.
		var a := PI + PI * i / 6.0
		var p := MeshInstance3D.new()
		p.name = "Pillar%d" % i
		p.mesh = pillar_mesh
		p.position = Vector3(cos(a) * 5.5, 1.5, sin(a) * 4.0 - 0.5)
		_add(root, pillars, p)

	var lights := Node3D.new()
	lights.name = "Lights"
	_add(root, root, lights)
	var key := DirectionalLight3D.new()
	key.name = "Key"
	key.light_energy = 0.25
	key.rotation_degrees = Vector3(-50, 30, 0)
	_add(root, lights, key)
	var kick := OmniLight3D.new()
	kick.name = "Kick"
	kick.light_color = Color(1.0, 0.25, 0.15)
	kick.light_energy = 0.0
	kick.omni_range = 16.0
	kick.position = Vector3(0, 3.0, 1.8)
	_add(root, lights, kick)
	var snare := SpotLight3D.new()
	snare.name = "Snare"
	snare.light_color = Color(0.3, 0.5, 1.0)
	snare.light_energy = 0.0
	snare.spot_range = 20.0
	snare.spot_angle = 35.0
	snare.position = Vector3(-5, 6, 4)
	_add(root, lights, snare)
	snare.look_at_from_position(snare.position, Vector3(0, 1, 0))
	var hats := OmniLight3D.new()
	hats.name = "Hats"
	hats.light_color = Color(1.0, 1.0, 0.9)
	hats.light_energy = 0.0
	hats.omni_range = 8.0
	hats.position = Vector3(4, 4, -2)
	_add(root, lights, hats)

	var camera := Camera3D.new()
	camera.name = "Camera"
	camera.fov = 62.0
	camera.current = true
	_add(root, root, camera)
	camera.look_at_from_position(Vector3(0, 2.4, 8), Vector3(0, 1.2, 0))

	var music := AudioStreamPlayer.new()
	music.name = "Music"
	_add(root, root, music)

	var player: Node = ClassDB.instantiate("ZtripPlayer")
	player.name = "ZtripPlayer"
	player.set("audio_player", NodePath("../Music"))
	player.set("graph_file", "res://demo/stage.mgraph.json")
	player.set("target_root", NodePath(".."))
	player.set("autoplay", true)
	_add(root, root, player)

	var hud := CanvasLayer.new()
	hud.name = "HUD"
	_add(root, root, hud)

	var packed := PackedScene.new()
	var err := packed.pack(root)
	if err == OK:
		err = ResourceSaver.save(packed, "res://demo/stage.tscn")
	print("build_stage: ", error_string(err))
	root.free()
	quit(0 if err == OK else 1)


func _add(owner_node: Node, parent: Node, child: Node) -> void:
	parent.add_child(child)
	child.owner = owner_node
