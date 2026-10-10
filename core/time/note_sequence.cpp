#include "ztrip/time/note_sequence.hpp"

#include "ztrip/time/tempo_map.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace ztrip::time {
namespace {

template <typename T>
auto find_named(T& items, std::string_view name) -> decltype(items.data()) {
    for (auto& item : items) {
        if (item.name == name) return &item;
    }
    return nullptr;
}

// Held level of an ADSR envelope x beats into a note, before velocity.
double adsr_held(const Envelope& e, double x) {
    const double sustain = std::clamp(e.sustain, 0.0, 1.0);
    if (x < e.attack_beats) return e.attack_beats > 0.0 ? x / e.attack_beats : 1.0;
    x -= std::max(e.attack_beats, 0.0);
    if (x < e.decay_beats) return 1.0 + (sustain - 1.0) * x / e.decay_beats;
    return sustain;
}

double curve_at(const std::vector<double>& p, double u) {
    if (p.size() < 2) return 1.0;
    if (u <= p[0]) return p[1];
    for (std::size_t i = 2; i + 1 < p.size(); i += 2) {
        if (u <= p[i]) {
            const double x0 = p[i - 2], y0 = p[i - 1], x1 = p[i], y1 = p[i + 1];
            return x1 > x0 ? y0 + (y1 - y0) * (u - x0) / (x1 - x0) : y1;
        }
    }
    return p[p.size() - 1];
}

} // namespace

void Lane::sort() {
    std::stable_sort(notes.begin(), notes.end(),
                     [](const Note& a, const Note& b) { return a.start_beat < b.start_beat; });
}

double Lane::swung(double beat) const noexcept {
    const double s = std::clamp(swing, 0.0, 0.75);
    const double d = swing_division;
    if (s == 0.0 || !(d > 0.0)) return beat;
    const double pair = 2.0 * d;
    const double base = std::floor(beat / pair) * pair;
    const double x = beat - base;
    return base + (x < d ? x * (1.0 + s) : d * (1.0 + s) + (x - d) * (1.0 - s));
}

Lane* NoteSequence::find_lane(std::string_view lane_name) noexcept { return find_named(lanes, lane_name); }
const Lane* NoteSequence::find_lane(std::string_view lane_name) const noexcept {
    return find_named(lanes, lane_name);
}
const Envelope* NoteSequence::find_envelope(std::string_view envelope_name) const noexcept {
    return envelope_name.empty() ? nullptr : find_named(envelopes, envelope_name);
}

double envelope_level(const Envelope* e, const Note& note, double x) noexcept {
    const double velocity = std::clamp(static_cast<double>(note.velocity), 0.0, 1.0);
    const double length = std::max(note.length_beats, 0.0);
    if (x < 0.0) return 0.0;
    if (!e) return x < length ? velocity : 0.0;
    if (e->kind == Envelope::Kind::Curve) {
        if (x >= length || length <= 0.0) return 0.0;
        return velocity * curve_at(e->points, x / length);
    }
    if (x < length) return velocity * adsr_held(*e, x);
    const double since = x - length;
    if (!(e->release_beats > 0.0) || since >= e->release_beats) return 0.0;
    return velocity * adsr_held(*e, length) * (1.0 - since / e->release_beats);
}

std::optional<GridDivision> parse_grid(std::string_view text) noexcept {
    GridDivision g;
    if (!text.empty() && (text.back() == 'T' || text.back() == 't')) {
        g.triplet = true;
        text.remove_suffix(1);
    }
    if (text.substr(0, 2) != "1/") return std::nullopt;
    text.remove_prefix(2);
    int d = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), d);
    if (ec != std::errc() || end != text.data() + text.size()) return std::nullopt;
    if (d < 1 || d > 64 || (d & (d - 1)) != 0) return std::nullopt;
    g.denominator = d;
    return g;
}

std::string to_string(const GridDivision& grid) {
    return "1/" + std::to_string(grid.denominator) + (grid.triplet ? "T" : "");
}

double grid_step_beats(const GridDivision& grid, int beat_unit) noexcept {
    const double step = static_cast<double>(beat_unit) / static_cast<double>(std::max(grid.denominator, 1));
    return grid.triplet ? step * 2.0 / 3.0 : step;
}

double snap(double beat, double step_beats, double strength) noexcept {
    if (!(step_beats > 0.0)) return beat;
    const double nearest = std::round(beat / step_beats) * step_beats;
    return beat + (nearest - beat) * std::clamp(strength, 0.0, 1.0);
}

LaneTimeline::LaneTimeline(const NoteSequence& sequence, const Lane& lane, const TempoMap& tempo) : tempo_(&tempo) {
    notes_.reserve(lane.notes.size());
    for (const Note& n : lane.notes) {
        TimedNote t;
        t.note = &n;
        t.envelope = sequence.find_envelope(n.envelope_ref);
        t.start_beat = lane.swung(n.start_beat);
        t.end_beat = std::max(lane.swung(n.start_beat + std::max(n.length_beats, 0.0)), t.start_beat);
        t.start_s = tempo.seconds_at(t.start_beat);
        t.end_s = tempo.seconds_at(t.end_beat);
        notes_.push_back(t);
    }
    std::stable_sort(notes_.begin(), notes_.end(),
                     [](const TimedNote& a, const TimedNote& b) { return a.start_s < b.start_s; });
    max_end_s_.resize(notes_.size());
    double running = -std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < notes_.size(); ++i) {
        const TimedNote& t = notes_[i];
        double end = t.end_s;
        if (t.envelope && t.envelope->kind == Envelope::Kind::Adsr && t.envelope->release_beats > 0.0) {
            end = tempo.seconds_at(t.end_beat + t.envelope->release_beats);
        }
        running = std::max(running, end);
        max_end_s_[i] = running;
    }
}

bool LaneTimeline::gate(double t) const noexcept {
    auto it = std::upper_bound(notes_.begin(), notes_.end(), t,
                               [](double x, const TimedNote& n) { return x < n.start_s; });
    for (auto k = static_cast<std::size_t>(it - notes_.begin()); k-- > 0;) {
        if (max_end_s_[k] <= t) break;
        if (notes_[k].end_s > t) return true;
    }
    return false;
}

const TimedNote* LaneTimeline::started_in(double t0, double t1) const noexcept {
    auto it = std::upper_bound(notes_.begin(), notes_.end(), t0,
                               [](double x, const TimedNote& n) { return x < n.start_s; });
    const TimedNote* best = nullptr;
    for (; it != notes_.end() && it->start_s <= t1; ++it) {
        if (!best || it->note->velocity > best->note->velocity) best = &*it;
    }
    return best;
}

const TimedNote* LaneTimeline::current(double t) const noexcept {
    auto it = std::upper_bound(notes_.begin(), notes_.end(), t,
                               [](double x, const TimedNote& n) { return x < n.start_s; });
    return it == notes_.begin() ? nullptr : &*(it - 1);
}

double LaneTimeline::envelope(double t) const noexcept {
    if (!tempo_) return 0.0;
    auto it = std::upper_bound(notes_.begin(), notes_.end(), t,
                               [](double x, const TimedNote& n) { return x < n.start_s; });
    double level = 0.0;
    double beat = 0.0;
    bool have_beat = false;
    for (auto k = static_cast<std::size_t>(it - notes_.begin()); k-- > 0;) {
        if (max_end_s_[k] <= t) break;
        if (!have_beat) {
            beat = tempo_->beat_at(t);
            have_beat = true;
        }
        const TimedNote& n = notes_[k];
        Note shaped = *n.note;
        shaped.length_beats = n.end_beat - n.start_beat;
        level = std::max(level, envelope_level(n.envelope, shaped, std::max(beat - n.start_beat, 0.0)));
    }
    return level;
}

} // namespace ztrip::time
