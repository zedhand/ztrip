@tool
extends EditorPlugin
## Adds the "Motion Graph" bottom panel, which edits .mgraph.json files and
## shows live values from the ZtripPlayer that plays them, and the "Timeline"
## bottom panel, which edits that player's tempo map and note lanes.

var _panel: Control
var _timeline: Control


func _enter_tree() -> void:
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
