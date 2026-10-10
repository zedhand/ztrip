#include "ztrip/time/tempo_map.hpp"

#include "ztrip/analysis/beats.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ztrip::time {
namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

// Below this per-beat change a ramp is treated as constant, which keeps the
// closed forms away from 0/0.
constexpr double kFlat = 1e-12;

struct Ramp {
    double b0 = 120.0; // BPM at the segment start
    double k = 0.0;    // Linear: BPM change per beat
    double r = 0.0;    // Exponential: log BPM change per beat
    TempoCurve curve = TempoCurve::Step;
};

Ramp ramp_of(const TempoSegment& s, double length) {
    Ramp p{s.bpm_start, 0.0, 0.0, TempoCurve::Step};
    if (!std::isfinite(length) || length <= 0.0 || s.curve == TempoCurve::Step) return p;
    if (s.curve == TempoCurve::Linear) {
        p.k = (s.bpm_end - s.bpm_start) / length;
        if (std::abs(p.k) > kFlat) p.curve = TempoCurve::Linear;
    } else {
        p.r = std::log(s.bpm_end / s.bpm_start) / length;
        if (std::abs(p.r) > kFlat) p.curve = TempoCurve::Exponential;
    }
    return p;
}

// Seconds from the segment start to x beats into it.
double ramp_seconds(const Ramp& p, double x) {
    switch (p.curve) {
    case TempoCurve::Linear: return 60.0 / p.k * std::log1p(p.k * x / p.b0);
    case TempoCurve::Exponential: return -60.0 / (p.b0 * p.r) * std::expm1(-p.r * x);
    case TempoCurve::Step: break;
    }
    return 60.0 * x / p.b0;
}

// Beats into the segment after t seconds (the inverse of ramp_seconds).
double ramp_beats(const Ramp& p, double t) {
    switch (p.curve) {
    case TempoCurve::Linear: return p.b0 * std::expm1(p.k * t / 60.0) / p.k;
    case TempoCurve::Exponential: return -std::log1p(std::max(-t * p.b0 * p.r / 60.0, -1.0 + 1e-15)) / p.r;
    case TempoCurve::Step: break;
    }
    return t * p.b0 / 60.0;
}

double ramp_bpm(const Ramp& p, double x) {
    switch (p.curve) {
    case TempoCurve::Linear: return p.b0 + p.k * x;
    case TempoCurve::Exponential: return p.b0 * std::exp(p.r * x);
    case TempoCurve::Step: break;
    }
    return p.b0;
}

void check_bpm(double bpm) {
    if (!std::isfinite(bpm) || bpm <= 0.0) throw std::invalid_argument("tempo map: BPM must be positive and finite");
}

} // namespace

std::string_view to_string(TempoCurve curve) noexcept {
    switch (curve) {
    case TempoCurve::Step: return "step";
    case TempoCurve::Linear: return "linear";
    case TempoCurve::Exponential: return "exponential";
    }
    return "step";
}

bool parse_tempo_curve(std::string_view name, TempoCurve& out) noexcept {
    for (TempoCurve c : {TempoCurve::Step, TempoCurve::Linear, TempoCurve::Exponential}) {
        if (to_string(c) == name) {
            out = c;
            return true;
        }
    }
    return false;
}

TempoMap::TempoMap(double bpm, double offset_s) {
    set_segments({{0.0, bpm, bpm, TempoCurve::Step}});
    set_time_signatures({});
    set_offset(offset_s);
}

void TempoMap::set_segments(std::vector<TempoSegment> segments) {
    if (segments.empty()) throw std::invalid_argument("tempo map: needs at least one segment");
    for (const TempoSegment& s : segments) {
        if (!std::isfinite(s.start_beat)) throw std::invalid_argument("tempo map: start_beat must be finite");
        check_bpm(s.bpm_start);
        check_bpm(s.bpm_end);
    }
    std::stable_sort(segments.begin(), segments.end(),
                     [](const TempoSegment& a, const TempoSegment& b) { return a.start_beat < b.start_beat; });
    for (std::size_t i = 1; i < segments.size(); ++i) {
        if (segments[i].start_beat == segments[i - 1].start_beat) {
            throw std::invalid_argument("tempo map: two segments start on the same beat");
        }
    }
    segments_ = std::move(segments);
    rebuild();
}

void TempoMap::set_time_signatures(std::vector<TimeSignature> signatures) {
    for (const TimeSignature& t : signatures) {
        if (t.bar < 0) throw std::invalid_argument("tempo map: time signature bars must be >= 0");
        if (t.numerator < 1 || t.numerator > 64) throw std::invalid_argument("tempo map: numerator must be 1..64");
        if (t.denominator < 1 || t.denominator > 64 || (t.denominator & (t.denominator - 1)) != 0) {
            throw std::invalid_argument("tempo map: denominator must be a power of two up to 64");
        }
    }
    std::stable_sort(signatures.begin(), signatures.end(),
                     [](const TimeSignature& a, const TimeSignature& b) { return a.bar < b.bar; });
    for (std::size_t i = 1; i < signatures.size(); ++i) {
        if (signatures[i].bar == signatures[i - 1].bar) {
            throw std::invalid_argument("tempo map: two time signatures on the same bar");
        }
    }
    if (signatures.empty() || signatures.front().bar != 0) signatures.insert(signatures.begin(), TimeSignature{});
    signatures_ = std::move(signatures);
    signature_beats_.assign(signatures_.size(), 0.0);
    for (std::size_t i = 1; i < signatures_.size(); ++i) {
        signature_beats_[i] = signature_beats_[i - 1] +
                              static_cast<double>(signatures_[i].bar - signatures_[i - 1].bar) *
                                  signatures_[i - 1].numerator;
    }
}

void TempoMap::set_offset(double seconds) {
    if (!std::isfinite(seconds)) throw std::invalid_argument("tempo map: offset must be finite");
    offset_s_ = seconds;
}

void TempoMap::set_nudge(double seconds) {
    if (!std::isfinite(seconds)) throw std::invalid_argument("tempo map: nudge must be finite");
    nudge_s_ = seconds;
}

void TempoMap::scale_tempo(double factor) {
    check_bpm(factor);
    auto segments = segments_;
    for (TempoSegment& s : segments) {
        s.bpm_start *= factor;
        s.bpm_end *= factor;
    }
    set_segments(std::move(segments));
}

void TempoMap::rebuild() {
    const std::size_t n = segments_.size();
    segment_seconds_.assign(n, 0.0);
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const double length = segments_[i + 1].start_beat - segments_[i].start_beat;
        segment_seconds_[i + 1] = segment_seconds_[i] + ramp_seconds(ramp_of(segments_[i], length), length);
    }
    // Re-anchor so that beat 0 is at 0 s.
    const double zero = integrate(0.0);
    for (double& s : segment_seconds_) s -= zero;
}

double TempoMap::integrate(double beat) const noexcept {
    const TempoSegment& first = segments_.front();
    if (beat < first.start_beat) return segment_seconds_.front() + 60.0 * (beat - first.start_beat) / first.bpm_start;
    const auto it = std::upper_bound(segments_.begin(), segments_.end(), beat,
                                     [](double b, const TempoSegment& s) { return b < s.start_beat; });
    const auto i = static_cast<std::size_t>(it - segments_.begin()) - 1;
    const double length = i + 1 < segments_.size() ? segments_[i + 1].start_beat - segments_[i].start_beat : kInf;
    return segment_seconds_[i] + ramp_seconds(ramp_of(segments_[i], length), beat - segments_[i].start_beat);
}

double TempoMap::seconds_at(double beat) const noexcept { return offset_s_ + nudge_s_ + integrate(beat); }

double TempoMap::beat_at(double seconds) const noexcept {
    const double rel = seconds - offset_s_ - nudge_s_;
    const TempoSegment& first = segments_.front();
    if (rel < segment_seconds_.front()) return first.start_beat + (rel - segment_seconds_.front()) * first.bpm_start / 60.0;
    const auto it = std::upper_bound(segment_seconds_.begin(), segment_seconds_.end(), rel);
    const auto i = static_cast<std::size_t>(it - segment_seconds_.begin()) - 1;
    const double length = i + 1 < segments_.size() ? segments_[i + 1].start_beat - segments_[i].start_beat : kInf;
    const double x = ramp_beats(ramp_of(segments_[i], length), rel - segment_seconds_[i]);
    // Keep rounding from crossing into the next segment.
    return segments_[i].start_beat + (std::isfinite(length) ? std::min(x, length) : x);
}

double TempoMap::bpm_at(double beat) const noexcept {
    const TempoSegment& first = segments_.front();
    if (beat < first.start_beat) return first.bpm_start;
    const auto it = std::upper_bound(segments_.begin(), segments_.end(), beat,
                                     [](double b, const TempoSegment& s) { return b < s.start_beat; });
    const auto i = static_cast<std::size_t>(it - segments_.begin()) - 1;
    const double length = i + 1 < segments_.size() ? segments_[i + 1].start_beat - segments_[i].start_beat : kInf;
    return ramp_bpm(ramp_of(segments_[i], length), beat - segments_[i].start_beat);
}

const TimeSignature& TempoMap::time_signature_at(double beat) const noexcept {
    const auto it = std::upper_bound(signature_beats_.begin(), signature_beats_.end(), beat);
    const auto i = it == signature_beats_.begin() ? 0 : static_cast<std::size_t>(it - signature_beats_.begin()) - 1;
    return signatures_[i];
}

double TempoMap::bar_start_beat(int bar) const noexcept {
    std::size_t i = 0;
    while (i + 1 < signatures_.size() && signatures_[i + 1].bar <= bar) ++i;
    return signature_beats_[i] + static_cast<double>(bar - signatures_[i].bar) * signatures_[i].numerator;
}

MusicalPosition TempoMap::position_at(double beat) const noexcept {
    const auto it = std::upper_bound(signature_beats_.begin(), signature_beats_.end(), beat);
    const auto i = it == signature_beats_.begin() ? 0 : static_cast<std::size_t>(it - signature_beats_.begin()) - 1;
    const TimeSignature& sig = signatures_[i];
    const double num = sig.numerator;
    const double rel = beat - signature_beats_[i];
    const double bars = std::floor(rel / num);
    const double within = std::clamp(rel - bars * num, 0.0, std::nextafter(num, 0.0));
    MusicalPosition p;
    p.bar = sig.bar + static_cast<int>(bars);
    p.beat = static_cast<int>(std::floor(within));
    p.beat_phase = within - std::floor(within);
    p.bar_phase = within / num;
    p.numerator = sig.numerator;
    p.denominator = sig.denominator;
    return p;
}

TempoMap tempo_map_from_beats(const analysis::BeatGrid& grid, const TempoFitSettings& settings) {
    const std::vector<double>& b = grid.beats;
    const std::size_t n = b.size();
    if (n < 2) return TempoMap();
    const std::size_t phase = std::min(static_cast<std::size_t>(std::max(grid.downbeat_phase, 0)), n - 1);
    const double tol = std::max(settings.tolerance_s, 0.0);

    // Every beat strictly inside (i, j) within tol of the line from b[i] to b[j].
    auto fits = [&](std::size_t i, std::size_t j) {
        if (!(b[j] > b[i])) return false;
        const double per_beat = (b[j] - b[i]) / static_cast<double>(j - i);
        for (std::size_t k = i + 1; k < j; ++k) {
            if (std::abs(b[i] + per_beat * static_cast<double>(k - i) - b[k]) > tol) return false;
        }
        return true;
    };

    std::vector<TempoSegment> segments;
    std::size_t i = 0;
    while (i + 1 < n) {
        std::size_t j = i + 1;
        // Extend the run, but end one at the downbeat so beat 0 is a knot.
        while (j + 1 < n && j != phase && fits(i, j + 1)) ++j;
        const double span = b[j] - b[i];
        if (span > 0.0) {
            const double bpm = 60.0 * static_cast<double>(j - i) / span;
            segments.push_back({static_cast<double>(i) - static_cast<double>(phase), bpm, bpm, TempoCurve::Step});
        }
        i = j;
    }
    if (segments.empty()) return TempoMap();

    TempoMap map(segments.front().bpm_start, b[phase]);
    map.set_segments(std::move(segments));
    map.set_time_signatures({{0, std::max(1, std::min(grid.beats_per_bar, 64)), 4}});
    return map;
}

} // namespace ztrip::time
