#include "graph_fixtures.hpp"
#include "synth.hpp"

#include "ztrip/io/audio_capture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <thread>

using namespace ztrip;
using ztrip::test::GraphBuilder;

// These run in real time with no audio device: miniaudio's null backend, and
// a synthetic song played into the live input as if it were one.

TEST_CASE("listing capture devices never throws", "[live][capture]") {
    CHECK_NOTHROW(io::list_capture_devices());
}

TEST_CASE("capture delivers audio from the null backend in real time", "[live][capture]") {
    io::CaptureSettings settings;
    settings.null_backend = true;
    settings.sample_rate = 48000;
    io::AudioCapture capture(settings);
    CHECK(capture.sample_rate() == 48000);
    const auto start = std::chrono::steady_clock::now();
    while (capture.available() < 4800 && std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::vector<float> buf(4800);
    REQUIRE(capture.read(buf) == buf.size());
    CHECK(std::all_of(buf.begin(), buf.end(), [](float x) { return x == 0.0f; }));
}

TEST_CASE("a song played into the live input drives a live graph", "[live][capture]") {
    constexpr int kSr = 48000;
    io::AudioBuffer audio;
    audio.sample_rate = kSr;
    audio.channels = 1;
    audio.samples = test::groove(kSr, 120.0, 1, 0.25, false); // kicks at 0.25, 0.75, 1.25, 1.75 s
    io::LiveAudioInput input(audio, "groove", false);
    CHECK(input.source_name() == "groove");

    GraphBuilder g;
    g.node("onset", "AudioOnset", {{"band", std::string("Bass")}, {"min_velocity", 0.3}});
    // A long decay keeps each flash above 0.5 across a slow frame: Windows
    // sleeps round up to its ~15.6 ms timer, so detection latency plus one
    // frame can pass the ~70 ms a 100 ms decay stays lit for.
    g.node("decay", "ImpulseDecay", {{"decay_ms", 250.0}});
    g.edge("onset.trigger", "decay.trigger").out("light", "decay.out");
    graph::EvaluatorSettings settings;
    settings.live = true;
    auto ev = g.evaluator(settings);
    ev.bind_source("main", &input.result());

    // A 60 fps game loop: drain the input, evaluate at its time.
    int flashes = 0;
    bool lit = false;
    const auto start = std::chrono::steady_clock::now();
    while (input.time() < 2.2 && std::chrono::steady_clock::now() - start < std::chrono::seconds(10)) {
        std::this_thread::sleep_for(std::chrono::microseconds(16667));
        input.update();
        ev.evaluate(input.time());
        const bool now = *ev.output("light") > 0.5;
        if (now && !lit) ++flashes;
        lit = now;
    }
    CHECK(input.time() >= 2.2);
    CHECK(flashes == 4);
    CHECK(input.dropped() == 0);
}
