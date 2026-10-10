extends Node3D
## Demo stage: the red light flashes on kicks, the blue spot on snares, the
## white light on hi-hats, the orb glows with the bass and pulses on each beat,
## and the camera pushes in on every bar. A HUD shows where the song is.
##
## Command line (after --), used to render videos:
##   --song=<path.wav>   play this WAV instead of the Music node's stream
##   --start=<seconds>   start there
##   --length=<seconds>  quit after this much of the song
##   --caption=<text>    a line of text under the HUD

@onready var _player: Node = $ZtripPlayer
@onready var _music: AudioStreamPlayer = $Music

var _quit_at := -1.0
var _info: Label
var _caption: Label
var _beats: Array[ColorRect] = []
var _meters := {}


func _ready() -> void:
	var args := _parse_args()
	_build_hud(args.get("caption", ""))
	var start := float(args.get("start", "0"))
	if args.has("length"):
		_quit_at = start + float(args.length)
	if args.has("song"):
		var stream := AudioStreamWAV.load_from_file(args.song)
		if stream == null:
			push_error("Could not load " + args.song)
			get_tree().quit(1)
			return
		_player.stop()
		_music.stream = stream
		_player.threaded_loading = false # analyse now, so frame 0 is already in sync
		_player.audio_file = args.song
	elif _music.stream == null and FileAccess.file_exists("res://music/song.wav"):
		_player.stop()
		_music.stream = load("res://music/song.wav")
		_player.reload()
	_player.wait_until_loaded()
	if not _player.get_status().is_empty():
		print(_player.get_status())
	_player.play(start)


func _parse_args() -> Dictionary:
	var out := {}
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--") and "=" in a:
			var kv := a.substr(2).split("=", true, 1)
			out[kv[0]] = kv[1]
	return out


func _build_hud(caption: String) -> void:
	var panel := VBoxContainer.new()
	panel.position = Vector2(24, 20)
	$HUD.add_child(panel)
	_info = Label.new()
	_info.add_theme_font_size_override("font_size", 22)
	panel.add_child(_info)
	var row := HBoxContainer.new()
	row.add_theme_constant_override("separation", 8)
	panel.add_child(row)
	for i in 4:
		var r := ColorRect.new()
		r.custom_minimum_size = Vector2(34, 34)
		row.add_child(r)
		_beats.append(r)
	for m in [["Kick", "kick_flash", Color(1, 0.3, 0.2)], ["Snare", "snare_flash", Color(0.35, 0.55, 1)],
			["Hats", "hats_flash", Color(1, 1, 0.85)], ["Bass", "bass_curve", Color(0.2, 0.6, 1)]]:
		var line := HBoxContainer.new()
		panel.add_child(line)
		var l := Label.new()
		l.text = m[0]
		l.custom_minimum_size.x = 64
		l.add_theme_font_size_override("font_size", 18)
		line.add_child(l)
		var bar := ProgressBar.new()
		bar.custom_minimum_size = Vector2(220, 18)
		bar.max_value = 1.0
		bar.show_percentage = false
		bar.modulate = m[2]
		line.add_child(bar)
		_meters[m[1]] = bar
	if caption != "":
		_caption = Label.new()
		_caption.text = caption
		_caption.add_theme_font_size_override("font_size", 20)
		_caption.position = Vector2(24, 660)
		$HUD.add_child(_caption)


func _process(_delta: float) -> void:
	var t: float = _player.time
	var clock = _player.get_port_value("clock", "clock")
	var beats_per_bar: int = 4
	var analysis = _player.get_analysis()
	if analysis:
		beats_per_bar = analysis.get_beats_per_bar()
	var beat_in_bar := 0
	var bar := 0
	if clock != null:
		var b := floorf(float(clock))
		bar = int(floor(b / beats_per_bar)) + 1
		beat_in_bar = int(posmod(b, beats_per_bar))
	_info.text = "%s   bar %d  beat %d   %.0f BPM" % [_clock_text(t), bar, beat_in_bar + 1, _player.get_bpm()]
	var phase = _player.get_port_value("clock", "beat_phase")
	var flash: float = 1.0 - (float(phase) if phase != null else 1.0)
	for i in _beats.size():
		var on := i == beat_in_bar and clock != null
		_beats[i].color = Color(1, 0.8, 0.3).lerp(Color(0.2, 0.2, 0.25), 1.0 - flash) if on else Color(0.2, 0.2, 0.25)
	for node in _meters:
		var v = _player.get_port_value(node, "out")
		_meters[node].value = clampf(v if v != null else 0.0, 0.0, 1.0)
	if _quit_at > 0.0 and t >= _quit_at:
		get_tree().quit()


func _clock_text(t: float) -> String:
	return "%d:%05.2f" % [int(t) / 60, fmod(t, 60.0)]
