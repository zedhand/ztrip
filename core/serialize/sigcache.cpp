#include "ztrip/serialize/sigcache.hpp"

#include <bit>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace ztrip::serialize {
namespace {

constexpr char kMagic[8] = {'Z', 'T', 'R', 'S', 'I', 'G', 'C', '\0'};

class Writer {
public:
    void bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        out_.insert(out_.end(), b, b + n);
    }
    void u32(std::uint32_t v) { le(v); }
    void i32(std::int32_t v) { le(static_cast<std::uint32_t>(v)); }
    void u64(std::uint64_t v) { le(v); }
    void f32(float v) { le(std::bit_cast<std::uint32_t>(v)); }
    void f64(double v) { le(std::bit_cast<std::uint64_t>(v)); }
    void str(const std::string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        bytes(s.data(), s.size());
    }
    std::vector<std::uint8_t> take() { return std::move(out_); }

private:
    template <typename U>
    void le(U v) {
        for (std::size_t i = 0; i < sizeof(U); ++i) out_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    std::vector<std::uint8_t> out_;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> b) : b_(b) {}

    bool ok() const { return ok_; }
    bool at_end() const { return pos_ == b_.size(); }
    bool bytes(void* p, std::size_t n) {
        if (!take(n)) return false;
        std::memcpy(p, b_.data() + pos_ - n, n);
        return true;
    }
    std::uint32_t u32() { return le<std::uint32_t>(); }
    std::int32_t i32() { return static_cast<std::int32_t>(le<std::uint32_t>()); }
    std::uint64_t u64() { return le<std::uint64_t>(); }
    float f32() { return std::bit_cast<float>(le<std::uint32_t>()); }
    double f64() { return std::bit_cast<double>(le<std::uint64_t>()); }
    std::string str() {
        const std::uint32_t n = u32();
        if (!take(n)) return {};
        return std::string(reinterpret_cast<const char*>(b_.data() + pos_ - n), n);
    }
    // Guards element counts against the bytes actually left, so a corrupt
    // count can't trigger a huge allocation.
    bool count_fits(std::uint64_t count, std::size_t element_size) {
        if (!ok_ || count > (b_.size() - pos_) / element_size) ok_ = false;
        return ok_;
    }

private:
    bool take(std::size_t n) {
        if (!ok_ || n > b_.size() - pos_) {
            ok_ = false;
            return false;
        }
        pos_ += n;
        return true;
    }
    template <typename U>
    U le() {
        if (!take(sizeof(U))) return 0;
        U v = 0;
        for (std::size_t i = 0; i < sizeof(U); ++i) v |= static_cast<U>(b_[pos_ - sizeof(U) + i]) << (8 * i);
        return v;
    }
    std::span<const std::uint8_t> b_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

} // namespace

std::vector<std::uint8_t> encode_sigcache(const SigcacheKey& key, const analysis::AnalysisResult& r) {
    Writer w;
    w.bytes(kMagic, sizeof kMagic);
    w.u32(kSigcacheVersion);
    w.u64(key.audio_hash);
    w.u64(key.settings_hash);
    w.f64(r.signal_rate_hz);
    w.f64(r.duration_s);
    w.f64(r.beats.bpm);
    w.i32(r.beats.beats_per_bar);
    w.i32(r.beats.downbeat_phase);
    w.u32(static_cast<std::uint32_t>(r.signals.size()));
    for (const auto& s : r.signals) {
        w.str(s.name);
        w.u64(s.values.size());
        for (float v : s.values) w.f32(v);
    }
    w.u32(static_cast<std::uint32_t>(r.events.size()));
    for (const auto& e : r.events) {
        w.str(e.name);
        w.u64(e.events.size());
        for (const auto& t : e.events) {
            w.f64(t.time);
            w.f32(t.velocity);
        }
    }
    w.u64(r.beats.beats.size());
    for (double t : r.beats.beats) w.f64(t);
    w.u64(r.beats.downbeats.size());
    for (double t : r.beats.downbeats) w.f64(t);
    return w.take();
}

std::optional<analysis::AnalysisResult> decode_sigcache(std::span<const std::uint8_t> bytes,
                                                        const SigcacheKey& expected) {
    Reader rd(bytes);
    char magic[8] = {};
    if (!rd.bytes(magic, sizeof magic) || std::memcmp(magic, kMagic, sizeof kMagic) != 0) return std::nullopt;
    if (rd.u32() != kSigcacheVersion) return std::nullopt;
    SigcacheKey key;
    key.audio_hash = rd.u64();
    key.settings_hash = rd.u64();
    if (!rd.ok() || key != expected) return std::nullopt;

    analysis::AnalysisResult r;
    r.signal_rate_hz = rd.f64();
    r.duration_s = rd.f64();
    r.beats.bpm = rd.f64();
    r.beats.beats_per_bar = rd.i32();
    r.beats.downbeat_phase = rd.i32();

    const std::uint32_t n_signals = rd.u32();
    for (std::uint32_t i = 0; i < n_signals && rd.ok(); ++i) {
        analysis::SignalBuffer s;
        s.name = rd.str();
        const std::uint64_t n = rd.u64();
        if (!rd.count_fits(n, 4)) break;
        s.values.resize(static_cast<std::size_t>(n));
        for (float& v : s.values) v = rd.f32();
        r.signals.push_back(std::move(s));
    }
    const std::uint32_t n_lists = rd.u32();
    for (std::uint32_t i = 0; i < n_lists && rd.ok(); ++i) {
        analysis::EventList e;
        e.name = rd.str();
        const std::uint64_t n = rd.u64();
        if (!rd.count_fits(n, 12)) break;
        e.events.resize(static_cast<std::size_t>(n));
        for (auto& t : e.events) {
            t.time = rd.f64();
            t.velocity = rd.f32();
        }
        r.events.push_back(std::move(e));
    }
    for (auto* times : {&r.beats.beats, &r.beats.downbeats}) {
        const std::uint64_t n = rd.u64();
        if (!rd.count_fits(n, 8)) break;
        times->resize(static_cast<std::size_t>(n));
        for (double& t : *times) t = rd.f64();
    }
    if (!rd.ok() || !rd.at_end()) return std::nullopt;
    return r;
}

void write_sigcache(const std::filesystem::path& path, const SigcacheKey& key,
                    const analysis::AnalysisResult& result) {
    const std::vector<std::uint8_t> bytes = encode_sigcache(key, result);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("sigcache: cannot write " + path.string());
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("sigcache: write failed for " + path.string());
}

std::optional<analysis::AnalysisResult> read_sigcache(const std::filesystem::path& path,
                                                      const SigcacheKey& expected) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                          std::istreambuf_iterator<char>());
    return decode_sigcache(bytes, expected);
}

} // namespace ztrip::serialize
