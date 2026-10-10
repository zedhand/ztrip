@tool
extends VBoxContainer
## Timeline dock: the tempo map and note lanes of the song a ZtripPlayer plays.
##
## Shows a bars/beats ruler, the tempo and meter, the song's loudness and one
## row per Note Sequence lane, with the detected hits drawn as ghost markers
## under the notes. Time runs left to right in seconds, so the audio stays
## put and the grid and notes move when the tempo map is nudged.
##
## Editing (notes snap to the grid unless Alt is held):
##   click an empty spot in a lane    add a note
##   drag a note / its right edge     move / resize (all selected notes)
##   right-click a note               delete it
##   Shift+click                      add to the selection
##   Left/Right                       move the selection one grid step (Shift: a quarter step)
##   Up/Down                          velocity of the selection
##   Delete                           delete the selection
##   Q                                quantize the selection to the grid
##   Ctrl+Z / Ctrl+Shift+Z            undo / redo
##   Space                            play / stop
##   click the ruler                  seek
##   wheel / Ctrl+wheel               scroll / zoom; Shift+wheel on a note sets velocity
## The player hears every edit at once; Save writes .tempo.json and .nseq.json.

const RULER_H := 22.0
const TEMPO_H := 34.0
const WAVE_H := 40.0
const LANE_H := 32.0
const HEADER_W := 120.0
const EDGE_PX := 6.0
const GRIDS := ["1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/4T", "1/8T", "1/16T", "1/32T"]
const LANE_COLORS := [Color(1.0, 0.35, 0.3), Color(0.35, 0.6, 1.0), Color(0.95, 0.95, 0.75),
		Color(0.55, 0.9, 0.45), Color(0.85, 0.55, 1.0), Color(1.0, 0.7, 0.3)]
const MAX_UNDO := 100

var _player_ref: WeakRef
var _timeline = null        # ZtripTimeline of the attached player
var _analysis = null        # ZtripAnalysis of the attached player
var _lanes: Array = []      # lane names, in order
var _notes := {}            # lane -> Array of note Dictionaries (plus "sel", "s0", "s1")
var _ghosts := {}           # lane -> PackedFloat64Array of detected hit times
var _grid := "1/16"
var _snap := true
var _follow := true
var _dirty := false
var _committing := false
var _undo: Array = []
var _redo: Array = []

var _view_start := 0.0      # seconds at the left edge of the lanes
var _px_per_s := 100.0
var _drag := {}             # the drag in progress, or empty
var _seeking := false

var _canvas: Control
var _hscroll: HScrollBar
var _play_button: Button
var _time_label: Label
var _grid_option: OptionButton
var _nudge_label: Label
var _tempo_label: Label
var _file_label: Label
var _status_label: Label
var _lane_menu: PopupMenu
var _lane_menu_lane := ""
var _lane_dialog: ConfirmationDialog
var _lane_name: LineEdit
var _find_timer: Timer


func _init() -> void:
	custom_minimum_size = Vector2(0, 240)
	_build_ui()


# ---- UI -----------------------------------------------------------------------

func _build_ui() -> void:
	var bar := HBoxContainer.new()
	add_child(bar)
	_play_button = _button(bar, "Play", toggle_playback, "Play or stop the song (Space)")
	_time_label = Label.new()
	_time_label.custom_minimum_size.x = 150
	bar.add_child(_time_label)
	bar.add_child(VSeparator.new())

	_label(bar, "Grid")
	_grid_option = OptionButton.new()
	for g in GRIDS:
		_grid_option.add_item(g)
	_grid_option.select(GRIDS.find(_grid))
	_grid_option.item_selected.connect(func(i: int): _grid = GRIDS[i]; _canvas.queue_redraw())
	_grid_option.tooltip_text = "Snap grid for drawing, moving and quantizing notes"
	bar.add_child(_grid_option)
	var snap := CheckBox.new()
	snap.text = "Snap"
	snap.button_pressed = _snap
	snap.toggled.connect(func(on: bool): _snap = on)
	bar.add_child(snap)
	bar.add_child(VSeparator.new())

	_label(bar, "Nudge")
	_button(bar, "-10", nudge.bind(-0.010), "Move the beat grid and notes 10 ms earlier")
	_button(bar, "-1", nudge.bind(-0.001), "Move the beat grid and notes 1 ms earlier")
	_nudge_label = Label.new()
	_nudge_label.custom_minimum_size.x = 64
	_nudge_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	bar.add_child(_nudge_label)
	_button(bar, "+1", nudge.bind(0.001), "Move the beat grid and notes 1 ms later")
	_button(bar, "+10", nudge.bind(0.010), "Move the beat grid and notes 10 ms later")
	bar.add_child(VSeparator.new())

	_label(bar, "Tempo")
	_button(bar, "-", scale_tempo.bind(1.0 / 1.001), "Slow the whole tempo map by 0.1 %")
	_tempo_label = Label.new()
	_tempo_label.custom_minimum_size.x = 84
	_tempo_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	bar.add_child(_tempo_label)
	_button(bar, "+", scale_tempo.bind(1.001), "Speed up the whole tempo map by 0.1 %")
	bar.add_child(VSeparator.new())

	_button(bar, "Re-draft", redraft, "Draft the tempo map and the Kick, Snare and Hats lanes from the song again (keeps the nudge; undo with Ctrl+Z)")
	_button(bar, "+ Lane", _on_add_lane, "Add an empty note lane")
	_button(bar, "Save", save, "Write the tempo map and notes to the player's tempo_file and sequence_file")
	var follow := CheckBox.new()
	follow.text = "Follow"
	follow.button_pressed = _follow
	follow.toggled.connect(func(on: bool): _follow = on)
	bar.add_child(follow)
	_file_label = Label.new()
	_file_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_file_label.clip_text = true
	_file_label.horizontal_alignment = HORIZONTAL_ALIGNMENT_RIGHT
	bar.add_child(_file_label)
	_status_label = Label.new()
	bar.add_child(_status_label)

	var scroll := ScrollContainer.new()
	scroll.size_flags_vertical = Control.SIZE_EXPAND_FILL
	scroll.horizontal_scroll_mode = ScrollContainer.SCROLL_MODE_DISABLED
	add_child(scroll)
	_canvas = Control.new()
	_canvas.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	_canvas.focus_mode = Control.FOCUS_ALL
	_canvas.clip_contents = true
	_canvas.draw.connect(_draw_canvas)
	_canvas.gui_input.connect(_on_canvas_input)
	_canvas.resized.connect(_update_scrollbar)
	scroll.add_child(_canvas)

	_hscroll = HScrollBar.new()
	_hscroll.step = 0.0
	_hscroll.value_changed.connect(func(v: float): _view_start = v; _canvas.queue_redraw())
	add_child(_hscroll)

	_lane_menu = PopupMenu.new()
	_lane_menu.add_item("Clear notes", 0)
	_lane_menu.add_item("Remove lane", 1)
	_lane_menu.id_pressed.connect(_on_lane_menu)
	add_child(_lane_menu)
	_lane_dialog = ConfirmationDialog.new()
	_lane_dialog.title = "Add lane"
	_lane_name = LineEdit.new()
	_lane_name.placeholder_text = "Lane name, e.g. Crash"
	_lane_dialog.add_child(_lane_name)
	_lane_dialog.confirmed.connect(func(): add_lane(_lane_name.text.strip_edges()))
	add_child(_lane_dialog)

	_find_timer = Timer.new()
	_find_timer.wait_time = 0.5
	_find_timer.autostart = true
	_find_timer.timeout.connect(_find_player_in_scene)
	add_child(_find_timer)
	_update_labels()


func _button(parent: Control, text: String, callback: Callable, tip: String) -> Button:
	var b := Button.new()
	b.text = text
	b.tooltip_text = tip
	b.flat = true
	b.pressed.connect(callback)
	parent.add_child(b)
	return b


func _label(parent: Control, text: String) -> void:
	var l := Label.new()
	l.text = text
	l.modulate = Color(1, 1, 1, 0.6)
	parent.add_child(l)


# ---- The player and its timeline ----------------------------------------------

## Shows and edits the timeline of this ZtripPlayer.
func attach_player(player: Node) -> void:
	var old := _player()
	if old == player:
		return
	if old:
		_disconnect(old)
	_player_ref = weakref(player)
	player.timeline_loaded.connect(_on_timeline_loaded)
	player.analysis_loaded.connect(_on_analysis_loaded)
	_timeline = player.get_timeline()
	_timeline.changed.connect(_on_timeline_changed)
	_undo.clear()
	_redo.clear()
	_dirty = false
	_on_analysis_loaded()


func _disconnect(player: Node) -> void:
	if player.timeline_loaded.is_connected(_on_timeline_loaded):
		player.timeline_loaded.disconnect(_on_timeline_loaded)
	if player.analysis_loaded.is_connected(_on_analysis_loaded):
		player.analysis_loaded.disconnect(_on_analysis_loaded)
	if _timeline and _timeline.changed.is_connected(_on_timeline_changed):
		_timeline.changed.disconnect(_on_timeline_changed)


func _player() -> Node:
	if _player_ref == null:
		return null
	var p = _player_ref.get_ref()
	return p if is_instance_valid(p) else null


func _find_player_in_scene() -> void:
	if not Engine.is_editor_hint() or not is_visible_in_tree():
		return
	var p := _player()
	if p and p.is_inside_tree():
		return
	var root := EditorInterface.get_edited_scene_root()
	if root == null:
		return
	var found := root.find_children("*", "ZtripPlayer", true, false)
	if not found.is_empty():
		attach_player(found[0])


func _on_analysis_loaded() -> void:
	var p := _player()
	_analysis = p.get_analysis() if p else null
	_reload_from_timeline()


func _on_timeline_loaded() -> void:
	_reload_from_timeline()


func _on_timeline_changed() -> void:
	if not _committing:
		_reload_from_timeline()


## Re-reads lanes and notes from the timeline (dropping the selection).
func _reload_from_timeline() -> void:
	_lanes.clear()
	_notes.clear()
	_ghosts.clear()
	if _timeline == null:
		_canvas.queue_redraw()
		return
	for lane in _timeline.get_lane_names():
		_lanes.append(lane)
		_notes[lane] = _timeline.get_notes(lane)
		_recompute_times(lane)
	if _analysis:
		for spec in _timeline.get_draft_lanes():
			var times: PackedFloat64Array = _analysis.get_event_times(spec.events)
			if times.is_empty() and spec.fallback != "":
				times = _analysis.get_event_times(spec.fallback)
			_ghosts[spec.lane] = times
	_canvas.custom_minimum_size.y = _lanes_top() + LANE_H * maxi(_lanes.size(), 1) + 4
	_update_scrollbar()
	_update_labels()
	_canvas.queue_redraw()


func _recompute_times(lane: String) -> void:
	for n in _notes[lane]:
		n.s0 = _timeline.note_seconds(lane, n.start_beat)
		n.s1 = _timeline.note_seconds(lane, n.start_beat + n.length_beats)


## Writes a lane's notes to the timeline; the player rebinds at once.
func _commit(lane: String) -> void:
	var notes: Array = _notes[lane]
	notes.sort_custom(func(a, b): return a.start_beat < b.start_beat)
	_committing = true
	_timeline.set_notes(lane, notes)
	_committing = false
	_recompute_times(lane)
	_mark_dirty()


func _mark_dirty() -> void:
	_dirty = true
	_update_labels()
	_canvas.queue_redraw()


# ---- Undo ------------------------------------------------------------------------

func _snapshot() -> Dictionary:
	return {"tempo": _timeline.tempo_to_json(), "sequence": _timeline.sequence_to_json()}


## Call before an edit, so undo can put it back.
func _push_undo() -> void:
	if _timeline != null:
		_record_undo(_snapshot())


func _restore(state: Dictionary) -> void:
	_committing = true
	_timeline.tempo_from_json(state.tempo)
	_timeline.sequence_from_json(state.sequence)
	_committing = false
	_reload_from_timeline()
	_mark_dirty()


func undo() -> bool:
	if _undo.is_empty() or _timeline == null:
		return false
	_redo.append(_snapshot())
	_restore(_undo.pop_back())
	return true


func redo() -> bool:
	if _redo.is_empty() or _timeline == null:
		return false
	_undo.append(_snapshot())
	_restore(_redo.pop_back())
	return true


# ---- Commands --------------------------------------------------------------------

func toggle_playback() -> void:
	var p := _player()
	if p == null:
		return
	if p.is_playing():
		p.stop()
	else:
		p.play()
	_update_labels()


func nudge(seconds: float) -> void:
	if _timeline == null:
		return
	_push_undo()
	_committing = true
	_timeline.nudge_by(seconds)
	_committing = false
	for lane in _lanes:
		_recompute_times(lane)
	_mark_dirty()


func scale_tempo(factor: float) -> void:
	if _timeline == null:
		return
	_push_undo()
	_committing = true
	_timeline.scale_tempo(factor)
	_committing = false
	for lane in _lanes:
		_recompute_times(lane)
	_mark_dirty()


func redraft() -> void:
	var p := _player()
	if p == null or _timeline == null:
		return
	_push_undo()
	if p.draft_timeline(_grid) != OK:
		_undo.pop_back()
		_set_status(_timeline.get_error(), true)
		return
	_reload_from_timeline()
	_mark_dirty()


func add_lane(lane_name: String) -> bool:
	if _timeline == null or lane_name == "" or _timeline.has_lane(lane_name):
		return false
	_push_undo()
	_timeline.add_lane(lane_name) # reloads through `changed`
	_mark_dirty()
	return true


## Adds a note at a beat (snapped when snapping is on) and selects it.
func add_note(lane: String, beat: float, velocity := 0.8) -> Dictionary:
	if not _notes.has(lane):
		return {}
	_push_undo()
	_clear_selection()
	var step := _step()
	var start := _snap_beat(beat)
	var n := {"start_beat": start, "length_beats": step, "velocity": velocity, "pitch": -1,
			"envelope_ref": "", "sel": true}
	_notes[lane].append(n)
	_commit(lane)
	return n


## Moves the selected notes by a number of beats.
func move_selection(delta_beats: float) -> void:
	if not _has_selection():
		return
	_push_undo()
	for lane in _lanes:
		var changed := false
		for n in _notes[lane]:
			if n.get("sel", false):
				n.start_beat += delta_beats
				changed = true
		if changed:
			_commit(lane)


func set_selection_velocity(delta: float) -> void:
	if not _has_selection():
		return
	_push_undo()
	for lane in _lanes:
		var changed := false
		for n in _notes[lane]:
			if n.get("sel", false):
				n.velocity = clampf(n.velocity + delta, 0.05, 1.0)
				changed = true
		if changed:
			_commit(lane)


func delete_selection() -> void:
	if not _has_selection():
		return
	_push_undo()
	for lane in _lanes:
		var before: int = _notes[lane].size()
		_notes[lane] = _notes[lane].filter(func(n): return not n.get("sel", false))
		if _notes[lane].size() != before:
			_commit(lane)


func quantize_selection() -> void:
	if not _has_selection():
		return
	_push_undo()
	var step := _step()
	for lane in _lanes:
		var changed := false
		for n in _notes[lane]:
			if n.get("sel", false):
				n.start_beat = ZtripTimeline.snap(n.start_beat, step)
				changed = true
		if changed:
			_commit(lane)


func select_all() -> void:
	for lane in _lanes:
		for n in _notes[lane]:
			n.sel = true
	_canvas.queue_redraw()


## Saves the timeline next to the song (or where the player's tempo_file and
## sequence_file point).
func save() -> Error:
	var p := _player()
	if p == null:
		_set_status("No ZtripPlayer selected", true)
		return ERR_UNCONFIGURED
	if p.tempo_file == "" or p.sequence_file == "":
		var base := _default_base(p)
		if p.tempo_file == "":
			p.tempo_file = base + ".tempo.json"
		if p.sequence_file == "":
			p.sequence_file = base + ".nseq.json"
		if Engine.is_editor_hint():
			EditorInterface.mark_scene_as_unsaved()
	var err: Error = p.save_timeline()
	if err != OK:
		_set_status("Not saved: " + _timeline.get_error(), true)
		return err
	if Engine.is_editor_hint():
		for f in [p.tempo_file, p.sequence_file]:
			if f.begins_with("res://"):
				EditorInterface.get_resource_filesystem().update_file(f)
	_dirty = false
	_set_status("Saved", false)
	_update_labels()
	return OK


func _default_base(p: Node) -> String:
	var song: String = p.get_resolved_audio_file()
	if song.begins_with("res://"):
		return song.get_basename()
	var stem := song.get_file().get_basename() if song != "" else "timeline"
	return "res://" + stem


func _on_add_lane() -> void:
	_lane_name.text = ""
	_lane_dialog.popup_centered(Vector2i(320, 90))
	_lane_name.grab_focus()


func _on_lane_menu(id: int) -> void:
	var lane := _lane_menu_lane
	if not _notes.has(lane):
		return
	_push_undo()
	if id == 0:
		_notes[lane] = []
		_commit(lane)
	elif id == 1:
		_timeline.remove_lane(lane) # reloads through `changed`
		_mark_dirty()


# ---- Geometry --------------------------------------------------------------------

func _lanes_top() -> float:
	return RULER_H + TEMPO_H + WAVE_H


func _x_of(t: float) -> float:
	return HEADER_W + (t - _view_start) * _px_per_s


func _t_of(x: float) -> float:
	return _view_start + (x - HEADER_W) / _px_per_s


## Screen x of a beat in a lane (swing applied).
func beat_to_x(lane: String, beat: float) -> float:
	return _x_of(_timeline.note_seconds(lane, beat))


## Canvas position at the middle of a lane row at a beat, for scripts and tests.
func lane_point(lane: String, beat: float) -> Vector2:
	return Vector2(beat_to_x(lane, beat), _lanes_top() + LANE_H * (_lanes.find(lane) + 0.5))


func _lane_at(y: float) -> String:
	var i := floori((y - _lanes_top()) / LANE_H)
	return _lanes[i] if i >= 0 and i < _lanes.size() else ""


func _step() -> float:
	var step: float = ZtripTimeline.grid_step(_grid)
	return step if step > 0.0 else 0.25


func _snap_beat(beat: float) -> float:
	if not _snap or Input.is_key_pressed(KEY_ALT):
		return beat
	return ZtripTimeline.snap(beat, _step())


func _duration() -> float:
	var p := _player()
	var d: float = p.get_duration() if p else 0.0
	return d if d > 0.0 else 60.0


func _view_seconds() -> float:
	return maxf(_canvas.size.x - HEADER_W, 1.0) / _px_per_s


func _update_scrollbar() -> void:
	if _hscroll == null:
		return
	_hscroll.max_value = _duration()
	_hscroll.page = _view_seconds()
	_hscroll.set_value_no_signal(_view_start)


func set_view(start_s: float, px_per_s: float) -> void:
	_px_per_s = clampf(px_per_s, 4.0, 4000.0)
	_view_start = clampf(start_s, 0.0, maxf(_duration() - _view_seconds() * 0.5, 0.0))
	_update_scrollbar()
	_canvas.queue_redraw()


# ---- Hit testing -----------------------------------------------------------------

## The note under a canvas position: {lane, note, edge} or empty.
func _note_at(pos: Vector2) -> Dictionary:
	var lane := _lane_at(pos.y)
	if lane == "":
		return {}
	var t := _t_of(pos.x)
	var best := {}
	for n in _notes[lane]:
		var x0 := _x_of(n.s0)
		var x1 := maxf(_x_of(n.s1), x0 + 3.0)
		if pos.x >= x0 - 1.0 and pos.x <= x1 + 1.0:
			best = {"lane": lane, "note": n, "edge": pos.x >= x1 - EDGE_PX and x1 - x0 > EDGE_PX * 1.5}
	return best


func _clear_selection() -> void:
	for lane in _lanes:
		for n in _notes[lane]:
			n.sel = false


func _has_selection() -> bool:
	for lane in _lanes:
		for n in _notes[lane]:
			if n.get("sel", false):
				return true
	return false


# ---- Input -----------------------------------------------------------------------

func _on_canvas_input(event: InputEvent) -> void:
	if _timeline == null:
		return
	if event is InputEventMouseButton:
		_on_mouse_button(event)
	elif event is InputEventMouseMotion:
		_on_mouse_motion(event)
	elif event is InputEventKey and event.pressed:
		_on_key(event)


func _on_mouse_button(e: InputEventMouseButton) -> void:
	if e.pressed and e.button_index in [MOUSE_BUTTON_WHEEL_UP, MOUSE_BUTTON_WHEEL_DOWN]:
		var dir := -1.0 if e.button_index == MOUSE_BUTTON_WHEEL_UP else 1.0
		if e.ctrl_pressed:
			var anchor := _t_of(e.position.x)
			var scale := 1.25 if dir < 0 else 0.8
			_px_per_s = clampf(_px_per_s * scale, 4.0, 4000.0)
			set_view(anchor - (e.position.x - HEADER_W) / _px_per_s, _px_per_s)
		elif e.shift_pressed and not _note_at(e.position).is_empty():
			var hit := _note_at(e.position)
			_push_undo()
			hit.note.velocity = clampf(hit.note.velocity - dir * 0.05, 0.05, 1.0)
			_commit(hit.lane)
		else:
			set_view(_view_start + dir * _view_seconds() * 0.1, _px_per_s)
		_canvas.accept_event()
		return

	if e.button_index == MOUSE_BUTTON_LEFT and not e.pressed:
		_drag = {}
		_seeking = false
		return
	if not e.pressed:
		return
	_canvas.grab_focus()

	if e.position.y < _lanes_top():
		if e.button_index == MOUSE_BUTTON_LEFT and e.position.x >= HEADER_W:
			_seeking = true
			_seek_to(_t_of(e.position.x))
		_canvas.accept_event()
		return

	var lane := _lane_at(e.position.y)
	if lane == "":
		return
	if e.position.x < HEADER_W:
		if e.button_index == MOUSE_BUTTON_RIGHT:
			_lane_menu_lane = lane
			_lane_menu.position = Vector2i(_canvas.get_screen_position() + e.position)
			_lane_menu.popup()
		_canvas.accept_event()
		return

	var hit := _note_at(e.position)
	if e.button_index == MOUSE_BUTTON_RIGHT:
		if not hit.is_empty():
			_push_undo()
			_notes[hit.lane].erase(hit.note)
			_commit(hit.lane)
		_canvas.accept_event()
		return
	if e.button_index != MOUSE_BUTTON_LEFT:
		return

	if hit.is_empty():
		# add_note records the undo step, and dragging on adjusts the same note.
		var n := add_note(lane, _timeline.beat_at(_t_of(e.position.x)))
		_begin_drag({"lane": lane, "note": n, "edge": false}, e.position, {})
	else:
		if e.shift_pressed:
			hit.note.sel = not hit.note.get("sel", false)
		elif not hit.note.get("sel", false):
			_clear_selection()
			hit.note.sel = true
		_begin_drag(hit, e.position, _snapshot())
	_canvas.queue_redraw()
	_canvas.accept_event()


## Starts moving or resizing the selection. `before` is the state to undo to,
## recorded on the first movement; empty when the undo step already exists.
func _begin_drag(hit: Dictionary, pos: Vector2, before: Dictionary) -> void:
	var originals := []
	for lane in _lanes:
		for n in _notes[lane]:
			if n.get("sel", false):
				originals.append([lane, n, n.start_beat, n.length_beats])
	_drag = {"lane": hit.lane, "note": hit.note, "resize": hit.edge, "x": pos.x,
			"beat": _timeline.beat_at(_t_of(pos.x)), "anchor_start": hit.note.start_beat,
			"anchor_end": hit.note.start_beat + hit.note.length_beats,
			"originals": originals, "before": before, "moved": before.is_empty()}


func _record_undo(state: Dictionary) -> void:
	_undo.append(state)
	if _undo.size() > MAX_UNDO:
		_undo.pop_front()
	_redo.clear()


func _on_mouse_motion(e: InputEventMouseMotion) -> void:
	if _seeking:
		_seek_to(_t_of(e.position.x))
		return
	if _drag.is_empty():
		_canvas.mouse_default_cursor_shape = Control.CURSOR_HSIZE if _note_at(e.position).get("edge", false) else Control.CURSOR_ARROW
		return
	if not _drag.moved and absf(e.position.x - _drag.x) < 3.0:
		return
	var beat: float = _timeline.beat_at(_t_of(e.position.x))
	var delta: float = beat - _drag.beat
	var lanes_touched := {}
	if _drag.resize:
		var end := _snap_beat(_drag.anchor_end + delta)
		var change: float = end - _drag.anchor_end
		for o in _drag.originals:
			o[1].length_beats = maxf(o[3] + change, _step() if _snap else 0.01)
			lanes_touched[o[0]] = true
	else:
		var start := _snap_beat(_drag.anchor_start + delta)
		var change: float = start - _drag.anchor_start
		for o in _drag.originals:
			o[1].start_beat = o[2] + change
			lanes_touched[o[0]] = true
	if not _drag.moved:
		_record_undo(_drag.before)
		_drag.moved = true
	for lane in lanes_touched:
		_commit(lane)


func _on_key(e: InputEventKey) -> void:
	var handled := true
	var cmd := e.ctrl_pressed or e.meta_pressed
	match e.keycode:
		KEY_Z when cmd and e.shift_pressed:
			redo()
		KEY_Z when cmd:
			undo()
		KEY_Y when cmd:
			redo()
		KEY_A when cmd:
			select_all()
		KEY_LEFT:
			move_selection(-_step() * (0.25 if e.shift_pressed else 1.0))
		KEY_RIGHT:
			move_selection(_step() * (0.25 if e.shift_pressed else 1.0))
		KEY_UP:
			set_selection_velocity(0.1)
		KEY_DOWN:
			set_selection_velocity(-0.1)
		KEY_DELETE, KEY_BACKSPACE:
			delete_selection()
		KEY_Q:
			quantize_selection()
		KEY_SPACE:
			toggle_playback()
		KEY_ESCAPE:
			_clear_selection()
			_canvas.queue_redraw()
		_:
			handled = false
	if handled:
		_canvas.accept_event()


func _seek_to(t: float) -> void:
	var p := _player()
	if p:
		p.seek(clampf(t, 0.0, _duration()))
	_update_labels()
	_canvas.queue_redraw()


# ---- Playback follow ---------------------------------------------------------------

func _process(_delta: float) -> void:
	var p := _player()
	if p == null or not is_visible_in_tree():
		return
	if p.is_playing():
		var t: float = p.time
		if _follow and (t > _view_start + _view_seconds() * 0.9 or t < _view_start):
			set_view(t - _view_seconds() * 0.1, _px_per_s)
		_update_labels()
		_canvas.queue_redraw()


func _update_labels() -> void:
	var p := _player()
	_play_button.text = "Stop" if p and p.is_playing() else "Play"
	if _timeline == null or p == null:
		_time_label.text = "no ZtripPlayer"
		_nudge_label.text = ""
		_tempo_label.text = ""
		_file_label.text = "Select a ZtripPlayer"
		return
	var t: float = p.time
	var pos: Dictionary = _timeline.position_at(_timeline.beat_at(t))
	_time_label.text = "%d.%d  %d:%05.2f" % [pos.bar + 1, pos.beat + 1, int(t) / 60, fmod(t, 60.0)]
	_nudge_label.text = "%+d ms" % roundi(_timeline.get_nudge() * 1000.0)
	_tempo_label.text = "%.2f BPM" % _timeline.bpm_at(_timeline.beat_at(t))
	var file: String = p.sequence_file
	_file_label.text = (file.get_file() if file != "" else "(not saved)") + (" *" if _dirty else "")
	_file_label.tooltip_text = "%s\n%s" % [p.tempo_file, p.sequence_file]


func _set_status(text: String, error: bool) -> void:
	_status_label.text = text
	_status_label.modulate = Color(1, 0.5, 0.4) if error else Color(0.6, 1, 0.6)


# ---- Drawing -------------------------------------------------------------------------

func _draw_canvas() -> void:
	var c := _canvas
	var w := c.size.x
	var font := get_theme_default_font()
	var fs := get_theme_default_font_size()
	var top := _lanes_top()
	var bottom := top + LANE_H * maxi(_lanes.size(), 1)
	c.draw_rect(Rect2(0, 0, w, c.size.y), Color(0.11, 0.12, 0.14))
	if _timeline == null:
		c.draw_string(font, Vector2(12, 28), "Select a ZtripPlayer to see its timeline.", HORIZONTAL_ALIGNMENT_LEFT, -1, fs, Color(1, 1, 1, 0.6))
		return
	var t0 := _view_start
	var t1 := _t_of(w)

	# Row backgrounds.
	c.draw_rect(Rect2(0, 0, w, RULER_H), Color(0.16, 0.17, 0.2))
	c.draw_rect(Rect2(0, RULER_H, w, TEMPO_H), Color(0.13, 0.14, 0.17))
	for i in _lanes.size():
		c.draw_rect(Rect2(0, top + i * LANE_H, w, LANE_H), Color(0.14, 0.15, 0.18) if i % 2 == 0 else Color(0.12, 0.13, 0.16))

	# Loudness under everything, so hits line up with the waveform.
	if _analysis:
		var cols := int((w - HEADER_W) / 2.0)
		if cols > 0:
			var peaks: PackedFloat32Array = _analysis.get_signal_peaks("rms", t0, t1, cols)
			var mid := RULER_H + TEMPO_H + WAVE_H * 0.5
			for i in peaks.size():
				var h := peaks[i] * WAVE_H * 0.46
				c.draw_rect(Rect2(HEADER_W + i * 2.0, mid - h, 1.6, h * 2.0), Color(0.4, 0.75, 0.7, 0.75))

	# Beat grid: bars, beats and grid steps.
	var b0: float = _timeline.beat_at(t0)
	var b1: float = _timeline.beat_at(t1)
	var step := _step()
	var beat_px := _px_per_s * 60.0 / maxf(_timeline.bpm_at(b0), 1.0)
	if step * beat_px >= 6.0:
		var k := floorf(b0 / step)
		while k * step <= b1:
			var x := _x_of(_timeline.seconds_at(k * step))
			c.draw_line(Vector2(x, top), Vector2(x, bottom), Color(1, 1, 1, 0.04))
			k += 1.0
	var beat := floorf(b0)
	var label_every := maxi(1, ceili(48.0 / maxf(beat_px * 4.0, 1.0)))
	while beat <= b1 + 1.0:
		var x := _x_of(_timeline.seconds_at(beat))
		var pos: Dictionary = _timeline.position_at(beat)
		if x >= HEADER_W:
			if pos.beat == 0:
				c.draw_line(Vector2(x, 0), Vector2(x, bottom), Color(1, 1, 1, 0.28))
				if posmod(pos.bar, label_every) == 0:
					c.draw_string(font, Vector2(x + 3, RULER_H - 6), str(pos.bar + 1), HORIZONTAL_ALIGNMENT_LEFT, -1, fs - 2, Color(1, 1, 1, 0.85))
			elif beat_px >= 8.0:
				c.draw_line(Vector2(x, RULER_H * 0.6), Vector2(x, RULER_H), Color(1, 1, 1, 0.3))
				c.draw_line(Vector2(x, top), Vector2(x, bottom), Color(1, 1, 1, 0.1))
		beat += 1.0

	# Tempo: BPM as a line, with the value and meter at each change.
	var bpms := []
	var lo := 1e9
	var hi := -1e9
	var cols_t := maxi(int((w - HEADER_W) / 4.0), 2)
	for i in cols_t + 1:
		var bpm: float = _timeline.bpm_at(_timeline.beat_at(t0 + (t1 - t0) * i / cols_t))
		bpms.append(bpm)
		lo = minf(lo, bpm)
		hi = maxf(hi, bpm)
	var span := maxf(hi - lo, 2.0)
	var mid_bpm := (hi + lo) * 0.5
	var pts := PackedVector2Array()
	for i in bpms.size():
		var y: float = RULER_H + TEMPO_H * 0.5 - (bpms[i] - mid_bpm) / span * (TEMPO_H - 12.0)
		pts.append(Vector2(HEADER_W + (w - HEADER_W) * i / cols_t, y))
	c.draw_polyline(pts, Color(1.0, 0.8, 0.35, 0.9), 1.5)
	# What is in force at the left edge, then each change in view.
	var edge_meter: Dictionary = _timeline.position_at(b0)
	c.draw_string(font, Vector2(HEADER_W + 4, RULER_H + 12), "%.1f" % _timeline.bpm_at(b0), HORIZONTAL_ALIGNMENT_LEFT, -1, fs - 4, Color(1.0, 0.85, 0.5, 0.8))
	c.draw_string(font, Vector2(HEADER_W + 4, RULER_H + TEMPO_H - 4), "%d/%d" % [edge_meter.numerator, edge_meter.denominator], HORIZONTAL_ALIGNMENT_LEFT, -1, fs - 3, Color(1, 1, 1, 0.7))
	var last_label_x := HEADER_W
	for seg in _timeline.get_segments():
		var x := _x_of(_timeline.seconds_at(seg.start_beat))
		if x < HEADER_W or x > w or x - last_label_x < 70.0:
			continue
		c.draw_line(Vector2(x, RULER_H), Vector2(x, RULER_H + TEMPO_H), Color(1.0, 0.8, 0.35, 0.35))
		c.draw_string(font, Vector2(x + 3, RULER_H + 12), "%.1f" % seg.bpm_start, HORIZONTAL_ALIGNMENT_LEFT, -1, fs - 4, Color(1.0, 0.85, 0.5, 0.8))
		last_label_x = x
	for sig in _timeline.get_time_signatures():
		var x := _x_of(_timeline.seconds_at(_timeline.bar_start_beat(sig.bar)))
		if x >= HEADER_W + 50.0 and x <= w:
			c.draw_string(font, Vector2(x + 3, RULER_H + TEMPO_H - 4), "%d/%d" % [sig.numerator, sig.denominator], HORIZONTAL_ALIGNMENT_LEFT, -1, fs - 3, Color(1, 1, 1, 0.7))

	# Lanes: ghost hits, then notes.
	for i in _lanes.size():
		var lane: String = _lanes[i]
		var y := top + i * LANE_H
		var color: Color = LANE_COLORS[i % LANE_COLORS.size()]
		for g in _ghosts.get(lane, PackedFloat64Array()):
			if g < t0 or g > t1:
				continue
			var gx := _x_of(g)
			c.draw_line(Vector2(gx, y + 3), Vector2(gx, y + LANE_H - 3), Color(color, 0.28), 1.0)
		for n in _notes[lane]:
			if n.s1 < t0 or n.s0 > t1:
				continue
			var x0 := maxf(_x_of(n.s0), HEADER_W)
			var x1 := maxf(_x_of(n.s1), x0 + 3.0)
			var h: float = (LANE_H - 8.0) * (0.35 + 0.65 * n.velocity)
			var r := Rect2(x0, y + LANE_H - 4.0 - h, x1 - x0, h)
			c.draw_rect(r, Color(color, 0.45 + 0.5 * n.velocity))
			if n.get("sel", false):
				c.draw_rect(r.grow(1.0), Color.WHITE, false, 1.5)
			c.draw_line(Vector2(x0, y + 3), Vector2(x0, y + LANE_H - 3), color.lightened(0.3), 1.0)

	# Headers on top, so notes scrolled left slide under them.
	c.draw_rect(Rect2(0, 0, HEADER_W, bottom), Color(0.17, 0.18, 0.21))
	c.draw_string(font, Vector2(8, RULER_H - 6), "bar", HORIZONTAL_ALIGNMENT_LEFT, -1, fs - 3, Color(1, 1, 1, 0.5))
	c.draw_string(font, Vector2(8, RULER_H + 15), "tempo", HORIZONTAL_ALIGNMENT_LEFT, -1, fs - 3, Color(1, 1, 1, 0.5))
	c.draw_string(font, Vector2(8, RULER_H + TEMPO_H + 15), "song", HORIZONTAL_ALIGNMENT_LEFT, -1, fs - 3, Color(1, 1, 1, 0.5))
	for i in _lanes.size():
		var lane: String = _lanes[i]
		var y := top + i * LANE_H
		var color: Color = LANE_COLORS[i % LANE_COLORS.size()]
		c.draw_rect(Rect2(0, y + 4, 4, LANE_H - 8), color)
		c.draw_string(font, Vector2(10, y + LANE_H * 0.5 + 5), lane, HORIZONTAL_ALIGNMENT_LEFT, HEADER_W - 50, fs - 1, Color(1, 1, 1, 0.9))
		c.draw_string(font, Vector2(HEADER_W - 40, y + LANE_H * 0.5 + 5), str(_notes[lane].size()), HORIZONTAL_ALIGNMENT_RIGHT, 34, fs - 4, Color(1, 1, 1, 0.45))
	if _lanes.is_empty():
		c.draw_string(font, Vector2(HEADER_W + 10, top + LANE_H * 0.6), "No lanes: press Re-draft or + Lane.", HORIZONTAL_ALIGNMENT_LEFT, -1, fs, Color(1, 1, 1, 0.5))
	c.draw_line(Vector2(HEADER_W, 0), Vector2(HEADER_W, bottom), Color(1, 1, 1, 0.2))

	# Playhead.
	var p := _player()
	if p:
		var px := _x_of(p.time)
		if px >= HEADER_W and px <= w:
			c.draw_line(Vector2(px, 0), Vector2(px, bottom), Color(1, 1, 1, 0.9), 1.5)
