extends SceneTree
## Headless checks of the ztrip Godot host (run after an import):
##   godot --headless --path hosts/godot/project --import
##   godot --headless --path hosts/godot/project -s res://tests/test_host.gd
## Synthesises a 120 BPM kick track, plays it through a graph into a light, a
## shader uniform and a camera, and checks the values, seeking, macros,
## hot reload and write-back. Exits with the number of failed checks.

const RATE := 48000
const BPM := 120.0
const SECONDS := 10.0
const WAV := "user://ztrip_test_kicks.wav"
const GRAPH := "user://ztrip_test.mgraph.json"

var _failures := 0
var _checks := 0


func _initialize() -> void:
	_main()


func _main() -> void:
	await _run()
	print("%d/%d checks passed" % [_checks - _failures, _checks])
	quit(_failures)


func _check(ok: bool, what: String) -> void:
	_checks += 1
	if ok:
		print("  ok    ", what)
	else:
		_failures += 1
		printerr("  FAIL  ", what)


func _run() -> void:
	_check(ClassDB.class_exists("ZtripPlayer"), "the GDExtension is loaded")
	if not ClassDB.class_exists("ZtripPlayer"):
		return
	print("ztrip ", ZtripGraphTools.get_version(), " on Godot ", Engine.get_version_info().string)

	var types: Array = ZtripGraphTools.get_node_types()
	_check(types.size() == 15, "the node catalog lists 15 node types (got %d)" % types.size())
	var bad: Dictionary = ZtripGraphTools.check_graph("{not json")
	_check(not bad.ok and bad.errors.size() > 0, "check_graph reports malformed JSON")
	var stage_text := FileAccess.get_file_as_string("res://demo/stage.mgraph.json")
	var stage_check: Dictionary = ZtripGraphTools.check_graph(stage_text)
	_check(stage_check.ok, "the demo stage graph compiles: %s" % str(stage_check.errors))
	_check(ZtripGraphTools.normalize_graph(stage_text).ok, "normalize_graph rewrites the demo graph")

	_write_kick_wav(WAV)
	_write_graph(GRAPH, 4.0, "Lights/Key:light_energy")

	var analysis := ZtripAnalysis.new()
	_check(analysis.load_file(WAV) == OK, "ZtripAnalysis loads the synthetic WAV: " + analysis.get_error())
	_check(absf(analysis.get_bpm() - BPM) < 2.0, "tempo is about 120 BPM (got %.2f)" % analysis.get_bpm())
	var again := ZtripAnalysis.new()
	again.load_file(WAV)
	_check(again.is_from_cache(), "a second load comes from the sigcache")

	# The scene: a light, a shader-uniform material and a camera.
	var scene := Node3D.new()
	scene.name = "Scene"
	var lights := Node3D.new()
	lights.name = "Lights"
	scene.add_child(lights)
	var light := OmniLight3D.new()
	light.name = "Key"
	light.light_energy = 0.7
	lights.add_child(light)
	var shader := Shader.new()
	shader.code = "shader_type spatial;\nuniform float glow = 0.0;\nvoid fragment() { EMISSION = vec3(glow); }\n"
	var material := ShaderMaterial.new()
	material.shader = shader
	material.set_shader_parameter("glow", 0.123)
	var orb := MeshInstance3D.new()
	orb.name = "Orb"
	orb.mesh = SphereMesh.new()
	orb.material_override = material
	scene.add_child(orb)
	var camera := Camera3D.new()
	camera.name = "Camera"
	camera.fov = 75.0
	scene.add_child(camera)

	var player: Node = ClassDB.instantiate("ZtripPlayer")
	player.name = "ZtripPlayer"
	player.threaded_loading = false
	player.audio_file = WAV
	player.graph_file = GRAPH
	scene.add_child(player)
	root.add_child(scene)
	await process_frame # ready: loads the song and graph
	player.wait_until_loaded()

	_check(player.is_ready(), "the player is ready: " + player.get_status())
	_check(absf(player.get_duration() - SECONDS) < 0.05, "duration is 10 s")

	var kicks: PackedFloat64Array = player.get_analysis().get_event_times("onset.Sub")
	_check(kicks.size() >= 18, "about 20 kicks detected (got %d)" % kicks.size())
	if kicks.size() < 10:
		return
	var k := kicks[6]

	player.seek(k + 0.02)
	var hit: float = light.light_energy
	player.seek(k + 0.4)
	var tail: float = light.light_energy
	_check(hit > 2.0, "the light flashes on a kick (energy %.2f)" % hit)
	_check(tail < hit * 0.25, "and decays before the next one (energy %.2f)" % tail)
	var glow = material.get_shader_parameter("glow")
	_check(glow != null and absf(float(glow) - 0.123) > 1e-4, "the shader uniform follows the Sub band (%s)" % str(glow))
	_check(camera.fov >= 50.0 and camera.fov <= 70.0 and absf(camera.fov - 75.0) > 0.1,
			"the camera FOV follows the beat (%.2f)" % camera.fov)
	_check(is_equal_approx(camera.position.x, 1.5), "a vector component target is written (x = %.2f)" % camera.position.x)
	var problems: PackedStringArray = player.get_binding_problems()
	_check(problems.size() == 1 and "Nope" in problems[0], "a missing target is reported: %s" % str(problems))
	_check(player.get_bound_count() == 4, "four targets bound (got %d)" % player.get_bound_count())

	# Seeking gives what playing would.
	var target := k + 1.37
	player.seek(target)
	var sought: float = light.light_energy
	player.seek(0.0)
	var t := 0.0
	while t < target:
		t = minf(t + 1.0 / 60.0, target)
		player.seek(t)
	_check(absf(light.light_energy - sought) < 1e-5,
			"seek and play agree (%.6f vs %.6f)" % [sought, light.light_energy])

	# Macros scale the output.
	player.seek(k + 0.02)
	var before: float = light.light_energy
	_check(player.get_macro_names() == PackedStringArray(["Gain"]), "the Gain macro is listed")
	player.set_macro("Gain", 8.0)
	player.update_now()
	_check(absf(light.light_energy - before * 2.0) < 1e-3, "doubling the Gain macro doubles the flash")
	_check(is_equal_approx(player.get("macros/Gain"), 8.0), "macros are inspector properties")

	# Turning the player off restores the scene exactly.
	player.active = false
	_check(is_equal_approx(light.light_energy, 0.7), "inactive: light energy restored (%.3f)" % light.light_energy)
	_check(is_equal_approx(float(material.get_shader_parameter("glow")), 0.123), "inactive: shader uniform restored")
	_check(is_equal_approx(camera.fov, 75.0) and is_zero_approx(camera.position.x), "inactive: camera restored")
	player.active = true
	_check(light.light_energy > 2.0, "active again: values written again")

	# Hot reload: point the graph somewhere else, the old target goes back.
	_write_graph(GRAPH, 4.0, "Lights/Key:light_indirect_energy")
	player.reload_graph()
	player.update_now()
	_check(is_equal_approx(light.light_energy, 0.7), "reloading restores targets the graph dropped")
	_check(light.light_indirect_energy > 2.0, "and drives the new target")
	_check(is_equal_approx(player.get_macro("Gain"), 8.0), "macro values survive a reload")

	# Leaving the tree restores too.
	scene.remove_child(player)
	_check(is_equal_approx(light.light_indirect_energy, 1.0), "leaving the tree restores the scene")
	player.free()
	scene.free()

	await _test_graph_panel()


# The editor's Motion Graph panel: load, edit, check and save a graph.
func _test_graph_panel() -> void:
	var panel: Control = load("res://addons/ztrip/editor/graph_panel.gd").new()
	root.add_child(panel)
	await process_frame
	panel.open_file("res://demo/stage.mgraph.json")
	_check(panel._status_label.text.begins_with("OK"), "the panel opens the demo graph: " + panel._status_label.text)
	_check(panel._node_by_id.size() == 27, "and shows its 27 nodes")
	panel._add_node("Constant", false)
	var gn_from: StringName = panel._node_by_id["constant"].name
	var gn_to: StringName = panel._node_by_id["kick_gain"].name
	panel._on_connection_request(gn_from, 0, gn_to, 0) # replaces kick_flash.out -> kick_gain.a
	var out := "user://ztrip_panel_test.mgraph.json"
	panel._save_to(out)
	var saved := FileAccess.get_file_as_string(out)
	var r: Dictionary = ZtripGraphTools.check_graph(saved)
	_check(r.ok, "the saved graph compiles: %s" % str(r.errors))
	var doc = JSON.parse_string(saved)
	var into_gain: Array = doc.edges.filter(func(e): return e.to == "kick_gain.a")
	_check(doc.nodes.size() == 28 and into_gain.size() == 1 and into_gain[0].from == "constant.out",
			"the new node and its edge were saved, replacing the old edge")
	_check(saved == ZtripGraphTools.normalize_graph(saved).text, "saved files are in the canonical format")
	panel.queue_free()
	await process_frame


# A kick drum every beat: a decaying 55 Hz sine with a click, 16-bit mono.
func _write_kick_wav(path: String) -> void:
	var frames := int(RATE * SECONDS)
	var data := PackedByteArray()
	data.resize(frames * 2)
	var beat := 60.0 / BPM
	for i in frames:
		var t := float(i) / RATE
		var since := fmod(t, beat)
		var s := 0.0
		if since < 0.3:
			s = sin(TAU * 55.0 * since) * exp(-since * 14.0) * 0.8
			s += sin(TAU * 1800.0 * since) * exp(-since * 400.0) * 0.2
		data.encode_s16(i * 2, int(clampf(s, -1.0, 1.0) * 32000.0))
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_buffer("RIFF".to_ascii_buffer())
	f.store_32(36 + data.size())
	f.store_buffer("WAVEfmt ".to_ascii_buffer())
	f.store_32(16)
	f.store_16(1)
	f.store_16(1)
	f.store_32(RATE)
	f.store_32(RATE * 2)
	f.store_16(2)
	f.store_16(16)
	f.store_buffer("data".to_ascii_buffer())
	f.store_32(data.size())
	f.store_buffer(data)
	f.close()


func _write_graph(path: String, gain: float, light_target: String) -> void:
	var doc := {
		"format": "ztrip.mgraph", "version": 1, "name": "host test",
		"nodes": [
			{"id": "kick", "type": "AudioOnset", "params": {"band": "Sub", "min_velocity": 0.2}},
			{"id": "flash", "type": "ImpulseDecay", "params": {"decay_ms": 150, "velocity": false}},
			{"id": "gain", "type": "Multiply", "params": {"b": gain}},
			{"id": "light", "type": "PropertyOut", "params": {"target": light_target}},
			{"id": "sub", "type": "AudioBand", "params": {"band": "Sub"}},
			{"id": "glow", "type": "PropertyOut", "params": {"target": "Orb:material_override:shader_parameter/glow"}},
			{"id": "clock", "type": "BeatClock"},
			{"id": "fov", "type": "RemapRange", "params": {"out_min": 50, "out_max": 70}},
			{"id": "cam", "type": "PropertyOut", "params": {"target": "Camera:fov"}},
			{"id": "x", "type": "Constant", "params": {"value": 1.5}},
			{"id": "cam_x", "type": "PropertyOut", "params": {"target": "Camera:position:x"}},
			{"id": "missing", "type": "PropertyOut", "params": {"target": "Nope:light_energy"}},
		],
		"edges": [
			{"from": "kick.trigger", "to": "flash.trigger"},
			{"from": "flash.out", "to": "gain.a"},
			{"from": "gain.out", "to": "light.in"},
			{"from": "sub.out", "to": "glow.in"},
			{"from": "clock.beat_phase", "to": "fov.in"},
			{"from": "fov.out", "to": "cam.in"},
			{"from": "x.out", "to": "cam_x.in"},
			{"from": "x.out", "to": "missing.in"},
		],
		"macros": [{"name": "Gain", "node": "gain", "param": "b", "min": 0, "max": 16, "default": gain}],
	}
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string(JSON.stringify(doc, "  "))
	f.close()
