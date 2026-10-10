@tool
extends VBoxContainer
## Motion Graph editor: a GraphEdit over one .mgraph.json document.
##
## The document is kept as the same Dictionary shape as the JSON file
## (name, nodes, edges, macros); GraphNodes are a view of it. Saving writes it
## through ZtripGraphTools.normalize_graph, so files keep the core's stable key
## order. A ZtripPlayer playing the file reloads it on save, and its live port
## values show on each output.

const PORT_TYPES := {"Continuous": 0, "Trigger": 1, "Clock": 2}
const PORT_COLORS := [Color(0.35, 0.75, 1.0), Color(1.0, 0.6, 0.2), Color(0.7, 0.5, 1.0)]
const BANDS := ["Sub", "Bass", "LowMid", "Mid", "HighMid", "Air"]

var _specs := {}            # type -> spec Dictionary from ZtripGraphTools
var _doc := {}              # the document being edited
var _path := ""             # file on disk, "" for unsaved
var _dirty := false
var _node_by_id := {}       # node id -> GraphNode
var _value_labels := {}     # "id.port" -> Label showing the live value
var _player_ref: WeakRef
var _popup_position := Vector2.ZERO

var _graph: GraphEdit
var _file_label: Label
var _status_label: Label
var _add_menu: MenuButton
var _node_popup: PopupMenu
var _macro_box: VBoxContainer
var _open_dialog: ConfirmationDialog # EditorFileDialog, or FileDialog outside the editor
var _save_dialog: ConfirmationDialog
var _check_timer: Timer
var _live_timer: Timer


func _init() -> void:
	custom_minimum_size = Vector2(0, 260)
	for spec in ZtripGraphTools.get_node_types():
		_specs[spec.type] = spec
	_build_ui()
	_new_document()


# ---- UI -----------------------------------------------------------------------

func _build_ui() -> void:
	var bar := HBoxContainer.new()
	add_child(bar)
	for item in [["New", _on_new], ["Open...", _on_open], ["Save", _on_save], ["Save As...", _on_save_as]]:
		var b := Button.new()
		b.text = item[0]
		b.flat = true
		b.pressed.connect(item[1])
		bar.add_child(b)
	_add_menu = MenuButton.new()
	_add_menu.text = "Add Node"
	_add_menu.flat = false
	bar.add_child(_add_menu)
	_fill_add_menu(_add_menu.get_popup(), false)
	_file_label = Label.new()
	_file_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_file_label.clip_text = true
	bar.add_child(_file_label)
	_status_label = Label.new()
	_status_label.mouse_filter = Control.MOUSE_FILTER_PASS
	bar.add_child(_status_label)

	var split := HSplitContainer.new()
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	add_child(split)

	_graph = GraphEdit.new()
	_graph.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_graph.right_disconnects = true
	_graph.show_arrange_button = false
	_graph.add_valid_connection_type(PORT_TYPES.Trigger, PORT_TYPES.Continuous)
	_graph.add_valid_connection_type(PORT_TYPES.Clock, PORT_TYPES.Continuous)
	_graph.add_valid_connection_type(PORT_TYPES.Continuous, PORT_TYPES.Clock)
	_graph.connection_request.connect(_on_connection_request)
	_graph.disconnection_request.connect(_on_disconnection_request)
	_graph.delete_nodes_request.connect(_on_delete_nodes_request)
	_graph.popup_request.connect(_on_popup_request)
	_graph.end_node_move.connect(_mark_dirty)
	split.add_child(_graph)

	var side := VBoxContainer.new()
	side.custom_minimum_size = Vector2(260, 0)
	split.add_child(side)
	var macro_head := HBoxContainer.new()
	side.add_child(macro_head)
	var macro_title := Label.new()
	macro_title.text = "Macros"
	macro_title.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	macro_head.add_child(macro_title)
	var add_macro := MenuButton.new()
	add_macro.text = "+ Macro"
	add_macro.flat = false
	add_macro.about_to_popup.connect(_fill_macro_menu.bind(add_macro))
	add_macro.get_popup().index_pressed.connect(_on_add_macro.bind(add_macro))
	macro_head.add_child(add_macro)
	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	side.add_child(scroll)
	_macro_box = VBoxContainer.new()
	_macro_box.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	scroll.add_child(_macro_box)

	_node_popup = PopupMenu.new()
	_fill_add_menu(_node_popup, true)
	add_child(_node_popup)

	_open_dialog = _make_dialog(FileDialog.FILE_MODE_OPEN_FILE, _load_file)
	_save_dialog = _make_dialog(FileDialog.FILE_MODE_SAVE_FILE, _save_to)

	_check_timer = Timer.new()
	_check_timer.one_shot = true
	_check_timer.wait_time = 0.3
	_check_timer.timeout.connect(_check)
	add_child(_check_timer)
	_live_timer = Timer.new()
	_live_timer.wait_time = 0.1
	_live_timer.autostart = true
	_live_timer.timeout.connect(_update_live_values)
	add_child(_live_timer)


func _make_dialog(mode: int, callback: Callable) -> ConfirmationDialog:
	# EditorFileDialog only exists inside the editor; plain FileDialog elsewhere (tests).
	var d = EditorFileDialog.new() if Engine.is_editor_hint() else FileDialog.new()
	d.file_mode = mode
	d.access = FileDialog.ACCESS_RESOURCES
	d.add_filter("*.mgraph.json", "Motion Graph")
	d.file_selected.connect(callback)
	add_child(d)
	return d


func _fill_add_menu(menu: PopupMenu, at_mouse: bool) -> void:
	menu.clear()
	var by_category := {}
	for type in _specs:
		var cat: String = _specs[type].category
		if not by_category.has(cat):
			by_category[cat] = []
		by_category[cat].append(type)
	for cat in by_category:
		var sub := PopupMenu.new()
		sub.name = "cat_" + cat
		for type in by_category[cat]:
			sub.add_item(type)
			sub.set_item_tooltip(sub.item_count - 1, _specs[type].summary)
		sub.index_pressed.connect(func(i: int): _add_node(by_category[cat][i], at_mouse))
		menu.add_child(sub)
		menu.add_submenu_node_item(cat, sub)


# ---- Documents ------------------------------------------------------------------

func _new_document() -> void:
	_doc = {"format": "ztrip.mgraph", "version": 1, "name": "Untitled", "nodes": [], "edges": [], "macros": []}
	_path = ""
	_dirty = false
	_rebuild()


func _load_file(path: String) -> void:
	var text := FileAccess.get_file_as_string(path)
	var parsed = JSON.parse_string(text)
	if typeof(parsed) != TYPE_DICTIONARY:
		_set_status("Could not parse " + path.get_file(), true)
		return
	_doc = parsed
	for key in ["nodes", "edges", "macros"]:
		if not _doc.has(key):
			_doc[key] = []
	_path = path
	_dirty = false
	_rebuild()


func _save_to(path: String) -> void:
	_sync_positions()
	var result: Dictionary = ZtripGraphTools.normalize_graph(_doc_json())
	if not result.ok:
		_set_status("Not saved: " + result.error, true)
		return
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		_set_status("Could not write " + path, true)
		return
	f.store_string(result.text)
	f.close()
	_path = path
	_dirty = false
	_update_title()
	if Engine.is_editor_hint():
		EditorInterface.get_resource_filesystem().update_file(path)
	var player := _find_player()
	if player:
		player.reload_graph() # don't wait for it to notice the file changed
	_check()


## Opens the graph a ZtripPlayer plays and shows its live values.
func attach_player(player: Node) -> void:
	_player_ref = weakref(player)
	var file: String = player.graph_file
	if file != "" and file != _path and not _dirty:
		_load_file(file)


func open_file(path: String) -> void:
	_load_file(path)


func _on_new() -> void:
	_new_document()


func _on_open() -> void:
	_popup_dialog(_open_dialog)


func _on_save() -> void:
	if _path == "":
		_on_save_as()
	else:
		_save_to(_path)


func _on_save_as() -> void:
	_save_dialog.current_file = (_doc.get("name", "graph") as String).to_snake_case() + ".mgraph.json"
	_popup_dialog(_save_dialog)


func _popup_dialog(d: ConfirmationDialog) -> void:
	if d is EditorFileDialog:
		d.popup_file_dialog()
	else:
		d.popup_centered_ratio(0.6)


func _mark_dirty() -> void:
	_dirty = true
	_update_title()
	_check_timer.start()


func _update_title() -> void:
	_file_label.text = (_path.get_file() if _path != "" else "(unsaved)") + (" *" if _dirty else "")
	_file_label.tooltip_text = _path


# ---- Building the view -------------------------------------------------------

func _rebuild() -> void:
	_graph.clear_connections()
	for child in _graph.get_children():
		if child is GraphNode:
			_graph.remove_child(child)
			child.queue_free()
	_node_by_id.clear()
	_value_labels.clear()
	for node in _doc.nodes:
		_make_graph_node(node)
	for edge in _doc.edges:
		var from := _split_port(edge.get("from", ""))
		var to := _split_port(edge.get("to", ""))
		var fport := _port_index(from[0], from[1], "outputs")
		var tport := _port_index(to[0], to[1], "inputs")
		if fport >= 0 and tport >= 0:
			_graph.connect_node(_node_by_id[from[0]].name, fport, _node_by_id[to[0]].name, tport)
	_rebuild_macros()
	_update_title()
	_check()


func _make_graph_node(node: Dictionary) -> GraphNode:
	var spec: Dictionary = _specs.get(node.get("type", ""), {})
	var gn := GraphNode.new()
	gn.name = "n_%d" % _graph.get_child_count()
	while _graph.has_node(NodePath(gn.name)):
		gn.name = gn.name + "_"
	gn.title = "%s  (%s)" % [node.id, node.get("type", "?")]
	gn.set_meta("id", node.id)
	var pos = node.get("pos", [0, 0])
	gn.position_offset = Vector2(pos[0], pos[1]) if pos is Array and pos.size() >= 2 else Vector2.ZERO
	gn.tooltip_text = spec.get("summary", "Unknown node type")
	if not node.has("params"):
		node["params"] = {}
	_graph.add_child(gn)
	_node_by_id[node.id] = gn
	if spec.is_empty():
		var l := Label.new()
		l.text = "Unknown type " + str(node.get("type", ""))
		gn.add_child(l)
		return gn

	var inputs: Array = spec.inputs
	var outputs: Array = spec.outputs
	var input_names := []
	for i in max(inputs.size(), outputs.size()):
		var row := HBoxContainer.new()
		row.custom_minimum_size.y = 26
		gn.add_child(row)
		if i < inputs.size():
			var label := Label.new()
			label.text = inputs[i].name
			row.add_child(label)
			input_names.append(inputs[i].name)
			var param := _param_spec(spec, inputs[i].name)
			if inputs[i].type == "Continuous" and not param.is_empty():
				row.add_child(_param_control(node, param))
		var spacer := Control.new()
		spacer.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(spacer)
		if i < outputs.size():
			var value := Label.new()
			value.modulate = Color(1, 1, 1, 0.6)
			row.add_child(value)
			_value_labels["%s.%s" % [node.id, outputs[i].name]] = value
			var out_label := Label.new()
			out_label.text = outputs[i].name
			row.add_child(out_label)
		var in_type: int = PORT_TYPES.get(inputs[i].type, 0) if i < inputs.size() else 0
		var out_type: int = PORT_TYPES.get(outputs[i].type, 0) if i < outputs.size() else 0
		gn.set_slot(i, i < inputs.size(), in_type, PORT_COLORS[in_type],
				i < outputs.size(), out_type, PORT_COLORS[out_type])

	for param in spec.params:
		if param.name in input_names:
			continue
		var row := HBoxContainer.new()
		gn.add_child(row)
		var label := Label.new()
		label.text = param.name
		label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		row.add_child(label)
		row.add_child(_param_control(node, param))
	return gn


func _param_spec(spec: Dictionary, name: String) -> Dictionary:
	for p in spec.params:
		if p.name == name:
			return p
	return {}


func _param_control(node: Dictionary, param: Dictionary) -> Control:
	var params: Dictionary = node.params
	var value = params.get(param.name, param.default)
	var set_param := func(v):
		params[param.name] = v
		_mark_dirty()
	match param.kind:
		"Number":
			var spin := SpinBox.new()
			spin.min_value = -100000
			spin.max_value = 100000
			spin.step = 0.001
			spin.allow_greater = true
			spin.allow_lesser = true
			spin.custom_minimum_size.x = 84
			spin.value = float(value)
			spin.value_changed.connect(set_param)
			return spin
		"Bool":
			var check := CheckBox.new()
			check.button_pressed = bool(value)
			check.toggled.connect(set_param)
			return check
		"String":
			var choices: Array = Array(param.choices)
			if choices.is_empty() and param.name == "band":
				choices = BANDS.duplicate()
				if node.type == "AudioOnset":
					choices.append("full")
			if not choices.is_empty():
				var opt := OptionButton.new()
				for c in choices:
					opt.add_item(c)
				if not (str(value) in choices):
					opt.add_item(str(value))
				opt.select(maxi(0, choices.find(str(value))) if str(value) in choices else opt.item_count - 1)
				opt.item_selected.connect(func(i: int): set_param.call(opt.get_item_text(i)))
				return opt
			var edit := LineEdit.new()
			edit.text = str(value)
			edit.custom_minimum_size.x = 220 if param.name == "target" else 100
			edit.placeholder_text = "Node/Path:property" if param.name == "target" else ""
			edit.text_changed.connect(set_param)
			return edit
		_:
			var list := LineEdit.new()
			list.text = ", ".join(PackedStringArray(Array(value).map(func(x): return str(x))))
			list.custom_minimum_size.x = 140
			list.text_changed.connect(func(t: String):
				var out := []
				for part in t.split(",", false):
					out.append(float(part.strip_edges()))
				set_param.call(out))
			return list


func _rebuild_macros() -> void:
	for c in _macro_box.get_children():
		c.queue_free()
	for m in _doc.macros:
		var box := VBoxContainer.new()
		_macro_box.add_child(box)
		var head := HBoxContainer.new()
		box.add_child(head)
		var name_edit := LineEdit.new()
		name_edit.text = m.get("name", "")
		name_edit.size_flags_horizontal = Control.SIZE_EXPAND_FILL
		name_edit.text_changed.connect(func(t: String):
			m["name"] = t
			_mark_dirty())
		head.add_child(name_edit)
		var remove := Button.new()
		remove.text = "x"
		remove.tooltip_text = "Remove macro"
		remove.pressed.connect(func():
			_doc.macros.erase(m)
			_rebuild_macros()
			_mark_dirty())
		head.add_child(remove)
		var info := Label.new()
		info.text = "%s.%s" % [m.get("node", ""), m.get("param", "")]
		info.modulate = Color(1, 1, 1, 0.6)
		box.add_child(info)
		var range_row := HBoxContainer.new()
		box.add_child(range_row)
		for key in ["min", "max", "default"]:
			var l := Label.new()
			l.text = key
			range_row.add_child(l)
			var spin := SpinBox.new()
			spin.min_value = -100000
			spin.max_value = 100000
			spin.step = 0.001
			spin.allow_greater = true
			spin.allow_lesser = true
			spin.custom_minimum_size.x = 64
			spin.value = float(m.get(key, 0.0))
			spin.value_changed.connect(func(v: float):
				m[key] = v
				_mark_dirty())
			range_row.add_child(spin)
		box.add_child(HSeparator.new())


func _fill_macro_menu(button: MenuButton) -> void:
	var menu := button.get_popup()
	menu.clear()
	var entries := []
	for node in _doc.nodes:
		var spec: Dictionary = _specs.get(node.type, {})
		for p in spec.get("params", []):
			if p.kind == "Number":
				menu.add_item("%s.%s" % [node.id, p.name])
				entries.append([node, p])
	button.set_meta("entries", entries)


func _on_add_macro(index: int, button: MenuButton) -> void:
	var entry: Array = button.get_meta("entries")[index]
	var node: Dictionary = entry[0]
	var p: Dictionary = entry[1]
	var current := float(node.params.get(p.name, p.default))
	var hi := maxf(1.0, current * 2.0)
	_doc.macros.append({"name": p.name.capitalize().replace(" ", ""), "node": node.id, "param": p.name,
			"min": minf(0.0, current), "max": hi, "default": current})
	_rebuild_macros()
	_mark_dirty()


# ---- Editing --------------------------------------------------------------------

func _add_node(type: String, at_mouse: bool) -> void:
	var base := type.to_snake_case()
	var id := base
	var n := 2
	while _find_node(id) != null:
		id = "%s_%d" % [base, n]
		n += 1
	var pos := (_popup_position if at_mouse else _graph.size * 0.4)
	pos = (pos + _graph.scroll_offset) / _graph.zoom
	var node := {"id": id, "type": type, "params": {}, "pos": [roundf(pos.x), roundf(pos.y)]}
	_doc.nodes.append(node)
	_make_graph_node(node)
	_mark_dirty()


func _find_node(id: String):
	for node in _doc.nodes:
		if node.id == id:
			return node
	return null


func _id_of(graph_node_name: StringName) -> String:
	var gn := _graph.get_node_or_null(NodePath(graph_node_name))
	return gn.get_meta("id") if gn else ""


func _port_name(id: String, index: int, side: String) -> String:
	var node = _find_node(id)
	if node == null:
		return ""
	var ports: Array = _specs.get(node.type, {}).get(side, [])
	return ports[index].name if index < ports.size() else ""


func _port_index(id: String, port: String, side: String) -> int:
	var node = _find_node(id)
	if node == null or not _node_by_id.has(id):
		return -1
	var ports: Array = _specs.get(node.type, {}).get(side, [])
	for i in ports.size():
		if ports[i].name == port:
			return i
	return -1


func _split_port(s: String) -> Array:
	var dot := s.rfind(".")
	return [s.substr(0, dot), s.substr(dot + 1)] if dot > 0 else [s, ""]


func _on_connection_request(from: StringName, from_port: int, to: StringName, to_port: int) -> void:
	var to_id := _id_of(to)
	var to_key := "%s.%s" % [to_id, _port_name(to_id, to_port, "inputs")]
	# An input takes one edge: replace whatever fed it.
	for edge in _doc.edges.duplicate():
		if edge.to == to_key:
			_doc.edges.erase(edge)
	for c in _graph.get_connection_list():
		if c.to_node == to and c.to_port == to_port:
			_graph.disconnect_node(c.from_node, c.from_port, c.to_node, c.to_port)
	var from_id := _id_of(from)
	_doc.edges.append({"from": "%s.%s" % [from_id, _port_name(from_id, from_port, "outputs")], "to": to_key})
	_graph.connect_node(from, from_port, to, to_port)
	_mark_dirty()


func _on_disconnection_request(from: StringName, from_port: int, to: StringName, to_port: int) -> void:
	var from_id := _id_of(from)
	var to_id := _id_of(to)
	var from_key := "%s.%s" % [from_id, _port_name(from_id, from_port, "outputs")]
	var to_key := "%s.%s" % [to_id, _port_name(to_id, to_port, "inputs")]
	for edge in _doc.edges.duplicate():
		if edge.from == from_key and edge.to == to_key:
			_doc.edges.erase(edge)
	_graph.disconnect_node(from, from_port, to, to_port)
	_mark_dirty()


func _on_delete_nodes_request(names: Array) -> void:
	for name in names:
		var id := _id_of(name)
		var node = _find_node(id)
		if node == null:
			continue
		_doc.nodes.erase(node)
		for edge in _doc.edges.duplicate():
			if edge.from.begins_with(id + ".") or edge.to.begins_with(id + "."):
				_doc.edges.erase(edge)
		for m in _doc.macros.duplicate():
			if m.get("node", "") == id:
				_doc.macros.erase(m)
	_sync_positions()
	_rebuild()
	_mark_dirty()


func _on_popup_request(at: Vector2) -> void:
	_popup_position = at
	_node_popup.position = Vector2i(_graph.get_screen_position() + at)
	_node_popup.reset_size()
	_node_popup.popup()


func _sync_positions() -> void:
	for node in _doc.nodes:
		var gn: GraphNode = _node_by_id.get(node.id)
		if gn:
			node["pos"] = [roundf(gn.position_offset.x), roundf(gn.position_offset.y)]


# ---- Checks and live values ------------------------------------------------------------

func _doc_json() -> String:
	# Godot's JSON parser reads every number as a float; the format version must be an integer.
	_doc["version"] = int(_doc.get("version", 1))
	return JSON.stringify(_doc, "  ")


func _check() -> void:
	_sync_positions()
	var r: Dictionary = ZtripGraphTools.check_graph(_doc_json())
	var lines := PackedStringArray()
	lines.append_array(r.errors)
	lines.append_array(r.warnings)
	if not r.errors.is_empty():
		_set_status("%d error(s)" % r.errors.size(), true, "\n".join(lines))
	elif not r.warnings.is_empty():
		_set_status("OK, %d warning(s)" % r.warnings.size(), false, "\n".join(lines))
	else:
		_set_status("OK: %d nodes, %d targets" % [_doc.nodes.size(), r.targets.size()], false)


func _set_status(text: String, error: bool, tooltip := "") -> void:
	_status_label.text = text
	_status_label.tooltip_text = tooltip if tooltip != "" else text
	_status_label.modulate = Color(1, 0.45, 0.4) if error else Color(0.6, 1, 0.6)


func _find_player() -> Node:
	if _player_ref:
		var p = _player_ref.get_ref()
		if p and p.is_inside_tree() and p.graph_file == _path:
			return p
	if not Engine.is_editor_hint() or _path == "":
		return null
	var root := EditorInterface.get_edited_scene_root()
	if root == null:
		return null
	for p in root.find_children("*", "ZtripPlayer", true, false):
		if p.graph_file == _path:
			_player_ref = weakref(p)
			return p
	return null


func _update_live_values() -> void:
	if not is_visible_in_tree():
		return
	var player := _find_player()
	for key in _value_labels:
		var label: Label = _value_labels[key]
		if player == null or _dirty:
			label.text = ""
			continue
		var parts := _split_port(key)
		var v = player.get_port_value(parts[0], parts[1])
		label.text = "" if v == null else "%.2f" % v
