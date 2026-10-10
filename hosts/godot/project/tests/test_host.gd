extends SceneTree
## Headless checks of the ztrip Godot host (run after an import):
##   godot --headless --path hosts/godot/project --import
##   godot --headless --path hosts/godot/project -s res://tests/test_host.gd
## Synthesises a 120 BPM kick track, plays it through a graph into a light, a
## shader uniform and a camera, and checks the values, seeking, macros,
## hot reload and write-back, then drafts and edits the song's timeline
## through the Timeline panel, and plays the kick track in as simulated live
## input. Exits with the number of failed checks.

const RATE := 48000
const BPM := 120.0
const SECONDS := 10.0
const WAV := "user://ztrip_test_kicks.wav"
const GRAPH := "user://ztrip_test.mgraph.json"
const LANE_GRAPH := "user://ztrip_test_lanes.mgraph.json"
const TEMPO := "user://ztrip_test.tempo.json"
const SEQUENCE := "user://ztrip_test.nseq.json"
const LIVE_GRAPH := "user://ztrip_test_live.mgraph.json"

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
	_check(types.size() >= 16, "the node catalog lists every node type, NoteLane included (got %d)" % types.size())
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
	await _test_timeline()
	await _test_live()


# Live input: the kick track played in real time as if from a microphone.
func _test_live() -> void:
	_write_graph(LIVE_GRAPH, 4.0, "Lights/Key:light_energy")
	var devices: Array = ZtripPlayer.get_live_devices()
	_check(devices is Array, "capture devices can be listed (%d found)" % devices.size())

	var scene := Node3D.new()
	var lights := Node3D.new()
	lights.name = "Lights"
	scene.add_child(lights)
	var light := OmniLight3D.new()
	light.name = "Key"
	light.light_energy = 0.7
	lights.add_child(light)
	var player: Node = ClassDB.instantiate("ZtripPlayer")
	player.graph_file = LIVE_GRAPH
	player.live_input = true
	player.live_simulate_file = WAV
	scene.add_child(player)
	root.add_child(scene)
	await process_frame
	_check(not player.is_live_running(), "live capture waits for play")

	player.play()
	_check(player.is_live_running(), "play starts live input: " + player.get_status())
	# Watch the light for 2.6 s of real time: kicks come every 0.5 s.
	var flashes := 0
	var lit := false
	var brightest := 0.0
	var start := Time.get_ticks_msec()
	while Time.get_ticks_msec() - start < 2600:
		await process_frame
		var now: bool = light.light_energy > 1.5
		if now and not lit:
			flashes += 1
		lit = now
		brightest = maxf(brightest, light.light_energy)
	var t: float = player.time
	_check(t > 2.0 and t < 3.0, "live time follows the input's clock (%.2f s after 2.6 s)" % t)
	_check(flashes >= 4 and flashes <= 6, "the light flashes on live kicks (%d flashes, peak %.2f)" % [flashes, brightest])
	player.stop()
	_check(not player.is_live_running(), "stop ends live input")
	scene.free()


# The editor's Motion Graph panel: load, edit, check and save a graph.
func _test_graph_panel() -> void:
	var script: GDScript = load("res://addons/ztrip/editor/graph_panel.gd")
	_check(script != null and script.can_instantiate(), "the Motion Graph panel script loads")
	if script == null or not script.can_instantiate():
		return
	var panel: Control = script.new()
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


# The song's timeline: drafted on load, played by NoteLane, edited in the
# Timeline panel, saved and loaded back.
func _test_timeline() -> void:
	_check(is_equal_approx(ZtripTimeline.grid_step("1/8T"), 1.0 / 3.0) and ZtripTimeline.grid_step("1/7") == 0.0,
			"grid steps parse, triplets included")
	for f in [TEMPO, SEQUENCE]:
		if FileAccess.file_exists(f):
			DirAccess.remove_absolute(ProjectSettings.globalize_path(f))
	_write_lane_graph(LANE_GRAPH)

	var scene := Node3D.new()
	var light := OmniLight3D.new()
	light.name = "Key"
	light.light_energy = 0.0
	scene.add_child(light)
	var player: Node = ClassDB.instantiate("ZtripPlayer")
	player.threaded_loading = false
	player.audio_file = WAV
	player.graph_file = LANE_GRAPH
	player.tempo_file = TEMPO
	player.sequence_file = SEQUENCE
	scene.add_child(player)
	root.add_child(scene)
	await process_frame
	player.wait_until_loaded()

	var tl = player.get_timeline()
	_check(tl.get_lane_names() == PackedStringArray(["Kick", "Snare", "Hats"]), "lanes are drafted: %s" % str(tl.get_lane_names()))
	_check(absf(tl.bpm_at(4.0) - BPM) < 1.0, "the tempo map is drafted at 120 BPM (%.2f)" % tl.bpm_at(4.0))
	var notes: Array = tl.get_notes("Kick")
	var worst := 0.0
	for n in notes:
		var s: float = tl.note_seconds("Kick", n.start_beat)
		worst = maxf(worst, absf(s - roundf(s / 0.5) * 0.5))
	_check(notes.size() >= 18 and notes.size() <= 21, "about 20 kick notes drafted (got %d)" % notes.size())
	_check(worst < 0.020, "every drafted kick lands within 20 ms of the true kick (worst %.1f ms)" % (worst * 1000.0))
	_check("drafted" in player.get_status(), "the status says the timeline is drafted")

	var kick_t: float = tl.note_seconds("Kick", notes[6].start_beat)
	player.seek(kick_t + 0.02)
	_check(light.light_energy > 2.0, "a Kick note flashes the light (%.2f)" % light.light_energy)
	tl.remove_note("Kick", 6)
	player.update_now()
	_check(light.light_energy < 0.5, "removing the note darkens it at once (%.2f)" % light.light_energy)

	# The panel, driven by synthetic mouse and key events.
	var script: GDScript = load("res://addons/ztrip/editor/timeline_panel.gd")
	_check(script != null and script.can_instantiate(), "the Timeline panel script loads")
	if script == null or not script.can_instantiate():
		return
	var panel: Control = script.new()
	panel.size = Vector2(1400, 320)
	root.add_child(panel)
	panel.attach_player(player)
	await process_frame
	panel.set_view(0.0, 120.0)
	_check(panel._lanes.size() == 3 and panel._notes["Kick"].size() == notes.size() - 1, "the panel shows the lanes and notes")

	var count: int = tl.get_note_count("Kick")
	var at: Vector2 = panel.lane_point("Kick", 6.5)
	_click(panel, at, true)
	_click(panel, at, false)
	_check(tl.get_note_count("Kick") == count + 1, "clicking an empty spot adds a note")
	var at_click: Array = tl.get_notes("Kick").filter(func(n): return is_equal_approx(n.start_beat, 6.5))
	_check(at_click.size() == 1, "on the grid where it was clicked")
	player.seek(tl.note_seconds("Kick", 6.5) + 0.02)
	_check(light.light_energy > 2.0, "and the player plays it (%.2f)" % light.light_energy)

	# Drag it a quarter beat later: it snaps to the 1/16 grid.
	var from: Vector2 = panel.lane_point("Kick", 6.5) + Vector2(2, 0)
	var to: Vector2 = panel.lane_point("Kick", 6.77) + Vector2(2, 0)
	_click(panel, from, true)
	var motion := InputEventMouseMotion.new()
	motion.position = to
	panel._on_canvas_input(motion)
	_click(panel, to, false)
	var starts: Array = tl.get_notes("Kick").map(func(n): return n.start_beat)
	_check(starts.has(6.75) and not starts.has(6.5), "dragging moves the note, snapped to the grid")
	_key(panel, KEY_LEFT)
	starts = tl.get_notes("Kick").map(func(n): return n.start_beat)
	_check(starts.has(6.5), "Left moves the selection one grid step back")
	_key(panel, KEY_DELETE)
	_check(tl.get_note_count("Kick") == count, "Delete removes it")
	_key(panel, KEY_Z, true)
	_check(tl.get_note_count("Kick") == count + 1, "Ctrl+Z brings it back")
	_key(panel, KEY_Z, true, true)
	_check(tl.get_note_count("Kick") == count, "Ctrl+Shift+Z removes it again")

	var before: float = tl.note_seconds("Kick", 8.0)
	panel.nudge(0.010)
	_check(absf(tl.note_seconds("Kick", 8.0) - before - 0.010) < 1e-6 and is_equal_approx(tl.get_nudge(), 0.010),
			"Nudge moves the grid and notes 10 ms")
	panel.add_lane("Crash")
	panel.add_note("Crash", 4.0)
	_check(tl.get_note_count("Crash") == 1, "a new lane takes notes")

	_check(panel.save() == OK and FileAccess.file_exists(TEMPO) and FileAccess.file_exists(SEQUENCE),
			"Save writes the .tempo.json and .nseq.json")
	var tempo_doc = JSON.parse_string(FileAccess.get_file_as_string(TEMPO))
	var seq_doc = JSON.parse_string(FileAccess.get_file_as_string(SEQUENCE))
	_check(tempo_doc is Dictionary and tempo_doc.format == "ztrip.tempo" and is_equal_approx(tempo_doc.nudge_s, 0.010),
			"the tempo file keeps the nudge")
	_check(seq_doc is Dictionary and seq_doc.format == "ztrip.nseq" and seq_doc.lanes.size() == 4, "the sequence file has all four lanes")
	panel.queue_free()
	scene.free()
	await process_frame

	# A fresh player loads the saved files instead of drafting.
	var again: Node = ClassDB.instantiate("ZtripPlayer")
	again.threaded_loading = false
	again.audio_file = WAV
	again.tempo_file = TEMPO
	again.sequence_file = SEQUENCE
	root.add_child(again)
	await process_frame
	again.wait_until_loaded()
	var tl2 = again.get_timeline()
	_check(tl2.get_note_count("Kick") == count and tl2.get_note_count("Crash") == 1 and is_equal_approx(tl2.get_nudge(), 0.010),
			"a new player loads the saved timeline")
	_check(not ("drafted" in again.get_status()), "and doesn't draft over it")
	again.free()


func _click(panel: Control, pos: Vector2, pressed: bool) -> void:
	var e := InputEventMouseButton.new()
	e.button_index = MOUSE_BUTTON_LEFT
	e.pressed = pressed
	e.position = pos
	panel._on_canvas_input(e)


func _key(panel: Control, key: Key, ctrl := false, shift := false) -> void:
	var e := InputEventKey.new()
	e.keycode = key
	e.pressed = true
	e.ctrl_pressed = ctrl
	e.shift_pressed = shift
	panel._on_canvas_input(e)


func _write_lane_graph(path: String) -> void:
	var doc := {
		"format": "ztrip.mgraph", "version": 1, "name": "lane test",
		"nodes": [
			{"id": "kick", "type": "NoteLane", "params": {"lane": "Kick"}},
			{"id": "flash", "type": "ImpulseDecay", "params": {"decay_ms": 150, "velocity": false}},
			{"id": "gain", "type": "Multiply", "params": {"b": 4}},
			{"id": "light", "type": "PropertyOut", "params": {"target": "Key:light_energy"}},
		],
		"edges": [
			{"from": "kick.trigger", "to": "flash.trigger"},
			{"from": "flash.out", "to": "gain.a"},
			{"from": "gain.out", "to": "light.in"},
		],
		"macros": [],
	}
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string(JSON.stringify(doc, "  "))
	f.close()


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
