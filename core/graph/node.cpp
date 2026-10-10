#include "ztrip/graph/node.hpp"

#include <algorithm>
#include <stdexcept>

namespace ztrip::graph {
namespace {

template <typename T>
const T& param_or_default(const NodeSetup& setup, std::string_view name) {
    if (setup.doc) {
        const auto it = setup.doc->params.find(std::string(name));
        if (it != setup.doc->params.end()) {
            if (const T* v = std::get_if<T>(&it->second)) return *v;
        }
    }
    const int i = setup.spec ? setup.spec->param_index(name) : -1;
    if (i < 0) throw std::logic_error("node has no param '" + std::string(name) + "'");
    const auto& def = setup.spec->params[static_cast<std::size_t>(i)].default_value;
    if (const T* v = std::get_if<T>(&def)) return *v;
    throw std::logic_error("param '" + std::string(name) + "' has another kind");
}

template <typename T>
void bind_slot(std::vector<std::pair<std::string, const T*>>& slots, std::string slot, const T* value) {
    for (auto& s : slots) {
        if (s.first == slot) {
            s.second = value;
            return;
        }
    }
    slots.emplace_back(std::move(slot), value);
}

template <typename T>
const T* find_slot(const std::vector<std::pair<std::string, const T*>>& slots, std::string_view slot) noexcept {
    for (const auto& s : slots) {
        if (s.first == slot) return s.second;
    }
    return nullptr;
}

template <typename Ports>
int index_of(const Ports& ports, std::string_view name) {
    for (std::size_t i = 0; i < ports.size(); ++i) {
        if (ports[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

} // namespace

std::string_view to_string(PortType type) noexcept {
    switch (type) {
    case PortType::Continuous: return "Continuous";
    case PortType::Trigger: return "Trigger";
    case PortType::Clock: return "Clock";
    }
    return "?";
}

const NodeDoc* GraphDoc::find_node(const std::string& id) const noexcept {
    for (const auto& n : nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

void SourceSet::bind(std::string slot, const analysis::AnalysisResult* result) {
    bind_slot(slots_, std::move(slot), result);
}
const analysis::AnalysisResult* SourceSet::find(std::string_view slot) const noexcept {
    return find_slot(slots_, slot);
}

void SourceSet::bind_tempo(std::string slot, const time::TempoMap* tempo) {
    bind_slot(tempos_, std::move(slot), tempo);
}
const time::TempoMap* SourceSet::find_tempo(std::string_view slot) const noexcept {
    return find_slot(tempos_, slot);
}

void SourceSet::bind_sequence(std::string slot, const time::NoteSequence* sequence) {
    bind_slot(sequences_, std::move(slot), sequence);
}
const time::NoteSequence* SourceSet::find_sequence(std::string_view slot) const noexcept {
    return find_slot(sequences_, slot);
}

double NodeSetup::number(std::string_view param) const { return param_or_default<double>(*this, param); }
bool NodeSetup::boolean(std::string_view param) const { return param_or_default<bool>(*this, param); }
const std::string& NodeSetup::string(std::string_view param) const {
    return param_or_default<std::string>(*this, param);
}
const std::vector<double>& NodeSetup::list(std::string_view param) const {
    return param_or_default<std::vector<double>>(*this, param);
}

void Node::reset(std::span<double> state) const { std::fill(state.begin(), state.end(), 0.0); }

int NodeSpec::input_index(std::string_view name) const noexcept { return index_of(inputs, name); }
int NodeSpec::output_index(std::string_view name) const noexcept { return index_of(outputs, name); }
int NodeSpec::param_index(std::string_view name) const noexcept { return index_of(params, name); }

} // namespace ztrip::graph
