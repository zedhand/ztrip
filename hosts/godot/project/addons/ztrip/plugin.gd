@tool
extends EditorPlugin
## Adds the "Motion Graph" bottom panel, which edits .mgraph.json files and
## shows live values from the ZtripPlayer that plays them, and the "Timeline"
## bottom panel, which edits that player's tempo map and note lanes. It also
## rewrites the headers of WAVs Godot can't import (see editor/wav_header.gd).

const WavHeader := preload("res://addons/ztrip/editor/wav_header.gd")

var _panel: Control
var _timeline: Control
var _checked_wavs := {} # path -> modified time when last checked


func _enter_tree() -> void:
	var fs := EditorInterface.get_resource_filesystem()
	fs.filesystem_changed.connect(_fix_wavs, CONNECT_DEFERRED)
	_fix_wavs.call_deferred()
	if not ClassDB.class_exists("ZtripGraphTools"):
		push_warning("ztrip: the GDExtension library isn't built yet, so the Motion Graph panel is off. Build it with `cmake --preset godot && cmake --build --preset godot` (see hosts/godot/README.md in the ztrip repository).")
		return
	# Loaded (not preloaded) so this script still parses without the extension.
	_panel = load("res://addons/ztrip/editor/graph_panel.gd").new()
	_panel.name = "MotionGraph"
	add_control_to_bottom_panel(_panel, "Motion Graph")
	_timeline = load("res://addons/ztrip/editor/timeline_panel.gd").new()
	_timeline.name = "Timeline"
	add_control_to_bottom_panel(_timeline, "Timeline")


func _exit_tree() -> void:
	var fs := EditorInterface.get_resource_filesystem()
	if fs.filesystem_changed.is_connected(_fix_wavs):
		fs.filesystem_changed.disconnect(_fix_wavs)
	if _timeline:
		remove_control_from_bottom_panel(_timeline)
		_timeline.queue_free()
		_timeline = null
	if _panel:
		remove_control_from_bottom_panel(_panel)
		_panel.queue_free()
		_panel = null


func _handles(object: Object) -> bool:
	return _panel != null and object != null and object.is_class("ZtripPlayer")


func _edit(object: Object) -> void:
	if _panel and object and object.is_class("ZtripPlayer"):
		_panel.attach_player(object)
		_timeline.attach_player(object)


func _make_visible(visible: bool) -> void:
	if _panel and visible:
		make_bottom_panel_item_visible(_panel)


func _fix_wavs() -> void:
	var fs := EditorInterface.get_resource_filesystem()
	if fs.is_scanning():
		return # filesystem_changed fires again when the scan ends
	var fixed := PackedStringArray()
	_find_wavs(fs.get_filesystem(), fixed)
	if not fixed.is_empty():
		fs.reimport_files(fixed)


func _find_wavs(dir: EditorFileSystemDirectory, fixed: PackedStringArray) -> void:
	if dir == null:
		return
	for i in dir.get_file_count():
		var path := dir.get_file_path(i)
		if path.get_extension().to_lower() != "wav":
			continue
		var modified := FileAccess.get_modified_time(path)
		if _checked_wavs.get(path, -1) == modified:
			continue
		if WavHeader.needs_fix(path):
			if WavHeader.fix(path) == OK:
				print("ztrip: rewrote the header of %s so Godot can import it (it was WAVE_FORMAT_EXTENSIBLE; the audio is unchanged)." % path)
				fixed.append(path)
			else:
				push_warning("ztrip: %s is a WAV Godot can't import, and rewriting its header failed. Convert it with: ffmpeg -i in.wav -c:a pcm_s16le out.wav" % path)
		_checked_wavs[path] = FileAccess.get_modified_time(path)
	for i in dir.get_subdir_count():
		_find_wavs(dir.get_subdir(i), fixed)
