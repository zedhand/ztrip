#include "ztrip/graph/compiler.hpp"
#include "ztrip/serialize/mgraph.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>

using namespace ztrip;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("mgraph parses nodes, params, edges and macros", "[mgraph]") {
    const auto doc = serialize::parse_mgraph(R"({
      "format": "ztrip.mgraph", "version": 1, "name": "test",
      "nodes": [
        {"id": "a", "type": "ResponseCurve", "params": {"curve": "custom", "points": [[0, 0], [1, 1]], "amount": 3}},
        {"id": "b", "type": "RemapRange", "params": {"clamp": false}, "pos": [10.5, -4]}
      ],
      "edges": [{"from": "a.out", "to": "b.in"}],
      "macros": [{"name": "Amt", "node": "a", "param": "amount", "min": 1, "max": 5, "default": 2}]
    })");
    CHECK(doc.name == "test");
    REQUIRE(doc.nodes.size() == 2);
    CHECK(std::get<std::string>(doc.nodes[0].params.at("curve")) == "custom");
    CHECK(std::get<std::vector<double>>(doc.nodes[0].params.at("points")) == std::vector<double>{0, 0, 1, 1});
    CHECK(std::get<double>(doc.nodes[0].params.at("amount")) == 3.0);
    CHECK(std::get<bool>(doc.nodes[1].params.at("clamp")) == false);
    CHECK(doc.nodes[1].x == 10.5f);
    CHECK(doc.nodes[1].y == -4.0f);
    REQUIRE(doc.edges.size() == 1);
    CHECK(doc.edges[0].from_node == "a");
    CHECK(doc.edges[0].to_port == "in");
    REQUIRE(doc.macros.size() == 1);
    CHECK(doc.macros[0].default_value == 2.0);
    CHECK(graph::compile(doc).ok());
}

TEST_CASE("mgraph write then parse is lossless and stable", "[mgraph]") {
    const auto path = std::filesystem::path(ZTRIP_SOURCE_DIR) / "examples" / "kitchen_sink.mgraph.json";
    const auto doc = serialize::load_mgraph(path);
    const std::string once = serialize::write_mgraph(doc);
    const auto again = serialize::parse_mgraph(once);
    CHECK(serialize::write_mgraph(again) == once);
    CHECK(again.nodes.size() == doc.nodes.size());
    CHECK(again.edges.size() == doc.edges.size());
    CHECK(again.nodes[5].params == doc.nodes[5].params);

    const auto tmp = std::filesystem::temp_directory_path() / "ztrip_test_roundtrip.mgraph.json";
    serialize::save_mgraph(tmp, again);
    CHECK(serialize::write_mgraph(serialize::load_mgraph(tmp)) == once);
    std::filesystem::remove(tmp);
}

TEST_CASE("mgraph rejects malformed documents with a clear message", "[mgraph]") {
    auto error_of = [](const char* json) {
        try {
            serialize::parse_mgraph(json);
        } catch (const serialize::MgraphError& e) {
            return std::string(e.what());
        }
        return std::string("no error");
    };
    CHECK_THAT(error_of("{"), ContainsSubstring("invalid JSON"));
    CHECK_THAT(error_of("[]"), ContainsSubstring("must be an object"));
    CHECK_THAT(error_of(R"({"nodes": []})"), ContainsSubstring("\"version\""));
    CHECK_THAT(error_of(R"({"version": 99})"), ContainsSubstring("newer than this build"));
    CHECK_THAT(error_of(R"({"version": 1, "format": "other"})"), ContainsSubstring("not a ztrip.mgraph"));
    CHECK_THAT(error_of(R"({"version": 1, "nodes": [{"type": "Add"}]})"), ContainsSubstring("nodes[0] is missing \"id\""));
    CHECK_THAT(error_of(R"({"version": 1, "edges": [{"from": "a", "to": "b.in"}]})"),
               ContainsSubstring("edges[0].from must look like \"node.port\""));
    CHECK_THAT(error_of(R"({"version": 1, "nodes": [{"id": "a", "type": "Add", "params": {"a": {}}}]})"),
               ContainsSubstring("nodes[0].params.a"));
    CHECK_THAT(error_of(R"({"version": 1, "nodes": [{"id": "a", "type": "Add", "pos": [1]}]})"),
               ContainsSubstring("pos must be [x, y]"));
    CHECK_THAT(error_of(R"({"version": 1, "macros": [{"name": "m", "node": "a", "param": "b", "min": "0"}]})"),
               ContainsSubstring("macros[0].min must be a number"));
}
