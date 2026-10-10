#pragma once

#include "ztrip/io/controls.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ztrip::io {

// ---- OSC -----------------------------------------------------------------

// Parses an OSC packet (a message or a bundle, nested bundles included) into
// control messages. Each message sets and fires "osc<address>" with its
// first argument (ints and floats as given, true/false as 1/0, no argument
// as 1); further numeric arguments set "osc<address>/<index>" (index from 1).
// Malformed packets add nothing. Returns how many were added.
std::size_t osc_to_controls(std::span<const std::uint8_t> packet, std::vector<ControlMessage>& out);

// ---- DMX -----------------------------------------------------------------

enum class DmxProtocol { ArtNet, Sacn };

// A DMX frame from one packet: channel 1 is levels[0].
struct DmxFrame {
    int universe = 0;
    std::size_t count = 0;
    std::array<std::uint8_t, 512> levels{};
};

// Art-Net ArtDmx (OpCode 0x5000) packet: the 15-bit port address is the
// universe (Net, Sub-Net and Universe together, so universe 0 is the first).
// Returns false for anything else.
bool parse_artnet(std::span<const std::uint8_t> packet, DmxFrame& frame);

// sACN (ANSI E1.31) data packet with DMX start code 0; universes count from 1.
// Returns false for anything else (including sync and discovery packets).
bool parse_sacn(std::span<const std::uint8_t> packet, DmxFrame& frame);

// ---- Sending -------------------------------------------------------------

// Sends UDP datagrams to one IPv4 host and port (OSC out, test senders).
class UdpSender {
public:
    // Throws std::runtime_error for a bad address or no socket.
    UdpSender(const std::string& host, int port);
    ~UdpSender();
    UdpSender(const UdpSender&) = delete;
    UdpSender& operator=(const UdpSender&) = delete;
    bool send(std::span<const std::uint8_t> packet);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// An OSC message with float arguments, e.g. osc_message("/fader/1", {0.5f}).
std::vector<std::uint8_t> osc_message(std::string_view address, std::span<const float> args);

// An Art-Net ArtDmx packet for one universe.
std::vector<std::uint8_t> artnet_packet(int universe, std::span<const std::uint8_t> levels, std::uint8_t sequence = 0);

// An sACN (E1.31) data packet for one universe.
std::vector<std::uint8_t> sacn_packet(int universe, std::span<const std::uint8_t> levels, std::uint8_t sequence = 0);

// ---- Network inputs ------------------------------------------------------

struct NetworkInputSettings {
    // UDP port: 0 for the protocol's default (OSC 9000, Art-Net 6454, sACN 5568).
    int port = 0;
    // Local address to listen on; empty for all interfaces.
    std::string bind_address;
    // sACN universes to join by multicast (239.255.<hi>.<lo>); unicast sACN
    // to this machine arrives either way.
    std::vector<int> sacn_universes{1};
};

// Listens for UDP packets on its own thread and posts what they carry.
class UdpControlInput : public ControlInput {
public:
    ~UdpControlInput() override;
    const std::string& source_name() const noexcept override { return name_; }
    int port() const noexcept { return port_; }

    // Feeds a packet as if it arrived on the socket (simulation and tests).
    virtual void receive(std::span<const std::uint8_t> packet) = 0;

protected:
    UdpControlInput() = default;
    // Throws std::runtime_error naming the port when it can't listen.
    void start(const NetworkInputSettings& settings, int default_port, const std::string& what);
    // Stops and joins the receive thread. Derived classes call it first in
    // their destructor, since the thread calls their receive().
    void stop_listening();
    // Joins an IPv4 multicast group (host byte order); false if it can't.
    bool join_multicast(std::uint32_t group);

    std::string name_;

private:
    struct Socket;
    std::unique_ptr<Socket> socket_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    int port_ = 0;
};

class OscInput : public UdpControlInput {
public:
    // Starts listening. Pass listen = false for an input fed only by receive().
    explicit OscInput(const NetworkInputSettings& settings = {}, bool listen = true);
    ~OscInput() override { stop_listening(); }
    void receive(std::span<const std::uint8_t> packet) override;

private:
    std::vector<ControlMessage> scratch_;
};

// DMX over Art-Net or sACN: sets dmx/<universe>/<channel> to level/255 for
// each channel that changed since the universe's last frame.
class DmxInput : public UdpControlInput {
public:
    explicit DmxInput(DmxProtocol protocol, const NetworkInputSettings& settings = {}, bool listen = true);
    ~DmxInput() override { stop_listening(); }
    void receive(std::span<const std::uint8_t> packet) override;

private:
    DmxProtocol protocol_;
    struct Universe {
        int number = -1;
        std::array<int, 512> last{};
    };
    std::vector<Universe> universes_;
};

} // namespace ztrip::io
