#pragma once

#include "ztrip/analysis/onsets.hpp"
#include "ztrip/io/spsc_ring.hpp"

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ztrip::io {

// One named control (a MIDI CC, a held note, an OSC address, a DMX channel)
// as a history: the value it took at each change, and the times it fired.
struct Control {
    std::string name;
    std::vector<std::pair<double, float>> changes; // (time, value), time ascending
    std::vector<analysis::Trigger> triggers;       // time ascending

    // The value at time t: the last change at or before t, 0 before any.
    float value_at(double t) const noexcept;
};

// Live controls by name, recorded on the evaluation clock. Graph source
// nodes (MidiNote, MidiCC, OscValue, DmxChannel) read them through
// Evaluator::bind_controls. Because each control is a history, not just its
// current value, evaluation stays a function of time: seeking back plays
// back what the controls did, as automation would.
//
// Names: midi/<channel 1-16, or 0 for any>/note/<n>, midi/<ch>/cc/<n>,
// midi/<ch>/pitchbend, osc<address> (e.g. "osc/fader/1"), and
// dmx/<universe>/<channel 1-512>.
//
// Not thread safe: I/O threads hand ControlMessages over through an SpscRing
// and the evaluating thread records them.
class ControlSet {
public:
    // Records a new value at time t. Recorded changes after t are dropped
    // (new input overwrites the future, e.g. after a seek back), and a value
    // equal to the current one is not stored again.
    void set(std::string_view name, double t, float value);
    // Records that the control fired at t with a velocity (a note-on, an OSC
    // message); later triggers are dropped as for set().
    void trigger(std::string_view name, double t, float velocity);

    const Control* find(std::string_view name) const noexcept;
    const std::map<std::string, Control, std::less<>>& controls() const noexcept { return controls_; }

    // The control that changed or fired last, and a counter that goes up on
    // every change, for MIDI learn: wait for the counter to move, then bind
    // last_changed().
    const std::string& last_changed() const noexcept { return last_changed_; }
    std::uint64_t change_count() const noexcept { return change_count_; }

    void clear();

private:
    Control& get(std::string_view name);
    std::map<std::string, Control, std::less<>> controls_;
    std::string last_changed_;
    std::uint64_t change_count_ = 0;
};

// A controller profile entry: a control drives a graph macro, its 0..1 (or
// raw OSC) value mapped linearly onto [min, max] and then clamped by the
// macro's own range. Profiles save as *.controls.json (serialize/controls.hpp)
// so a show can swap controllers without touching the graph.
struct ControlBinding {
    std::string macro;
    std::string control;
    double min = 0.0;
    double max = 1.0;

    double map(double value) const noexcept { return min + value * (max - min); }
};

// MIDI/OSC/DMX learn: arm() with a macro, then poll() each frame; the first
// control that changes after arming is bound to it.
class ControlLearn {
public:
    void arm(std::string macro, const ControlSet& controls, double min = 0.0, double max = 1.0) {
        macro_ = std::move(macro);
        min_ = min;
        max_ = max;
        seen_ = controls.change_count();
    }
    void cancel() { macro_.clear(); }
    bool armed() const noexcept { return !macro_.empty(); }
    const std::string& macro() const noexcept { return macro_; }

    // A binding once a control has moved since arm(), else false.
    bool poll(const ControlSet& controls, ControlBinding& out) {
        if (macro_.empty() || controls.change_count() == seen_) return false;
        out = {macro_, controls.last_changed(), min_, max_};
        macro_.clear();
        return true;
    }

private:
    std::string macro_;
    double min_ = 0.0, max_ = 1.0;
    std::uint64_t seen_ = 0;
};

// A control change in a fixed-size, trivially copyable form, for passing
// through an SpscRing from an I/O thread.
struct ControlMessage {
    enum Kind : std::uint8_t { Set, Trigger, SetAndTrigger };
    char name[55] = {};
    Kind kind = Set;
    float value = 0.0f;

    static ControlMessage make(std::string_view n, Kind k, float v) {
        ControlMessage m;
        const std::size_t len = n.size() < sizeof m.name - 1 ? n.size() : sizeof m.name - 1;
        std::memcpy(m.name, n.data(), len);
        m.kind = k;
        m.value = v;
        return m;
    }
    void apply(ControlSet& set, double t) const {
        if (kind == Set || kind == SetAndTrigger) set.set(name, t, value);
        if (kind == Trigger || kind == SetAndTrigger) set.trigger(name, t, value);
    }
};

// Base of the MIDI, OSC and DMX inputs: their I/O thread posts messages,
// and update() records everything posted since the last call into a
// ControlSet at the evaluation time `now`. Stamping on arrival at the
// evaluating thread means an event is never earlier than a tick already
// evaluated, at the cost of up to one host frame of extra latency.
class ControlInput {
public:
    explicit ControlInput(std::size_t capacity = 4096) : ring_(capacity) {}
    virtual ~ControlInput() = default;
    ControlInput(const ControlInput&) = delete;
    ControlInput& operator=(const ControlInput&) = delete;

    void update(ControlSet& controls, double now) {
        ControlMessage m;
        while (ring_.read(std::span<ControlMessage>(&m, 1)) == 1) m.apply(controls, now);
        dropped_ += ring_.take_dropped();
    }
    // Whether messages are waiting for update() (consumer side).
    bool pending() const noexcept { return ring_.available() > 0; }
    // Messages lost because update() was not called often enough.
    std::uint64_t dropped() const noexcept { return dropped_; }
    virtual const std::string& source_name() const noexcept = 0;

protected:
    // From the I/O thread (or a test). Never blocks.
    void post(const ControlMessage& m) noexcept { ring_.write(std::span<const ControlMessage>(&m, 1)); }

private:
    SpscRing<ControlMessage> ring_;
    std::uint64_t dropped_ = 0;
};

} // namespace ztrip::io
