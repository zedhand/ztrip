#pragma once
// Helpers for graph tests: a terse GraphDoc builder and synthetic analysis
// results with known signals, onsets and a drifting beat grid.

#include "ztrip/analysis/offline.hpp"
#include "ztrip/graph/compiler.hpp"
#include "ztrip/graph/evaluator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace ztrip::test {

struct GraphBuilder {
    graph::GraphDoc doc;

    GraphBuilder& node(std::string id, std::string type, std::map<std::string, graph::ParamValue> params = {}) {
        doc.nodes.push_back({std::move(id), std::move(type), std::move(params), 0.0f, 0.0f});
        return *this;
    }
    // Edge from "node.port" to "node.port".
    GraphBuilder& edge(const std::string& from, const std::string& to) {
        const auto f = from.find('.');
        const auto t = to.find('.');
        doc.edges.push_back({from.substr(0, f), from.substr(f + 1), to.substr(0, t), to.substr(t + 1)});
        return *this;
    }
    GraphBuilder& macro(std::string name, std::string node, std::string param, double min, double max, double def) {
        doc.macros.push_back({std::move(name), std::move(node), std::move(param), min, max, def});
        return *this;
    }
    // Shortcut: PropertyOut node `id` reading `from`, with target = id.
    GraphBuilder& out(const std::string& id, const std::string& from) {
        node(id, "PropertyOut", {{"target", id}});
        return edge(from, id + ".in");
    }

    std::unique_ptr<graph::CompiledGraph> compile() const {
        auto r = graph::compile(doc);
        INFO(r.error_text());
        REQUIRE(r.ok());
        return std::move(r.graph);
    }
    graph::Evaluator evaluator(graph::EvaluatorSettings settings = {}) const {
        return graph::Evaluator(compile(), settings);
    }
};

// An analysis result holding one 120 Hz signal and/or one event list.
inline analysis::AnalysisResult signal_source(std::string name, std::vector<float> values,
                                              std::string events_name = {},
                                              std::vector<analysis::Trigger> events = {}) {
    analysis::AnalysisResult r;
    r.signal_rate_hz = 120.0;
    r.duration_s = static_cast<double>(values.size()) / 120.0;
    r.signals.push_back({std::move(name), std::move(values)});
    if (!events_name.empty()) r.events.push_back({std::move(events_name), std::move(events)});
    return r;
}

// A deterministic song-like analysis: 60 s of band levels and envelopes for
// the default bands, onsets on a kick/snare/hat pattern with varying
// velocity, and a beat grid that drifts from 120 to 126 BPM, starting at
// 0.37 s with the downbeat on the second detected beat.
inline analysis::AnalysisResult synthetic_song(double duration_s = 60.0) {
    analysis::AnalysisResult r;
    r.signal_rate_hz = 120.0;
    r.duration_s = duration_s;
    const auto count = static_cast<std::size_t>(duration_s * 120.0) + 1;

    std::vector<double> beats;
    for (double t = 0.37; t < duration_s - 0.2;) {
        beats.push_back(t);
        const double bpm = 120.0 + 6.0 * t / duration_s;
        t += 60.0 / bpm;
    }
    r.beats.beats_per_bar = 4;
    r.beats.downbeat_phase = 1;
    r.beats.beats = beats;
    for (std::size_t i = 1; i < beats.size(); i += 4) r.beats.downbeats.push_back(beats[i]);
    r.beats.bpm = 123.0;

    const char* bands[] = {"Sub", "Bass", "LowMid", "Mid", "HighMid", "Air"};
    for (int b = 0; b < 6; ++b) {
        std::vector<float> level(count), env(count);
        float e = 0.0f;
        for (std::size_t i = 0; i < count; ++i) {
            const double t = static_cast<double>(i) / 120.0;
            const double v = 0.5 + 0.3 * std::sin(2.0 * std::numbers::pi * (0.7 + 0.31 * b) * t) +
                             0.2 * std::sin(2.0 * std::numbers::pi * 5.3 * t + b);
            level[i] = static_cast<float>(std::clamp(v, 0.0, 1.0));
            e += (level[i] - e) * (level[i] > e ? 0.8f : 0.06f);
            env[i] = e;
        }
        r.signals.push_back({std::string("band.") + bands[b], level});
        r.signals.push_back({std::string("env.") + bands[b], env});
    }

    analysis::EventList kick{"onset.Sub", {}}, snare{"onset.Mid", {}}, hat{"onset.Air", {}}, full{"onset.full", {}};
    analysis::EventList beat{"beat", {}}, down{"downbeat", {}};
    for (std::size_t i = 0; i < beats.size(); ++i) {
        const float vel = static_cast<float>(0.55 + 0.45 * std::abs(std::sin(1.7 * static_cast<double>(i))));
        // Slightly late, like real detections.
        const double t = beats[i] + 0.004;
        (i % 2 == 1 ? kick : snare).events.push_back({t, vel});
        full.events.push_back({t, vel});
        beat.events.push_back({beats[i], 1.0f});
        if (i + 1 < beats.size()) {
            const double half = (beats[i] + beats[i + 1]) / 2.0;
            hat.events.push_back({half, vel * 0.6f});
            full.events.push_back({half, vel * 0.6f});
        }
    }
    for (double t : r.beats.downbeats) down.events.push_back({t, 1.0f});
    r.events = {kick, snare, hat, full, beat, down};
    return r;
}

} // namespace ztrip::test
