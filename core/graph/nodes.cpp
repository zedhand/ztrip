// The v1 essential node catalog. See docs/NODES.md for the reference.
#include "ztrip/analysis/offline.hpp"
#include "ztrip/graph/registry.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace ztrip::graph {
namespace {

using C = PortType;

double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }

// Strongest event in the tick's window (t - dt, t], as a Trigger value.
Value events_in_tick(const std::vector<analysis::Trigger>& events, const EvalContext& ctx, double min_velocity = 0.0) {
    const double t0 = static_cast<double>(ctx.step - 1) / ctx.rate_hz;
    const double t1 = ctx.time;
    auto it = std::upper_bound(events.begin(), events.end(), t0,
                               [](double t, const analysis::Trigger& e) { return t < e.time; });
    Value v;
    for (; it != events.end() && it->time <= t1; ++it) {
        if (it->velocity < min_velocity) continue;
        v.fired = true;
        v.v = std::max(v.v, static_cast<double>(it->velocity));
    }
    return v;
}

std::uint64_t splitmix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

// Uniform 0..1 from a seed and an integer index.
double hash01(std::uint64_t seed, std::int64_t index) {
    const std::uint64_t h = splitmix64(seed ^ splitmix64(static_cast<std::uint64_t>(index)));
    return static_cast<double>(h >> 11) * 0x1.0p-53;
}

ParamSpec number(std::string name, double def) { return {std::move(name), ParamKind::Number, def, {}}; }
ParamSpec boolean(std::string name, bool def) { return {std::move(name), ParamKind::Bool, def, {}}; }
ParamSpec text(std::string name, std::string def, std::vector<std::string> choices = {}) {
    return {std::move(name), ParamKind::String, std::move(def), std::move(choices)};
}

template <typename T>
NodeFactory factory() {
    return [](const NodeSetup& s) -> std::unique_ptr<Node> { return std::make_unique<T>(s); };
}

// ---- Sources ---------------------------------------------------------------

class AudioBandNode final : public Node {
public:
    explicit AudioBandNode(const NodeSetup& s)
        : slot_(s.string("source")),
          name_(!s.string("signal").empty() ? s.string("signal")
                                            : (s.boolean("smoothed") ? "env." : "band.") + s.string("band")) {}
    void bind(const SourceSet& sources) override {
        result_ = sources.find(slot_);
        signal_ = result_ ? result_->find_signal(name_) : nullptr;
    }
    void evaluate(const EvalContext& ctx, NodeIO& io) const override {
        io.out[0].v = signal_ ? static_cast<double>(result_->sample(*signal_, ctx.time)) : 0.0;
    }

private:
    std::string slot_, name_;
    const analysis::AnalysisResult* result_ = nullptr;
    const analysis::SignalBuffer* signal_ = nullptr;
};

class AudioOnsetNode final : public Node {
public:
    explicit AudioOnsetNode(const NodeSetup& s) : slot_(s.string("source")), name_("onset." + s.string("band")) {}
    void bind(const SourceSet& sources) override {
        const auto* r = sources.find(slot_);
        events_ = r ? r->find_events(name_) : nullptr;
    }
    void evaluate(const EvalContext& ctx, NodeIO& io) const override {
        if (events_) io.out[0] = events_in_tick(events_->events, ctx, io.numbers[kMinVelocity]);
    }
    static constexpr std::size_t kMinVelocity = 2;

private:
    std::string slot_, name_;
    const analysis::EventList* events_ = nullptr;
};

class BeatClockNode final : public Node {
public:
    explicit BeatClockNode(const NodeSetup& s) : slot_(s.string("source")) {}
    void bind(const SourceSet& sources) override {
        const auto* r = sources.find(slot_);
        grid_ = (r && r->beats.beats.size() >= 2) ? &r->beats : nullptr;
    }
    void evaluate(const EvalContext& ctx, NodeIO& io) const override {
        enum { kClock, kBeat, kBar, kBeatPhase, kBarPhase };
        double per_bar = 4.0;
        double pos = 0.0;
        if (grid_) {
            per_bar = std::max(1, grid_->beats_per_bar);
            pos = position(ctx.time) - grid_->downbeat_phase;
            io.out[kBeat] = in_tick(grid_->beats, ctx);
            io.out[kBar] = in_tick(grid_->downbeats, ctx);
        } else {
            // No beat grid bound: a steady clock at the fallback tempo.
            per_bar = std::max(1.0, std::round(io.numbers[kBeatsPerBarParam]));
            const double bps = std::max(io.numbers[kBpmParam], 1.0) / 60.0;
            pos = ctx.time * bps;
            const double prev = static_cast<double>(ctx.step - 1) / ctx.rate_hz * bps;
            if (std::floor(pos) > std::floor(prev)) io.out[kBeat] = {1.0, true};
            if (std::floor(pos / per_bar) > std::floor(prev / per_bar)) io.out[kBar] = {1.0, true};
        }
        io.out[kClock].v = pos;
        io.out[kBeatPhase].v = pos - std::floor(pos);
        const double bars = pos / per_bar;
        io.out[kBarPhase].v = bars - std::floor(bars);
    }
    static constexpr std::size_t kBpmParam = 1;
    static constexpr std::size_t kBeatsPerBarParam = 2;

private:
    static Value in_tick(const std::vector<double>& times, const EvalContext& ctx) {
        const double t0 = static_cast<double>(ctx.step - 1) / ctx.rate_hz;
        const auto it = std::upper_bound(times.begin(), times.end(), t0);
        if (it != times.end() && *it <= ctx.time) return {1.0, true};
        return {};
    }
    // Beat index at time t, interpolated between detected beats and
    // extrapolated at the first/last interval outside them.
    double position(double t) const {
        const auto& b = grid_->beats;
        const std::size_t n = b.size();
        if (t < b[0]) return (t - b[0]) / (b[1] - b[0]);
        if (t >= b[n - 1]) return static_cast<double>(n - 1) + (t - b[n - 1]) / (b[n - 1] - b[n - 2]);
        const auto k = static_cast<std::size_t>(std::upper_bound(b.begin(), b.end(), t) - b.begin()) - 1;
        return static_cast<double>(k) + (t - b[k]) / (b[k + 1] - b[k]);
    }

    std::string slot_;
    const analysis::BeatGrid* grid_ = nullptr;
};

class ConstantNode final : public Node {
public:
    explicit ConstantNode(const NodeSetup&) {}
    void evaluate(const EvalContext&, NodeIO& io) const override { io.out[0].v = io.numbers[0]; }
};

// ---- Shaping ---------------------------------------------------------------

class SmoothNode final : public Node {
public:
    explicit SmoothNode(const NodeSetup&) {}
    void evaluate(const EvalContext& ctx, NodeIO& io) const override {
        const double in = io.in[0].v;
        double& env = io.state[0];
        const double coeff = (in > env ? io.numbers[1] : io.numbers[2]) / 1000.0;
        env = coeff <= 0.0 ? in : env + (in - env) * (1.0 - std::exp(-ctx.dt / coeff));
        io.out[0].v = env;
    }
};

class ThresholdNode final : public Node {
public:
    explicit ThresholdNode(const NodeSetup&) {}
    void evaluate(const EvalContext&, NodeIO& io) const override {
        const double in = io.in[0].v;
        const double half = std::max(io.numbers[2], 0.0) / 2.0;
        double& on = io.state[0];
        if (on == 0.0 && in >= io.numbers[1] + half) {
            on = 1.0;
            io.out[1] = {clamp01(in), true};
        } else if (on != 0.0 && in < io.numbers[1] - half) {
            on = 0.0;
            io.out[2] = {1.0, true};
        }
        io.out[0].v = on;
    }
};

class RemapRangeNode final : public Node {
public:
    explicit RemapRangeNode(const NodeSetup& s) : clamp_(s.boolean("clamp")) {}
    void evaluate(const EvalContext&, NodeIO& io) const override {
        const double in_min = io.numbers[1], in_max = io.numbers[2];
        const double out_min = io.numbers[3], out_max = io.numbers[4];
        double u = in_max != in_min ? (io.in[0].v - in_min) / (in_max - in_min) : 0.0;
        if (clamp_) u = clamp01(u);
        io.out[0].v = out_min + (out_max - out_min) * u;
    }

private:
    bool clamp_;
};

class ResponseCurveNode final : public Node {
public:
    enum class Curve { Linear, EaseIn, EaseOut, EaseInOut, Smoothstep, Exponential, Logarithmic, Custom };
    static inline const std::vector<std::string> kNames = {"linear",      "ease_in",     "ease_out",
                                                           "ease_in_out", "smoothstep",  "exponential",
                                                           "logarithmic", "custom"};

    explicit ResponseCurveNode(const NodeSetup& s) {
        const auto& name = s.string("curve");
        curve_ = static_cast<Curve>(std::find(kNames.begin(), kNames.end(), name) - kNames.begin());
        if (curve_ == Curve::Custom) {
            points_ = s.list("points");
            if (points_.size() < 4 || points_.size() % 2 != 0) {
                throw std::invalid_argument("custom curve needs 'points' as x0, y0, x1, y1, ... with at least two points");
            }
            for (std::size_t i = 2; i < points_.size(); i += 2) {
                if (!(points_[i] > points_[i - 2])) throw std::invalid_argument("custom curve x values must increase");
            }
        }
    }
    void evaluate(const EvalContext&, NodeIO& io) const override {
        const double x = clamp01(io.in[0].v);
        const double a = std::max(io.numbers[2], 1e-3);
        double y = x;
        switch (curve_) {
        case Curve::Linear: break;
        case Curve::EaseIn: y = std::pow(x, a); break;
        case Curve::EaseOut: y = 1.0 - std::pow(1.0 - x, a); break;
        case Curve::EaseInOut:
            y = x < 0.5 ? 0.5 * std::pow(2.0 * x, a) : 1.0 - 0.5 * std::pow(2.0 - 2.0 * x, a);
            break;
        case Curve::Smoothstep: y = x * x * (3.0 - 2.0 * x); break;
        case Curve::Exponential: y = std::expm1(a * x) / std::expm1(a); break;
        case Curve::Logarithmic: {
            const double k = std::expm1(a);
            y = std::log1p(k * x) / std::log1p(k);
            break;
        }
        case Curve::Custom: y = custom(x); break;
        }
        io.out[0].v = y;
    }

private:
    double custom(double x) const {
        const std::size_t n = points_.size() / 2;
        if (x <= points_[0]) return points_[1];
        for (std::size_t i = 1; i < n; ++i) {
            const double x1 = points_[2 * i];
            if (x <= x1) {
                const double x0 = points_[2 * i - 2], y0 = points_[2 * i - 1], y1 = points_[2 * i + 1];
                return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
            }
        }
        return points_.back();
    }

    Curve curve_ = Curve::Linear;
    std::vector<double> points_;
};

class ClampNode final : public Node {
public:
    explicit ClampNode(const NodeSetup&) {}
    void evaluate(const EvalContext&, NodeIO& io) const override {
        const double lo = std::min(io.numbers[1], io.numbers[2]);
        const double hi = std::max(io.numbers[1], io.numbers[2]);
        io.out[0].v = std::clamp(io.in[0].v, lo, hi);
    }
};

// ---- Triggers --------------------------------------------------------------

class AdsrNode final : public Node {
public:
    explicit AdsrNode(const NodeSetup& s) : gated_(s.input_connected[1]), velocity_(s.boolean("velocity")) {}
    void evaluate(const EvalContext& ctx, NodeIO& io) const override {
        enum Stage { Idle, Attack, Decay, Sustain, Release };
        int stage = static_cast<int>(io.state[0]);
        double& level = io.state[1];
        double& time = io.state[2];
        double& peak = io.state[3];
        double& from = io.state[4]; // level the current attack or release started at
        const double attack = io.numbers[2] / 1000.0, decay = io.numbers[3] / 1000.0;
        const double sustain = clamp01(io.numbers[4]), release = io.numbers[5] / 1000.0;
        const double hold = io.numbers[6] / 1000.0;
        const bool gate_low = gated_ && io.in[1].v < 0.5;

        if (io.in[0].fired) {
            stage = Attack;
            time = 0.0;
            from = level;
            peak = velocity_ ? io.in[0].v : 1.0;
        } else if (gate_low && stage != Idle && stage != Release) {
            stage = Release;
            time = 0.0;
            from = level;
        }
        time += ctx.dt;
        // Times accumulate tick by tick, so allow for rounding at stage ends.
        auto done = [&](double length) { return time >= length - 1e-9; };
        auto progress = [&](double length) { return done(length) ? 1.0 : time / length; };
        switch (stage) {
        case Attack:
            level = from + (peak - from) * progress(attack);
            if (done(attack)) stage = Decay, time = 0.0;
            break;
        case Decay:
            level = peak + (sustain * peak - peak) * progress(decay);
            if (done(decay)) stage = Sustain, time = 0.0;
            break;
        case Sustain:
            level = sustain * peak;
            if (gated_ ? gate_low : done(hold)) stage = Release, time = 0.0, from = level;
            break;
        case Release:
            level = from * (1.0 - progress(release));
            if (done(release)) stage = Idle, level = 0.0;
            break;
        default: level = 0.0; break;
        }
        io.state[0] = stage;
        io.out[0].v = level;
    }

private:
    bool gated_;
    bool velocity_;
};

class ImpulseDecayNode final : public Node {
public:
    explicit ImpulseDecayNode(const NodeSetup& s) : velocity_(s.boolean("velocity")) {}
    void evaluate(const EvalContext& ctx, NodeIO& io) const override {
        double& level = io.state[0];
        const double tau = io.numbers[1] / 1000.0;
        level = tau > 0.0 ? level * std::exp(-ctx.dt / tau) : 0.0;
        if (io.in[0].fired) level = std::max(level, velocity_ ? io.in[0].v : 1.0);
        io.out[0].v = level;
    }

private:
    bool velocity_;
};

// ---- Time ------------------------------------------------------------------

class BeatLfoNode final : public Node {
public:
    enum class Shape { Sine, Triangle, Saw, Ramp, Square, Random };
    static inline const std::vector<std::string> kNames = {"sine", "triangle", "saw", "ramp", "square", "random"};

    explicit BeatLfoNode(const NodeSetup& s) {
        shape_ = static_cast<Shape>(std::find(kNames.begin(), kNames.end(), s.string("shape")) - kNames.begin());
        const double seed = s.number("seed");
        seed_ = seed != 0.0 ? static_cast<std::uint64_t>(std::llround(seed)) : s.seed;
    }
    void evaluate(const EvalContext&, NodeIO& io) const override {
        const double cycles = io.in[0].v / std::max(io.numbers[1], 1e-6) + io.numbers[2];
        const double phase = cycles - std::floor(cycles);
        double y = 0.0;
        switch (shape_) {
        case Shape::Sine: y = 0.5 + 0.5 * std::cos(2.0 * std::numbers::pi * phase); break;
        case Shape::Triangle: y = std::abs(1.0 - 2.0 * phase); break;
        case Shape::Saw: y = 1.0 - phase; break;
        case Shape::Ramp: y = phase; break;
        case Shape::Square: y = phase < io.numbers[3] ? 1.0 : 0.0; break;
        case Shape::Random: y = hash01(seed_, static_cast<std::int64_t>(std::floor(cycles))); break;
        }
        io.out[0].v = y;
    }

private:
    Shape shape_ = Shape::Sine;
    std::uint64_t seed_ = 0;
};

// ---- Math ------------------------------------------------------------------

class AddNode final : public Node {
public:
    explicit AddNode(const NodeSetup&) {}
    void evaluate(const EvalContext&, NodeIO& io) const override { io.out[0].v = io.in[0].v + io.in[1].v; }
};

class MultiplyNode final : public Node {
public:
    explicit MultiplyNode(const NodeSetup&) {}
    void evaluate(const EvalContext&, NodeIO& io) const override { io.out[0].v = io.in[0].v * io.in[1].v; }
};

// ---- Targets ---------------------------------------------------------------

class PropertyOutNode final : public Node {
public:
    explicit PropertyOutNode(const NodeSetup&) {}
    void evaluate(const EvalContext&, NodeIO&) const override {} // the evaluator reads its input
};

} // namespace

void register_builtin_nodes(NodeRegistry& r) {
    // Param order matters: nodes read number params by spec index. Continuous
    // inputs get their number param appended by the registry when not listed.
    r.add({"AudioBand", "Sources", "Band level or envelope of an analysed audio source.",
           {}, {{"out", C::Continuous}},
           {text("source", "main"), text("band", "Bass"), boolean("smoothed", true), text("signal", "")},
           0, false, factory<AudioBandNode>()});
    r.add({"AudioOnset", "Sources", "Fires on onsets detected in one band (or \"full\") of an audio source.",
           {}, {{"trigger", C::Trigger}},
           {text("source", "main"), text("band", "full"), number("min_velocity", 0.0)},
           0, false, factory<AudioOnsetNode>()});
    r.add({"BeatClock", "Sources", "Musical position from a source's beat grid, with beat and bar triggers.",
           {}, {{"clock", C::Clock}, {"beat", C::Trigger}, {"bar", C::Trigger}, {"beat_phase", C::Continuous},
                {"bar_phase", C::Continuous}},
           {text("source", "main"), number("bpm", 120.0), number("beats_per_bar", 4.0)},
           0, false, factory<BeatClockNode>()});
    r.add({"Constant", "Sources", "A fixed value.", {}, {{"out", C::Continuous}}, {number("value", 0.0)},
           0, false, factory<ConstantNode>()});

    r.add({"Smooth", "Shaping", "Attack/release envelope follower.", {{"in", C::Continuous}}, {{"out", C::Continuous}},
           {number("in", 0.0), number("attack_ms", 10.0), number("release_ms", 200.0)},
           1, false, factory<SmoothNode>()});
    r.add({"Threshold", "Shaping", "Gate that opens above a threshold, with hysteresis and edge triggers.",
           {{"in", C::Continuous}}, {{"gate", C::Continuous}, {"rise", C::Trigger}, {"fall", C::Trigger}},
           {number("in", 0.0), number("threshold", 0.5), number("hysteresis", 0.05)},
           1, false, factory<ThresholdNode>()});
    r.add({"RemapRange", "Shaping", "Maps [in_min, in_max] onto [out_min, out_max].",
           {{"in", C::Continuous}}, {{"out", C::Continuous}},
           {number("in", 0.0), number("in_min", 0.0), number("in_max", 1.0), number("out_min", 0.0),
            number("out_max", 1.0), boolean("clamp", true)},
           0, false, factory<RemapRangeNode>()});
    r.add({"ResponseCurve", "Shaping", "Reshapes a 0-1 value with a preset or custom curve.",
           {{"in", C::Continuous}}, {{"out", C::Continuous}},
           {number("in", 0.0), text("curve", "linear", ResponseCurveNode::kNames), number("amount", 2.0),
            {"points", ParamKind::List, std::vector<double>{0.0, 0.0, 1.0, 1.0}, {}}},
           0, false, factory<ResponseCurveNode>()});
    r.add({"Clamp", "Shaping", "Limits a value to [min, max].", {{"in", C::Continuous}}, {{"out", C::Continuous}},
           {number("in", 0.0), number("min", 0.0), number("max", 1.0)}, 0, false, factory<ClampNode>()});

    r.add({"ADSR", "Triggers", "Attack/decay/sustain/release envelope started by a trigger.",
           {{"trigger", C::Trigger}, {"gate", C::Continuous, 1.0}}, {{"out", C::Continuous}},
           {number("gate", 1.0), boolean("velocity", true), number("attack_ms", 5.0), number("decay_ms", 150.0),
            number("sustain", 0.6), number("release_ms", 300.0), number("hold_ms", 0.0)},
           5, false, factory<AdsrNode>()});
    r.add({"ImpulseDecay", "Triggers", "Jumps to the trigger velocity, then decays exponentially.",
           {{"trigger", C::Trigger}}, {{"out", C::Continuous}},
           {boolean("velocity", true), number("decay_ms", 250.0)}, 1, false, factory<ImpulseDecayNode>()});

    r.add({"BeatLFO", "Time", "Periodic shape locked to a clock, `beats` per cycle.",
           {{"clock", C::Clock}}, {{"out", C::Continuous}},
           {text("shape", "sine", BeatLfoNode::kNames), number("beats", 1.0), number("offset", 0.0),
            number("width", 0.5), number("seed", 0.0)},
           0, false, factory<BeatLfoNode>()});

    r.add({"Add", "Math", "a + b.", {{"a", C::Continuous}, {"b", C::Continuous}}, {{"out", C::Continuous}},
           {number("a", 0.0), number("b", 0.0)}, 0, false, factory<AddNode>()});
    r.add({"Multiply", "Math", "a * b.", {{"a", C::Continuous, 1.0}, {"b", C::Continuous, 1.0}},
           {{"out", C::Continuous}}, {number("a", 1.0), number("b", 1.0)}, 0, false, factory<MultiplyNode>()});

    r.add({"PropertyOut", "Targets", "Sends its input to a host property path.", {{"in", C::Continuous}}, {},
           {number("in", 0.0), text("target", "")}, 0, true, factory<PropertyOutNode>()});
}

} // namespace ztrip::graph
