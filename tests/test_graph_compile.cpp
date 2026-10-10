#include "graph_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ztrip;
using ztrip::test::GraphBuilder;

namespace {

bool has_error(const graph::CompileResult& r, const std::string& fragment) {
    for (const auto& d : r.diagnostics) {
        if (d.severity == graph::Diagnostic::Severity::Error && d.message.find(fragment) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("the builtin registry has the 15 Phase 2 nodes and NoteLane", "[graph][compile]") {
    const auto& reg = graph::NodeRegistry::builtin();
    for (const char* type : {"AudioBand", "AudioOnset", "BeatClock", "Constant", "Smooth", "Threshold", "RemapRange",
                             "ResponseCurve", "Clamp", "ADSR", "ImpulseDecay", "BeatLFO", "Add", "Multiply",
                             "PropertyOut", "NoteLane"}) {
        INFO(type);
        CHECK(reg.find(type) != nullptr);
    }
    CHECK(reg.specs().size() == 16);
    // Continuous inputs double as number params for when they're unconnected.
    const auto* add = reg.find("Add");
    CHECK(add->param_index("a") >= 0);
    CHECK(add->param_index("b") >= 0);
}

TEST_CASE("compile orders nodes topologically, keeping document order on ties", "[graph][compile]") {
    GraphBuilder g;
    g.node("sum", "Add").node("c2", "Constant").node("c1", "Constant").out("o", "sum.out");
    g.edge("c1.out", "sum.a").edge("c2.out", "sum.b");
    const auto compiled = g.compile();
    std::vector<std::string> ids;
    for (const auto& n : compiled->nodes) ids.push_back(n.id);
    CHECK(ids == std::vector<std::string>{"c2", "c1", "sum", "o"});
    CHECK(compiled->value_count == 3); // c2.out, c1.out, sum.out
}

TEST_CASE("compile rejects malformed graphs with a diagnostic each", "[graph][compile]") {
    SECTION("unknown type") {
        GraphBuilder g;
        g.node("x", "Teleport");
        CHECK(has_error(graph::compile(g.doc), "unknown node type"));
    }
    SECTION("duplicate and dotted ids") {
        GraphBuilder g;
        g.node("a", "Constant").node("a", "Constant").node("b.c", "Constant");
        const auto r = graph::compile(g.doc);
        CHECK_FALSE(r.ok());
        CHECK(has_error(r, "duplicate node id"));
        CHECK(has_error(r, "can't contain '.'"));
    }
    SECTION("missing node or port") {
        GraphBuilder g;
        g.node("a", "Constant").node("b", "Smooth").edge("a.out", "zz.in").edge("a.nope", "b.in").edge("a.out", "b.nope");
        const auto r = graph::compile(g.doc);
        CHECK(has_error(r, "missing node"));
        CHECK(has_error(r, "no output port 'nope'"));
        CHECK(has_error(r, "no input port 'nope'"));
    }
    SECTION("port type mismatch") {
        GraphBuilder g;
        g.node("a", "Constant").node("d", "ImpulseDecay").edge("a.out", "d.trigger");
        CHECK(has_error(graph::compile(g.doc), "can't connect Continuous to Trigger"));
    }
    SECTION("two edges into one input") {
        GraphBuilder g;
        g.node("a", "Constant").node("b", "Constant").node("s", "Smooth").edge("a.out", "s.in").edge("b.out", "s.in");
        CHECK(has_error(graph::compile(g.doc), "more than one edge"));
    }
    SECTION("cycle") {
        GraphBuilder g;
        g.node("a", "Smooth").node("b", "Smooth").node("c", "Constant");
        g.edge("a.out", "b.in").edge("b.out", "a.in");
        const auto r = graph::compile(g.doc);
        CHECK(has_error(r, "cycle through: a, b"));
    }
    SECTION("wrong param kind and bad choice") {
        GraphBuilder g;
        g.node("a", "Smooth", {{"attack_ms", std::string("fast")}});
        g.node("b", "BeatLFO", {{"shape", std::string("wobble")}});
        const auto r = graph::compile(g.doc);
        CHECK(has_error(r, "must be a number"));
        CHECK(has_error(r, "must be one of: sine"));
    }
    SECTION("bad custom curve") {
        GraphBuilder g;
        g.node("a", "ResponseCurve", {{"curve", std::string("custom")}, {"points", std::vector<double>{0, 0, 0, 1}}});
        CHECK(has_error(graph::compile(g.doc), "x values must increase"));
    }
    SECTION("bad macros") {
        GraphBuilder g;
        g.node("m", "Multiply").node("l", "BeatLFO");
        g.macro("A", "nobody", "b", 0, 1, 0).macro("B", "l", "shape", 0, 1, 0).macro("C", "m", "b", 2, 1, 0);
        const auto r = graph::compile(g.doc);
        CHECK(has_error(r, "missing node 'nobody'"));
        CHECK(has_error(r, "must drive a number param"));
        CHECK(has_error(r, "min <= max"));
    }
}

TEST_CASE("compile accepts implicit conversions and warns on unknown params", "[graph][compile]") {
    GraphBuilder g;
    g.node("clock", "BeatClock").node("onset", "AudioOnset").node("sum", "Add", {{"colour", 3.0}});
    g.node("lfo", "BeatLFO").node("c", "Constant");
    g.edge("onset.trigger", "sum.a").edge("clock.clock", "sum.b"); // Trigger and Clock into Continuous
    g.edge("c.out", "lfo.clock");                                   // Continuous into Clock
    g.node("o", "PropertyOut");                                     // no target: warning
    const auto r = graph::compile(g.doc);
    INFO(r.error_text());
    REQUIRE(r.ok());
    CHECK(r.diagnostics.size() == 2);
}

TEST_CASE("macros start at their clamped default", "[graph][compile]") {
    GraphBuilder g;
    g.node("m", "Multiply", {{"a", 2.0}}).out("o", "m.out").macro("Gain", "m", "b", 0.0, 4.0, 9.0);
    auto ev = g.evaluator();
    CHECK(ev.macro("Gain") == 4.0);
    ev.evaluate(0.0);
    CHECK(ev.output("o") == 8.0);
    CHECK(ev.set_macro("Gain", 0.5));
    ev.evaluate(0.0);
    CHECK(ev.output("o") == 1.0);
    CHECK_FALSE(ev.set_macro("Nope", 1.0));
}
