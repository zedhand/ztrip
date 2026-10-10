#pragma once

#include "ztrip/io/controls.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ztrip::io {

// Turns one MIDI message into control messages (see ControlSet for names).
// Note-on sets midi/<ch>/note/<n> to velocity/127 and fires it, note-off
// (or note-on with velocity 0) sets it to 0; control change sets
// midi/<ch>/cc/<n> to value/127; pitch bend sets midi/<ch>/pitchbend to
// 0..1 (0.5 centred). Each is also written under channel 0, meaning any
// channel. Other messages are ignored. Returns how many were added.
std::size_t midi_to_controls(std::span<const std::uint8_t> message, std::vector<ControlMessage>& out);

// MIDI input ports this machine offers (empty when MIDI is unavailable).
std::vector<std::string> list_midi_inputs();

// One MIDI input port, through RtMidi (ALSA on Linux, Windows MM on Windows).
class MidiInput : public ControlInput {
public:
    // Opens the first port whose name contains `port` (empty: the first port).
    // Throws std::runtime_error when there is no such port or MIDI is unavailable.
    explicit MidiInput(const std::string& port);
    ~MidiInput() override;

    const std::string& source_name() const noexcept override { return name_; }

    // Feeds raw bytes as if they arrived on the port (simulation and tests).
    void receive(std::span<const std::uint8_t> message);

    struct Port;

private:
    MidiInput() = default;
    friend std::unique_ptr<MidiInput> make_simulated_midi_input(std::string name);
    std::unique_ptr<Port> port_;
    std::string name_;
    std::vector<ControlMessage> scratch_;
};

// A MidiInput with no port behind it, fed only through receive().
std::unique_ptr<MidiInput> make_simulated_midi_input(std::string name);

} // namespace ztrip::io
