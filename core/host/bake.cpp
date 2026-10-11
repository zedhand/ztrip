#include "ztrip/host/bake.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace ztrip::host {

namespace {

double lerp_at(const BakeKey& a, const BakeKey& b, double t) {
    if (b.time <= a.time) return b.value;
    const double u = (t - a.time) / (b.time - a.time);
    return a.value + (b.value - a.value) * u;
}

} // namespace

std::vector<BakedCurve> bake(graph::Evaluator& evaluator, double duration_s, const BakeSettings& settings) {
    std::vector<BakedCurve> curves;
    std::vector<std::string> targets = settings.targets;
    if (targets.empty()) {
        for (const auto& n : evaluator.graph().nodes)
            if (n.spec->is_target) targets.push_back(n.target);
    }
    for (auto& t : targets) curves.push_back({std::move(t), {}, 0});
    if (!(settings.fps > 0.0) || !std::isfinite(settings.fps)) return curves;

    const double start = std::max(0.0, settings.start_s);
    const double end = settings.end_s < 0.0 ? duration_s : settings.end_s;
    if (!(end >= start)) return curves;
    const auto frames = static_cast<std::size_t>(std::floor((end - start) * settings.fps + 1e-9)) + 1;

    std::vector<std::vector<BakeKey>> samples(curves.size());
    for (auto& s : samples) s.reserve(frames);
    for (std::size_t f = 0; f < frames; ++f) {
        const double t = start + static_cast<double>(f) / settings.fps;
        evaluator.evaluate(t);
        for (std::size_t i = 0; i < curves.size(); ++i) {
            if (const auto v = evaluator.output(curves[i].target)) samples[i].push_back({t, *v});
        }
    }
    for (std::size_t i = 0; i < curves.size(); ++i) {
        curves[i].frames = samples[i].size();
        curves[i].keys = reduce_keys(samples[i], std::max(0.0, settings.tolerance));
    }
    return curves;
}

std::vector<BakeKey> reduce_keys(std::span<const BakeKey> keys, double tolerance) {
    if (keys.size() <= 2) return {keys.begin(), keys.end()};
    std::vector<bool> keep(keys.size(), false);
    keep.front() = true;
    keep.back() = true;
    // Iterative Douglas-Peucker: split each span at its worst key until every
    // dropped key is within tolerance of the line between its span's ends.
    std::vector<std::pair<std::size_t, std::size_t>> spans{{0, keys.size() - 1}};
    while (!spans.empty()) {
        const auto [a, b] = spans.back();
        spans.pop_back();
        if (b <= a + 1) continue;
        double worst = -1.0;
        std::size_t at = a;
        for (std::size_t k = a + 1; k < b; ++k) {
            const double err = std::abs(keys[k].value - lerp_at(keys[a], keys[b], keys[k].time));
            if (err > worst) {
                worst = err;
                at = k;
            }
        }
        // Rounding alone (keys on a straight line) never keeps a key.
        const double slack = 1e-9 * (1.0 + std::abs(keys[a].value) + std::abs(keys[b].value));
        if (worst > tolerance + slack) {
            keep[at] = true;
            spans.emplace_back(a, at);
            spans.emplace_back(at, b);
        }
    }
    std::vector<BakeKey> out;
    for (std::size_t k = 0; k < keys.size(); ++k)
        if (keep[k]) out.push_back(keys[k]);
    return out;
}

double max_error(std::span<const BakeKey> keys, std::span<const BakeKey> samples) {
    if (keys.empty()) return samples.empty() ? 0.0 : std::numeric_limits<double>::infinity();
    double worst = 0.0;
    std::size_t j = 0;
    for (const auto& s : samples) {
        double v = 0.0;
        if (s.time <= keys.front().time) {
            v = keys.front().value;
        } else if (s.time >= keys.back().time) {
            v = keys.back().value;
        } else {
            while (j + 1 < keys.size() && keys[j + 1].time < s.time) ++j;
            v = lerp_at(keys[j], keys[j + 1], s.time);
        }
        worst = std::max(worst, std::abs(v - s.value));
    }
    return worst;
}

} // namespace ztrip::host
