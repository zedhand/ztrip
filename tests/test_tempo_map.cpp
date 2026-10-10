#include "ztrip/analysis/beats.hpp"
#include "ztrip/time/tempo_map.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <stdexcept>

using namespace ztrip;
using Catch::Approx;

namespace {

// Seconds from beat a to beat b by summing 60 / bpm in tiny steps.
double numeric_seconds(const time::TempoMap& m, double a, double b, int steps = 200000) {
    const double h = (b - a) / steps;
    double t = 0.0;
    for (int i = 0; i < steps; ++i) t += 60.0 / m.bpm_at(a + (i + 0.5) * h) * h;
    return t;
}

} // namespace

TEST_CASE("constant tempo converts beats and seconds both ways") {
    time::TempoMap m(120.0, 0.5);
    CHECK(m.seconds_at(0.0) == Approx(0.5));
    CHECK(m.seconds_at(4.0) == Approx(2.5));
    CHECK(m.seconds_at(-1.0) == Approx(0.0));
    CHECK(m.beat_at(2.5) == Approx(4.0));
    CHECK(m.beat_at(0.0) == Approx(-1.0));
    CHECK(m.bpm_at(100.0) == Approx(120.0));
}

TEST_CASE("offset and nudge move beat 0 without changing tempo") {
    time::TempoMap m(100.0, 1.0);
    m.nudge_by(0.015);
    m.nudge_by(-0.005);
    CHECK(m.nudge() == Approx(0.010));
    CHECK(m.seconds_at(0.0) == Approx(1.010));
    CHECK(m.seconds_at(10.0) - m.seconds_at(0.0) == Approx(6.0));
    m.set_offset(2.0);
    CHECK(m.seconds_at(0.0) == Approx(2.010));
    CHECK_THROWS_AS(m.set_nudge(std::nan("")), std::invalid_argument);
}

TEST_CASE("step segments change tempo at their start beat") {
    time::TempoMap m;
    m.set_segments({{0.0, 120.0, 120.0, time::TempoCurve::Step}, {8.0, 60.0, 60.0, time::TempoCurve::Step}});
    CHECK(m.seconds_at(8.0) == Approx(4.0));
    CHECK(m.seconds_at(10.0) == Approx(6.0));
    CHECK(m.beat_at(6.0) == Approx(10.0));
    CHECK(m.bpm_at(7.99) == Approx(120.0));
    CHECK(m.bpm_at(8.0) == Approx(60.0));
}

TEST_CASE("linear and exponential ramps match numeric integration and invert") {
    for (auto curve : {time::TempoCurve::Linear, time::TempoCurve::Exponential}) {
        time::TempoMap m;
        m.set_segments({{-4.0, 90.0, 90.0, time::TempoCurve::Step},
                        {0.0, 90.0, 150.0, curve},
                        {32.0, 150.0, 80.0, curve},
                        {48.0, 80.0, 80.0, time::TempoCurve::Step}});
        INFO(time::to_string(curve));
        CHECK(m.bpm_at(0.0) == Approx(90.0));
        CHECK(m.bpm_at(32.0) == Approx(150.0));
        CHECK(m.bpm_at(47.999) == Approx(80.0).epsilon(1e-3));
        CHECK(m.bpm_at(16.0) > 90.0);
        CHECK(m.bpm_at(16.0) < 150.0);
        CHECK(m.seconds_at(32.0) - m.seconds_at(0.0) == Approx(numeric_seconds(m, 0.0, 32.0)).epsilon(1e-7));
        CHECK(m.seconds_at(48.0) - m.seconds_at(32.0) == Approx(numeric_seconds(m, 32.0, 48.0)).epsilon(1e-7));
        for (double beat = -10.0; beat < 70.0; beat += 0.37) {
            CHECK(m.beat_at(m.seconds_at(beat)) == Approx(beat).margin(1e-9));
        }
        // Monotonic.
        double prev = m.seconds_at(-10.0);
        for (double beat = -9.9; beat < 70.0; beat += 0.1) {
            const double t = m.seconds_at(beat);
            CHECK(t > prev);
            prev = t;
        }
    }
}

TEST_CASE("a ramp in the last segment holds its start tempo") {
    time::TempoMap m;
    m.set_segments({{0.0, 100.0, 200.0, time::TempoCurve::Linear}});
    CHECK(m.bpm_at(50.0) == Approx(100.0));
    CHECK(m.seconds_at(10.0) == Approx(6.0));
}

TEST_CASE("scale_tempo keeps beat 0 in place") {
    time::TempoMap m(120.0, 3.0);
    m.set_segments({{0.0, 120.0, 120.0, time::TempoCurve::Step}, {4.0, 140.0, 160.0, time::TempoCurve::Linear},
                    {8.0, 160.0, 160.0, time::TempoCurve::Step}});
    const double before = m.seconds_at(8.0) - m.seconds_at(0.0);
    m.scale_tempo(1.01);
    CHECK(m.seconds_at(0.0) == Approx(3.0));
    CHECK(m.seconds_at(8.0) - m.seconds_at(0.0) == Approx(before / 1.01));
}

TEST_CASE("invalid segments and meters are rejected") {
    time::TempoMap m;
    CHECK_THROWS_AS(m.set_segments({}), std::invalid_argument);
    CHECK_THROWS_AS(m.set_segments({{0.0, 0.0, 120.0, time::TempoCurve::Step}}), std::invalid_argument);
    CHECK_THROWS_AS(m.set_segments({{0.0, 120.0, 120.0, time::TempoCurve::Step},
                                    {0.0, 130.0, 130.0, time::TempoCurve::Step}}),
                    std::invalid_argument);
    CHECK_THROWS_AS(m.set_time_signatures({{0, 4, 3}}), std::invalid_argument);
    CHECK_THROWS_AS(m.set_time_signatures({{-1, 4, 4}}), std::invalid_argument);
    CHECK_THROWS_AS(m.set_time_signatures({{0, 0, 4}}), std::invalid_argument);
}

TEST_CASE("time signature changes at bar boundaries drive bar positions") {
    time::TempoMap m;
    m.set_time_signatures({{4, 3, 4}, {6, 6, 8}});
    REQUIRE(m.time_signatures().size() == 3); // 4/4 at bar 0 added
    CHECK(m.bar_start_beat(0) == Approx(0.0));
    CHECK(m.bar_start_beat(4) == Approx(16.0));
    CHECK(m.bar_start_beat(5) == Approx(19.0));
    CHECK(m.bar_start_beat(6) == Approx(22.0));
    CHECK(m.bar_start_beat(7) == Approx(28.0));
    CHECK(m.bar_start_beat(-1) == Approx(-4.0));

    auto p = m.position_at(17.5);
    CHECK(p.bar == 4);
    CHECK(p.beat == 1);
    CHECK(p.beat_phase == Approx(0.5));
    CHECK(p.bar_phase == Approx(1.5 / 3.0));
    CHECK(p.numerator == 3);

    p = m.position_at(23.0);
    CHECK(p.bar == 6);
    CHECK(p.beat == 1);
    CHECK(p.denominator == 8);

    p = m.position_at(-0.5); // pickup
    CHECK(p.bar == -1);
    CHECK(p.beat == 3);
    CHECK(p.beat_phase == Approx(0.5));
}

TEST_CASE("tempo_map_from_beats passes through detected beats") {
    analysis::BeatGrid grid;
    grid.beats_per_bar = 3;
    grid.downbeat_phase = 2;
    // Steady 120 BPM for 20 beats, then drifting slower beat by beat.
    double t = 0.31;
    for (int i = 0; i < 20; ++i, t += 0.5) grid.beats.push_back(t);
    double ibi = 0.5;
    for (int i = 0; i < 30; ++i) {
        ibi += 0.004;
        t += ibi;
        grid.beats.push_back(t);
    }
    const auto m = time::tempo_map_from_beats(grid, {0.002});
    CHECK(m.offset() == Approx(grid.beats[2]));
    CHECK(m.time_signatures().front().numerator == 3);
    for (std::size_t k = 0; k < grid.beats.size(); ++k) {
        const double beat = static_cast<double>(k) - 2.0;
        CHECK(m.seconds_at(beat) == Approx(grid.beats[k]).margin(0.002));
    }
    // The steady run collapses into very few segments.
    CHECK(m.segments().size() < 25);
    CHECK(m.bpm_at(5.0) == Approx(120.0));
    CHECK(m.bpm_at(45.0) < 100.0);
}

TEST_CASE("tempo_map_from_beats on a tiny grid falls back to 120 BPM") {
    analysis::BeatGrid grid;
    grid.beats = {1.0};
    const auto m = time::tempo_map_from_beats(grid);
    CHECK(m.bpm_at(0.0) == Approx(120.0));
    CHECK(m.offset() == Approx(0.0));
}
