#include "ztrip/io/midi.hpp"

#include <RtMidi.h>

#include <stdexcept>
#include <string>

namespace ztrip::io {
namespace {

void add(std::vector<ControlMessage>& out, int channel, const std::string& rest, ControlMessage::Kind kind,
         float value) {
    out.push_back(ControlMessage::make("midi/" + std::to_string(channel) + "/" + rest, kind, value));
    out.push_back(ControlMessage::make("midi/0/" + rest, kind, value));
}

} // namespace

std::size_t midi_to_controls(std::span<const std::uint8_t> m, std::vector<ControlMessage>& out) {
    const std::size_t before = out.size();
    if (m.size() < 2 || m[0] < 0x80 || m[0] >= 0xF0) return 0;
    const int type = m[0] & 0xF0;
    const int channel = (m[0] & 0x0F) + 1;
    const int data1 = m[1] & 0x7F;
    const int data2 = m.size() > 2 ? m[2] & 0x7F : 0;
    switch (type) {
    case 0x90:
        if (m.size() < 3) break;
        if (data2 > 0) {
            add(out, channel, "note/" + std::to_string(data1), ControlMessage::SetAndTrigger,
                static_cast<float>(data2) / 127.0f);
            break;
        }
        [[fallthrough]];
    case 0x80:
        add(out, channel, "note/" + std::to_string(data1), ControlMessage::Set, 0.0f);
        break;
    case 0xB0:
        if (m.size() < 3) break;
        add(out, channel, "cc/" + std::to_string(data1), ControlMessage::Set, static_cast<float>(data2) / 127.0f);
        break;
    case 0xE0:
        if (m.size() < 3) break;
        add(out, channel, "pitchbend", ControlMessage::Set, static_cast<float>(data1 | (data2 << 7)) / 16383.0f);
        break;
    default: break;
    }
    return out.size() - before;
}

struct MidiInput::Port {
    RtMidiIn in;
};

std::vector<std::string> list_midi_inputs() {
    std::vector<std::string> out;
    try {
        RtMidiIn in;
        in.setErrorCallback([](RtMidiError::Type, const std::string&, void*) {});
        const unsigned count = in.getPortCount();
        for (unsigned i = 0; i < count; ++i) out.push_back(in.getPortName(i));
    } catch (const std::exception&) {
        // No MIDI system: no ports.
    }
    return out;
}

MidiInput::MidiInput(const std::string& port) {
    try {
        port_ = std::make_unique<Port>();
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("MIDI: unavailable (") + e.what() + ")");
    }
    RtMidiIn& in = port_->in;
    const unsigned count = in.getPortCount();
    unsigned index = count;
    for (unsigned i = 0; i < count && index == count; ++i) {
        if (port.empty() || in.getPortName(i).find(port) != std::string::npos) index = i;
    }
    if (index == count) {
        throw std::runtime_error(port.empty() ? "MIDI: no input ports found"
                                              : "MIDI: no input port named \"" + port + "\"");
    }
    name_ = in.getPortName(index);
    scratch_.reserve(8);
    in.setCallback(
        [](double, std::vector<unsigned char>* message, void* self) {
            if (message) static_cast<MidiInput*>(self)->receive(*message);
        },
        this);
    in.ignoreTypes(true, true, true);
    in.openPort(index, "ztrip");
}

MidiInput::~MidiInput() {
    if (port_) port_->in.closePort();
}

void MidiInput::receive(std::span<const std::uint8_t> message) {
    scratch_.clear();
    midi_to_controls(message, scratch_);
    for (const ControlMessage& m : scratch_) post(m);
}

std::unique_ptr<MidiInput> make_simulated_midi_input(std::string name) {
    std::unique_ptr<MidiInput> in(new MidiInput());
    in->name_ = std::move(name);
    return in;
}

} // namespace ztrip::io
