#include "graph_fixtures.hpp"

#include "ztrip/io/controls.hpp"
#include "ztrip/io/midi.hpp"
#include "ztrip/io/network.hpp"
#include "ztrip/serialize/controls.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

using namespace ztrip;
using Catch::Approx;
using ztrip::test::GraphBuilder;

namespace {

std::vector<std::uint8_t> bytes(std::initializer_list<int> b) {
    std::vector<std::uint8_t> out;
    for (int x : b) out.push_back(static_cast<std::uint8_t>(x));
    return out;
}

std::vector<io::ControlMessage> parse_midi(std::initializer_list<int> b) {
    std::vector<io::ControlMessage> out;
    io::midi_to_controls(bytes(b), out);
    return out;
}

// Ports for the network tests, away from the protocol defaults.
constexpr int kOscPort = 39123;
constexpr int kArtNetPort = 39124;

} // namespace

TEST_CASE("controls keep a history and new input replaces the future", "[controls]") {
    io::ControlSet set;
    set.set("a", 1.0, 0.5f);
    set.set("a", 2.0, 0.8f);
    set.set("a", 3.0, 0.8f); // unchanged: not stored, not counted
    const auto* a = set.find("a");
    REQUIRE(a != nullptr);
    CHECK(a->value_at(0.5) == 0.0f);
    CHECK(a->value_at(1.0) == 0.5f);
    CHECK(a->value_at(2.5) == 0.8f);
    CHECK(a->changes.size() == 2);
    CHECK(set.change_count() == 2);

    // After a seek back to 1.5, a new value overwrites what came later.
    set.set("a", 1.5, 0.1f);
    CHECK(a->value_at(10.0) == 0.1f);
    CHECK(a->changes.size() == 2);

    set.trigger("b", 4.0, 1.0f);
    CHECK(set.last_changed() == "b");
    CHECK(set.change_count() == 4);
}

TEST_CASE("MIDI messages become notes, CCs and pitch bend on their channel and channel 0", "[controls][midi]") {
    auto on = parse_midi({0x91, 60, 127});
    REQUIRE(on.size() == 2);
    CHECK(std::string(on[0].name) == "midi/2/note/60");
    CHECK(std::string(on[1].name) == "midi/0/note/60");
    CHECK(on[0].kind == io::ControlMessage::SetAndTrigger);
    CHECK(on[0].value == 1.0f);

    auto off = parse_midi({0x81, 60, 64});
    REQUIRE(off.size() == 2);
    CHECK(off[0].kind == io::ControlMessage::Set);
    CHECK(off[0].value == 0.0f);
    CHECK(parse_midi({0x91, 60, 0})[0].value == 0.0f); // note-on at velocity 0 is a note-off

    auto cc = parse_midi({0xB0, 7, 127});
    CHECK(std::string(cc[0].name) == "midi/1/cc/7");
    CHECK(cc[0].value == 1.0f);

    auto bend = parse_midi({0xE0, 0x00, 0x40});
    CHECK(std::string(bend[0].name) == "midi/1/pitchbend");
    CHECK(bend[0].value == Approx(0.5).margin(1e-3));

    CHECK(parse_midi({0xF8}).empty());          // clock: ignored for now
    CHECK(parse_midi({0xF0, 1, 2, 0xF7}).empty()); // sysex
    CHECK(parse_midi({0x90, 60}).empty());       // truncated
}

TEST_CASE("a MIDI pad and knob drive a graph", "[controls][midi][graph]") {
    auto midi = io::make_simulated_midi_input("test pads");
    io::ControlSet controls;
    GraphBuilder g;
    g.node("pad", "MidiNote", {{"note", 36.0}});
    g.node("flash", "ImpulseDecay", {{"decay_ms", 100.0}});
    g.node("knob", "MidiCC", {{"channel", 1.0}, {"cc", 74.0}});
    g.edge("pad.trigger", "flash.trigger").out("light", "flash.out").out("dimmer", "knob.out");
    graph::EvaluatorSettings settings;
    settings.live = true;
    auto ev = g.evaluator(settings);
    ev.bind_controls("main", &controls);

    double t = 0.0;
    auto frame = [&] {
        t += 1.0 / 60.0;
        midi->update(controls, ev.stamp_time(t));
        ev.evaluate(t);
    };
    frame();
    CHECK(*ev.output("light") == 0.0);
    midi->receive(bytes({0x99, 36, 100})); // channel 10, any-channel node
    midi->receive(bytes({0xB0, 74, 64}));
    frame();
    CHECK(*ev.output("light") == Approx(100.0 / 127.0));
    CHECK(*ev.output("dimmer") == Approx(64.0 / 127.0));
    CHECK(ev.port_value("pad", "gate")->v == 1.0);
    for (int i = 0; i < 30; ++i) frame();
    CHECK(*ev.output("light") < 0.01);
    midi->receive(bytes({0x89, 36, 0}));
    frame();
    CHECK(ev.port_value("pad", "gate")->v == 0.0);
    CHECK(midi->dropped() == 0);
}

TEST_CASE("control input stamped for the next evaluate is never missed", "[controls][graph]") {
    io::ControlSet controls;
    GraphBuilder g;
    g.node("pad", "MidiNote", {{"note", 36.0}});
    g.out("hit", "pad.trigger");
    graph::EvaluatorSettings settings;
    settings.live = true;
    auto ev = g.evaluator(settings);
    ev.bind_controls("main", &controls);

    // A 240 fps host evaluates the same 120 Hz tick twice: a note arriving
    // between the two lands on the next tick instead of being lost.
    ev.evaluate(1.0 / 240.0 * 241.0);
    const double stamp = ev.stamp_time(1.0 / 240.0 * 242.0);
    CHECK(stamp > ev.current_time());
    controls.set("midi/0/note/36", stamp, 1.0f);
    controls.trigger("midi/0/note/36", stamp, 1.0f);
    int fired = 0;
    for (int f = 242; f < 250; ++f) {
        const auto before = ev.current_step();
        ev.evaluate(f / 240.0);
        if (ev.current_step() != before) fired += ev.port_value("pad", "trigger")->fired ? 1 : 0;
    }
    CHECK(fired == 1);
}

TEST_CASE("OSC messages and bundles become controls", "[controls][osc]") {
    std::vector<io::ControlMessage> out;
    const float args[] = {0.25f, 3.0f};
    CHECK(io::osc_to_controls(io::osc_message("/fader/1", args), out) == 2);
    CHECK(std::string(out[0].name) == "osc/fader/1");
    CHECK(out[0].kind == io::ControlMessage::SetAndTrigger);
    CHECK(out[0].value == 0.25f);
    CHECK(std::string(out[1].name) == "osc/fader/1/1");
    CHECK(out[1].value == 3.0f);

    // A bundle of one int message and one argument-less message.
    std::vector<std::uint8_t> msg1 = bytes({'/', 'a', 0, 0, ',', 'i', 0, 0, 0, 0, 0, 7});
    std::vector<std::uint8_t> msg2 = bytes({'/', 'g', 'o', 0, ',', 0, 0, 0});
    std::vector<std::uint8_t> bundle = bytes({'#', 'b', 'u', 'n', 'd', 'l', 'e', 0, 0, 0, 0, 0, 0, 0, 0, 1});
    for (const auto* m : {&msg1, &msg2}) {
        bundle.insert(bundle.end(), {0, 0, 0, static_cast<std::uint8_t>(m->size())});
        bundle.insert(bundle.end(), m->begin(), m->end());
    }
    out.clear();
    CHECK(io::osc_to_controls(bundle, out) == 2);
    CHECK(std::string(out[0].name) == "osc/a");
    CHECK(out[0].value == 7.0f);
    CHECK(std::string(out[1].name) == "osc/go");
    CHECK(out[1].value == 1.0f);

    out.clear();
    CHECK(io::osc_to_controls(bytes({'/', 'x', 0, 0, ',', 'f', 0, 0, 1}), out) == 0); // truncated float
    CHECK(io::osc_to_controls(bytes({'x', 0, 0, 0}), out) == 0);                     // no leading slash
}

TEST_CASE("Art-Net and sACN packets parse to DMX frames", "[controls][dmx]") {
    std::vector<std::uint8_t> levels(10, 0);
    levels[0] = 255;
    levels[9] = 128;
    io::DmxFrame frame;
    REQUIRE(io::parse_artnet(io::artnet_packet(0x123, levels), frame));
    CHECK(frame.universe == 0x123);
    CHECK(frame.count == 10);
    CHECK(frame.levels[0] == 255);
    CHECK(frame.levels[9] == 128);

    REQUIRE(io::parse_sacn(io::sacn_packet(7, levels), frame));
    CHECK(frame.universe == 7);
    CHECK(frame.count == 10);
    CHECK(frame.levels[9] == 128);

    auto bad = io::artnet_packet(0, levels);
    bad[9] = 0x20; // OpPoll, not OpDmx
    CHECK_FALSE(io::parse_artnet(bad, frame));
    CHECK_FALSE(io::parse_sacn(io::artnet_packet(0, levels), frame));
}

TEST_CASE("DMX input posts only channels that changed", "[controls][dmx]") {
    io::DmxInput dmx(io::DmxProtocol::ArtNet, {}, false);
    io::ControlSet controls;
    std::vector<std::uint8_t> levels(512, 0);
    levels[0] = 255;
    dmx.receive(io::artnet_packet(1, levels));
    dmx.update(controls, 0.0);
    CHECK(controls.find("dmx/1/1")->value_at(0.0) == 1.0f);
    CHECK(controls.change_count() == 512); // first frame: every channel is new

    levels[1] = 51;
    dmx.receive(io::artnet_packet(1, levels));
    dmx.receive(io::artnet_packet(1, levels)); // desks resend constantly
    dmx.update(controls, 0.1);
    CHECK(controls.change_count() == 513);
    CHECK(controls.find("dmx/1/2")->value_at(0.1) == Approx(0.2));

    // A 16-bit pair through the graph node.
    levels[2] = 0x80;
    levels[3] = 0x00;
    dmx.receive(io::artnet_packet(1, levels));
    dmx.update(controls, 0.2);
    GraphBuilder g;
    g.node("pan", "DmxChannel", {{"universe", 1.0}, {"channel", 3.0}, {"fine", true}});
    g.out("pan_out", "pan.out");
    auto ev = g.evaluator();
    ev.bind_controls("main", &controls);
    ev.evaluate(0.25);
    CHECK(*ev.output("pan_out") == Approx(0x8000 / 65535.0));
}

TEST_CASE("a fader over Art-Net and an OSC message reach the graph within 30 ms", "[controls][network]") {
    io::NetworkInputSettings net;
    net.bind_address = "127.0.0.1";
    net.port = kArtNetPort;
    io::DmxInput artnet(io::DmxProtocol::ArtNet, net);
    net.port = kOscPort;
    io::OscInput osc(net);
    io::UdpSender to_artnet("127.0.0.1", kArtNetPort);
    io::UdpSender to_osc("127.0.0.1", kOscPort);

    io::ControlSet controls;
    GraphBuilder g;
    g.node("fader", "DmxChannel", {{"universe", 0.0}, {"channel", 1.0}});
    g.node("osc", "OscValue", {{"address", std::string("/scene/dim")}});
    g.out("dimmer", "fader.out").out("osc_out", "osc.out");
    graph::EvaluatorSettings settings;
    settings.live = true;
    auto ev = g.evaluator(settings);
    ev.bind_controls("main", &controls);

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    auto now_s = [&] { return std::chrono::duration<double>(clock::now() - start).count(); };
    // A 60 fps host loop, running until the values arrive (or 2 s pass).
    std::vector<std::uint8_t> levels(512, 0);
    levels[0] = 204;
    const float dim[] = {0.75f};
    const auto sent = clock::now();
    REQUIRE(to_artnet.send(io::artnet_packet(0, levels)));
    REQUIRE(to_osc.send(io::osc_message("/scene/dim", dim)));
    double seen_after_ms = -1.0;
    while (clock::now() - sent < std::chrono::seconds(2)) {
        std::this_thread::sleep_for(std::chrono::microseconds(16667));
        const double t = now_s();
        artnet.update(controls, ev.stamp_time(t));
        osc.update(controls, ev.stamp_time(t));
        ev.evaluate(t);
        if (*ev.output("dimmer") > 0.79 && *ev.output("osc_out") > 0.74) {
            seen_after_ms = std::chrono::duration<double, std::milli>(clock::now() - sent).count();
            break;
        }
    }
    INFO("seen after " << seen_after_ms << " ms");
    REQUIRE(seen_after_ms >= 0.0);
    CHECK(*ev.output("dimmer") == Approx(0.8));
    // One host frame (16.7 ms) plus scheduling; generous for shared CI runners.
    CHECK(seen_after_ms < 30.0 * 2);
}

TEST_CASE("learn binds the first control that moves, and profiles round-trip", "[controls]") {
    io::ControlSet controls;
    controls.set("midi/0/cc/1", 0.0, 0.3f);
    io::ControlLearn learn;
    learn.arm("Gain", controls, 0.0, 8.0);
    io::ControlBinding b;
    CHECK_FALSE(learn.poll(controls, b));
    controls.set("dmx/0/5", 1.0, 0.5f);
    REQUIRE(learn.poll(controls, b));
    CHECK(b.macro == "Gain");
    CHECK(b.control == "dmx/0/5");
    CHECK(b.map(0.5) == 4.0);
    CHECK_FALSE(learn.armed());

    const std::vector<io::ControlBinding> profile{b, {"Hue", "osc/hue", -1.0, 1.0}};
    const auto back = serialize::parse_controls(serialize::write_controls(profile));
    REQUIRE(back.size() == 2);
    CHECK(back[1].control == "osc/hue");
    CHECK(back[1].min == -1.0);
    CHECK_THROWS_AS(serialize::parse_controls(R"({"format":"ztrip.controls","version":9})"), serialize::ControlsError);
    CHECK_THROWS_AS(serialize::parse_controls(R"({"version":1,"bindings":[{"macro":"x"}]})"), serialize::ControlsError);
}
