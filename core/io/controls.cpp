#include "ztrip/io/controls.hpp"

#include <algorithm>

namespace ztrip::io {

float Control::value_at(double t) const noexcept {
    const auto it = std::upper_bound(changes.begin(), changes.end(), t,
                                     [](double x, const std::pair<double, float>& c) { return x < c.first; });
    return it == changes.begin() ? 0.0f : std::prev(it)->second;
}

Control& ControlSet::get(std::string_view name) {
    auto it = controls_.find(name);
    if (it == controls_.end()) {
        it = controls_.emplace(std::string(name), Control{}).first;
        it->second.name = it->first;
    }
    return it->second;
}

void ControlSet::set(std::string_view name, double t, float value) {
    Control& c = get(name);
    while (!c.changes.empty() && c.changes.back().first > t) c.changes.pop_back();
    if (!c.changes.empty() && c.changes.back().second == value) return; // DMX and OSC resend unchanged values
    if (!c.changes.empty() && c.changes.back().first == t) {
        c.changes.back().second = value;
    } else {
        c.changes.emplace_back(t, value);
    }
    last_changed_ = c.name;
    ++change_count_;
}

void ControlSet::trigger(std::string_view name, double t, float velocity) {
    Control& c = get(name);
    while (!c.triggers.empty() && c.triggers.back().time > t) c.triggers.pop_back();
    c.triggers.push_back({t, velocity});
    last_changed_ = c.name;
    ++change_count_;
}

const Control* ControlSet::find(std::string_view name) const noexcept {
    const auto it = controls_.find(name);
    return it == controls_.end() ? nullptr : &it->second;
}

void ControlSet::clear() {
    controls_.clear();
    last_changed_.clear();
    change_count_ = 0;
}

} // namespace ztrip::io
