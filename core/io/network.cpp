#include "ztrip/io/network.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t kNoSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t kNoSocket = -1;
#endif

namespace ztrip::io {
namespace {

// ---- Byte reading ----------------------------------------------------------

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3];
}
std::uint16_t be16(const std::uint8_t* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

// An OSC string: null-terminated, padded to a multiple of 4. Advances pos.
bool osc_string(std::span<const std::uint8_t> b, std::size_t& pos, std::string_view& out) {
    const std::size_t start = pos;
    while (pos < b.size() && b[pos] != 0) ++pos;
    if (pos >= b.size()) return false;
    out = std::string_view(reinterpret_cast<const char*>(b.data() + start), pos - start);
    pos = (pos + 4) & ~std::size_t{3};
    return pos <= b.size();
}

bool osc_message(std::span<const std::uint8_t> b, std::vector<ControlMessage>& out) {
    std::size_t pos = 0;
    std::string_view address, tags;
    if (!osc_string(b, pos, address) || address.empty() || address[0] != '/') return false;
    // A missing type tag string (very old senders) means no arguments.
    if (pos < b.size() && !osc_string(b, pos, tags)) return false;
    if (!tags.empty() && tags[0] != ',') return false;
    std::vector<float> values;
    for (std::size_t i = 1; i < tags.size(); ++i) {
        switch (tags[i]) {
        case 'i':
            if (pos + 4 > b.size()) return false;
            values.push_back(static_cast<float>(static_cast<std::int32_t>(be32(b.data() + pos))));
            pos += 4;
            break;
        case 'f': {
            if (pos + 4 > b.size()) return false;
            const std::uint32_t u = be32(b.data() + pos);
            float f;
            std::memcpy(&f, &u, 4);
            values.push_back(f);
            pos += 4;
            break;
        }
        case 'd': {
            if (pos + 8 > b.size()) return false;
            const std::uint64_t u = (std::uint64_t{be32(b.data() + pos)} << 32) | be32(b.data() + pos + 4);
            double d;
            std::memcpy(&d, &u, 8);
            values.push_back(static_cast<float>(d));
            pos += 8;
            break;
        }
        case 'h': case 't':
            if (pos + 8 > b.size()) return false;
            if (tags[i] == 'h') {
                const std::uint64_t u = (std::uint64_t{be32(b.data() + pos)} << 32) | be32(b.data() + pos + 4);
                values.push_back(static_cast<float>(static_cast<std::int64_t>(u)));
            }
            pos += 8;
            break;
        case 'T': values.push_back(1.0f); break;
        case 'F': values.push_back(0.0f); break;
        case 'N': case 'I': break;
        case 's': case 'S': {
            std::string_view ignored;
            if (!osc_string(b, pos, ignored)) return false;
            break;
        }
        case 'b': {
            if (pos + 4 > b.size()) return false;
            const std::uint32_t n = be32(b.data() + pos);
            pos += 4 + ((n + 3) & ~3u);
            if (pos > b.size()) return false;
            break;
        }
        default: return false; // unknown type: can't find the next argument
        }
    }
    const std::string name = "osc" + std::string(address);
    out.push_back(ControlMessage::make(name, ControlMessage::SetAndTrigger, values.empty() ? 1.0f : values[0]));
    for (std::size_t k = 1; k < values.size(); ++k) {
        out.push_back(ControlMessage::make(name + "/" + std::to_string(k), ControlMessage::Set, values[k]));
    }
    return true;
}

bool osc_packet(std::span<const std::uint8_t> b, std::vector<ControlMessage>& out, int depth) {
    if (b.size() >= 16 && std::memcmp(b.data(), "#bundle\0", 8) == 0) {
        if (depth > 8) return false;
        std::size_t pos = 16; // "#bundle\0" and the time tag, which is ignored: events land on arrival
        while (pos + 4 <= b.size()) {
            const std::uint32_t n = be32(b.data() + pos);
            pos += 4;
            if (n > b.size() - pos) return false;
            osc_packet(b.subspan(pos, n), out, depth + 1);
            pos += n;
        }
        return true;
    }
    return osc_message(b, out);
}

#ifdef _WIN32
struct WinsockInit {
    WinsockInit() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WinsockInit() { WSACleanup(); }
};
#endif

} // namespace

std::size_t osc_to_controls(std::span<const std::uint8_t> packet, std::vector<ControlMessage>& out) {
    const std::size_t before = out.size();
    if (!osc_packet(packet, out, 0)) out.resize(before);
    return out.size() - before;
}

bool parse_artnet(std::span<const std::uint8_t> b, DmxFrame& frame) {
    if (b.size() < 18 || std::memcmp(b.data(), "Art-Net\0", 8) != 0) return false;
    if ((b[8] | (b[9] << 8)) != 0x5000) return false; // OpDmx, little-endian
    const std::size_t length = be16(b.data() + 16);
    if (length < 1 || length > 512 || b.size() < 18 + length) return false;
    frame.universe = ((b[15] & 0x7F) << 8) | b[14];
    frame.count = length;
    std::copy_n(b.data() + 18, length, frame.levels.begin());
    return true;
}

bool parse_sacn(std::span<const std::uint8_t> b, DmxFrame& frame) {
    static constexpr std::uint8_t kId[12] = {'A', 'S', 'C', '-', 'E', '1', '.', '1', '7', 0, 0, 0};
    if (b.size() < 126 || be16(b.data()) != 0x0010 || std::memcmp(b.data() + 4, kId, 12) != 0) return false;
    if (be32(b.data() + 18) != 0x00000004) return false; // root vector: E1.31 data
    if (be32(b.data() + 40) != 0x00000002) return false; // framing vector: DMP
    if (b[117] != 0x02) return false;                     // DMP vector: set property
    const std::size_t values = be16(b.data() + 123);     // start code plus channels
    if (values < 1 || values > 513 || b.size() < 125 + values) return false;
    if (b[125] != 0) return false;                        // only DMX level data
    frame.universe = be16(b.data() + 113);
    frame.count = values - 1;
    std::copy_n(b.data() + 126, frame.count, frame.levels.begin());
    return true;
}

// ---- Packet building -----------------------------------------------------------

namespace {
void put_be16(std::vector<std::uint8_t>& b, std::size_t at, std::uint16_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 8);
    b[at + 1] = static_cast<std::uint8_t>(v & 0xFF);
}
void put_be32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    put_be16(b, at, static_cast<std::uint16_t>(v >> 16));
    put_be16(b, at + 2, static_cast<std::uint16_t>(v & 0xFFFF));
}
void append_osc_string(std::vector<std::uint8_t>& b, std::string_view s) {
    b.insert(b.end(), s.begin(), s.end());
    b.push_back(0);
    while (b.size() % 4 != 0) b.push_back(0);
}
} // namespace

std::vector<std::uint8_t> osc_message(std::string_view address, std::span<const float> args) {
    std::vector<std::uint8_t> b;
    append_osc_string(b, address);
    append_osc_string(b, "," + std::string(args.size(), 'f'));
    for (float f : args) {
        std::uint32_t u;
        std::memcpy(&u, &f, 4);
        b.resize(b.size() + 4);
        put_be32(b, b.size() - 4, u);
    }
    return b;
}

std::vector<std::uint8_t> artnet_packet(int universe, std::span<const std::uint8_t> levels, std::uint8_t sequence) {
    const std::size_t n = std::min<std::size_t>(levels.size(), 512);
    const std::size_t len = n + (n % 2); // ArtDmx data length is even
    std::vector<std::uint8_t> b(18 + len, 0);
    std::memcpy(b.data(), "Art-Net\0", 8);
    b[8] = 0x00;
    b[9] = 0x50; // OpDmx 0x5000, little-endian
    b[11] = 14;  // protocol version
    b[12] = sequence;
    b[14] = static_cast<std::uint8_t>(universe & 0xFF);
    b[15] = static_cast<std::uint8_t>((universe >> 8) & 0x7F);
    put_be16(b, 16, static_cast<std::uint16_t>(len));
    std::copy_n(levels.begin(), n, b.begin() + 18);
    return b;
}

std::vector<std::uint8_t> sacn_packet(int universe, std::span<const std::uint8_t> levels, std::uint8_t sequence) {
    const std::size_t n = std::min<std::size_t>(levels.size(), 512);
    std::vector<std::uint8_t> b(126 + n, 0);
    static constexpr std::uint8_t kId[12] = {'A', 'S', 'C', '-', 'E', '1', '.', '1', '7', 0, 0, 0};
    put_be16(b, 0, 0x0010);
    std::memcpy(b.data() + 4, kId, 12);
    put_be16(b, 16, static_cast<std::uint16_t>(0x7000 | (b.size() - 16)));
    put_be32(b, 18, 0x00000004);
    put_be16(b, 38, static_cast<std::uint16_t>(0x7000 | (b.size() - 38)));
    put_be32(b, 40, 0x00000002);
    std::memcpy(b.data() + 44, "ztrip", 5); // source name
    b[108] = 100;                           // priority
    b[111] = sequence;
    put_be16(b, 113, static_cast<std::uint16_t>(universe));
    put_be16(b, 115, static_cast<std::uint16_t>(0x7000 | (b.size() - 115)));
    b[117] = 0x02;
    b[118] = 0xA1;
    put_be16(b, 121, 0x0001);
    put_be16(b, 123, static_cast<std::uint16_t>(n + 1));
    std::copy_n(levels.begin(), n, b.begin() + 126);
    return b;
}

// ---- UDP ---------------------------------------------------------------------

namespace {
struct UdpSocket {
#ifdef _WIN32
    WinsockInit winsock;
#endif
    socket_t fd = kNoSocket;
    UdpSocket() = default;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    ~UdpSocket() {
        if (fd == kNoSocket) return;
#ifdef _WIN32
        closesocket(fd);
#else
        close(fd);
#endif
    }
    // Waits up to timeout_ms for a datagram; returns its size, or 0.
    std::size_t receive(std::vector<std::uint8_t>& buf, int timeout_ms) {
#ifdef _WIN32
        WSAPOLLFD p{fd, POLLRDNORM, 0};
        if (WSAPoll(&p, 1, timeout_ms) <= 0) return 0;
        const int n = recv(fd, reinterpret_cast<char*>(buf.data()), static_cast<int>(buf.size()), 0);
#else
        pollfd p{fd, POLLIN, 0};
        if (poll(&p, 1, timeout_ms) <= 0) return 0;
        const auto n = recv(fd, buf.data(), buf.size(), 0);
#endif
        return n > 0 ? static_cast<std::size_t>(n) : 0;
    }
};
} // namespace

struct UdpControlInput::Socket : UdpSocket {};

struct UdpSender::Impl {
    UdpSocket socket;
    sockaddr_in to{};
};

UdpSender::UdpSender(const std::string& host, int port) : impl_(std::make_unique<Impl>()) {
    impl_->socket.fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->socket.fd == kNoSocket) throw std::runtime_error("UDP: cannot create a socket");
    impl_->to.sin_family = AF_INET;
    impl_->to.sin_port = htons(static_cast<std::uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &impl_->to.sin_addr) != 1) throw std::runtime_error("UDP: bad address " + host);
}

UdpSender::~UdpSender() = default;

bool UdpSender::send(std::span<const std::uint8_t> packet) {
#ifdef _WIN32
    const int size = static_cast<int>(packet.size());
#else
    const std::size_t size = packet.size();
#endif
    const auto n = sendto(impl_->socket.fd, reinterpret_cast<const char*>(packet.data()), size, 0,
                          reinterpret_cast<const sockaddr*>(&impl_->to), sizeof impl_->to);
    return n >= 0 && static_cast<std::size_t>(n) == packet.size();
}

void UdpControlInput::start(const NetworkInputSettings& s, int default_port, const std::string& what) {
    port_ = s.port > 0 ? s.port : default_port;
    const std::string where = what + " on UDP port " + std::to_string(port_);
    socket_ = std::make_unique<Socket>();
    socket_->fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_->fd == kNoSocket) throw std::runtime_error(where + ": cannot create a socket");
    // Let other programs (or a second ztrip) listen to the same broadcast port.
    const int yes = 1;
    setsockopt(socket_->fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port_));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (!s.bind_address.empty() && inet_pton(AF_INET, s.bind_address.c_str(), &addr.sin_addr) != 1) {
        throw std::runtime_error(where + ": bad address " + s.bind_address);
    }
    if (::bind(socket_->fd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0) {
        throw std::runtime_error(where + ": cannot listen (is another program using the port?)");
    }
    name_ = where;
    thread_ = std::thread([this] {
        std::vector<std::uint8_t> buf(65536);
        while (!stop_.load(std::memory_order_relaxed)) {
            const std::size_t n = socket_->receive(buf, 100);
            if (n > 0) receive(std::span<const std::uint8_t>(buf.data(), n));
        }
    });
}

bool UdpControlInput::join_multicast(std::uint32_t group) {
    if (!socket_) return false;
    ip_mreq m{};
    m.imr_multiaddr.s_addr = htonl(group);
    m.imr_interface.s_addr = htonl(INADDR_ANY);
    return setsockopt(socket_->fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, reinterpret_cast<const char*>(&m), sizeof m) == 0;
}

void UdpControlInput::stop_listening() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

UdpControlInput::~UdpControlInput() { stop_listening(); }

OscInput::OscInput(const NetworkInputSettings& settings, bool listen) {
    scratch_.reserve(16);
    name_ = "OSC";
    if (listen) start(settings, 9000, "OSC");
}

void OscInput::receive(std::span<const std::uint8_t> packet) {
    scratch_.clear();
    osc_to_controls(packet, scratch_);
    for (const ControlMessage& m : scratch_) post(m);
}

DmxInput::DmxInput(DmxProtocol protocol, const NetworkInputSettings& settings, bool listen)
    : protocol_(protocol) {
    const bool artnet = protocol == DmxProtocol::ArtNet;
    name_ = artnet ? "Art-Net" : "sACN";
    if (!listen) return;
    start(settings, artnet ? 6454 : 5568, name_);
    if (!artnet) {
        for (int u : settings.sacn_universes) {
            if (u < 1 || u > 63999) continue;
            // 239.255.<hi>.<lo>. Without a multicast route this fails;
            // unicast sACN still arrives.
            join_multicast(0xEFFF0000u | static_cast<std::uint32_t>(u));
        }
    }
}

void DmxInput::receive(std::span<const std::uint8_t> packet) {
    DmxFrame frame;
    const bool ok = protocol_ == DmxProtocol::ArtNet ? parse_artnet(packet, frame) : parse_sacn(packet, frame);
    if (!ok) return;
    auto it = std::find_if(universes_.begin(), universes_.end(), [&](const Universe& u) { return u.number == frame.universe; });
    if (it == universes_.end()) {
        Universe fresh;
        fresh.number = frame.universe;
        fresh.last.fill(-1);
        universes_.push_back(fresh);
        it = universes_.end() - 1;
    }
    const std::string prefix = "dmx/" + std::to_string(frame.universe) + "/";
    for (std::size_t c = 0; c < frame.count; ++c) {
        if (it->last[c] == frame.levels[c]) continue;
        it->last[c] = frame.levels[c];
        post(ControlMessage::make(prefix + std::to_string(c + 1), ControlMessage::Set,
                                  static_cast<float>(frame.levels[c]) / 255.0f));
    }
}

} // namespace ztrip::io
