#include "ztrip/serialize/mgraph.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <functional>
#include <sstream>

namespace ztrip::serialize {
namespace {

using Json = nlohmann::ordered_json;

// migrations[v] upgrades a version v document to v + 1 in place.
const std::vector<std::function<void(Json&)>>& migrations() {
    static const std::vector<std::function<void(Json&)>> list = {
        nullptr, // there is no version 0
    };
    return list;
}

[[noreturn]] void fail(const std::string& what) { throw MgraphError("mgraph: " + what); }

const Json& member(const Json& obj, const char* key, const std::string& where) {
    const auto it = obj.find(key);
    if (it == obj.end()) fail(where + " is missing \"" + key + "\"");
    return *it;
}

std::string string_member(const Json& obj, const char* key, const std::string& where) {
    const Json& v = member(obj, key, where);
    if (!v.is_string()) fail(where + "." + key + " must be a string");
    return v.get<std::string>();
}

double number_member(const Json& obj, const char* key, const std::string& where, double fallback) {
    const auto it = obj.find(key);
    if (it == obj.end()) return fallback;
    if (!it->is_number()) fail(where + "." + key + " must be a number");
    return it->get<double>();
}

// The array under key, or an empty one when absent.
Json array_member(const Json& obj, const char* key) {
    const auto it = obj.find(key);
    if (it == obj.end()) return Json::array();
    if (!it->is_array()) fail(std::string("\"") + key + "\" must be an array");
    return *it;
}

graph::ParamValue param_from_json(const Json& v, const std::string& where) {
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) return v.get<std::string>();
    if (v.is_array()) {
        std::vector<double> list;
        for (const Json& item : v) {
            if (item.is_number()) {
                list.push_back(item.get<double>());
            } else if (item.is_array()) {
                for (const Json& x : item) {
                    if (!x.is_number()) fail(where + " must hold numbers");
                    list.push_back(x.get<double>());
                }
            } else {
                fail(where + " must hold numbers");
            }
        }
        return list;
    }
    fail(where + " must be a number, bool, string or list of numbers");
}

Json param_to_json(const graph::ParamValue& v) {
    return std::visit([](const auto& x) { return Json(x); }, v);
}

// Splits "node.port" at the first '.' (node ids can't contain one).
std::pair<std::string, std::string> split_endpoint(const std::string& s, const std::string& where) {
    const auto dot = s.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == s.size()) fail(where + " must look like \"node.port\"");
    return {s.substr(0, dot), s.substr(dot + 1)};
}

} // namespace

graph::GraphDoc parse_mgraph(std::string_view text) {
    Json root;
    try {
        root = Json::parse(text.begin(), text.end());
    } catch (const Json::parse_error& e) {
        fail(std::string("invalid JSON: ") + e.what());
    }
    if (!root.is_object()) fail("document must be an object");
    if (const auto it = root.find("format"); it != root.end() && *it != "ztrip.mgraph") {
        fail("not a ztrip.mgraph document");
    }
    const auto version_it = root.find("version");
    if (version_it == root.end() || !version_it->is_number_integer()) fail("\"version\" must be an integer");
    int version = version_it->get<int>();
    if (version < 1) fail("unsupported version " + std::to_string(version));
    if (version > kMgraphVersion) {
        fail("version " + std::to_string(version) + " is newer than this build supports (" +
             std::to_string(kMgraphVersion) + ")");
    }
    for (; version < kMgraphVersion; ++version) migrations()[static_cast<std::size_t>(version)](root);

    graph::GraphDoc doc;
    if (const auto it = root.find("name"); it != root.end()) {
        if (!it->is_string()) fail("\"name\" must be a string");
        doc.name = it->get<std::string>();
    }
    for (const Json& n : array_member(root, "nodes")) {
        const std::string where = "nodes[" + std::to_string(doc.nodes.size()) + "]";
        if (!n.is_object()) fail(where + " must be an object");
        graph::NodeDoc node;
        node.id = string_member(n, "id", where);
        node.type = string_member(n, "type", where);
        if (const auto it = n.find("params"); it != n.end()) {
            if (!it->is_object()) fail(where + ".params must be an object");
            for (const auto& [key, value] : it->items()) {
                node.params[key] = param_from_json(value, where + ".params." + key);
            }
        }
        if (const auto it = n.find("pos"); it != n.end()) {
            if (!it->is_array() || it->size() != 2 || !(*it)[0].is_number() || !(*it)[1].is_number()) {
                fail(where + ".pos must be [x, y]");
            }
            node.x = (*it)[0].get<float>();
            node.y = (*it)[1].get<float>();
        }
        doc.nodes.push_back(std::move(node));
    }
    for (const Json& e : array_member(root, "edges")) {
        const std::string where = "edges[" + std::to_string(doc.edges.size()) + "]";
        if (!e.is_object()) fail(where + " must be an object");
        auto [fn, fp] = split_endpoint(string_member(e, "from", where), where + ".from");
        auto [tn, tp] = split_endpoint(string_member(e, "to", where), where + ".to");
        doc.edges.push_back({std::move(fn), std::move(fp), std::move(tn), std::move(tp)});
    }
    for (const Json& m : array_member(root, "macros")) {
        const std::string where = "macros[" + std::to_string(doc.macros.size()) + "]";
        if (!m.is_object()) fail(where + " must be an object");
        graph::MacroDoc macro;
        macro.name = string_member(m, "name", where);
        macro.node = string_member(m, "node", where);
        macro.param = string_member(m, "param", where);
        macro.min = number_member(m, "min", where, 0.0);
        macro.max = number_member(m, "max", where, 1.0);
        macro.default_value = number_member(m, "default", where, macro.min);
        doc.macros.push_back(std::move(macro));
    }
    return doc;
}

std::string write_mgraph(const graph::GraphDoc& doc) {
    Json root;
    root["format"] = "ztrip.mgraph";
    root["version"] = kMgraphVersion;
    root["name"] = doc.name;
    Json nodes = Json::array();
    for (const auto& n : doc.nodes) {
        Json params = Json::object();
        for (const auto& [key, value] : n.params) params[key] = param_to_json(value);
        nodes.push_back({{"id", n.id}, {"type", n.type}, {"params", params}, {"pos", {n.x, n.y}}});
    }
    root["nodes"] = std::move(nodes);
    Json edges = Json::array();
    for (const auto& e : doc.edges) {
        edges.push_back({{"from", e.from_node + "." + e.from_port}, {"to", e.to_node + "." + e.to_port}});
    }
    root["edges"] = std::move(edges);
    Json macros = Json::array();
    for (const auto& m : doc.macros) {
        macros.push_back({{"name", m.name}, {"node", m.node}, {"param", m.param}, {"min", m.min}, {"max", m.max},
                          {"default", m.default_value}});
    }
    root["macros"] = std::move(macros);
    return root.dump(2) + "\n";
}

graph::GraphDoc load_mgraph(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) fail("can't open " + path.string());
    std::stringstream ss;
    ss << in.rdbuf();
    return parse_mgraph(ss.str());
}

void save_mgraph(const std::filesystem::path& path, const graph::GraphDoc& doc) {
    std::ofstream out(path, std::ios::binary);
    if (!out) fail("can't write " + path.string());
    out << write_mgraph(doc);
    if (!out) fail("failed writing " + path.string());
}

} // namespace ztrip::serialize
