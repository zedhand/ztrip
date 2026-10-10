@tool
extends RefCounted
## Godot can't import WAVs whose header is WAVE_FORMAT_EXTENSIBLE ("Format not
## supported for WAVE file (not PCM)"), which is how ffmpeg and many DAWs save
## 24-bit, 32-bit and float audio. The samples inside are plain PCM or float,
## so rewriting the header as a plain one makes the file importable without
## touching the audio.

const FORMAT_PCM := 1
const FORMAT_FLOAT := 3
const FORMAT_EXTENSIBLE := 0xFFFE


## True if the file is an EXTENSIBLE WAV holding PCM or float samples.
static func needs_fix(path: String) -> bool:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return false
	var head := f.get_buffer(mini(f.get_length(), 4096))
	var fmt := _find_fmt(head)
	if fmt < 0 or head.size() < fmt + 8 + 26:
		return false
	if head.decode_u16(fmt + 8) != FORMAT_EXTENSIBLE:
		return false
	var sub := head.decode_u16(fmt + 8 + 24)
	return sub == FORMAT_PCM or sub == FORMAT_FLOAT


## Rewrites an EXTENSIBLE WAV's fmt chunk as a plain 16-byte one, in place.
## The sample data is copied unchanged.
static func fix(path: String) -> Error:
	if not needs_fix(path):
		return ERR_INVALID_DATA
	var bytes := FileAccess.get_file_as_bytes(path)
	var out := PackedByteArray()
	out.append_array("WAVE".to_ascii_buffer())
	var pos := 12
	while pos + 8 <= bytes.size():
		var id := bytes.slice(pos, pos + 4)
		var size := bytes.decode_u32(pos + 4)
		var body := bytes.slice(pos + 8, mini(pos + 8 + size, bytes.size()))
		if id.get_string_from_ascii() == "fmt ":
			var plain := body.slice(0, 16)
			plain.encode_u16(0, body.decode_u16(24)) # the subformat GUID starts with the format code
			body = plain
		out.append_array(id)
		var size_bytes := PackedByteArray()
		size_bytes.resize(4)
		size_bytes.encode_u32(0, body.size())
		out.append_array(size_bytes)
		out.append_array(body)
		if body.size() % 2 == 1:
			out.append(0)
		pos += 8 + size + (size & 1)
	var riff := "RIFF".to_ascii_buffer()
	riff.resize(8)
	riff.encode_u32(4, out.size())
	riff.append_array(out)
	var f := FileAccess.open(path, FileAccess.WRITE)
	if f == null:
		return FileAccess.get_open_error()
	f.store_buffer(riff)
	return OK


static func _find_fmt(head: PackedByteArray) -> int:
	if head.size() < 12 or head.slice(0, 4).get_string_from_ascii() != "RIFF" \
			or head.slice(8, 12).get_string_from_ascii() != "WAVE":
		return -1
	var pos := 12
	while pos + 8 <= head.size():
		if head.slice(pos, pos + 4).get_string_from_ascii() == "fmt ":
			return pos
		var size := head.decode_u32(pos + 4)
		pos += 8 + size + (size & 1)
	return -1
