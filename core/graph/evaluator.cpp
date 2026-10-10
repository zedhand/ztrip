#include "ztrip/graph/evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ztrip::graph {
namespace {

std::vector<double> initial_state_of(const CompiledGraph& g) {
    std::vector<double> state(g.state_size, 0.0);
    for (const CompiledNode& n : g.nodes) {
        n.impl->reset(std::span<double>(state).subspan(n.state_offset, n.spec->state_size));
    }
    return state;
}

Value read_input(const CompiledNode& n, std::size_t k, const std::vector<Value>& values) {
    const InputSource& src = n.inputs[k];
    if (src.value >= 0) return values[static_cast<std::size_t>(src.value)];
    if (src.param >= 0) return {n.numbers[static_cast<std::size_t>(src.param)], false};
    return {};
}

} // namespace

Evaluator::Evaluator(std::unique_ptr<CompiledGraph> graph, EvaluatorSettings settings)
    : graph_(std::move(graph)), settings_(settings) {
    if (!graph_) throw std::invalid_argument("Evaluator needs a compiled graph");
    if (!(settings_.rate_hz > 0.0)) throw std::invalid_argument("Evaluator rate must be positive");
    checkpoint_every_ = std::max<std::int64_t>(1, std::llround(settings_.checkpoint_interval_s * settings_.rate_hz));
    for (auto& n : graph_->nodes) n.impl->bind(sources_);
    reset();
}

void Evaluator::reset() {
    state_ = initial_state_of(*graph_);
    values_.assign(graph_->value_count, Value{});
    step_ = -1;
    outputs_.clear();
    clear_checkpoints();
}

void Evaluator::clear_checkpoints() {
    dirty_ = true;
    checkpoints_.clear();
    if (!settings_.live) checkpoints_[0] = initial_state_of(*graph_);
}

double Evaluator::current_time() const noexcept {
    return step_ < 0 ? 0.0 : static_cast<double>(step_) / settings_.rate_hz;
}

void Evaluator::bind_source(std::string slot, const analysis::AnalysisResult* result) {
    sources_.bind(std::move(slot), result);
    for (auto& n : graph_->nodes) n.impl->bind(sources_);
    clear_checkpoints();
}

void Evaluator::bind_tempo(std::string slot, const time::TempoMap* tempo) {
    sources_.bind_tempo(std::move(slot), tempo);
    for (auto& n : graph_->nodes) n.impl->bind(sources_);
    clear_checkpoints();
}

void Evaluator::bind_sequence(std::string slot, const time::NoteSequence* sequence) {
    sources_.bind_sequence(std::move(slot), sequence);
    for (auto& n : graph_->nodes) n.impl->bind(sources_);
    clear_checkpoints();
}

void Evaluator::bind_controls(std::string slot, const io::ControlSet* controls) {
    sources_.bind_controls(std::move(slot), controls);
    for (auto& n : graph_->nodes) n.impl->bind(sources_);
    clear_checkpoints();
}

double Evaluator::stamp_time(double t) const noexcept {
    const double pos = std::floor(t * settings_.rate_hz + 1e-6);
    const std::int64_t n = std::isfinite(pos) ? std::max<std::int64_t>(0, static_cast<std::int64_t>(pos)) : 0;
    return static_cast<double>(std::max(n, step_ + 1)) / settings_.rate_hz;
}

bool Evaluator::set_macro(std::string_view name, double value) {
    for (const CompiledMacro& m : graph_->macros) {
        if (m.name != name) continue;
        if (!std::isfinite(value)) return false;
        graph_->nodes[m.node].numbers[m.param] = std::clamp(value, m.min, m.max);
        clear_checkpoints();
        return true;
    }
    return false;
}

std::optional<double> Evaluator::macro(std::string_view name) const {
    for (const CompiledMacro& m : graph_->macros) {
        if (m.name == name) return graph_->nodes[m.node].numbers[m.param];
    }
    return std::nullopt;
}

std::unique_ptr<CompiledGraph> Evaluator::swap_graph(std::unique_ptr<CompiledGraph> graph) {
    if (!graph) throw std::invalid_argument("swap_graph needs a compiled graph");
    std::vector<double> state = initial_state_of(*graph);
    std::vector<Value> values(graph->value_count, Value{});
    for (const CompiledNode& n : graph->nodes) {
        const int old = graph_->find_node(n.id);
        if (old < 0) continue;
        const CompiledNode& o = graph_->nodes[static_cast<std::size_t>(old)];
        if (o.spec->type != n.spec->type || o.spec->state_size != n.spec->state_size) continue;
        std::copy_n(state_.begin() + static_cast<std::ptrdiff_t>(o.state_offset), n.spec->state_size,
                    state.begin() + static_cast<std::ptrdiff_t>(n.state_offset));
        std::copy_n(values_.begin() + static_cast<std::ptrdiff_t>(o.value_offset), n.spec->outputs.size(),
                    values.begin() + static_cast<std::ptrdiff_t>(n.value_offset));
    }
    for (const CompiledMacro& m : graph->macros) {
        if (const auto v = macro(m.name)) graph->nodes[m.node].numbers[m.param] = std::clamp(*v, m.min, m.max);
    }
    for (auto& n : graph->nodes) n.impl->bind(sources_);
    std::swap(graph_, graph);
    state_ = std::move(state);
    values_ = std::move(values);
    clear_checkpoints();
    return graph;
}

void Evaluator::tick(std::int64_t step) {
    if (!settings_.live && step % checkpoint_every_ == 0) checkpoints_.try_emplace(step, state_);
    EvalContext ctx;
    ctx.step = step;
    ctx.rate_hz = settings_.rate_hz;
    ctx.time = static_cast<double>(step) / settings_.rate_hz;
    ctx.dt = 1.0 / settings_.rate_hz;
    ctx.live = settings_.live;
    ctx.sources = &sources_;
    for (const CompiledNode& n : graph_->nodes) {
        const std::size_t in_count = n.inputs.size();
        if (scratch_.size() < in_count) scratch_.resize(in_count);
        for (std::size_t k = 0; k < in_count; ++k) scratch_[k] = read_input(n, k, values_);
        auto out = std::span<Value>(values_).subspan(n.value_offset, n.spec->outputs.size());
        std::fill(out.begin(), out.end(), Value{});
        NodeIO io{std::span<const Value>(scratch_.data(), in_count), out,
                  std::span<double>(state_).subspan(n.state_offset, n.spec->state_size), n.numbers};
        n.impl->evaluate(ctx, io);
    }
    step_ = step;
    ++ticks_;
}

void Evaluator::evaluate(double t) {
    const double pos = std::floor(t * settings_.rate_hz + 1e-6);
    const std::int64_t n = std::isfinite(pos) ? std::max<std::int64_t>(0, static_cast<std::int64_t>(pos)) : 0;

    if (settings_.live) {
        if (n < step_) {
            state_ = initial_state_of(*graph_);
            step_ = n - 1;
        }
        // Step through a gap so smoothing stays rate-correct, but no more than
        // one checkpoint interval of it.
        step_ = std::max(step_, n - checkpoint_every_ - 1);
    } else if (n != step_ || dirty_) {
        // Resume from the latest checkpoint at or before n when going back, or
        // when it saves ticks going forward. Re-evaluating the same tick after
        // a change replays it too (with only checkpoint 0 left after the
        // change, that is a replay from the start), so a paused frame shows
        // what playing to it with the new settings would.
        auto it = checkpoints_.upper_bound(n);
        --it; // checkpoint 0 always exists
        if (n <= step_ || it->first > step_ + 1) {
            state_ = it->second;
            step_ = it->first - 1;
        }
    }
    while (step_ < n) tick(step_ + 1);
    dirty_ = false;

    outputs_.clear();
    for (const CompiledNode& node : graph_->nodes) {
        if (node.spec->is_target) outputs_.push_back({node.target, read_input(node, 0, values_).v});
    }
}

std::optional<double> Evaluator::output(std::string_view target) const {
    for (const TargetValue& o : outputs_) {
        if (o.target == target) return o.value;
    }
    return std::nullopt;
}

std::optional<Value> Evaluator::port_value(std::string_view node, std::string_view port) const {
    const int i = graph_->find_node(node);
    if (i < 0) return std::nullopt;
    const CompiledNode& n = graph_->nodes[static_cast<std::size_t>(i)];
    const int p = n.spec->output_index(port);
    if (p < 0) return std::nullopt;
    return values_[n.value_offset + static_cast<std::size_t>(p)];
}

} // namespace ztrip::graph
