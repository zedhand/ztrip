// The one exported function of the check library: runs a song and a graph
// through the staged core the way the Unreal player does (Session, then
// bake, then the target syntax) and prints what happened.
#include "ztrip/host/bake.hpp"
#include "ztrip/host/session.hpp"
#include "ztrip/host/unreal_target.hpp"

#include <cstdio>
#include <string>

#if defined(_WIN32)
#define ZTRIP_CHECK_API extern "C" __declspec(dllexport)
#else
#define ZTRIP_CHECK_API extern "C" __attribute__((visibility("default")))
#endif

ZTRIP_CHECK_API int ztrip_unreal_check(const char* wav, const char* graph, const char* cache) {
    using namespace ztrip;
    host::Session session({true, host::path_from_utf8(cache), 0.5});
    if (!session.load_audio_file(host::path_from_utf8(wav))) {
        std::printf("audio: %s\n", session.audio_error().c_str());
        return 1;
    }
    session.wait();
    session.load_graph_file(host::path_from_utf8(graph));
    for (const auto& line : session.status_lines()) std::printf("  %s\n", line.c_str());
    if (!session.analysis() || !session.has_graph()) return 1;

    double peak = 0.0;
    for (double t = 0.0; t < session.duration(); t += 1.0 / 60.0) {
        session.evaluate(t);
        for (const auto& o : session.outputs()) peak = o.value > peak ? o.value : peak;
    }
    std::printf("  evaluated %.1f s at 60 fps, largest output %.3f\n", session.duration(), peak);

    host::BakeSettings bake;
    bake.fps = 30.0;
    bake.tolerance = 0.01;
    std::size_t keys = 0, frames = 0;
    for (const auto& curve : host::bake(*session.evaluator(), session.duration(), bake)) {
        keys += curve.keys.size();
        frames += curve.frames;
        std::string error;
        const auto target = host::parse_unreal_target(curve.target, error);
        std::printf("  %-48s %5zu keys of %zu frames  %s\n", curve.target.c_str(), curve.keys.size(), curve.frames,
                    target ? ("-> " + host::to_string(*target)).c_str() : error.c_str());
    }
    std::printf("  baked %zu keys from %zu frames\n", keys, frames);
    return peak > 0.0 && keys > 0 && keys < frames ? 0 : 1;
}
